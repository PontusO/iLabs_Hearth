/*
 * MatterEndPoint.cpp - base class implementation and the declaration
 * registry.
 */
#include "MatterEndPoint.h"
#include "Hearth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

MatterEndPoint::HearthDeclaration MatterEndPoint::_hearthDeclared[HEARTH_MAX_ENDPOINTS];
uint8_t MatterEndPoint::_hearthDeclaredCount = 0;
bool MatterEndPoint::_hearthReconciled = false;

uint16_t MatterEndPoint::getEndPointId() {
  return endpoint_id;
}

void MatterEndPoint::setEndPointId(uint16_t ep) {
  if (ep == 0) {
    return;  // 0 is reserved for the Root Node, never a real endpoint
  }
  endpoint_id = ep;
}

/*
 * Command construction, showing the mode split that is the point of this
 * class: setAttributeVal (mode 0) is silent, updateAttributeVal (mode 1) is
 * reported to the fabric. See AT_MT_SPEC.md S3.8.
 */
bool MatterEndPoint::hearthWriteAttr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal, int mode) {
  long v;
  if (attrVal == nullptr || !hearthAttrValToLong(*attrVal, &v)) {
    Hearth.hearthSetError(5);  // the wire's "type not carryable" code
    return false;
  }
  char cmd[64];
  snprintf(
    cmd, sizeof(cmd), "AT+MTATTR=%u,%lu,%lu,%ld,%d", (unsigned)endpoint_id, (unsigned long)cluster_id, (unsigned long)attribute_id, v, mode
  );
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterEndPoint::setAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal) {
  return hearthWriteAttr(cluster_id, attribute_id, attrVal, 0);
}

bool MatterEndPoint::updateAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal) {
  return hearthWriteAttr(cluster_id, attribute_id, attrVal, 1);
}

/*
 * Read result context for hearthOnAttrLine(): the target type comes from
 * the caller's attrVal (already typed, e.g. esp_matter_bool()), since the
 * wire only carries the flat integer, not the type.
 */
namespace {
struct HearthReadCtx {
  esp_matter_val_type_t type;
  long value;
  bool got;
};
}  // namespace

void MatterEndPoint::hearthOnAttrLine(const char *line, void *arg) {
  HearthReadCtx *ctx = (HearthReadCtx *)arg;
  if (strncmp(line, "+MTATTR:", 8) != 0) {
    return;  // not our read result; ignore
  }
  const char *lastComma = strrchr(line, ',');
  if (!lastComma) {
    return;
  }
  ctx->value = atol(lastComma + 1);
  ctx->got = true;
}

bool MatterEndPoint::getAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal) {
  if (attrVal == nullptr) {
    Hearth.hearthSetError(1);
    return false;
  }
  /*
   * The result is rebuilt from attrVal->type (see the header comment): an
   * invalid or out-of-enum incoming type must not be allowed through to
   * hearthAttrValFromLong, whose default branch would otherwise hand back a
   * value silently misread out of the wrong union member. Reuse
   * hearthAttrValToLong's own known-type check rather than duplicating its
   * switch; the value it flattens is discarded, only the type verdict
   * matters here.
   */
  long discard;
  if (!hearthAttrValToLong(*attrVal, &discard)) {
    Hearth.hearthSetError(5);  // the wire's "type not carryable" code
    return false;
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+MTATTR=%u,%lu,%lu", (unsigned)endpoint_id, (unsigned long)cluster_id, (unsigned long)attribute_id);

  HearthReadCtx ctx;
  ctx.type = attrVal->type;
  ctx.value = 0;
  ctx.got = false;
  int rc = Hearth.hearthCommand(cmd, hearthOnAttrLine, &ctx);
  if (rc != 0 || !ctx.got) {
    return false;
  }
  *attrVal = hearthAttrValFromLong(ctx.type, ctx.value);
  return true;
}

bool MatterEndPoint::endpointIdentifyCB(uint16_t endpoint_id, bool identifyIsEnabled) {
  (void)endpoint_id;  // kept for signature parity with upstream; unused here
  if (_onEndPointIdentifyCB) {
    return _onEndPointIdentifyCB(identifyIsEnabled);
  }
  return true;
}

void MatterEndPoint::onIdentify(EndPointIdentifyCB onEndPointIdentifyCB) {
  _onEndPointIdentifyCB = onEndPointIdentifyCB;
}

bool MatterEndPoint::hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId) {
  if (ep == nullptr || _hearthDeclaredCount >= HEARTH_MAX_ENDPOINTS) {
    return false;
  }
  if (_hearthReconciled) {
    /*
     * Matter.begin() already reconciled the registry against the C6 and
     * will not query it again; a declaration arriving after that point
     * would sit in the registry with endpoint_id 0 forever. Reused error
     * code 10, the wire's own "composition change rejected" code (S3.9),
     * since this is the same rejection just caught host-side before it
     * ever reaches the wire.
     */
    Hearth.hearthSetError(10);
    return false;
  }
  _hearthDeclared[_hearthDeclaredCount].ep = ep;
  _hearthDeclared[_hearthDeclaredCount].deviceTypeId = deviceTypeId;
  _hearthDeclaredCount++;
  return true;
}

uint8_t MatterEndPoint::hearthDeclaredCount() {
  return _hearthDeclaredCount;
}

MatterEndPoint *MatterEndPoint::hearthDeclaredAt(uint8_t index) {
  if (index >= _hearthDeclaredCount) {
    return nullptr;
  }
  return _hearthDeclared[index].ep;
}

uint32_t MatterEndPoint::hearthDeclaredTypeAt(uint8_t index) {
  if (index >= _hearthDeclaredCount) {
    return 0;
  }
  return _hearthDeclared[index].deviceTypeId;
}

void MatterEndPoint::hearthClearDeclarations() {
  _hearthDeclaredCount = 0;
  _hearthReconciled = false;
}

void MatterEndPoint::hearthMarkReconciled() {
  _hearthReconciled = true;
}

MatterEndPoint *MatterEndPoint::hearthFindByEndPointId(uint16_t ep) {
  for (uint8_t i = 0; i < _hearthDeclaredCount; i++) {
    if (_hearthDeclared[i].ep->getEndPointId() == ep) {
      return _hearthDeclared[i].ep;
    }
  }
  return nullptr;
}

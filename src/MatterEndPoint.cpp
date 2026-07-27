/*
 * MatterEndPoint.cpp - base class implementation and the declaration
 * registry.
 */
#include "MatterEndPoint.h"
/* HearthGlobal.h, not Hearth.h: this file calls through the Hearth object,
 * so it needs the declaration even in a build that set NO_GLOBAL_INSTANCES
 * or NO_GLOBAL_HEARTH. See that header. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

MatterEndPoint::HearthDeclaration MatterEndPoint::_hearthDeclared[HEARTH_MAX_ENDPOINTS];
uint8_t MatterEndPoint::_hearthDeclaredCount = 0;
bool MatterEndPoint::_hearthReconciled = false;

MatterEndPoint::~MatterEndPoint() {
  hearthUndeclare(this);
}

uint16_t MatterEndPoint::getEndPointId() {
  return endpoint_id;
}

/*
 * Guard shared by every attribute accessor below. endpoint_id is 0 until
 * ArduinoMatter::begin() adopts a real one from the C6, and setEndPointId()
 * refuses 0, so 0 means exactly one thing: this endpoint has not been
 * reconciled, because begin() was never called or it aborted. Formatting
 * AT+MTATTR=0,... in that state does not fail: 0 is the co-processor's Root
 * Node, a real endpoint that exists on every device, so the sketch silently
 * reads and writes attributes on it instead of on its own light or sensor.
 * Fail here, before the wire, carrying the protocol's own code 2 ("unknown
 * endpoint", AT_MT_SPEC.md S5), which is precisely what the condition is.
 */
bool MatterEndPoint::hearthEndPointAddressable() {
  if (endpoint_id != 0) {
    return true;
  }
  Hearth.hearthSetError(2);
  return false;
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
  if (!hearthEndPointAddressable()) {
    return false;
  }
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
  if (!hearthEndPointAddressable()) {
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

/*
 * Base default: ESP_MATTER_VAL_TYPE_INTEGER, i.e. today's behaviour before
 * any endpoint type overrode this. Cluster/attribute IDs are unused here on
 * purpose; a concrete type overrides this only for the (cluster, attribute)
 * pairs it actually knows about and falls through to this for the rest.
 */
esp_matter_val_type_t MatterEndPoint::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  (void)cluster_id;
  (void)attribute_id;
  return ESP_MATTER_VAL_TYPE_INTEGER;
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
  if (ep == nullptr) {
    return false;
  }
  /*
   * Already registered: update in place rather than appending a second
   * entry for the same object. begin(); end(); begin(); on one device
   * object is a plausible sketch (upstream's end() is documented as "just
   * stop processing events", so restarting is legitimate), and appending
   * turned that into a different, longer composition, which reconcile then
   * "fixed" with a clear/apply/co-processor-reboot cycle the sketch never
   * asked for. An exact repeat changes nothing at all and is allowed even
   * after reconcile; a repeat that changes the device type is a real
   * composition change and goes through the same post-reconcile refusal as
   * a brand new declaration.
   */
  for (uint8_t i = 0; i < _hearthDeclaredCount; i++) {
    if (_hearthDeclared[i].ep != ep) {
      continue;
    }
    if (_hearthDeclared[i].deviceTypeId == deviceTypeId) {
      return true;
    }
    if (_hearthReconciled) {
      Hearth.hearthSetError(10);
      return false;
    }
    _hearthDeclared[i].deviceTypeId = deviceTypeId;
    return true;
  }
  if (_hearthDeclaredCount >= HEARTH_MAX_ENDPOINTS) {
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

/*
 * Remove `ep` if it is registered, shifting everything after it down so
 * declaration order (which is what endpoint IDs are assigned from) survives
 * the removal. A no-op for an endpoint that was never declared, which is
 * the common case: the destructor calls this unconditionally, and plenty of
 * MatterEndPoint objects (a stack temporary, one whose begin() was refused)
 * never made it into the registry.
 */
void MatterEndPoint::hearthUndeclare(MatterEndPoint *ep) {
  if (ep == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < _hearthDeclaredCount; i++) {
    if (_hearthDeclared[i].ep != ep) {
      continue;
    }
    for (uint8_t j = (uint8_t)(i + 1); j < _hearthDeclaredCount; j++) {
      _hearthDeclared[j - 1] = _hearthDeclared[j];
    }
    _hearthDeclaredCount--;
    _hearthDeclared[_hearthDeclaredCount].ep = nullptr;
    _hearthDeclared[_hearthDeclaredCount].deviceTypeId = 0;
    return;
  }
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
  /* A new composition is a new set of inputs, so it deserves a real
   * reconcile attempt rather than inheriting the previous one's verdict.
   * See HearthClass::hearthReconcileFailed(). */
  Hearth.hearthSetReconcileFailed(false);
}

void MatterEndPoint::hearthMarkReconciled() {
  _hearthReconciled = true;
}

MatterEndPoint *MatterEndPoint::hearthFindByEndPointId(uint16_t ep) {
  /* 0 is the Root Node and also the "not reconciled yet" value every
   * declared endpoint carries, so a match on it is never right: see the
   * header. This is the only place the distinction can be made, because
   * from here the two cases are indistinguishable. */
  if (ep == 0) {
    return nullptr;
  }
  for (uint8_t i = 0; i < _hearthDeclaredCount; i++) {
    if (_hearthDeclared[i].ep->getEndPointId() == ep) {
      return _hearthDeclared[i].ep;
    }
  }
  return nullptr;
}

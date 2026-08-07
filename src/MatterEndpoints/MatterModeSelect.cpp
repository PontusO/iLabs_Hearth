/*
 * MatterModeSelect.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the SupportedModes /
 * CurrentMode split (one has no AT+MTATTR path and is not persisted, the
 * other is a plain persisted attribute), and the host-side grammar
 * enforcement rationale for setSupportedModes().
 */
#include "MatterEndpoints/MatterModeSelect.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* mode_select (ESP_MATTER_MODE_SELECT_DEVICE_TYPE_ID), ModeSelect::Id, and
 * ModeSelect::Attributes::CurrentMode::Id. See MatterModeSelect.h's header
 * comment for the quoted lines from the pinned esp-matter checkout's
 * generated headers. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kModeSelectDeviceType = 0x0027;
const uint32_t kModeSelectClusterId = 0x0050;  // 80 decimal
const uint32_t kCurrentModeAttributeId = 0x0003;
}  // namespace

MatterModeSelect::MatterModeSelect() {}

MatterModeSelect::~MatterModeSelect() {
  end();
}

bool MatterModeSelect::begin() {
  if (!hearthDeclare(this, kModeSelectDeviceType)) {
    return false;
  }
  currentMode = 0;
  supportedModesCount = 0;
  started = true;
  return true;
}

void MatterModeSelect::end() {
  started = false;
  supportedModesCount = 0;
}

/*
 * Builds and sends "AT+MTMODES=<ep>,<mode1>,"<label1>",..." (AT_MT_SPEC.md
 * S3.20) for exactly the pairs given. Wire-only: the caller decides
 * whether/what to commit to the cache afterwards (house discipline: a
 * failed write must not update it).
 */
bool MatterModeSelect::hearthSendSupportedModes(const uint8_t *modes, const char *const *labels, uint8_t count) {
  char cmd[400];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u", (unsigned)getEndPointId());
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,\"%s\"", (unsigned)modes[i], labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Host-side grammar enforcement (S3.20, see the header comment for why):
 * count bounds, mode uniqueness within THIS call's list, and per-label
 * length/printability/quote-exclusion. Every violation reports
 * Hearth.hearthSetError(1), the wire's own "grammar violation" code,
 * without ever reaching the wire.
 */
bool MatterModeSelect::setSupportedModes(const uint8_t *modes, const char *const *labels, uint8_t count) {
  if (!started) {
    return false;
  }
  if (modes == nullptr || labels == nullptr || count == 0 || count > kMaxModes) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint8_t i = 0; i < count; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < count; j++) {
      if (modes[i] == modes[j]) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
    if (labels[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(labels[i]);
    if (len == 0 || len > kMaxLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* AT_MT_SPEC.md S3.20's own grammar, checked host-side before the wire
     * would have to: every byte printable ASCII (0x20..0x7E), and never a
     * '"'. A comma is deliberately NOT rejected here: S3.20 states plainly
     * that a comma inside a quoted label is legal and part of its text. */
    for (const char *p = labels[i]; *p != '\0'; p++) {
      unsigned char ch = (unsigned char)*p;
      if (ch < 0x20 || ch > 0x7E || ch == '"') {
        Hearth.hearthSetError(1);
        return false;
      }
    }
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  if (!hearthSendSupportedModes(modes, labels, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(supportedModesArray, modes, count * sizeof(uint8_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(supportedLabels[i], labels[i], kMaxLabelLen);
    supportedLabels[i][kMaxLabelLen] = '\0';
  }
  supportedModesCount = count;
  return true;
}

/*
 * Plain AT+MTATTR write (cluster 80, attribute 3), the same shape as
 * MatterOnOffLight::setOnOff(): CurrentMode is an ordinary esp_matter
 * attribute, unlike SupportedModes (see the header comment).
 */
bool MatterModeSelect::setCurrentMode(uint8_t m) {
  if (!started) {
    return false;
  }
  if (currentMode == m) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(m);
  if (!updateAttributeVal(kModeSelectClusterId, kCurrentModeAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  currentMode = m;
  return true;
}

uint8_t MatterModeSelect::getCurrentMode() {
  return currentMode;
}

void MatterModeSelect::onChangeMode(std::function<void(uint8_t)> cb) {
  _onChangeModeCB = cb;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first: resends the cached SupportedModes list, which the
 * firmware does not persist across a reboot (S3.20), the same B120 shape as
 * MatterTemperatureControlledCabinet's TemperatureLevel labels. A no-op
 * (nothing to resend) until the sketch has called setSupportedModes() at
 * least once; unlike the door lock or the cabinet's TemperatureNumber mode,
 * there is no sketch-declared initial value at begin() to push here for
 * CurrentMode either -- see the header comment.
 */
void MatterModeSelect::hearthOnReconciled() {
  if (!started || supportedModesCount == 0) {
    return;
  }
  const char *labelPtrs[kMaxModes];
  for (uint8_t i = 0; i < supportedModesCount; i++) {
    labelPtrs[i] = supportedLabels[i];
  }
  hearthSendSupportedModes(supportedModesArray, labelPtrs, supportedModesCount);
}

/*
 * +MTATTR-driven cache update and onChangeMode() dispatch for CurrentMode
 * (cluster 80, attr 3): a controller's ChangeToMode command (validated and
 * applied entirely inside the SDK, per the header comment) or the firmware
 * itself changed the attribute out from under this host; the generic
 * dispatch (Hearth.cpp's hearthDispatchAttr()) routes it here via the base
 * class's attributeChangeCB() contract.
 */
bool MatterModeSelect::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kModeSelectClusterId) {
    return true;
  }
  if (attribute_id == kCurrentModeAttributeId) {
    currentMode = val->val.u8;
    if (_onChangeModeCB) {
      _onChangeModeCB(currentMode);
    }
  }
  return true;
}

esp_matter_val_type_t MatterModeSelect::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kModeSelectClusterId && attribute_id == kCurrentModeAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

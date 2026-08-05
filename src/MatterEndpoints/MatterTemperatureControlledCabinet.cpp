/*
 * MatterTemperatureControlledCabinet.cpp - implementation. See the header
 * for the four documented deviations from a literal transcription of
 * upstream's .cpp (validate-before-declare in both begin() overloads, no
 * initial-value AT traffic from begin() itself, cache-only getters, and
 * setSupportedTemperatureLevelLabels() as a Hearth-only addition).
 */
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* temperature_controlled_cabinet (esp_matter_endpoint.h's
 * ESP_MATTER_TEMPERATURE_CONTROLLED_CABINET_DEVICE_TYPE_ID), TemperatureControl::Id
 * (0x00000056, connectedhomeip's zap-generated ids/Clusters.h:172-174 at the
 * 3.3.8-bundled revision), and TemperatureControl::Attributes::
 * {TemperatureSetpoint,MinTemperature,MaxTemperature,Step,SelectedTemperatureLevel}
 * ::Id (ids/Attributes.h:2777-2795, same revision). Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kCabinetDeviceType = 0x0071;
const uint32_t kTemperatureControlClusterId = 0x0056;
const uint32_t kTemperatureSetpointAttributeId = 0x0000;
const uint32_t kMinTemperatureAttributeId = 0x0001;
const uint32_t kMaxTemperatureAttributeId = 0x0002;
const uint32_t kStepAttributeId = 0x0003;
const uint32_t kSelectedTemperatureLevelAttributeId = 0x0004;

/* AT+MTEP=<devtype>,<variant> (AT_MT_SPEC.md S3.9): 0 = TemperatureNumber,
 * 1 = TemperatureLevel. The two variants are mutually exclusive at the CHIP
 * level (the fifth abort trap, VALIDATE_FEATURES_EXACT_ONE, is what makes
 * this a firmware-side hard requirement, not just an API convention). */
const uint8_t kVariantTemperatureNumber = 0;
const uint8_t kVariantTemperatureLevel = 1;
}  // namespace

MatterTemperatureControlledCabinet::MatterTemperatureControlledCabinet() {}

MatterTemperatureControlledCabinet::~MatterTemperatureControlledCabinet() {
  end();
}

bool MatterTemperatureControlledCabinet::begin(double tempSetpoint, double minTemperature, double maxTemperature, double step) {
  /* Upstream's exact double->raw conversion (its own begin(double,...) body):
   * static_cast<int16_t>(v * 100.0), hundredths of a degree. */
  int16_t rawSetpoint = static_cast<int16_t>(tempSetpoint * 100.0);
  int16_t rawMin = static_cast<int16_t>(minTemperature * 100.0);
  int16_t rawMax = static_cast<int16_t>(maxTemperature * 100.0);
  int16_t rawStepValue = static_cast<int16_t>(step * 100.0);
  return begin(rawSetpoint, rawMin, rawMax, rawStepValue);
}

bool MatterTemperatureControlledCabinet::begin(int16_t _rawTempSetpoint, int16_t _rawMinTemperature, int16_t _rawMaxTemperature, int16_t _rawStep) {
  /* Deviation 1: hearthDeclare() first (it is what actually refuses a
   * re-begin after reconcile, +MTERR:10), before any member state changes,
   * so a refused call leaves the cache exactly as it was. */
  if (!hearthDeclare(this, kCabinetDeviceType, kVariantTemperatureNumber)) {
    return false;
  }
  rawTempSetpoint = _rawTempSetpoint;
  rawMinTemperature = _rawMinTemperature;
  rawMaxTemperature = _rawMaxTemperature;
  rawStep = _rawStep;
  selectedTempLevel = 0;
  supportedLevelsCount = 0;
  levelLabelCount = 0;
  useTemperatureNumber = true;
  started = true;
  return true;
}

bool MatterTemperatureControlledCabinet::begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel) {
  if (supportedLevels == nullptr || levelCount == 0 || levelCount > kMaxSupportedLevels) {
    return false;
  }
  /* Validate that selectedLevel exists in supportedLevels, upstream's own
   * check, before ever calling hearthDeclare() (deviation 1). */
  bool found = false;
  for (uint16_t i = 0; i < levelCount; i++) {
    if (supportedLevels[i] == selectedLevel) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  return beginInternal(supportedLevels, levelCount, selectedLevel);
}

bool MatterTemperatureControlledCabinet::beginInternal(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel) {
  if (!hearthDeclare(this, kCabinetDeviceType, kVariantTemperatureLevel)) {
    return false;
  }
  memcpy(supportedLevelsArray, supportedLevels, levelCount * sizeof(uint8_t));
  supportedLevelsCount = levelCount;
  selectedTempLevel = selectedLevel;
  useTemperatureNumber = false;
  rawTempSetpoint = 0;
  rawMinTemperature = 0;
  rawMaxTemperature = 0;
  rawStep = 0;
  hearthGenerateDefaultLabels();
  started = true;
  return true;
}

void MatterTemperatureControlledCabinet::end() {
  started = false;
  useTemperatureNumber = true;
  supportedLevelsCount = 0;
  levelLabelCount = 0;
}

void MatterTemperatureControlledCabinet::hearthGenerateDefaultLabels() {
  /* "Level <n>": n is the level IDENTIFIER (supportedLevelsArray[i]'s own
   * value), not the array index, per the design spec S5. */
  for (uint16_t i = 0; i < supportedLevelsCount; i++) {
    snprintf(levelLabels[i], sizeof(levelLabels[i]), "Level %u", (unsigned)supportedLevelsArray[i]);
  }
  levelLabelCount = supportedLevelsCount;
}

/*
 * Builds and sends "AT+MTTEMPLEVELS=<ep>,"<label1>",...,"<labelN>""
 * (AT_MT_SPEC.md S3.16) for exactly the labels given. Wire-only: the caller
 * decides whether/what to commit to the cache afterwards (house discipline:
 * a failed write must not update cached state).
 */
bool MatterTemperatureControlledCabinet::hearthSendLevelLabels(const char *const *labels, uint16_t count) {
  char cmd[400];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTTEMPLEVELS=%u", (unsigned)getEndPointId());
  for (uint16_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",\"%s\"", labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h): resend the TemperatureLevel
 * state that the C6 does not persist across a reboot (AT_MT_SPEC.md S3.16),
 * on every reconcile, not only the first. No-op in TemperatureNumber mode:
 * see the header's deviation 2, there is nothing to resend there, upstream's
 * config-at-creation approach has no equivalent on this wire.
 */
void MatterTemperatureControlledCabinet::hearthOnReconciled() {
  if (!started || useTemperatureNumber) {
    return;
  }
  const char *ptrs[kMaxSupportedLevels];
  for (uint16_t i = 0; i < levelLabelCount; i++) {
    ptrs[i] = levelLabels[i];
  }
  /* Best-effort: this runs deep inside ArduinoMatter::begin(), which has
   * already committed to its own success/failure verdict for the endpoint
   * composition itself by the time this hook runs. A failure here has no
   * further recourse within this call; the labels/level simply stay whatever
   * the C6 already has until the next reconcile tries again. */
  hearthSendLevelLabels(ptrs, levelLabelCount);

  esp_matter_attr_val_t val = esp_matter_uint8(selectedTempLevel);
  updateAttributeVal(kTemperatureControlClusterId, kSelectedTemperatureLevelAttributeId, &val);
}

bool MatterTemperatureControlledCabinet::setRawTemperatureSetpoint(int16_t _rawTemperature) {
  if (!started || !useTemperatureNumber) {
    return false;
  }
  if (_rawTemperature < rawMinTemperature || _rawTemperature > rawMaxTemperature) {
    return false;
  }
  if (rawTempSetpoint == _rawTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawTemperature);
  if (!updateAttributeVal(kTemperatureControlClusterId, kTemperatureSetpointAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  rawTempSetpoint = _rawTemperature;
  return true;
}

bool MatterTemperatureControlledCabinet::setTemperatureSetpoint(double temperature) {
  return setRawTemperatureSetpoint(static_cast<int16_t>(temperature * 100.0));
}

double MatterTemperatureControlledCabinet::getTemperatureSetpoint() {
  return (double)rawTempSetpoint / 100.0;
}

bool MatterTemperatureControlledCabinet::setRawMinTemperature(int16_t _rawTemperature) {
  if (!started || !useTemperatureNumber) {
    return false;
  }
  if (rawMinTemperature == _rawTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawTemperature);
  if (!updateAttributeVal(kTemperatureControlClusterId, kMinTemperatureAttributeId, &val)) {
    return false;
  }
  rawMinTemperature = _rawTemperature;
  return true;
}

bool MatterTemperatureControlledCabinet::setMinTemperature(double temperature) {
  return setRawMinTemperature(static_cast<int16_t>(temperature * 100.0));
}

double MatterTemperatureControlledCabinet::getMinTemperature() {
  return (double)rawMinTemperature / 100.0;
}

bool MatterTemperatureControlledCabinet::setRawMaxTemperature(int16_t _rawTemperature) {
  if (!started || !useTemperatureNumber) {
    return false;
  }
  if (rawMaxTemperature == _rawTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawTemperature);
  if (!updateAttributeVal(kTemperatureControlClusterId, kMaxTemperatureAttributeId, &val)) {
    return false;
  }
  rawMaxTemperature = _rawTemperature;
  return true;
}

bool MatterTemperatureControlledCabinet::setMaxTemperature(double temperature) {
  return setRawMaxTemperature(static_cast<int16_t>(temperature * 100.0));
}

double MatterTemperatureControlledCabinet::getMaxTemperature() {
  return (double)rawMaxTemperature / 100.0;
}

bool MatterTemperatureControlledCabinet::setRawStep(int16_t _rawStep) {
  if (!started || !useTemperatureNumber) {
    return false;
  }
  if (rawStep == _rawStep) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawStep);
  if (!updateAttributeVal(kTemperatureControlClusterId, kStepAttributeId, &val)) {
    return false;
  }
  rawStep = _rawStep;
  return true;
}

bool MatterTemperatureControlledCabinet::setStep(double step) {
  return setRawStep(static_cast<int16_t>(step * 100.0));
}

double MatterTemperatureControlledCabinet::getStep() {
  return (double)rawStep / 100.0;
}

bool MatterTemperatureControlledCabinet::setSelectedTemperatureLevel(uint8_t level) {
  if (!started || useTemperatureNumber) {
    return false;
  }
  bool found = false;
  for (uint16_t i = 0; i < supportedLevelsCount; i++) {
    if (supportedLevelsArray[i] == level) {
      found = true;
      break;
    }
  }
  if (!found) {
    return false;
  }
  if (selectedTempLevel == level) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(level);
  if (!updateAttributeVal(kTemperatureControlClusterId, kSelectedTemperatureLevelAttributeId, &val)) {
    return false;
  }
  selectedTempLevel = level;
  return true;
}

uint8_t MatterTemperatureControlledCabinet::getSelectedTemperatureLevel() {
  return selectedTempLevel;
}

bool MatterTemperatureControlledCabinet::setSupportedTemperatureLevels(uint8_t *levels, uint16_t count) {
  if (!started || useTemperatureNumber) {
    return false;
  }
  if (levels == nullptr || count == 0 || count > kMaxSupportedLevels) {
    return false;
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  /* SupportedTemperatureLevels has no bare-numeric wire form on this stack
   * (see the header's top comment): regenerate default labels for the new
   * identifier set and send those, the only channel this attribute has. */
  char generated[kMaxSupportedLevels][kMaxLevelLabelLen + 1];
  const char *ptrs[kMaxSupportedLevels];
  for (uint16_t i = 0; i < count; i++) {
    snprintf(generated[i], sizeof(generated[i]), "Level %u", (unsigned)levels[i]);
    ptrs[i] = generated[i];
  }
  if (!hearthSendLevelLabels(ptrs, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(supportedLevelsArray, levels, count * sizeof(uint8_t));
  supportedLevelsCount = count;
  for (uint16_t i = 0; i < count; i++) {
    strncpy(levelLabels[i], generated[i], kMaxLevelLabelLen);
    levelLabels[i][kMaxLevelLabelLen] = '\0';
  }
  levelLabelCount = count;
  return true;
}

uint16_t MatterTemperatureControlledCabinet::getSupportedTemperatureLevelsCount() {
  return supportedLevelsCount;
}

/*
 * Hearth-only extension (header deviation 4): real label text, sent
 * verbatim rather than generated from the numeric identifiers. Validates
 * AT_MT_SPEC.md S3.16's count/length grammar host-side before ever touching
 * the wire, exactly the shape a wire-side +MTERR:1 would reject.
 */
bool MatterTemperatureControlledCabinet::setSupportedTemperatureLevelLabels(const char *const *labels, uint16_t count) {
  if (!started || useTemperatureNumber) {
    return false;
  }
  if (labels == nullptr || count == 0 || count > kMaxSupportedLevels) {
    Hearth.hearthSetError(1);
    return false;
  }
  for (uint16_t i = 0; i < count; i++) {
    if (labels[i] == nullptr) {
      Hearth.hearthSetError(1);
      return false;
    }
    size_t len = strlen(labels[i]);
    if (len == 0 || len > kMaxLevelLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* AT_MT_SPEC.md S3.16's own grammar, checked host-side before the wire
     * would have to: every byte printable ASCII (0x20..0x7E), and never a
     * '"'. Sending a '"' through unescaped would not come back as a clean
     * +MTERR:1 from the firmware parser, it would corrupt the field
     * boundary of the AT+MTTEMPLEVELS line itself (a bare label byte of
     * '"' looks exactly like the closing quote the parser is scanning
     * for), so this must be caught here, not left for the wire to reject. */
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
  if (!hearthSendLevelLabels(labels, count)) {
    return false;  // cache untouched on a failed write
  }
  for (uint16_t i = 0; i < count; i++) {
    strncpy(levelLabels[i], labels[i], kMaxLevelLabelLen);
    levelLabels[i][kMaxLevelLabelLen] = '\0';
  }
  levelLabelCount = count;
  return true;
}

/*
 * Mirrors upstream's structure: ignore an update for the attribute of the
 * mode that is NOT active (the two feature sets are mutually exclusive, so
 * only one is ever real on the live cluster, but the wire dispatch here has
 * no other way to filter by mode than this class's own useTemperatureNumber
 * flag).
 */
bool MatterTemperatureControlledCabinet::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kTemperatureControlClusterId) {
    return true;
  }
  if (attribute_id == kTemperatureSetpointAttributeId) {
    if (useTemperatureNumber) {
      rawTempSetpoint = val->val.i16;
    }
  } else if (attribute_id == kMinTemperatureAttributeId) {
    if (useTemperatureNumber) {
      rawMinTemperature = val->val.i16;
    }
  } else if (attribute_id == kMaxTemperatureAttributeId) {
    if (useTemperatureNumber) {
      rawMaxTemperature = val->val.i16;
    }
  } else if (attribute_id == kStepAttributeId) {
    if (useTemperatureNumber) {
      rawStep = val->val.i16;
    }
  } else if (attribute_id == kSelectedTemperatureLevelAttributeId) {
    if (!useTemperatureNumber) {
      selectedTempLevel = val->val.u8;
    }
  }
  return true;
}

esp_matter_val_type_t MatterTemperatureControlledCabinet::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kTemperatureControlClusterId) {
    if (attribute_id == kSelectedTemperatureLevelAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT8;
    }
    if (attribute_id == kTemperatureSetpointAttributeId || attribute_id == kMinTemperatureAttributeId
        || attribute_id == kMaxTemperatureAttributeId || attribute_id == kStepAttributeId) {
      return ESP_MATTER_VAL_TYPE_INT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

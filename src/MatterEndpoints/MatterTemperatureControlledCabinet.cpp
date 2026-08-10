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

/* Task 8 (composed-appliance round):
 * RefrigeratorAndTemperatureControlledCabinetMode::Id (0x00000052,
 * connectedhomeip's zap-generated clusters/
 * RefrigeratorAndTemperatureControlledCabinetMode/ClusterId.h, "cluster
 * code: 82/0x52"), the conditional cluster the firmware derives onto a
 * Cooler cabinet composed under a Refrigerator (AT_MT_SPEC.md S3.9's 0x0071
 * note); ChangeToMode is ModeBase's command 0x0000, the same id every
 * ModeBase derivation shares (see MatterRoboticVacuum.h's quoted source). */
const uint32_t kRefrigeratorTccModeClusterId = 0x0052;  // 82 decimal
const uint32_t kChangeToModeCommandId = 0x0000;
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
  /* Task 8 owned path: the parent already declared this cabinet, parent
   * index and all, so a hearthDeclare() here would update the entry in
   * place and wipe that parent index (see the header comment). The owned
   * begin() therefore only validates and caches: refused outright on the
   * inert reject cabinet, on a re-begin while started (the same refusal
   * hearthDeclare()'s post-reconcile check gives the unowned path), and on
   * a flavour that does not match the declared variant. */
  if (hearthOwnedInert) {
    return false;
  }
  if (hearthOwnedByFridge) {
    if (started || hearthOwnedLevels) {
      return false;
    }
  } else if (!hearthDeclare(this, kCabinetDeviceType, kVariantTemperatureNumber)) {
    /* Deviation 1 (unowned path): hearthDeclare() first (it is what
     * actually refuses a re-begin after reconcile, +MTERR:10), before any
     * member state changes, so a refused call leaves the cache exactly as
     * it was. */
    return false;
  }
  rawTempSetpoint = _rawTempSetpoint;
  rawMinTemperature = _rawMinTemperature;
  rawMaxTemperature = _rawMaxTemperature;
  rawStep = _rawStep;
  selectedTempLevel = 0;
  supportedLevelsCount = 0;
  levelLabelCount = 0;
  fridgeModesCount = 0;
  currentFridgeMode = 0;
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
  /* Task 8 owned path, the mirror image of the TemperatureNumber begin()
   * above: no declaration (the parent's is authoritative), refused on the
   * inert cabinet, on a re-begin while started, and on a NUMBER-declared
   * cabinet (the flavour mismatch). */
  if (hearthOwnedInert) {
    return false;
  }
  if (hearthOwnedByFridge) {
    if (started || !hearthOwnedLevels) {
      return false;
    }
  } else if (!hearthDeclare(this, kCabinetDeviceType, kVariantTemperatureLevel)) {
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
  fridgeModesCount = 0;
  currentFridgeMode = 0;
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
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first. Two symmetric jobs, one per mode (see the header's
 * deviation 2 for the full "why" of both):
 *
 * TemperatureLevel: resend the label list the C6 does not persist across a
 * reboot (AT_MT_SPEC.md S3.16), then SelectedTemperatureLevel.
 *
 * TemperatureNumber (fix round 2, a real bench bug): push the four cached
 * values -- min, max, step, setpoint, in that order -- directly via
 * updateAttributeVal(), NOT by calling setMinTemperature()/setStep()/etc.
 * Those setters' skip-if-equal ("if (cached == new) return true;") is sound
 * only when the cache already mirrors the live device, which is exactly
 * what does NOT hold here: begin()'s cache is seeded from the sketch's own
 * arguments, while the C6 creates the TemperatureNumber cluster at
 * esp-matter's own defaults (0/10/1), not the sketch's. Calling the setter
 * with the sketch's own begin() value therefore always hit the skip branch
 * and never reached the wire at all -- confirmed on the bench: the device
 * stayed at 0/10/1 forever, and a controller's SetTemperature command
 * answered CONSTRAINT_ERROR against bounds the sketch believed it had set.
 * Raw AT writes of the same values propagated fine, exonerating the
 * firmware. Pushing here, once, unconditionally, is what establishes
 * cache==device in the first place; only after that does the setters' own
 * skip-if-equal become the correct optimisation it already is for every
 * other class in this library (including MatterThermostat, whose begin()
 * cache seeds deliberately MATCH the firmware thunk's own seeded defaults,
 * so its cache equals the device from boot -- a precedent that looks
 * applicable here but is not: the cabinet's TN seeds come from the sketch,
 * the thermostat's come from the firmware, and only one of those is true by
 * construction).
 */
void MatterTemperatureControlledCabinet::hearthOnReconciled() {
  if (!started) {
    return;
  }
  /*
   * Best-effort throughout, both branches: this runs deep inside
   * ArduinoMatter::begin(), which has already committed to its own
   * success/failure verdict for the endpoint composition itself by the time
   * this hook runs. A failed push here has no further recourse within this
   * call; the affected value simply stays whatever the C6 already has until
   * the next reconcile tries again. No cache mutation either way: the cache
   * already holds the value the sketch intends (from begin() or a prior
   * successful setter call), so this only ever tries to make the device
   * match it, never the reverse.
   */
  if (useTemperatureNumber) {
    esp_matter_attr_val_t minVal = esp_matter_int16(rawMinTemperature);
    updateAttributeVal(kTemperatureControlClusterId, kMinTemperatureAttributeId, &minVal);
    esp_matter_attr_val_t maxVal = esp_matter_int16(rawMaxTemperature);
    updateAttributeVal(kTemperatureControlClusterId, kMaxTemperatureAttributeId, &maxVal);
    esp_matter_attr_val_t stepVal = esp_matter_int16(rawStep);
    updateAttributeVal(kTemperatureControlClusterId, kStepAttributeId, &stepVal);
    esp_matter_attr_val_t setpointVal = esp_matter_int16(rawTempSetpoint);
    updateAttributeVal(kTemperatureControlClusterId, kTemperatureSetpointAttributeId, &setpointVal);
  } else {
    const char *ptrs[kMaxSupportedLevels];
    for (uint16_t i = 0; i < levelLabelCount; i++) {
      ptrs[i] = levelLabels[i];
    }
    hearthSendLevelLabels(ptrs, levelLabelCount);

    esp_matter_attr_val_t val = esp_matter_uint8(selectedTempLevel);
    updateAttributeVal(kTemperatureControlClusterId, kSelectedTemperatureLevelAttributeId, &val);
  }

  /* Task 8: an owned Cooler cabinet resends its cached 0x0052 mode list on
   * every reconcile, after the temperature push above, which the firmware
   * does not persist across a reboot (S3.20.1). A no-op when nothing has
   * been set yet, and structurally unreachable for an unowned cabinet
   * (setSupportedModes() refuses to populate the cache there). */
  if (hearthOwnedByFridge && fridgeModesCount > 0) {
    const char *labelPtrs[kMaxFridgeModes];
    for (uint8_t i = 0; i < fridgeModesCount; i++) {
      labelPtrs[i] = fridgeModeLabels[i];
    }
    hearthSendFridgeModes(fridgeModes, fridgeTags, labelPtrs, fridgeModesCount);
  }
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

/*
 * Builds and sends "AT+MTMODES=<ep>,82,<mode1>,<tag1>,"<label1>",..."
 * (AT_MT_SPEC.md S3.20.1's cluster-aware form) for exactly the triples
 * given. Wire-only: the caller decides whether/what to commit to the cache
 * afterwards (house discipline: a failed write must not update it). Same
 * shape as MatterRoboticVacuum::hearthSendModes() with the cluster fixed.
 */
bool MatterTemperatureControlledCabinet::hearthSendFridgeModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  char cmd[500];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMODES=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kRefrigeratorTccModeClusterId);
  for (uint8_t i = 0; i < count && n > 0 && (size_t)n < sizeof(cmd); i++) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%u,%u,\"%s\"", (unsigned)modes[i], (unsigned)tags[i], labels[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Task 8: replace the owned cabinet's 0x0052 SupportedModes list. Refused
 * outright on an unowned (or inert, or unstarted) cabinet: the cluster only
 * exists when the firmware derived it from a Refrigerator parent (S3.9's
 * 0x0071 note), so there is nothing on the wire for this to reach. The
 * grammar enforcement below is S3.20.1 verbatim, the identical discipline
 * MatterRoboticVacuum::hearthSetModeList() established: count bounds, mode
 * uniqueness within this call, per-label length/printability/quote
 * exclusion, every violation Hearth.hearthSetError(1) with no wire traffic;
 * an unaddressable endpoint (pre-reconcile) is hearthSetError(2). Cache
 * commit only after a successful wire write.
 */
bool MatterTemperatureControlledCabinet::setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count) {
  if (!hearthOwnedByFridge || hearthOwnedInert || !started) {
    return false;
  }
  if (modes == nullptr || tags == nullptr || labels == nullptr || count == 0 || count > kMaxFridgeModes) {
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
    if (len == 0 || len > kMaxFridgeModeLabelLen) {
      Hearth.hearthSetError(1);
      return false;
    }
    /* S3.20.1's own grammar, checked host-side before the wire would have
     * to: every byte printable ASCII (0x20..0x7E), and never a '"'. A comma
     * is deliberately NOT rejected: legal inside a quoted label. */
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
  if (!hearthSendFridgeModes(modes, tags, labels, count)) {
    return false;  // cache untouched on a failed write
  }
  memcpy(fridgeModes, modes, count * sizeof(uint8_t));
  memcpy(fridgeTags, tags, count * sizeof(uint16_t));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(fridgeModeLabels[i], labels[i], kMaxFridgeModeLabelLen);
    fridgeModeLabels[i][kMaxFridgeModeLabelLen] = '\0';
  }
  fridgeModesCount = count;
  return true;
}

void MatterTemperatureControlledCabinet::onChangeMode(std::function<bool(uint8_t)> cb) {
  _onChangeModeCB = cb;
}

uint8_t MatterTemperatureControlledCabinet::getCurrentMode() {
  return currentFridgeMode;
}

/*
 * Task 8: a controller-invoked ChangeToMode on this cabinet's own 0x0052
 * cluster arrives here for a verdict (S3.17/S3.20.1), the requested mode as
 * fields.value[0]. Only an owned, started cabinet ever adjudicates: an
 * unowned cabinet has no such cluster, so a (spurious) forward defers to
 * the base class default and is denied, registered callback or not. The
 * cache updates ONLY on an allow, the 0.6.0 CurrentMode rule (see the
 * header comment): there is no ember-level signal of any kind for a
 * CurrentMode change on a ModeBase-derived cluster, so the verdict this
 * host itself gives is the only trustworthy record of what the device's
 * CurrentMode actually became.
 */
bool MatterTemperatureControlledCabinet::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (hearthOwnedByFridge && !hearthOwnedInert && started && cluster_id == kRefrigeratorTccModeClusterId && command_id == kChangeToModeCommandId) {
    uint8_t requested = (fields.count > 0 && fields.present[0]) ? (uint8_t)fields.value[0] : 0;
    bool allow = _onChangeModeCB ? _onChangeModeCB(requested) : false;
    if (allow) {
      currentFridgeMode = requested;
    }
    return allow;
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
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

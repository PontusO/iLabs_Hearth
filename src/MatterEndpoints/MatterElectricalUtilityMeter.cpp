/*
 * MatterElectricalUtilityMeter.cpp - implementation. See the header for the
 * design notes: why the wire is one bundled command wearing five setters,
 * the power-threshold precondition, host-side validation, why there is no
 * read-back or getters, and the B229 reconcile pattern.
 */
#include "MatterEndpoints/MatterElectricalUtilityMeter.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* electrical_utility_meter (ESP_MATTER_ELECTRICAL_UTILITY_METER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:164,
 * "#define ESP_MATTER_ELECTRICAL_UTILITY_METER_DEVICE_TYPE_ID 0x0511").
 * Given as a plain integer: there is no esp-matter header on a host build. */
const uint32_t kMeterDeviceType = 0x0511;
}  // namespace

/*
 * Host-side grammar enforcement for a MeterIdentification string field
 * (PointOfDelivery / MeterSerialNumber / ProtocolVersion), the
 * MatterModeSelect::setSupportedModes() precedent: 0..kMaxStrLen bytes,
 * every byte printable ASCII (0x20..0x7E), never a '"' (an unescaped quote
 * would corrupt the wire line's own field boundary rather than surviving as
 * a clean +MTERR:1). A comma is deliberately NOT rejected:
 * mtmeterid_scan_string() (mt_at.c) lets it through as legal content.
 */
bool MatterElectricalUtilityMeter::hearthValidateMeterString(const char *s) {
  if (s == nullptr) {
    return false;
  }
  size_t len = strlen(s);
  if (len > kMaxStrLen) {
    return false;
  }
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x20 || c > 0x7E || c == '"') {
      return false;
    }
  }
  return true;
}

MatterElectricalUtilityMeter::MatterElectricalUtilityMeter() {}

MatterElectricalUtilityMeter::~MatterElectricalUtilityMeter() {
  end();
}

bool MatterElectricalUtilityMeter::begin() {
  if (!hearthDeclare(this, kMeterDeviceType)) {
    return false;
  }
  hasMeterType = false;
  meterType = 0;
  hasPod = false;
  pod[0] = '\0';
  hasSerial = false;
  serial[0] = '\0';
  hasProtocol = false;
  protocol[0] = '\0';
  hasPwr = false;
  pwr = 0;
  hasApparent = false;
  apparent = 0;
  hasSrc = false;
  src = 0;
  started = true;
  return true;
}

void MatterElectricalUtilityMeter::end() {
  started = false;
}

/*
 * "AT+MTMETERID=<ep>,<type>,\"<pod>\",\"<serial>\",\"<protocol>\",<pwr>,
 * <apparent>,<src>" (mt_at.c's cmd_mtmeterid), built from exactly the
 * values given: an absent pwr/apparent/src is a present-but-empty token
 * (the comma survives, the value between it and its neighbour does not),
 * matching cmd_mtmeterid's own pwr_present/apparent_present/src_present
 * convention. 320 bytes covers the worst case (design spec 6.2: "about 271
 * bytes"): 13-byte prefix, a 5-digit endpoint, a 1-digit type, three
 * 64-byte quoted strings, two 20-character signed int64 fields and one
 * 1-digit enum, comma-separated.
 */
bool MatterElectricalUtilityMeter::hearthSendIdentity(
  uint8_t type, const char *podVal, const char *serialVal, const char *protocolVal, bool pwrPresent, int64_t pwrVal, bool apparentPresent,
  int64_t apparentVal, bool srcPresent, uint8_t srcVal
) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[320];
  int n = snprintf(
    cmd, sizeof(cmd), "AT+MTMETERID=%u,%u,\"%s\",\"%s\",\"%s\",", (unsigned)getEndPointId(), (unsigned)type, podVal, serialVal,
    protocolVal
  );
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  if (pwrPresent) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, "%lld", (long long)pwrVal);
  }
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",");
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  if (apparentPresent) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, "%lld", (long long)apparentVal);
  }
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",");
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  if (srcPresent) {
    n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, "%u", (unsigned)srcVal);
  }
  if (n < 0 || (size_t)n >= sizeof(cmd)) {
    return false;
  }
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterElectricalUtilityMeter::setMeterType(uint8_t type) {
  if (!started) {
    return false;
  }
  if (type > 2) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hasPwr && !hasApparent) {
    // choice b: no push can ever succeed without a power threshold already
    // set (see the header comment).
    Hearth.hearthSetError(1);
    return false;
  }
  if (hasMeterType && meterType == type) {
    return true;
  }
  if (!hearthSendIdentity(type, pod, serial, protocol, hasPwr, pwr, hasApparent, apparent, hasSrc, src)) {
    return false;  // cache untouched: the device's idea of the identity and
                    // the host's idea of it must not diverge
  }
  meterType = type;
  hasMeterType = true;
  return true;
}

bool MatterElectricalUtilityMeter::setPointOfDelivery(const char *newPod) {
  if (!started) {
    return false;
  }
  if (!hearthValidateMeterString(newPod)) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hasPwr && !hasApparent) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (hasPod && strcmp(pod, newPod) == 0) {
    return true;
  }
  if (!hearthSendIdentity(meterType, newPod, serial, protocol, hasPwr, pwr, hasApparent, apparent, hasSrc, src)) {
    return false;
  }
  strncpy(pod, newPod, kMaxStrLen);
  pod[kMaxStrLen] = '\0';
  hasPod = true;
  return true;
}

bool MatterElectricalUtilityMeter::setSerialNumber(const char *newSerial) {
  if (!started) {
    return false;
  }
  if (!hearthValidateMeterString(newSerial)) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hasPwr && !hasApparent) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (hasSerial && strcmp(serial, newSerial) == 0) {
    return true;
  }
  if (!hearthSendIdentity(meterType, pod, newSerial, protocol, hasPwr, pwr, hasApparent, apparent, hasSrc, src)) {
    return false;
  }
  strncpy(serial, newSerial, kMaxStrLen);
  serial[kMaxStrLen] = '\0';
  hasSerial = true;
  return true;
}

bool MatterElectricalUtilityMeter::setProtocolVersion(const char *newProtocol) {
  if (!started) {
    return false;
  }
  if (!hearthValidateMeterString(newProtocol)) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hasPwr && !hasApparent) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (hasProtocol && strcmp(protocol, newProtocol) == 0) {
    return true;
  }
  if (!hearthSendIdentity(meterType, pod, serial, newProtocol, hasPwr, pwr, hasApparent, apparent, hasSrc, src)) {
    return false;
  }
  strncpy(protocol, newProtocol, kMaxStrLen);
  protocol[kMaxStrLen] = '\0';
  hasProtocol = true;
  return true;
}

bool MatterElectricalUtilityMeter::setPowerThreshold(
  bool pwrPresent, int64_t pwrVal, bool apparentPresent, int64_t apparentVal, bool srcPresent, uint8_t srcVal
) {
  if (!started) {
    return false;
  }
  if (!pwrPresent && !apparentPresent) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (srcPresent && srcVal > 2) {
    Hearth.hearthSetError(1);
    return false;
  }
  bool unchanged = (hasPwr == pwrPresent) && (!pwrPresent || pwr == pwrVal) && (hasApparent == apparentPresent)
                   && (!apparentPresent || apparent == apparentVal) && (hasSrc == srcPresent) && (!srcPresent || src == srcVal);
  if (unchanged) {
    return true;
  }
  if (!hearthSendIdentity(meterType, pod, serial, protocol, pwrPresent, pwrVal, apparentPresent, apparentVal, srcPresent, srcVal)) {
    return false;
  }
  hasPwr = pwrPresent;
  pwr = pwrVal;
  hasApparent = apparentPresent;
  apparent = apparentVal;
  hasSrc = srcPresent;
  src = srcVal;
  return true;
}

/*
 * The B229 reconcile pattern (header comment): every field here is
 * configuration, so hearthOnReconciled() unconditionally re-sends the
 * CURRENT cached identity via the low-level wire-only helper, bypassing
 * every setter's own unchanged-value guard, exactly the way
 * MatterEvse::hearthOnReconciled() re-pushes CircuitCapacity. A no-op if
 * nothing has been configured yet: the power-threshold precondition every
 * setter enforces means hasPwr/hasApparent are both false only when NO
 * setter has ever succeeded, so this is the single correct gate.
 */
void MatterElectricalUtilityMeter::hearthOnReconciled() {
  if (!started || (!hasPwr && !hasApparent)) {
    return;
  }
  hearthSendIdentity(meterType, pod, serial, protocol, hasPwr, pwr, hasApparent, apparent, hasSrc, src);
}

/*
 * Cluster 0x0B06 is Instance-served for every one of its five attributes
 * (task 9's report): no +MTATTR URC is ever raised for it, and an injected
 * one must move nothing. This class keeps no attribute cache reachable from
 * attributeChangeCB() at all, so the body is a documented no-op returning
 * `started`, the MatterEvse/MatterElectricalSensor shape.
 */
bool MatterElectricalUtilityMeter::attributeChangeCB(
  uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val
) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

/*
 * MatterSmokeCOAlarm.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the S3.22 field
 * table this file's kField* constants transcribe, and the design
 * rationale (notify-only self test, ExpressedState's live read).
 */
#include "MatterEndpoints/MatterSmokeCOAlarm.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* smoke_co_alarm (ESP_MATTER_SMOKE_CO_ALARM_DEVICE_TYPE_ID),
 * SmokeCoAlarm::Id, SmokeCoAlarm::Attributes::ExpressedState::Id, and
 * SmokeCoAlarm::Commands::SelfTestRequest::Id. See MatterSmokeCOAlarm.h's
 * header comment for the quoted lines from the pinned esp-matter
 * checkout's generated headers. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants
 * from. */
const uint32_t kSmokeCOAlarmDeviceType = 0x0076;
const uint32_t kSmokeCOAlarmClusterId = 0x005C;  // 92 decimal
const uint32_t kExpressedStateAttributeId = 0x0000;
const uint32_t kSelfTestRequestCommandId = 0x0000;

/* AT+MTALARM=<ep>,<field>,<value> (AT_MT_SPEC.md S3.22's own table). */
const uint8_t kFieldSmokeState = 1;
const uint8_t kFieldCOState = 2;
const uint8_t kFieldBatteryAlert = 3;
const uint8_t kFieldDeviceMuted = 4;
const uint8_t kFieldTestInProgress = 5;
const uint8_t kFieldHardwareFaultAlert = 6;
const uint8_t kFieldEndOfServiceAlert = 7;
const uint8_t kFieldInterconnectSmokeAlarm = 8;
const uint8_t kFieldInterconnectCOAlarm = 9;
const uint8_t kFieldContaminationState = 10;
const uint8_t kFieldSmokeSensitivityLevel = 11;
}  // namespace

MatterSmokeCOAlarm::MatterSmokeCOAlarm() {}

MatterSmokeCOAlarm::~MatterSmokeCOAlarm() {
  end();
}

bool MatterSmokeCOAlarm::begin() {
  if (!hearthDeclare(this, kSmokeCOAlarmDeviceType)) {
    return false;
  }
  smokeState = 0;
  coState = 0;
  batteryAlert = 0;
  deviceMuted = 0;
  hardwareFaultAlert = false;
  endOfServiceAlert = 0;
  interconnectSmokeAlarm = 0;
  interconnectCOAlarm = 0;
  contaminationState = 0;
  smokeSensitivityLevel = 0;
  started = true;
  return true;
}

void MatterSmokeCOAlarm::end() {
  started = false;
}

void MatterSmokeCOAlarm::onSelfTest(std::function<void()> cb) {
  _onSelfTestCB = cb;
}

/*
 * AT+MTALARM=<ep>,<field>,<value> (AT_MT_SPEC.md S3.22). getEndPointId()
 * == 0 is checked directly here, not through hearthEndPointAddressable():
 * that guard is private to MatterEndPoint and used internally by
 * setAttributeVal()/updateAttributeVal(), which AT+MTALARM does not go
 * through (it is its own command, not an attribute write) -- the same
 * pattern MatterDoorLock::hearthSendLockState() and
 * MatterOperationalStateEndpoint::hearthSendOperationalState() establish
 * for the same reason: a custom wire verb repeats the check locally.
 */
bool MatterSmokeCOAlarm::hearthSendAlarmField(uint8_t field, uint8_t value) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+MTALARM=%u,%u,%u", (unsigned)getEndPointId(), (unsigned)field, (unsigned)value);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Field 5/TestInProgress, value 0 only: this is the self-test completion
 * report (header comment), not a general TestInProgress setter -- the
 * brief exposes no way to set it true from the host side, matching
 * S3.22's own lifecycle ("the app-level hook... TestInProgress true...
 * answers the controller Success" already happened SDK-side by the time
 * onSelfTest() runs). Always reaches the wire: there is no cached
 * TestInProgress value to compare a no-op against.
 */
bool MatterSmokeCOAlarm::completeSelfTest() {
  if (!started) {
    return false;
  }
  return hearthSendAlarmField(kFieldTestInProgress, 0);
}

bool MatterSmokeCOAlarm::setSmokeState(uint8_t s) {
  if (!started) {
    return false;
  }
  if (smokeState == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldSmokeState, s)) {
    return false;  // cache untouched on a failed write
  }
  smokeState = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getSmokeState() {
  return smokeState;
}

bool MatterSmokeCOAlarm::setCOState(uint8_t s) {
  if (!started) {
    return false;
  }
  if (coState == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldCOState, s)) {
    return false;  // cache untouched on a failed write
  }
  coState = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getCOState() {
  return coState;
}

bool MatterSmokeCOAlarm::setBatteryAlert(uint8_t s) {
  if (!started) {
    return false;
  }
  if (batteryAlert == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldBatteryAlert, s)) {
    return false;  // cache untouched on a failed write
  }
  batteryAlert = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getBatteryAlert() {
  return batteryAlert;
}

bool MatterSmokeCOAlarm::setDeviceMuted(uint8_t s) {
  if (!started) {
    return false;
  }
  if (deviceMuted == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldDeviceMuted, s)) {
    return false;  // cache untouched on a failed write
  }
  deviceMuted = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getDeviceMuted() {
  return deviceMuted;
}

bool MatterSmokeCOAlarm::setHardwareFaultAlert(bool v) {
  if (!started) {
    return false;
  }
  if (hardwareFaultAlert == v) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldHardwareFaultAlert, v ? 1 : 0)) {
    return false;  // cache untouched on a failed write
  }
  hardwareFaultAlert = v;
  return true;
}

bool MatterSmokeCOAlarm::getHardwareFaultAlert() {
  return hardwareFaultAlert;
}

bool MatterSmokeCOAlarm::setEndOfServiceAlert(uint8_t v) {
  if (!started) {
    return false;
  }
  if (endOfServiceAlert == v) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldEndOfServiceAlert, v)) {
    return false;  // cache untouched on a failed write
  }
  endOfServiceAlert = v;
  return true;
}

uint8_t MatterSmokeCOAlarm::getEndOfServiceAlert() {
  return endOfServiceAlert;
}

bool MatterSmokeCOAlarm::setInterconnectSmokeAlarm(uint8_t s) {
  if (!started) {
    return false;
  }
  if (interconnectSmokeAlarm == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldInterconnectSmokeAlarm, s)) {
    return false;  // cache untouched on a failed write
  }
  interconnectSmokeAlarm = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getInterconnectSmokeAlarm() {
  return interconnectSmokeAlarm;
}

bool MatterSmokeCOAlarm::setInterconnectCOAlarm(uint8_t s) {
  if (!started) {
    return false;
  }
  if (interconnectCOAlarm == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldInterconnectCOAlarm, s)) {
    return false;  // cache untouched on a failed write
  }
  interconnectCOAlarm = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getInterconnectCOAlarm() {
  return interconnectCOAlarm;
}

bool MatterSmokeCOAlarm::setContaminationState(uint8_t s) {
  if (!started) {
    return false;
  }
  if (contaminationState == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldContaminationState, s)) {
    return false;  // cache untouched on a failed write
  }
  contaminationState = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getContaminationState() {
  return contaminationState;
}

bool MatterSmokeCOAlarm::setSmokeSensitivityLevel(uint8_t s) {
  if (!started) {
    return false;
  }
  if (smokeSensitivityLevel == s) {
    return true;
  }
  if (!hearthSendAlarmField(kFieldSmokeSensitivityLevel, s)) {
    return false;  // cache untouched on a failed write
  }
  smokeSensitivityLevel = s;
  return true;
}

uint8_t MatterSmokeCOAlarm::getSmokeSensitivityLevel() {
  return smokeSensitivityLevel;
}

/*
 * ExpressedState (cluster 92 attr 0): derived server-side, never cached
 * (header comment). getAttributeVal() is the base class's genuine
 * AT+MTATTR read/round-trip; the target type is pre-set on the caller's
 * own esp_matter_attr_val_t (MatterEndPoint.h's own contract), since the
 * wire never carries a type tag. Returns 0 (Normal) if the endpoint is not
 * addressable or the read fails outright.
 */
uint8_t MatterSmokeCOAlarm::getExpressedState() {
  if (!started) {
    return 0;
  }
  esp_matter_attr_val_t val = esp_matter_enum8(0);
  if (!getAttributeVal(kSmokeCOAlarmClusterId, kExpressedStateAttributeId, &val)) {
    return 0;
  }
  return val.val.u8;
}

/*
 * A documented no-op: see the header comment. No attribute this class
 * writes goes through AT+MTATTR (every setter uses AT+MTALARM instead),
 * and the one attribute it does read (ExpressedState) is a synchronous
 * getAttributeVal() reply consumed directly by that call, never routed
 * through the generic dispatcher's attributeChangeCB() path. Present only
 * because MatterEndPoint declares this pure virtual -- the same shape
 * MatterChime's own header comment establishes for a class with no
 * AT+MTATTR-reachable attribute at all.
 */
bool MatterSmokeCOAlarm::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

/*
 * AT_MT_SPEC.md S3.17/S3.22: the firmware forwards a controller-invoked
 * SelfTestRequest here, always under seq 0 (notify-only; see the header
 * comment for why there is never a verdict to give). Runs the callback if
 * one is registered; returns false for the wrong cluster, an unstarted
 * endpoint, an unrecognised command id, or no callback registered --
 * purely documentation of "did this class run something", since the
 * dispatcher discards the return value for seq 0 either way.
 */
bool MatterSmokeCOAlarm::hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) {
  (void)hasPayload;  // SelfTestRequest carries no S3.17 fifth field
  (void)payload;
  if (!started || cluster_id != kSmokeCOAlarmClusterId || command_id != kSelfTestRequestCommandId) {
    return false;
  }
  if (_onSelfTestCB) {
    _onSelfTestCB();
    return true;
  }
  return false;
}

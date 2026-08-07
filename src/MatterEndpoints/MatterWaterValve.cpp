/*
 * MatterWaterValve.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the design rationale,
 * and (most importantly) why a denying onOpen()/onClose() verdict cannot
 * fail the command on the wire the way MatterDoorLock's onLock()/onUnlock()
 * can.
 */
#include "MatterEndpoints/MatterWaterValve.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* water_valve (ESP_MATTER_WATER_VALVE_DEVICE_TYPE_ID),
 * ValveConfigurationAndControl::Id, and ValveConfigurationAndControl::
 * Attributes::CurrentState::Id / Commands::Open::Id / Commands::Close::Id.
 * See MatterWaterValve.h's header comment for the quoted lines from the
 * pinned esp-matter checkout's generated headers. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kWaterValveDeviceType = 0x0042;
const uint32_t kValveClusterId = 0x0081;  // 129 decimal
const uint32_t kCurrentStateAttributeId = 0x0004;
const uint32_t kOpenCommandId = 0x0000;
const uint32_t kCloseCommandId = 0x0001;
}  // namespace

MatterWaterValve::MatterWaterValve() {}

MatterWaterValve::~MatterWaterValve() {
  end();
}

bool MatterWaterValve::begin() {
  if (!hearthDeclare(this, kWaterValveDeviceType)) {
    return false;
  }
  valveState = kStateClosed;
  started = true;
  return true;
}

void MatterWaterValve::end() {
  started = false;
}

void MatterWaterValve::onOpen(std::function<bool()> cb) {
  _onOpenCB = cb;
}

void MatterWaterValve::onClose(std::function<bool()> cb) {
  _onCloseCB = cb;
}

/*
 * AT+MTVALVE=<ep>,<state>[,<level>] (AT_MT_SPEC.md S3.19). getEndPointId()
 * == 0 is checked directly here, not through hearthEndPointAddressable():
 * that guard is private to MatterEndPoint and used internally by
 * setAttributeVal()/updateAttributeVal(), which AT+MTVALVE does not go
 * through (it is its own command, not an attribute write) -- the same
 * pattern MatterDoorLock::hearthSendLockState() and
 * MatterTemperatureControlledCabinet::hearthSendLevelLabels() establish for
 * the same reason: a custom wire verb repeats the check locally.
 */
bool MatterWaterValve::hearthSendValveState(uint8_t state, bool hasLevel, uint8_t level) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  if (hasLevel) {
    snprintf(cmd, sizeof(cmd), "AT+MTVALVE=%u,%u,%u", (unsigned)getEndPointId(), (unsigned)state, (unsigned)level);
  } else {
    snprintf(cmd, sizeof(cmd), "AT+MTVALVE=%u,%u", (unsigned)getEndPointId(), (unsigned)state);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterWaterValve::setValveState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (valveState == state) {
    return true;
  }
  if (!hearthSendValveState(state, false, 0)) {
    return false;  // cache untouched on a failed write
  }
  valveState = state;
  return true;
}

/*
 * The two-arg overload always reaches the wire, even when <state> alone
 * would be a no-op: a level report is meaningful traffic even at an
 * unchanged state (S3.19: "not readable back", so there is no cached level
 * to compare against in the first place -- see the header comment).
 */
bool MatterWaterValve::setValveState(uint8_t state, uint8_t level) {
  if (!started) {
    return false;
  }
  if (!hearthSendValveState(state, true, level)) {
    return false;  // cache untouched on a failed write
  }
  valveState = state;
  return true;
}

uint8_t MatterWaterValve::getValveState() {
  return valveState;
}

/*
 * +MTATTR-driven cache update for CurrentState (cluster 129 attr 4): see
 * the header comment's "URC-fed cache" note. The generic dispatch
 * (Hearth.cpp's hearthDispatchAttr()) routes it here via the base class's
 * attributeChangeCB() contract, the same shape as MatterDoorLock's own
 * LockState handling.
 */
bool MatterWaterValve::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kValveClusterId) {
    return true;
  }
  if (attribute_id == kCurrentStateAttributeId) {
    valveState = val->val.u8;
  }
  return true;
}

esp_matter_val_type_t MatterWaterValve::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kValveClusterId && attribute_id == kCurrentStateAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * AT_MT_SPEC.md S3.17/S3.19: the firmware forwards a controller-invoked
 * Open/Close here for a verdict. Fails closed (false, i.e. deny) for the
 * wrong cluster, an unstarted endpoint, an unrecognised command id, or no
 * callback registered -- same shape as MatterDoorLock, though see the
 * header comment for why a deny here does not change what the controller
 * sees: the ValveConfigurationAndControl server discards the delegate's
 * return value unconditionally (S3.19, TEMPORARY_RETURN_IGNORED), so this
 * verdict only ever reaches this class's own callback, never the wire
 * response.
 */
bool MatterWaterValve::hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) {
  (void)hasPayload;  // Open/Close carry no S3.17 fifth field
  (void)payload;
  if (!started || cluster_id != kValveClusterId) {
    return false;
  }
  if (command_id == kOpenCommandId) {
    return _onOpenCB ? _onOpenCB() : false;
  }
  if (command_id == kCloseCommandId) {
    return _onCloseCB ? _onCloseCB() : false;
  }
  return false;
}

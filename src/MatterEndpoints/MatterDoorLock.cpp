/*
 * MatterDoorLock.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant and the design
 * rationale (B120 reconcile-push norm, fail-closed command dispatch).
 */
#include "MatterEndpoints/MatterDoorLock.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* door_lock (ESP_MATTER_DOOR_LOCK_DEVICE_TYPE_ID), DoorLock::Id, and
 * DoorLock::Attributes::LockState::Id / Commands::LockDoor::Id /
 * Commands::UnlockDoor::Id. See MatterDoorLock.h's header comment for the
 * quoted lines from the pinned esp-matter checkout's generated headers.
 * Given as plain integers: there is no connectedhomeip header on a host
 * build to pull the named constants from. */
const uint32_t kDoorLockDeviceType = 0x000A;
const uint32_t kDoorLockClusterId = 0x0101;  // 257 decimal
const uint32_t kLockStateAttributeId = 0x0000;
const uint32_t kLockDoorCommandId = 0x0000;
const uint32_t kUnlockDoorCommandId = 0x0001;
}  // namespace

MatterDoorLock::MatterDoorLock() {}

MatterDoorLock::~MatterDoorLock() {
  end();
}

bool MatterDoorLock::begin(bool locked) {
  if (!hearthDeclare(this, kDoorLockDeviceType)) {
    return false;
  }
  lockState = locked ? kStateLocked : kStateUnlocked;
  started = true;
  return true;
}

void MatterDoorLock::end() {
  started = false;
}

void MatterDoorLock::onLock(std::function<bool()> cb) {
  _onLockCB = cb;
}

void MatterDoorLock::onUnlock(std::function<bool()> cb) {
  _onUnlockCB = cb;
}

/*
 * AT+MTLOCK=<ep>,<state>,<source> (AT_MT_SPEC.md S3.18). Always emits the
 * source field explicitly, rather than relying on the firmware's own
 * omitted-field default (kManual): setLockState()'s default argument
 * already resolves to kSourceManual before this is ever called, so there is
 * no case where the field is unknown here, and always sending it keeps the
 * wire line exact and predictable rather than depending on a form the
 * caller happened not to pass.
 *
 * getEndPointId() == 0 (not yet reconciled, or Matter.begin() never
 * called/failed) is checked directly here, not through
 * hearthEndPointAddressable(): that guard is private to MatterEndPoint and
 * used internally by setAttributeVal()/updateAttributeVal(), which
 * AT+MTLOCK does not go through (it is its own command, not an attribute
 * write). MatterTemperatureControlledCabinet's hearthSendLevelLabels() /
 * setSupportedTemperatureLevels() establish the same pattern for the same
 * reason: a custom wire verb repeats the check locally.
 */
bool MatterDoorLock::hearthSendLockState(uint8_t state, uint8_t source) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTLOCK=%u,%u,%u", (unsigned)getEndPointId(), (unsigned)state, (unsigned)source);
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterDoorLock::setLockState(uint8_t state, uint8_t source) {
  if (!started) {
    return false;
  }
  if (lockState == state) {
    return true;
  }
  if (!hearthSendLockState(state, source)) {
    return false;  // cache untouched on a failed write
  }
  lockState = state;
  return true;
}

bool MatterDoorLock::lock() {
  return setLockState(kStateLocked, kSourceManual);
}

bool MatterDoorLock::unlock() {
  return setLockState(kStateUnlocked, kSourceManual);
}

uint8_t MatterDoorLock::getLockState() {
  return lockState;
}

/*
 * Reconcile push (B120 norm; see the header's design comment): unconditional,
 * bypassing setLockState()'s skip-if-equal, because the cache holds the
 * sketch's begin() intent while the C6 creates the DoorLock cluster at its
 * own default LockState -- the two are not equal by construction, so the
 * setter's own equality check would (as it did for the cabinet's TN values
 * before its own fix) skip the write entirely on the very first boot.
 * Best-effort: this runs deep inside ArduinoMatter::begin(), which has
 * already committed to its own composition verdict; a failed push here has
 * no further recourse this call, and no cache mutation either way, since
 * the cache already holds what the sketch intends.
 */
void MatterDoorLock::hearthOnReconciled() {
  if (!started) {
    return;
  }
  hearthSendLockState(lockState, kSourceManual);
}

/*
 * +MTATTR-driven cache update for LockState (cluster 257 attr 0): a
 * controller or the firmware itself changed the attribute out from under
 * this host, and the generic dispatch (Hearth.cpp's hearthDispatchAttr())
 * routes it here via the base class's attributeChangeCB() contract.
 */
bool MatterDoorLock::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kDoorLockClusterId) {
    return true;
  }
  if (attribute_id == kLockStateAttributeId) {
    lockState = val->val.u8;
  }
  return true;
}

esp_matter_val_type_t MatterDoorLock::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kDoorLockClusterId && attribute_id == kLockStateAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * AT_MT_SPEC.md S3.17: the firmware forwards a controller-invoked
 * LockDoor/UnlockDoor here for a verdict. Fails closed (false, i.e. deny)
 * for the wrong cluster, an unstarted endpoint, an unrecognised command id,
 * or no callback registered -- every path that is not an explicit allow,
 * exactly the wire contract's own "a lock fails closed, never open" rule.
 */
bool MatterDoorLock::hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) {
  (void)hasPayload;  // LockDoor/UnlockDoor carry no S3.17 fifth field
  (void)payload;
  if (!started || cluster_id != kDoorLockClusterId) {
    return false;
  }
  if (command_id == kLockDoorCommandId) {
    return _onLockCB ? _onLockCB() : false;
  }
  if (command_id == kUnlockDoorCommandId) {
    return _onUnlockCB ? _onUnlockCB() : false;
  }
  return false;
}

/*
 * MatterOperationalStateEndpoint.cpp - implementation. See the header for
 * the quoted pinned-header source of every transcribed constant, the
 * design rationale, and why a denying onPause()/onResume()/onStart()/
 * onStop() verdict IS the wire response, unlike the water valve's.
 */
#include "MatterEndpoints/MatterOperationalStateEndpoint.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* OperationalState::Id and OperationalState::Commands::{Pause,Stop,Start,
 * Resume}::Id. See MatterOperationalStateEndpoint.h's header comment for
 * the quoted lines from the pinned esp-matter checkout's generated
 * headers. Given as plain integers: there is no connectedhomeip header on
 * a host build to pull the named constants from. */
const uint32_t kOperationalStateClusterId = 0x0060;  // 96 decimal
const uint32_t kPauseCommandId = 0x0000;
const uint32_t kStopCommandId = 0x0001;
const uint32_t kStartCommandId = 0x0002;
const uint32_t kResumeCommandId = 0x0003;
}  // namespace

MatterOperationalStateEndpoint::MatterOperationalStateEndpoint() {}

MatterOperationalStateEndpoint::~MatterOperationalStateEndpoint() {
  end();
}

bool MatterOperationalStateEndpoint::hearthBeginOperationalState(uint32_t deviceTypeId) {
  if (!hearthDeclare(this, deviceTypeId)) {
    return false;
  }
  operationalState = kStateStopped;
  started = true;
  return true;
}

void MatterOperationalStateEndpoint::end() {
  started = false;
}

void MatterOperationalStateEndpoint::onPause(std::function<bool()> cb) {
  _onPauseCB = cb;
}

void MatterOperationalStateEndpoint::onResume(std::function<bool()> cb) {
  _onResumeCB = cb;
}

void MatterOperationalStateEndpoint::onStart(std::function<bool()> cb) {
  _onStartCB = cb;
}

void MatterOperationalStateEndpoint::onStop(std::function<bool()> cb) {
  _onStopCB = cb;
}

/*
 * AT+MTOPSTATE=<ep>,<state> (AT_MT_SPEC.md S3.21). getEndPointId() == 0 is
 * checked directly here, not through hearthEndPointAddressable(): that
 * guard is private to MatterEndPoint and used internally by
 * setAttributeVal()/updateAttributeVal(), which AT+MTOPSTATE does not go
 * through (it is its own command, not an attribute write) -- the same
 * pattern MatterDoorLock::hearthSendLockState() and
 * MatterWaterValve::hearthSendValveState() establish for the same reason:
 * a custom wire verb repeats the check locally.
 */
bool MatterOperationalStateEndpoint::hearthSendOperationalState(uint8_t state) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTOPSTATE=%u,%u", (unsigned)getEndPointId(), (unsigned)state);
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterOperationalStateEndpoint::setOperationalState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (operationalState == state) {
    return true;
  }
  if (!hearthSendOperationalState(state)) {
    return false;  // cache untouched on a failed write
  }
  operationalState = state;
  return true;
}

uint8_t MatterOperationalStateEndpoint::getOperationalState() {
  return operationalState;
}

/*
 * A documented no-op: see the header comment. No attribute on this
 * cluster has an AT+MTATTR path (every OperationalState attribute is
 * managed internally by the cluster's own SDK Instance, S3.21), so
 * hearthDispatchAttr() (Hearth.cpp) never has a (cluster, attribute) pair
 * from THIS cluster to route here in the first place. Present only
 * because MatterEndPoint declares this pure virtual -- the same shape
 * MatterChime's own header comment establishes for a class with no
 * AT+MTATTR-reachable attribute at all.
 */
bool MatterOperationalStateEndpoint::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

/*
 * AT_MT_SPEC.md S3.17/S3.21: the firmware forwards a controller-invoked
 * Pause/Resume/Start/Stop here for a verdict. Fails closed (false, i.e.
 * deny) for the wrong cluster, an unstarted endpoint, an unrecognised
 * command id, or no callback registered -- same fail-closed shape as
 * MatterDoorLock/MatterChime. Unlike MatterWaterValve, this verdict IS the
 * real wire response the controller sees (header comment): a deny here
 * genuinely fails the command with ErrorStateID 0x02
 * (UnableToCompleteOperation).
 */
bool MatterOperationalStateEndpoint::hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) {
  (void)hasPayload;  // Pause/Resume/Start/Stop carry no S3.17 fifth field
  (void)payload;
  if (!started || cluster_id != kOperationalStateClusterId) {
    return false;
  }
  if (command_id == kPauseCommandId) {
    return _onPauseCB ? _onPauseCB() : false;
  }
  if (command_id == kStopCommandId) {
    return _onStopCB ? _onStopCB() : false;
  }
  if (command_id == kStartCommandId) {
    return _onStartCB ? _onStartCB() : false;
  }
  if (command_id == kResumeCommandId) {
    return _onResumeCB ? _onResumeCB() : false;
  }
  return false;
}

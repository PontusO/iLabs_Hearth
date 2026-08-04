/*
 * MatterGenericSwitch.cpp - implementation. See the header for the one
 * documented deviation (click() returns bool) and why click() talks to
 * Hearth.hearthCommand() directly instead of going through
 * updateAttributeVal()/setAttributeVal() like every attribute-driven
 * endpoint class in this library.
 */
#include "MatterEndpoints/MatterGenericSwitch.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* generic_switch (esp_matter_endpoint.h's
 * ESP_MATTER_GENERIC_SWITCH_DEVICE_TYPE_ID), already verified on the
 * firmware side (main/mt_devtypes.cpp, AT_MT_SPEC.md's device type table).
 * Given as a plain integer: there is no connectedhomeip header on a host
 * build to pull the named constant from. */
const uint32_t kGenericSwitchDeviceType = 0x000F;
}  // namespace

MatterGenericSwitch::MatterGenericSwitch() {}

MatterGenericSwitch::~MatterGenericSwitch() {
  end();
}

bool MatterGenericSwitch::begin() {
  if (!hearthDeclare(this, kGenericSwitchDeviceType)) {
    return false;
  }
  started = true;
  return true;
}

void MatterGenericSwitch::end() {
  started = false;
}

/*
 * Guarded exactly like MatterEndPoint's own private hearthEndPointAddressable():
 * endpoint_id stays 0 until Matter.begin() reconciles this endpoint against
 * the C6, and 0 is the Root Node, a real endpoint that must never receive
 * traffic meant for this one. That helper is private to the base class, so
 * the check is repeated here rather than reused.
 */
bool MatterGenericSwitch::click() {
  if (!started || getEndPointId() == 0) {
    return false;
  }
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+MTSWITCH=%u", (unsigned)getEndPointId());
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * Documented no-op: this class drives no attribute (see the header), so
 * there is nothing to update from a +MTATTR URC. Mirrors upstream's own
 * body, which only logs and reports whether the device has begun.
 */
bool MatterGenericSwitch::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

/*
 * MatterDeviceEnergyManagement.cpp - implementation. See the header for
 * the design notes: the helper-plus-identity surface, the REPORT_ONLY
 * adjudication (why exactly one method is gated here and the helper stays
 * enabled), the reentrancy rule for the two verdict callbacks, and the
 * deliberate absence of a DEM Mode surface.
 *
 * The delegation shape is MatterHeatPump.cpp's: the `started` guard stays
 * HERE, in front of every delegation, and the mechanics live in
 * HearthDemControl.cpp.
 */
#include "MatterEndpoints/MatterDeviceEnergyManagement.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* device_energy_management (ESP_MATTER_DEVICE_ENERGY_MANAGEMENT_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:136,
 * "#define ESP_MATTER_DEVICE_ENERGY_MANAGEMENT_DEVICE_TYPE_ID 0x050D").
 * Given as a plain integer: there is no esp-matter header on a host build. */
const uint32_t kDemDeviceType = 0x050D;
}  // namespace

MatterDeviceEnergyManagement::MatterDeviceEnergyManagement() : dem(this) {}

MatterDeviceEnergyManagement::~MatterDeviceEnergyManagement() {
  end();
}

bool MatterDeviceEnergyManagement::begin(Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third
   * value, and the variant byte travels the wire verbatim, so validate it
   * host-side, the electrical sensor's shape. */
  if (variant != CONTROLLABLE && variant != REPORT_ONLY) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, kDemDeviceType, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  dem.reset();
  /* TRUE on BOTH variants, and that is the adjudicated point (header
   * comment): the DEM cluster exists either way, so the whole AT+MTMEAS
   * side of the surface stays live on REPORT_ONLY; only
   * setPowerAdjustmentCapability() is gated, and it is gated below, in
   * this class. Assigned explicitly rather than left to the constructor
   * default, the Task 5 ledger note. */
  dem.enabled = true;
  started = true;
  return true;
}

void MatterDeviceEnergyManagement::end() {
  started = false;
}

/* REPORT_ONLY serves no PowerAdjustmentCapability attribute (FeatureMap 0,
 * S3.26's +MTERR:4 row), so the answer is already known host-side: error 1,
 * zero wire traffic (the MatterWaterHeater::hearthRefusedOnMinimal()
 * precedent). Deliberately called from ONE place, not from every setter:
 * see the header comment. */
bool MatterDeviceEnergyManagement::hearthRefusedOnReportOnly() {
  if (variantSel == REPORT_ONLY) {
    Hearth.hearthSetError(1);
    return true;
  }
  return false;
}

bool MatterDeviceEnergyManagement::setESAType(uint8_t t) {
  if (!started) {
    return false;
  }
  return dem.setESAType(t);
}

bool MatterDeviceEnergyManagement::setESACanGenerate(bool g) {
  if (!started) {
    return false;
  }
  return dem.setESACanGenerate(g);
}

bool MatterDeviceEnergyManagement::setESAState(uint8_t s) {
  if (!started) {
    return false;
  }
  return dem.setESAState(s);
}

bool MatterDeviceEnergyManagement::setAbsMinPower(int64_t mw) {
  if (!started) {
    return false;
  }
  return dem.setAbsMinPower(mw);
}

bool MatterDeviceEnergyManagement::setAbsMaxPower(int64_t mw) {
  if (!started) {
    return false;
  }
  return dem.setAbsMaxPower(mw);
}

bool MatterDeviceEnergyManagement::setOptOutState(uint8_t s) {
  if (!started) {
    return false;
  }
  return dem.setOptOutState(s);
}

bool MatterDeviceEnergyManagement::pushAdjustmentEnergyUse(int64_t mwh) {
  if (!started) {
    return false;
  }
  return dem.pushAdjustmentEnergyUse(mwh);
}

/* The one gated method (header comment): REPORT_ONLY refuses before the
 * helper is ever reached, so nothing is cached either and a later
 * reconcile can never resend a capability this variant does not have. */
bool MatterDeviceEnergyManagement::setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n) {
  if (!started) {
    return false;
  }
  if (hearthRefusedOnReportOnly()) {
    return false;
  }
  return dem.setPowerAdjustmentCapability(cause, entries, n);
}

void MatterDeviceEnergyManagement::onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause)) {
  dem.onPowerAdjust(cb);
}

void MatterDeviceEnergyManagement::onCancelPowerAdjust(bool (*cb)()) {
  dem.onCancelPowerAdjust(cb);
}

bool MatterDeviceEnergyManagement::endAdjustment() {
  if (!started) {
    return false;
  }
  return dem.endAdjustment();
}

/* The helper's reconcile split: configuration (ESAType, ESACanGenerate,
 * AbsMin/MaxPower, the capability list) re-pushed, the volatile has-flags
 * (ESAState, OptOutState) cleared without a resend. See
 * HearthDemControl.cpp. */
void MatterDeviceEnergyManagement::hearthOnReconciled() {
  if (!started) {
    return;
  }
  dem.onReconciled();
}

/*
 * Cluster 152 is Instance-served (S3.25): no +MTATTR URC ever reports it,
 * and an injected one must not move any cache. This class keeps no
 * attribute cache of its own at all, so the body is a documented no-op
 * returning `started`, the MatterHeatPump shape.
 */
bool MatterDeviceEnergyManagement::attributeChangeCB(
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

/*
 * AT_MT_SPEC.md S3.17: PowerAdjustRequest (152,0) carries three flat
 * fields, "<power>,<duration>,<cause>", all mandatory (documented arity,
 * no presence mask), so a short tail is malformed and denied WITHOUT
 * consulting the callback -- the MatterWaterHeater Boost precedent, and
 * the reference shape Task 5's own suite pinned. CancelPowerAdjustRequest
 * (152,1) is payload-less. The verdict is returned to the dispatcher,
 * which sends AT+MTCMDRESP; an accept pushes NOTHING (the firmware owns
 * the state transition and the PowerAdjustStart emission), so unlike the
 * water heater's Boost there is no deferred work to arm here.
 */
bool MatterDeviceEnergyManagement::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
  if (started && cluster_id == kDemClusterId && command_id == kPowerAdjustRequestCmd) {
    if (fields.count < 3 || !fields.present[0] || !fields.present[1] || !fields.present[2]) {
      return false;  // malformed: denied without consulting the callback
    }
    int64_t power = fields.value[0];
    uint32_t duration = (uint32_t)fields.value[1];
    uint8_t cause = (uint8_t)fields.value[2];
    return dem.hearthOnPowerAdjustRequest(power, duration, cause);
  }
  if (started && cluster_id == kDemClusterId && command_id == kCancelPowerAdjustRequestCmd) {
    return dem.hearthOnCancelPowerAdjustRequest();
  }
  return MatterEndPoint::hearthOnForwardedCommandFields(cluster_id, command_id, fields);
}

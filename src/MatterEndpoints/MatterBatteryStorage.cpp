/*
 * MatterBatteryStorage.cpp - implementation. See the header for the design
 * notes: the three surfaces on one endpoint, the variant's DEM gate, the
 * ember attributes' two seed rules and their provenance, the three-way
 * reconcile split, and the reentrancy rule for the verdict callbacks.
 *
 * The delegation shape is MatterWaterHeater.cpp's: the `started` guard
 * stays HERE, in front of every delegation, and the mechanics live in
 * HearthMeasurementPush.cpp and HearthDemControl.cpp.
 */
#include "MatterEndpoints/MatterBatteryStorage.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* battery_storage (ESP_MATTER_BATTERY_STORAGE_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:144,
 * "#define ESP_MATTER_BATTERY_STORAGE_DEVICE_TYPE_ID 0x0018"). Given as a
 * plain integer: there is no esp-matter header on a host build. */
const uint32_t kBatteryStorageDeviceType = 0x0018;
}  // namespace

MatterBatteryStorage::MatterBatteryStorage() : meas(this), dem(this) {}

MatterBatteryStorage::~MatterBatteryStorage() {
  end();
}

bool MatterBatteryStorage::begin(Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third
   * value, and the variant byte travels the wire verbatim, so validate it
   * host-side, the electrical sensor's shape. */
  if (variant != FULL && variant != NO_DEM) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, kBatteryStorageDeviceType, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  /* The ember caches, per the header comment's two seed rules: the five
   * nullable attributes start with their has-flags clear (a first push of
   * 0 must still write), BatCapacity and BatChargeState seed from the
   * device's own creation defaults, which are both 0. */
  batVoltage = 0;
  batPercentRemaining = 0;
  batTimeRemaining = 0;
  batTimeToFullCharge = 0;
  batChargingCurrent = 0;
  batCapacity = 0;
  batChargeState = 0;
  hasBatVoltage = hasBatPercentRemaining = hasBatTimeRemaining = false;
  hasBatTimeToFullCharge = hasBatChargingCurrent = false;
  wroteBatCapacity = wroteBatChargeState = false;
  meas.reset();
  dem.reset();
  /* The measurement graft carries EEM on BOTH variants
   * (mk_battery_storage() passes its energy flag hard-wired true), so the
   * adders are enabled either way; the DEM cluster exists on FULL only, so
   * the DEM helper's whole surface is gated by the variant. Both assigned
   * explicitly, never left to the constructor defaults (the Task 5 ledger
   * note). */
  meas.enabled = true;
  dem.enabled = (variant == FULL);
  started = true;
  return true;
}

void MatterBatteryStorage::end() {
  started = false;
}

/* ---- the measurement surface: started guard, then the shared helper ---- */

bool MatterBatteryStorage::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  return meas.setVoltage(mv);
}

bool MatterBatteryStorage::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  return meas.setActiveCurrent(ma);
}

bool MatterBatteryStorage::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.setActivePower(mw);
}

bool MatterBatteryStorage::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  return meas.setFrequency(mhz);
}

bool MatterBatteryStorage::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.pushMeasurements(mv, ma, mw);
}

bool MatterBatteryStorage::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyImported(mwh);
}

bool MatterBatteryStorage::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyExported(mwh);
}

int64_t MatterBatteryStorage::getVoltage() {
  return meas.getVoltage();
}

int64_t MatterBatteryStorage::getActiveCurrent() {
  return meas.getActiveCurrent();
}

int64_t MatterBatteryStorage::getActivePower() {
  return meas.getActivePower();
}

int64_t MatterBatteryStorage::getFrequency() {
  return meas.getFrequency();
}

uint64_t MatterBatteryStorage::getEnergyImported() {
  return meas.getEnergyImported();
}

uint64_t MatterBatteryStorage::getEnergyExported() {
  return meas.getEnergyExported();
}

/* ---- the ember battery attributes ---- */

/*
 * The shared ember write shape (updateAttributeVal = AT+MTATTR mode 1,
 * reported to subscribers), the MatterPowerSource discipline with one
 * addition: `hasFlag` is non-null for the five NULLABLE attributes, whose
 * fabric-side value starts null, so a first push equal to the
 * zero-initialised cache must still reach the wire. It is null for
 * BatCapacity and BatChargeState, whose caches genuinely match the
 * device's creation defaults (see the header comment's two seed rules).
 * The cache commits on success only: the device's idea of the state and
 * the host's idea of it must not diverge.
 */
bool MatterBatteryStorage::hearthWriteBatteryAttr(uint32_t attribute_id, esp_matter_attr_val_t val, uint32_t value, uint32_t *cache, bool *hasFlag) {
  if ((hasFlag == nullptr || *hasFlag) && *cache == value) {
    return true;
  }
  if (!updateAttributeVal(kPowerSourceClusterId, attribute_id, &val)) {
    return false;  // cache untouched on a failed write
  }
  *cache = value;
  if (hasFlag != nullptr) {
    *hasFlag = true;
  }
  return true;
}

bool MatterBatteryStorage::setBatVoltage(uint32_t mv) {
  if (!started) {
    return false;
  }
  return hearthWriteBatteryAttr(kBatVoltageAttrId, esp_matter_uint32(mv), mv, &batVoltage, &hasBatVoltage);
}

bool MatterBatteryStorage::setBatPercentRemaining(uint8_t halfPercent) {
  if (!started) {
    return false;
  }
  return hearthWriteBatteryAttr(kBatPercentRemainingAttrId, esp_matter_uint8(halfPercent), halfPercent, &batPercentRemaining, &hasBatPercentRemaining);
}

bool MatterBatteryStorage::setBatTimeRemaining(uint32_t seconds) {
  if (!started) {
    return false;
  }
  return hearthWriteBatteryAttr(kBatTimeRemainingAttrId, esp_matter_uint32(seconds), seconds, &batTimeRemaining, &hasBatTimeRemaining);
}

/* Non-nullable, seeded from feature::rechargeable::add()'s config default
 * 0 (kUnknown), so no has-flag: a write of 0 at boot is a true no-op. The
 * separate `wrote` flag is the reconcile re-push's own record (header
 * comment: this is a CONFIG attribute, asserted on transitions rather than
 * sampled), and it is set even when the call no-opped: the sketch still
 * asserted that value, and the device may not hold it after a reboot. */
bool MatterBatteryStorage::setBatChargeState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (!hearthWriteBatteryAttr(kBatChargeStateAttrId, esp_matter_enum8(state), state, &batChargeState, nullptr)) {
    return false;
  }
  wroteBatChargeState = true;
  return true;
}

bool MatterBatteryStorage::setBatChargingCurrent(uint32_t ma) {
  if (!started) {
    return false;
  }
  return hearthWriteBatteryAttr(kBatChargingCurrentAttrId, esp_matter_uint32(ma), ma, &batChargingCurrent, &hasBatChargingCurrent);
}

/* Non-nullable, created with 0 (mk_battery_storage()'s
 * create_bat_capacity(ps_cl, 0, 0x00, 0xFFFF)), so no has-flag either; the
 * `wrote` flag is the reconcile re-push's record, same reasoning as
 * setBatChargeState() above. This is the attribute the whole re-push
 * exists for: a nameplate written once at setup().
 *
 * mAh, and the created max is 0xFFFF: a larger figure is refused by ember
 * with a bare `ERROR` (no +MTERR code, so lastError() stays 0) and this
 * returns false WITHOUT arming the re-push, which is the correct outcome
 * for a value the device never accepted. Measured on the bench, round C1
 * task 7: 65535 -> OK, 65536 -> ERROR. */
bool MatterBatteryStorage::setBatCapacity(uint32_t mah) {
  if (!started) {
    return false;
  }
  if (!hearthWriteBatteryAttr(kBatCapacityAttrId, esp_matter_uint32(mah), mah, &batCapacity, nullptr)) {
    return false;
  }
  wroteBatCapacity = true;
  return true;
}

bool MatterBatteryStorage::setBatTimeToFullCharge(uint32_t seconds) {
  if (!started) {
    return false;
  }
  return hearthWriteBatteryAttr(kBatTimeToFullChargeAttrId, esp_matter_uint32(seconds), seconds, &batTimeToFullCharge, &hasBatTimeToFullCharge);
}

/* ---- the DEM surface: started guard, then the shared helper, whose own
 * `enabled` flag carries the NO_DEM refusal (error 1, zero traffic) ---- */

bool MatterBatteryStorage::setESAType(uint8_t t) {
  if (!started) {
    return false;
  }
  return dem.setESAType(t);
}

bool MatterBatteryStorage::setESACanGenerate(bool g) {
  if (!started) {
    return false;
  }
  return dem.setESACanGenerate(g);
}

bool MatterBatteryStorage::setESAState(uint8_t s) {
  if (!started) {
    return false;
  }
  return dem.setESAState(s);
}

bool MatterBatteryStorage::setAbsMinPower(int64_t mw) {
  if (!started) {
    return false;
  }
  return dem.setAbsMinPower(mw);
}

bool MatterBatteryStorage::setAbsMaxPower(int64_t mw) {
  if (!started) {
    return false;
  }
  return dem.setAbsMaxPower(mw);
}

bool MatterBatteryStorage::setOptOutState(uint8_t s) {
  if (!started) {
    return false;
  }
  return dem.setOptOutState(s);
}

bool MatterBatteryStorage::pushAdjustmentEnergyUse(int64_t mwh) {
  if (!started) {
    return false;
  }
  return dem.pushAdjustmentEnergyUse(mwh);
}

bool MatterBatteryStorage::setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n) {
  if (!started) {
    return false;
  }
  return dem.setPowerAdjustmentCapability(cause, entries, n);
}

void MatterBatteryStorage::onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause)) {
  dem.onPowerAdjust(cb);
}

void MatterBatteryStorage::onCancelPowerAdjust(bool (*cb)()) {
  dem.onCancelPowerAdjust(cb);
}

bool MatterBatteryStorage::endAdjustment() {
  if (!started) {
    return false;
  }
  return dem.endAdjustment();
}

/*
 * The three-surface reconcile split (the header comment carries the full
 * reasoning and the per-attribute classification): the DEM helper
 * re-pushes its configuration and clears its volatile wire memory, the
 * measurement helper clears its own (B229), and the ember attributes get
 * the same split applied per attribute -- BatCapacity and BatChargeState
 * re-pushed because a sketch writes them once or on rare transitions and
 * they cannot self-heal (the
 * MatterTemperatureControlledCabinet::hearthOnReconciled() precedent,
 * whose comment records the bench run that proved the alternative leaves
 * the device stale forever), the five sampled readings cleared-not-resent
 * so the next sample reaches the wire even unchanged.
 *
 * The two re-pushes go through updateAttributeVal() DIRECTLY rather than
 * through their setters, the cabinet's own reasoning: the setters'
 * skip-if-equal is exactly what suppresses the write here, since the cache
 * already holds the value being restored. Best-effort and unchecked, the
 * MatterWaterHeater::hearthOnReconciled() shape (this runs deep inside
 * ArduinoMatter::begin(), which has already committed to its composition
 * verdict), and no cache mutation either way: the cache already holds what
 * the sketch intends, so this only ever makes the device match it.
 */
void MatterBatteryStorage::hearthOnReconciled() {
  if (!started) {
    return;
  }
  if (wroteBatCapacity) {
    esp_matter_attr_val_t capacityVal = esp_matter_uint32(batCapacity);
    updateAttributeVal(kPowerSourceClusterId, kBatCapacityAttrId, &capacityVal);
  }
  if (wroteBatChargeState) {
    esp_matter_attr_val_t chargeStateVal = esp_matter_enum8((uint8_t)batChargeState);
    updateAttributeVal(kPowerSourceClusterId, kBatChargeStateAttrId, &chargeStateVal);
  }
  hasBatVoltage = hasBatPercentRemaining = hasBatTimeRemaining = false;
  hasBatTimeToFullCharge = hasBatChargingCurrent = false;
  meas.onReconciled();
  dem.onReconciled();
}

/*
 * Cluster 47 is EMBER-served, so a controller write or a local change
 * arrives here as an ordinary +MTATTR URC and updates the cache, the
 * MatterPowerSource::attributeChangeCB() shape. Clusters 144, 145 and 152
 * are documented ignores: S3.25 promises no +MTATTR URC ever reports them,
 * and an injected one must not move any cache (the host's own pushes are
 * the single source of truth there). Returns true (consumed), the sibling
 * classes' shape.
 */
bool MatterBatteryStorage::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id != kPowerSourceClusterId || val == nullptr) {
    return true;
  }
  if (attribute_id == kBatVoltageAttrId) {
    batVoltage = val->val.u;
    hasBatVoltage = true;
  } else if (attribute_id == kBatPercentRemainingAttrId) {
    batPercentRemaining = val->val.u;
    hasBatPercentRemaining = true;
  } else if (attribute_id == kBatTimeRemainingAttrId) {
    batTimeRemaining = val->val.u;
    hasBatTimeRemaining = true;
  } else if (attribute_id == kBatCapacityAttrId) {
    batCapacity = val->val.u;
  } else if (attribute_id == kBatChargeStateAttrId) {
    batChargeState = val->val.u;
  } else if (attribute_id == kBatTimeToFullChargeAttrId) {
    batTimeToFullCharge = val->val.u;
    hasBatTimeToFullCharge = true;
  } else if (attribute_id == kBatChargingCurrentAttrId) {
    batChargingCurrent = val->val.u;
    hasBatChargingCurrent = true;
  }
  return true;
}

esp_matter_val_type_t MatterBatteryStorage::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kPowerSourceClusterId) {
    if (attribute_id == kBatPercentRemainingAttrId) {
      return ESP_MATTER_VAL_TYPE_UINT8;
    }
    if (attribute_id == kBatChargeStateAttrId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kBatVoltageAttrId || attribute_id == kBatTimeRemainingAttrId || attribute_id == kBatCapacityAttrId
        || attribute_id == kBatTimeToFullChargeAttrId || attribute_id == kBatChargingCurrentAttrId) {
      return ESP_MATTER_VAL_TYPE_UINT32;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * AT_MT_SPEC.md S3.17: PowerAdjustRequest (152,0) carries three flat
 * fields, "<power>,<duration>,<cause>", all mandatory (documented arity,
 * no presence mask), so a short tail is malformed and denied WITHOUT
 * consulting the callback -- the MatterWaterHeater Boost precedent.
 * CancelPowerAdjustRequest (152,1) is payload-less. The verdict is
 * returned to the dispatcher, which sends AT+MTCMDRESP; an accept pushes
 * NOTHING (the firmware owns the state transition and the
 * PowerAdjustStart emission), so there is no deferred work to arm here.
 * On the NO_DEM variant the helper denies both without consulting a
 * callback at all, which is defence in depth: the firmware forwards
 * nothing for a cluster it did not build.
 */
bool MatterBatteryStorage::hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) {
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

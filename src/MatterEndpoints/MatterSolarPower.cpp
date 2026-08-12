/*
 * MatterSolarPower.cpp - implementation. See the header for the design
 * notes: the measurement surface plus identity and nothing else, the
 * variant's energy gate, the disclosed conformance gaps, and the signed
 * generation direction. The delegation shape is MatterHeatPump.cpp's: the
 * started guard stays HERE, in front of every delegation, and the
 * mechanics live in HearthMeasurementPush.cpp.
 */
#include "MatterEndpoints/MatterSolarPower.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* solar_power (ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:142,
 * "#define ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID 0x0017"). Given as a
 * plain integer: there is no esp-matter header on a host build. */
const uint32_t kSolarPowerDeviceType = 0x0017;
}  // namespace

MatterSolarPower::MatterSolarPower() : meas(this) {}

MatterSolarPower::~MatterSolarPower() {
  end();
}

bool MatterSolarPower::begin(Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third
   * value, and the variant byte travels the wire verbatim, so validate it
   * host-side, the electrical sensor's shape. */
  if (variant != FULL && variant != NO_ENERGY) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, kSolarPowerDeviceType, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  meas.reset();
  /* NO_ENERGY builds no EEM cluster (mk_solar_power() passes `variant == 0`
   * as its energy flag), so the helper's energy adders refuse host-side
   * with error 1 and zero wire traffic: the POWER_ONLY gate, assigned
   * explicitly on both paths so the state is never implicit. */
  meas.enabled = (variant == FULL);
  started = true;
  return true;
}

void MatterSolarPower::end() {
  started = false;
}

bool MatterSolarPower::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  return meas.setVoltage(mv);
}

bool MatterSolarPower::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  return meas.setActiveCurrent(ma);
}

bool MatterSolarPower::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.setActivePower(mw);
}

bool MatterSolarPower::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  return meas.setFrequency(mhz);
}

bool MatterSolarPower::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.pushMeasurements(mv, ma, mw);
}

bool MatterSolarPower::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyImported(mwh);
}

bool MatterSolarPower::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyExported(mwh);
}

int64_t MatterSolarPower::getVoltage() {
  return meas.getVoltage();
}

int64_t MatterSolarPower::getActiveCurrent() {
  return meas.getActiveCurrent();
}

int64_t MatterSolarPower::getActivePower() {
  return meas.getActivePower();
}

int64_t MatterSolarPower::getFrequency() {
  return meas.getFrequency();
}

uint64_t MatterSolarPower::getEnergyImported() {
  return meas.getEnergyImported();
}

uint64_t MatterSolarPower::getEnergyExported() {
  return meas.getEnergyExported();
}

/* B229 semantics via the shared helper: resend NOTHING, clear only the
 * wire-pushed memory. See HearthMeasurementPush.cpp for the rationale.
 *
 * The `started` guard both siblings carry (MatterBatteryStorage.cpp,
 * MatterDeviceEnergyManagement.cpp) is here for consistency, not because
 * this class needs it differently: on an un-begun endpoint the helper has
 * nothing pushed and clearing is already a no-op, so the guard changes no
 * observable behaviour. It is the shape to copy into any new sibling. */
void MatterSolarPower::hearthOnReconciled() {
  if (!started) {
    return;
  }
  meas.onReconciled();
}

/*
 * S3.25: no +MTATTR URC ever reports the measurement clusters
 * (Instance-served, the 0.6.0 rule), so there is nothing legitimate to
 * cache here, and an injected line naming 144/145 must NOT move the
 * cache. Returns true (consumed), the sibling classes' shape.
 */
bool MatterSolarPower::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

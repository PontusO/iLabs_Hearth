/*
 * MatterElectricalSensor.cpp - implementation. See the header for the
 * design notes: null-until-pushed semantics via has-value flags, the
 * Instance-served rule (cache updates on successful local push only), why
 * measurements are never re-pushed on reconcile, and the energy adders'
 * accumulate-locally-push-the-total contract.
 *
 * Since round B (design spec 4.1) the mechanics live in
 * HearthMeasurementPush.cpp, moved there verbatim; what remains here is
 * the endpoint lifecycle (declare, started guard, variant validation) and
 * one-line delegation per method. The started guard stays HERE, in front
 * of every delegation: a not-yet-begun sensor returns false with no
 * Hearth error and no wire traffic, exactly as before the extraction.
 */
#include "MatterEndpoints/MatterElectricalSensor.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* electrical_sensor (ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID),
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:124,
 * "#define ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID 0x0510". Given as a
 * plain integer: there is no esp-matter header on a host build to pull the
 * named constant from. */
const uint32_t kElectricalSensorDeviceType = 0x0510;
}  // namespace

MatterElectricalSensor::MatterElectricalSensor() : meas(this) {}

MatterElectricalSensor::~MatterElectricalSensor() {
  end();
}

bool MatterElectricalSensor::begin(Variant_t variant) {
  return hearthBeginElectrical(kElectricalSensorDeviceType, variant);
}

void MatterElectricalSensor::end() {
  started = false;
}

bool MatterElectricalSensor::hearthBeginElectrical(uint32_t deviceTypeId, Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third
   * value, and the variant byte travels the wire verbatim, so validate it
   * host-side the way the cabinet classes validate their flavour. */
  if (variant != FULL && variant != POWER_ONLY) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, deviceTypeId, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  meas.reset();
  /* POWER_ONLY builds no energy cluster, so the helper's energy adders
   * refuse host-side with error 1 and zero wire traffic (the gate the
   * helper's `enabled` flag carries since the round B extraction). */
  meas.enabled = (variant == FULL);
  started = true;
  return true;
}

bool MatterElectricalSensor::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  return meas.setVoltage(mv);
}

bool MatterElectricalSensor::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  return meas.setActiveCurrent(ma);
}

bool MatterElectricalSensor::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.setActivePower(mw);
}

bool MatterElectricalSensor::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  return meas.setFrequency(mhz);
}

bool MatterElectricalSensor::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.pushMeasurements(mv, ma, mw);
}

bool MatterElectricalSensor::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyImported(mwh);
}

bool MatterElectricalSensor::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyExported(mwh);
}

int64_t MatterElectricalSensor::getVoltage() {
  return meas.getVoltage();
}

int64_t MatterElectricalSensor::getActiveCurrent() {
  return meas.getActiveCurrent();
}

int64_t MatterElectricalSensor::getActivePower() {
  return meas.getActivePower();
}

int64_t MatterElectricalSensor::getFrequency() {
  return meas.getFrequency();
}

uint64_t MatterElectricalSensor::getEnergyImported() {
  return meas.getEnergyImported();
}

uint64_t MatterElectricalSensor::getEnergyExported() {
  return meas.getEnergyExported();
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first. The B229 semantics (resend NOTHING, clear only the
 * wire-pushed memory so a repeated setter writes again; cache values and
 * energy accumulators survive) moved verbatim into the helper's
 * onReconciled(); see HearthMeasurementPush.cpp for the full rationale.
 */
void MatterElectricalSensor::hearthOnReconciled() {
  meas.onReconciled();
}

/*
 * S3.25: no +MTATTR URC ever reports these clusters (Instance-served, the
 * 0.6.0 rule), so there is nothing legitimate to cache here, and an
 * injected line naming cluster 144/145 anyway must NOT move the cache:
 * the host's own pushes are the single source of truth. Returns true (the
 * URC is consumed, not an error), matching the sibling read-direction
 * classes' shape.
 */
bool MatterElectricalSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

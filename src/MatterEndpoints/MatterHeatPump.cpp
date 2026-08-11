/*
 * MatterHeatPump.cpp - implementation. See the header for the design
 * notes: the measurement surface plus identity and nothing else, the
 * disclosed thermostat-composition gap, and the signed-power emphasis.
 * The delegation shape is MatterElectricalSensor.cpp's: the started guard
 * stays HERE, in front of every delegation, and the mechanics live in
 * HearthMeasurementPush.cpp.
 */
#include "MatterEndpoints/MatterHeatPump.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* heat_pump (ESP_MATTER_HEAT_PUMP_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:149,
 * "#define ESP_MATTER_HEAT_PUMP_DEVICE_TYPE_ID 0x0309"). Given as a plain
 * integer: there is no esp-matter header on a host build. */
const uint32_t kHeatPumpDeviceType = 0x0309;
}  // namespace

MatterHeatPump::MatterHeatPump() : meas(this) {}

MatterHeatPump::~MatterHeatPump() {
  end();
}

bool MatterHeatPump::begin() {
  if (!hearthDeclare(this, kHeatPumpDeviceType)) {
    return false;
  }
  meas.reset();
  /* Set explicitly, never left to the constructor default behind the
   * started guard (the Task 5 ledger note): the heat pump's single
   * variant always carries the energy cluster, so the adders are enabled,
   * and saying so here keeps the state visible rather than inherited. */
  meas.enabled = true;
  started = true;
  return true;
}

void MatterHeatPump::end() {
  started = false;
}

bool MatterHeatPump::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  return meas.setVoltage(mv);
}

bool MatterHeatPump::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  return meas.setActiveCurrent(ma);
}

bool MatterHeatPump::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.setActivePower(mw);
}

bool MatterHeatPump::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  return meas.setFrequency(mhz);
}

bool MatterHeatPump::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  return meas.pushMeasurements(mv, ma, mw);
}

bool MatterHeatPump::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyImported(mwh);
}

bool MatterHeatPump::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  return meas.addEnergyExported(mwh);
}

int64_t MatterHeatPump::getVoltage() {
  return meas.getVoltage();
}

int64_t MatterHeatPump::getActiveCurrent() {
  return meas.getActiveCurrent();
}

int64_t MatterHeatPump::getActivePower() {
  return meas.getActivePower();
}

int64_t MatterHeatPump::getFrequency() {
  return meas.getFrequency();
}

uint64_t MatterHeatPump::getEnergyImported() {
  return meas.getEnergyImported();
}

uint64_t MatterHeatPump::getEnergyExported() {
  return meas.getEnergyExported();
}

/* B229 semantics via the shared helper: resend NOTHING, clear only the
 * wire-pushed memory. See HearthMeasurementPush.cpp for the rationale. */
void MatterHeatPump::hearthOnReconciled() {
  meas.onReconciled();
}

/*
 * S3.25: no +MTATTR URC ever reports the measurement clusters
 * (Instance-served, the 0.6.0 rule), so there is nothing legitimate to
 * cache here, and an injected line naming 144/145 must NOT move the
 * cache. Returns true (consumed), the sibling classes' shape.
 */
bool MatterHeatPump::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

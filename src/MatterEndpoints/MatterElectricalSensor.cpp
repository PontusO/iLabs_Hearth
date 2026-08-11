/*
 * MatterElectricalSensor.cpp - implementation. See the header for the
 * design notes: null-until-pushed semantics via has-value flags, the
 * Instance-served rule (cache updates on successful local push only), why
 * measurements are never re-pushed on reconcile, and the energy adders'
 * accumulate-locally-push-the-total contract.
 */
#include "MatterEndpoints/MatterElectricalSensor.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* electrical_sensor (ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID),
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:124,
 * "#define ESP_MATTER_ELECTRICAL_SENSOR_DEVICE_TYPE_ID 0x0510". Given as a
 * plain integer: there is no esp-matter header on a host build to pull the
 * named constant from. */
const uint32_t kElectricalSensorDeviceType = 0x0510;
}  // namespace

MatterElectricalSensor::MatterElectricalSensor() {}

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
  voltage = activeCurrent = activePower = frequency = 0;
  hasVoltage = hasActiveCurrent = hasActivePower = hasFrequency = false;
  energyImported = energyExported = 0;
  started = true;
  return true;
}

/*
 * "AT+MTMEAS=<ep>,144,<field>,<value>[,...]" (AT_MT_SPEC.md S3.25).
 * getEndPointId() == 0 is checked directly here, not through
 * hearthEndPointAddressable(): that guard is private to MatterEndPoint and
 * used internally by the AT+MTATTR paths, which AT+MTMEAS does not go
 * through; a custom wire verb repeats the check locally, the
 * MatterWaterValve::hearthSendValveState() pattern.
 *
 * Buffer math: "AT+MTMEAS=" (10) + ep (up to 5) + ",144" (4) = 19, plus at
 * most three pairs of ",<f>,<value>" where the worst value is
 * "-9223372036854775808" (20 chars), so 3 * 23 = 69; 88 total plus NUL.
 */
bool MatterElectricalSensor::hearthSendPowerPairs(const uint8_t *fields, const int64_t *values, uint8_t count) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[112];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu", (unsigned)getEndPointId(), (unsigned long)kPowerMeasurementClusterId);
  for (uint8_t i = 0; i < count; i++) {
    n += snprintf(cmd + n, sizeof(cmd) - n, ",%u,%lld", (unsigned)fields[i], (long long)values[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/* Same shape on cluster 145. The counters are unsigned on the wire (the
 * firmware rejects a leading minus at parse), so %llu, never %lld: the
 * Task 6 signedness-first rule. */
bool MatterElectricalSensor::hearthSendEnergyTotal(uint8_t field, uint64_t total) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[64];
  snprintf(
    cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu,%u,%llu", (unsigned)getEndPointId(), (unsigned long)kEnergyMeasurementClusterId, (unsigned)field,
    (unsigned long long)total
  );
  return Hearth.hearthCommand(cmd) == 0;
}

/* The four setters share one shape: no-op only when the field has been
 * pushed before AND the value is unchanged (the fabric-side value starts
 * null, so the zero-initialised cache must not suppress a first push of
 * 0); cache and has-flag commit only on a successful write. */
bool MatterElectricalSensor::setVoltage(int64_t mv) {
  if (!started) {
    return false;
  }
  if (hasVoltage && voltage == mv) {
    return true;
  }
  const uint8_t f = kFieldVoltage;
  if (!hearthSendPowerPairs(&f, &mv, 1)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  voltage = mv;
  hasVoltage = true;
  return true;
}

bool MatterElectricalSensor::setActiveCurrent(int64_t ma) {
  if (!started) {
    return false;
  }
  if (hasActiveCurrent && activeCurrent == ma) {
    return true;
  }
  const uint8_t f = kFieldActiveCurrent;
  if (!hearthSendPowerPairs(&f, &ma, 1)) {
    return false;
  }
  activeCurrent = ma;
  hasActiveCurrent = true;
  return true;
}

bool MatterElectricalSensor::setActivePower(int64_t mw) {
  if (!started) {
    return false;
  }
  if (hasActivePower && activePower == mw) {
    return true;
  }
  const uint8_t f = kFieldActivePower;
  if (!hearthSendPowerPairs(&f, &mw, 1)) {
    return false;
  }
  activePower = mw;
  hasActivePower = true;
  return true;
}

bool MatterElectricalSensor::setFrequency(int64_t mhz) {
  if (!started) {
    return false;
  }
  if (hasFrequency && frequency == mhz) {
    return true;
  }
  const uint8_t f = kFieldFrequency;
  if (!hearthSendPowerPairs(&f, &mhz, 1)) {
    return false;
  }
  frequency = mhz;
  hasFrequency = true;
  return true;
}

/* Always writes, even byte-identical to the previous sample: a batch is a
 * fresh reading and each push re-reports the fields dirty (see the header
 * comment). All three caches commit atomically with the one wire line:
 * the firmware applies the pairs validate-then-apply, so a refused line
 * changed nothing on the device and must change nothing here. */
bool MatterElectricalSensor::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
  if (!started) {
    return false;
  }
  const uint8_t fields[3] = {kFieldVoltage, kFieldActiveCurrent, kFieldActivePower};
  const int64_t values[3] = {mv, ma, mw};
  if (!hearthSendPowerPairs(fields, values, 3)) {
    return false;
  }
  voltage = mv;
  activeCurrent = ma;
  activePower = mw;
  hasVoltage = hasActiveCurrent = hasActivePower = true;
  return true;
}

/* FULL only: POWER_ONLY builds no energy cluster, so refuse host-side
 * with error 1 and no wire traffic (the header comment's rationale). The
 * accumulator commits only on a successful push, so a refused or failed
 * line never lets the host's running total drift from what the fabric
 * last saw. Overflow past 2^64 wraps as unsigned arithmetic does; the
 * firmware's own range cap is 2^62 (S3.25), hit long before. */
bool MatterElectricalSensor::addEnergyImported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  if (variantSel != FULL) {
    Hearth.hearthSetError(1);
    return false;
  }
  uint64_t total = energyImported + mwh;
  if (!hearthSendEnergyTotal(kFieldEnergyImported, total)) {
    return false;
  }
  energyImported = total;
  return true;
}

bool MatterElectricalSensor::addEnergyExported(uint64_t mwh) {
  if (!started) {
    return false;
  }
  if (variantSel != FULL) {
    Hearth.hearthSetError(1);
    return false;
  }
  uint64_t total = energyExported + mwh;
  if (!hearthSendEnergyTotal(kFieldEnergyExported, total)) {
    return false;
  }
  energyExported = total;
  return true;
}

int64_t MatterElectricalSensor::getVoltage() {
  return voltage;
}

int64_t MatterElectricalSensor::getActiveCurrent() {
  return activeCurrent;
}

int64_t MatterElectricalSensor::getActivePower() {
  return activePower;
}

int64_t MatterElectricalSensor::getFrequency() {
  return frequency;
}

uint64_t MatterElectricalSensor::getEnergyImported() {
  return energyImported;
}

uint64_t MatterElectricalSensor::getEnergyExported() {
  return energyExported;
}

/*
 * Hearth's own reconcile hook (MatterEndPoint.h), on every reconcile, not
 * only the first. Unlike the label/mode-list overrides in the sibling
 * classes this resends NOTHING: measurements are volatile readings, and
 * re-pushing the cache would report a stale sample as fresh. It exists
 * because a reconcile means the co-processor rebooted (or first came up)
 * and the fabric-side fields are null again, so the "already on the wire"
 * memory is stale: cleared here, a setter repeating its pre-reboot value
 * writes instead of no-opping forever against a null fabric field. The
 * cache VALUES stay (the getters keep answering the last pushed sample),
 * and the energy accumulators stay too: they are the host-side source of
 * truth and the adders always push the cumulative total anyway, so the
 * first add after the reboot re-seeds the fabric's counter by
 * construction. Zero wire traffic here, deliberately.
 */
void MatterElectricalSensor::hearthOnReconciled() {
  hasVoltage = hasActiveCurrent = hasActivePower = hasFrequency = false;
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

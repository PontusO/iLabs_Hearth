/*
 * HearthMeasurementPush.cpp - implementation, the bodies moved verbatim
 * from MatterElectricalSensor.cpp (Task 5, energy round B; design spec
 * 4.1). See HearthMeasurementPush.h for the owner/helper division of
 * labour, and MatterElectricalSensor.h for the behaviour contract:
 * null-until-pushed semantics via has-value flags, the Instance-served
 * rule (cache updates on successful local push only), why measurements
 * are never re-pushed on reconcile, and the energy adders'
 * accumulate-locally-push-the-total contract.
 */
#include "HearthMeasurementPush.h"
#include "MatterEndPoint.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

HearthMeasurementPush::HearthMeasurementPush(MatterEndPoint *owner) : owner(owner) {}

void HearthMeasurementPush::reset() {
  voltage = activeCurrent = activePower = frequency = 0;
  hasVoltage = hasActiveCurrent = hasActivePower = hasFrequency = false;
  energyImported = energyExported = 0;
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
bool HearthMeasurementPush::hearthSendPowerPairs(const uint8_t *fields, const int64_t *values, uint8_t count) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[112];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu", (unsigned)owner->getEndPointId(), (unsigned long)kPowerMeasurementClusterId);
  for (uint8_t i = 0; i < count; i++) {
    n += snprintf(cmd + n, sizeof(cmd) - n, ",%u,%lld", (unsigned)fields[i], (long long)values[i]);
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/* Same shape on cluster 145. The counters are unsigned on the wire (the
 * firmware rejects a leading minus at parse), so %llu, never %lld: the
 * Task 6 signedness-first rule. */
bool HearthMeasurementPush::hearthSendEnergyTotal(uint8_t field, uint64_t total) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[64];
  snprintf(
    cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu,%u,%llu", (unsigned)owner->getEndPointId(), (unsigned long)kEnergyMeasurementClusterId, (unsigned)field,
    (unsigned long long)total
  );
  return Hearth.hearthCommand(cmd) == 0;
}

/* The four setters share one shape: no-op only when the field has been
 * pushed before AND the value is unchanged (the fabric-side value starts
 * null, so the zero-initialised cache must not suppress a first push of
 * 0); cache and has-flag commit only on a successful write. The owner's
 * started guard runs before delegation, never here. */
bool HearthMeasurementPush::setVoltage(int64_t mv) {
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

bool HearthMeasurementPush::setActiveCurrent(int64_t ma) {
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

bool HearthMeasurementPush::setActivePower(int64_t mw) {
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

bool HearthMeasurementPush::setFrequency(int64_t mhz) {
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
 * fresh reading and each push re-reports the fields dirty (see
 * MatterElectricalSensor.h's header comment). All three caches commit
 * atomically with the one wire line: the firmware applies the pairs
 * validate-then-apply, so a refused line changed nothing on the device
 * and must change nothing here. */
bool HearthMeasurementPush::pushMeasurements(int64_t mv, int64_t ma, int64_t mw) {
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

/* Enabled owners only: a disabled surface builds no energy cluster, so
 * refuse host-side with error 1 and no wire traffic (the sensor's
 * POWER_ONLY rationale; see MatterElectricalSensor.h's header comment).
 * The accumulator commits only on a successful push, so a refused or
 * failed line never lets the host's running total drift from what the
 * fabric last saw. Overflow past 2^64 wraps as unsigned arithmetic does;
 * the firmware's own range cap is 2^62 (S3.25), hit long before. */
bool HearthMeasurementPush::addEnergyImported(uint64_t mwh) {
  if (!enabled) {
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

bool HearthMeasurementPush::addEnergyExported(uint64_t mwh) {
  if (!enabled) {
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

int64_t HearthMeasurementPush::getVoltage() {
  return voltage;
}

int64_t HearthMeasurementPush::getActiveCurrent() {
  return activeCurrent;
}

int64_t HearthMeasurementPush::getActivePower() {
  return activePower;
}

int64_t HearthMeasurementPush::getFrequency() {
  return frequency;
}

uint64_t HearthMeasurementPush::getEnergyImported() {
  return energyImported;
}

uint64_t HearthMeasurementPush::getEnergyExported() {
  return energyExported;
}

/*
 * The B229 semantics, called from the owner's hearthOnReconciled() (on
 * every reconcile, not only the first). Unlike the label/mode-list
 * overrides in the sibling classes this resends NOTHING: measurements are
 * volatile readings, and re-pushing the cache would report a stale sample
 * as fresh. It exists because a reconcile means the co-processor rebooted
 * (or first came up) and the fabric-side fields are null again, so the
 * "already on the wire" memory is stale: cleared here, a setter repeating
 * its pre-reboot value writes instead of no-opping forever against a null
 * fabric field. The cache VALUES stay (the getters keep answering the
 * last pushed sample), and the energy accumulators stay too: they are the
 * host-side source of truth and the adders always push the cumulative
 * total anyway, so the first add after the reboot re-seeds the fabric's
 * counter by construction. Zero wire traffic here, deliberately.
 */
void HearthMeasurementPush::onReconciled() {
  hasVoltage = hasActiveCurrent = hasActivePower = hasFrequency = false;
}

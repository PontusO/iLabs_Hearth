/*
 * MatterElectricalSensor.h - Task 7 (energy round A): the Electrical
 * Sensor, device type 0x0510, the first measurement-push endpoint type
 * behind AT+MTMEAS (AT_MT_SPEC.md S3.25). A Hearth original: arduino-esp32's
 * Matter library ships no electrical measurement class at all (see
 * Hearth.h's umbrella comment), so the public surface below is this port's
 * own design against the firmware's wire contract.
 *
 * The endpoint carries ElectricalPowerMeasurement (cluster 0x0090, 144)
 * and, at the FULL variant, ElectricalEnergyMeasurement (0x0091, 145).
 * The variant byte maps S3.9's scheme: 0 (FULL) builds both clusters,
 * 1 (POWER_ONLY) builds power measurement only, the current-clamp case (a
 * product that measures instantaneous power but keeps no energy
 * accounting). The sensor additionally carries PowerTopology (0x009C,
 * NodeTopology feature) on the firmware side; that cluster is entirely
 * firmware-derived from the device type and has no host surface, which is
 * exactly what lets MatterElectricalMeter subclass this class unchanged.
 *
 * Everything here is Instance-served on the firmware side (S3.25's "no
 * AT+MTATTR path, no +MTATTR URCs" rule, the 0.6.0 convention): a
 * controller can never write any of these attributes, no +MTATTR URC ever
 * reports them, and the host's own pushed values are the single source of
 * truth. Consequences, all pinned by test_electrical.cpp:
 *
 * - Getters read a local last-pushed cache; there is no wire read-back.
 * - attributeChangeCB() deliberately ignores clusters 144/145: an injected
 *   +MTATTR URC naming them must not move the cache.
 * - The cache updates only on a successful local push (an OK-answered
 *   AT+MTMEAS line); a refused write leaves it untouched.
 *
 * NULL-UNTIL-PUSHED. Every power field starts null on the fabric and stays
 * null until the host first pushes it (S3.25). Host-side that is modelled
 * with per-field has-value flags, not a magic value: the getters return 0
 * before the first successful push, and a first setVoltage(0) is a real
 * change that reaches the wire even though the cache's zero-initialised
 * default compares equal. After the first push, a setter repeating the
 * cached value is a no-op (the sibling-class convention); pushMeasurements()
 * by contrast ALWAYS writes, because a batch is a fresh sample of volatile
 * readings and each push re-reports the fields dirty so subscriptions fire
 * per sample.
 *
 * MEASUREMENTS ARE NOT RE-PUSHED ON RECONCILE. Unlike the cabinet's level
 * labels (configuration the firmware does not persist), these are volatile
 * readings: re-pushing the cache after a link re-establishment would
 * report a stale sample as fresh. hearthOnReconciled() is therefore NOT
 * overridden; after a co-processor reboot the fabric-side fields are null
 * again until the sketch pushes its own next sample, and begin() after
 * reconcile leaves them null the same way. The energy accumulators live
 * host-side and also start over at 0 on a fresh boot of the HOST; a sketch
 * that needs lifetime totals across host reboots persists them itself
 * (e.g. via Preferences) and seeds the first addEnergyImported() call.
 *
 * The energy adders ACCUMULATE LOCALLY AND PUSH THE TOTAL: the firmware
 * serves cumulative counters wrapped in timestamped measurement structs
 * and emits a CumulativeEnergyMeasured event per push (S3.25), so
 * addEnergyImported(delta) sends the new running total, never the delta.
 * On POWER_ONLY both adders are refused HOST-side with Hearth error 1 and
 * zero wire traffic: the variant builds no energy cluster, so the answer
 * is already known without spending the round trip.
 *
 * Neither cluster has commands, so there is no +MTCMD dispatch path: this
 * class overrides no command virtual and the base class's fail-closed
 * defaults stand (pinned by a probe subclass in test_electrical.cpp).
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"

class MatterElectricalSensor : public MatterEndPoint {
public:
  // The 0x0510 variant byte (AT_MT_SPEC.md S3.9): FULL builds
  // ElectricalPowerMeasurement plus ElectricalEnergyMeasurement,
  // POWER_ONLY builds power measurement only (the current-clamp case).
  // On the SENSOR both variants are fully conformant: the device type XML
  // lists the two measurement clusters as a pick-at-least-one choice.
  enum Variant_t {
    FULL = 0,
    POWER_ONLY = 1
  };

  MatterElectricalSensor();
  ~MatterElectricalSensor();

  // declares the endpoint only; no initial state to reconcile
  // (measurements are volatile, see the header comment).
  bool begin(Variant_t variant = FULL);
  // this will stop processing Electrical Sensor Matter events
  void end();

  // Individual field pushes, each one AT+MTMEAS line on cluster 144. Units
  // are the wire's own (S3.25): millivolts, milliamperes, milliwatts,
  // millihertz. Full-width int64 end to end (Task 6); range enforcement
  // (+-2^62 for the first three, 0..1000000 for frequency) is the
  // firmware's, answered as +MTERR:1.
  bool setVoltage(int64_t mv);
  bool setActiveCurrent(int64_t ma);
  bool setActivePower(int64_t mw);
  bool setFrequency(int64_t mhz);

  // One wire line carrying all three field/value pairs (voltage mV,
  // current mA, power mW), the normal per-sample path. Always writes,
  // even byte-identical to the previous sample; see the header comment.
  bool pushMeasurements(int64_t mv, int64_t ma, int64_t mw);

  // Accumulate locally, push the new cumulative total (mWh) on cluster
  // 145. FULL only: POWER_ONLY refuses host-side with Hearth error 1 and
  // zero wire traffic. The firmware timestamps the measurement period and
  // emits the CumulativeEnergyMeasured event itself (S3.25).
  bool addEnergyImported(uint64_t mwh);
  bool addEnergyExported(uint64_t mwh);

  // Local last-pushed cache reads; no wire round trip (the attributes are
  // Instance-served, AT+MTATTR cannot reach them). 0 until first pushed.
  int64_t getVoltage();
  int64_t getActiveCurrent();
  int64_t getActivePower();
  int64_t getFrequency();
  uint64_t getEnergyImported();
  uint64_t getEnergyExported();

  // this function is called by Matter internal event processor. It could be
  // overwritten by the application, if necessary. A documented no-op here:
  // S3.25 promises no +MTATTR URC ever reports these clusters, and an
  // injected one must not move the cache (see the header comment).
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

protected:
  // clang-format off
  /* Wire constants (AT_MT_SPEC.md S3.25), field ids mirrored from the
   * firmware's main/include/mt_matter.h MT_MEAS_F_* / MT_ENERGY_F_* table
   * (read from that source, not transcribed from memory). The two field
   * spaces overlap numerically; only the cluster id decides which applies. */
  static const uint32_t kPowerMeasurementClusterId  = 0x0090;  // 144, ElectricalPowerMeasurement
  static const uint32_t kEnergyMeasurementClusterId = 0x0091;  // 145, ElectricalEnergyMeasurement
  static const uint8_t  kFieldVoltage        = 0;  // MT_MEAS_F_VOLTAGE, mV
  static const uint8_t  kFieldActiveCurrent  = 1;  // MT_MEAS_F_ACTIVE_CURRENT, mA
  static const uint8_t  kFieldActivePower    = 2;  // MT_MEAS_F_ACTIVE_POWER, mW
  static const uint8_t  kFieldFrequency      = 3;  // MT_MEAS_F_FREQUENCY, mHz
  static const uint8_t  kFieldEnergyImported = 0;  // MT_ENERGY_F_IMPORTED, mWh
  static const uint8_t  kFieldEnergyExported = 1;  // MT_ENERGY_F_EXPORTED, mWh
  // clang-format on

  // The meter subclass's begin() calls this with its own device type
  // constant, the MatterOperationalStateEndpoint trio's shape: the wire
  // contract past declaration is identical for both types (S3.25).
  bool hearthBeginElectrical(uint32_t deviceTypeId, Variant_t variant);

  // wire-only AT+MTMEAS writes; the caller decides whether/what to commit
  // to the cache (house discipline: a failed write must not update it).
  bool hearthSendPowerPairs(const uint8_t *fields, const int64_t *values, uint8_t count);
  bool hearthSendEnergyTotal(uint8_t field, uint64_t total);

  bool started = false;
  Variant_t variantSel = FULL;

  // last-pushed cache; the has-flags model the fabric's null-until-pushed
  // state (see the header comment).
  int64_t voltage = 0;
  int64_t activeCurrent = 0;
  int64_t activePower = 0;
  int64_t frequency = 0;
  bool hasVoltage = false;
  bool hasActiveCurrent = false;
  bool hasActivePower = false;
  bool hasFrequency = false;

  // host-side cumulative accumulators, the values the adders push.
  uint64_t energyImported = 0;
  uint64_t energyExported = 0;
};

/*
 * HearthMeasurementPush.h - Task 5 (energy round B): the shared
 * measurement-push helper, extracted verbatim from MatterElectricalSensor
 * (design spec 4.1) so the electrical push surface (the AT+MTMEAS wire
 * emission, the per-field cache with has-flags, the energy accumulators,
 * the B229 reconcile semantics) can be embedded as a member by any
 * endpoint class that carries the measurement clusters:
 * MatterElectricalSensor today, MatterWaterHeater and MatterHeatPump in
 * Task 6. The owning class keeps its own public API and delegates every
 * call here; behaviour is byte-identical to the pre-extraction sensor,
 * pinned by test_electrical.cpp passing untouched.
 *
 * The semantics themselves are documented where they always were, on
 * MatterElectricalSensor.h's class comment (null-until-pushed via
 * has-value flags, the Instance-served rule, why measurements are never
 * re-pushed on reconcile, the adders' accumulate-locally-push-the-total
 * contract). This class is the mechanism, that comment is the contract.
 *
 * Division of labour with the owner, deliberate and load-bearing:
 *
 * - The owner keeps its own `started` guard IN FRONT of every delegation:
 *   a not-yet-begun endpoint returns false with no Hearth error and no
 *   wire traffic, exactly as before the extraction. This class never
 *   checks it.
 * - `enabled` replaces the owner's variant-refusal branch on the energy
 *   adders (the sensor's POWER_ONLY case): false refuses HOST-side with
 *   Hearth error 1 and zero wire traffic. The owner sets it from its
 *   variant at begin(); it gates ONLY the adders, never the power-side
 *   setters, matching the branch it replaced.
 * - reset() is the owner's begin()-time re-initialisation (cache, flags,
 *   accumulators back to the fresh state). It does not touch `enabled`:
 *   the owner assigns that right after, from the variant it validated.
 * - onReconciled() is the B229 semantics: clears the wire-pushed memory
 *   (the has-value flags) ONLY. Cache values and accumulators survive,
 *   nothing is re-sent. The owner's hearthOnReconciled() calls it.
 */
#pragma once

#include <stdint.h>

class MatterEndPoint;

class HearthMeasurementPush {
public:
  // Stores the pointer only; the owner passes `this` from its own
  // constructor's init list, and the endpoint id is read per wire call
  // (it is 0 until the owner's declaration reconciles).
  explicit HearthMeasurementPush(MatterEndPoint *owner);

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
  // even byte-identical to the previous sample; see
  // MatterElectricalSensor.h's header comment.
  bool pushMeasurements(int64_t mv, int64_t ma, int64_t mw);

  // Accumulate locally, push the new cumulative total (mWh) on cluster
  // 145. Gated by `enabled` below: disabled refuses host-side with Hearth
  // error 1 and zero wire traffic. The firmware timestamps the
  // measurement period and emits the CumulativeEnergyMeasured event
  // itself (S3.25).
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

  // B229 semantics: clears the wire-pushed memory (the has-value flags)
  // only, so the next setter call reaches the wire again after a
  // co-processor reboot; cache values and accumulators survive, nothing
  // is re-sent. Called from the owner's hearthOnReconciled().
  void onReconciled();

  // begin()-time re-initialisation: cache, has-flags and accumulators
  // back to the fresh state. Leaves `enabled` alone (the owner assigns
  // it right after, from its validated variant).
  void reset();

  // false = the energy adders refuse with hearthSetError(1) and zero
  // wire traffic (the sensor's POWER_ONLY case; the answer is already
  // known without spending the round trip). Set by the owner from its
  // variant at begin(). Gates the adders only, never the power setters.
  bool enabled = true;

private:
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

  // wire-only AT+MTMEAS writes; the caller decides whether/what to commit
  // to the cache (house discipline: a failed write must not update it).
  bool hearthSendPowerPairs(const uint8_t *fields, const int64_t *values, uint8_t count);
  bool hearthSendEnergyTotal(uint8_t field, uint64_t total);

  MatterEndPoint *owner;

  // last-pushed cache; the has-flags model the fabric's null-until-pushed
  // state (see MatterElectricalSensor.h's header comment).
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

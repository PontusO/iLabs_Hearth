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
 * MEASUREMENTS ARE NOT RE-PUSHED ON RECONCILE, BUT THE WIRE-PUSHED MEMORY
 * IS CLEARED. Unlike the cabinet's level labels (configuration the
 * firmware does not persist), these are volatile readings: re-pushing the
 * cache after a link re-establishment would report a stale sample as
 * fresh. hearthOnReconciled() therefore resends NOTHING; after a
 * co-processor reboot the fabric-side fields are null again until the
 * sketch pushes its own next sample, which is the honest state. What the
 * override DOES do is clear the has-value flags, because the fabric's
 * nulls have made the "already on the wire" memory stale: without that, a
 * setter repeating its pre-reboot value (setFrequency is typically set
 * once) would no-op forever against a fabric field that is null. So after
 * a reconcile the host cache survives (the getters keep answering the
 * last pushed sample) but the wire-pushed memory does not: the next
 * setter call writes, even with an unchanged value, and re-arms the
 * usual no-op guard. The energy accumulators are NOT touched either way:
 * they are the host-side source of truth, the adders always push the
 * cumulative total, and so the first add after the reboot (even of 0)
 * re-seeds the fabric's counter by construction. The accumulators still
 * start over at 0 on a fresh boot of the HOST; a sketch that needs
 * lifetime totals across host reboots persists them itself (e.g. via
 * Preferences) and seeds the first addEnergyImported() call.
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
 *
 * SINCE ROUND B (design spec 4.1) the mechanics live in
 * HearthMeasurementPush, a member helper shared with the other
 * measurement-bearing endpoint classes: the cache, has-flags,
 * accumulators and AT+MTMEAS emission moved there verbatim, and every
 * method below delegates. This comment remains the behaviour contract;
 * nothing observable changed (pinned by test_electrical.cpp passing
 * untouched across the extraction).
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthMeasurementPush.h"

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
  // The meter subclass's begin() calls this with its own device type
  // constant, the MatterOperationalStateEndpoint trio's shape: the wire
  // contract past declaration is identical for both types (S3.25).
  bool hearthBeginElectrical(uint32_t deviceTypeId, Variant_t variant);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: resends nothing (measurements are volatile readings), only
  // clears the has-value flags so the next setter call reaches the wire
  // again. See the header comment. Delegates to meas.onReconciled().
  void hearthOnReconciled() override;

  bool started = false;
  Variant_t variantSel = FULL;

  // The extracted push surface (round B): wire constants, last-pushed
  // cache, has-flags and energy accumulators all live inside. begin()
  // resets it and sets its `enabled` gate from the variant (FULL only).
  HearthMeasurementPush meas;
};

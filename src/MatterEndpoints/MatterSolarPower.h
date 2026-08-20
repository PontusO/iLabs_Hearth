/*
 * MatterSolarPower.h - Task 6 (energy round C1): the Solar Power endpoint,
 * device type 0x0017. A Hearth original: arduino-esp32's Matter library
 * ships no solar class at all (see Hearth.h's umbrella comment), so the
 * public surface below is this port's own design against the firmware's
 * wire contract (AT_MT_SPEC.md S3.9/S3.25) and the round's design
 * spec 4.2.
 *
 * Device type 0x0017 is solar_power
 * (esp_matter_endpoint.h's ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:142,
 * "#define ESP_MATTER_SOLAR_POWER_DEVICE_TYPE_ID 0x0017").
 *
 * The host surface is the shared measurement-push helper plus identity,
 * NOTHING else, the MatterHeatPump shape with a variant byte added:
 *
 * - The firmware endpoint carries PowerSource (wired feature) purely at
 *   composition, entirely derived from the device type with no host
 *   surface, and grafts the Electrical Sensor (device type 0x0510) whose
 *   EPM/EEM clusters this class pushes into. Nothing else exists to set.
 * - DISCLOSED CONFORMANCE GAP (the variant-1 meter's precedent): the 1.5.1
 *   SolarPower.xml mandates User Label on the endpoint, which the firmware
 *   does not build (`SUPPORT_USER_LABEL_CLUSTER` stays n, design spec 2.2);
 *   and on the NO_ENERGY variant the composed Electrical Sensor is missing
 *   its mandated EEM cluster. Both are recorded in the firmware thunk
 *   (mt_devtypes.cpp's mk_solar_power()) and in ARCHITECTURE.md 8.12.
 *
 * VARIANTS (S3.9's 0x0017 rows): FULL (0) grafts the sensor WITH energy
 * measurement; NO_ENERGY (1) is the current-clamp shape, the sensor
 * without EEM (mk_solar_power() passes `variant == 0` as its energy flag),
 * disclosed sub-conformant. On NO_ENERGY the two energy adders refuse
 * HOST-side with Hearth error 1 and zero wire traffic -- the answer is
 * already known without spending the round trip -- while every power-side
 * setter keeps working. That is exactly the electrical sensor's POWER_ONLY
 * semantic, and it is expressed the same way: the embedded helper's
 * `enabled` flag, set from the variant at begin().
 *
 * Everything served is Instance-served on the firmware side (S3.25's "no
 * AT+MTATTR path, no +MTATTR URCs" rule): getters read the host-side
 * last-pushed cache, an injected +MTATTR naming cluster 144/145 must not
 * move it, measurements are NOT re-pushed on reconcile (B229: only the
 * wire-pushed memory clears), and the energy adders accumulate locally and
 * push the cumulative total. The class comment on MatterElectricalSensor.h
 * is the behaviour contract; HearthMeasurementPush is the shared mechanism.
 *
 * ActivePower is SIGNED and that is the point of this device type: an
 * array that is generating reports NEGATIVE milliwatts (energy leaving the
 * node), full-width int64 end to end, and its cumulative energy lands on
 * the EXPORTED counter. A string inverter with a house load behind the
 * same clamp swings the sign back positive; both directions travel the
 * same pipeline.
 *
 * Neither measurement cluster has commands, so there is no +MTCMD dispatch
 * path: this class overrides no command virtual and the base class's
 * fail-closed defaults stand (pinned by a probe subclass in
 * test_solarpower.cpp, the heat pump's own pattern). Device Energy
 * Management belongs to the battery and DEM device types, never here:
 * SolarPower.xml requests no DEM cluster and the firmware builds none, so
 * a sketch reaching for it is a compile error, which is the honest
 * enforcement.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthMeasurementPush.h"

class MatterSolarPower : public MatterEndPoint {
public:
  // The 0x0017 variant byte (AT_MT_SPEC.md S3.9): FULL grafts the composed
  // Electrical Sensor with its energy cluster, NO_ENERGY without it (the
  // current-clamp shape, disclosed sub-conformant; see the header comment).
  enum Variant_t {
    FULL = 0,
    NO_ENERGY = 1
  };

  MatterSolarPower();
  ~MatterSolarPower();

  // declares the endpoint only (device type 0x0017 plus the variant byte);
  // no initial state to reconcile, every measurement stays null on the
  // fabric until first pushed. No wire traffic.
  bool begin(Variant_t variant = FULL);
  // this will stop processing Solar Power Matter events
  void end();

  // The shared measurement surface (design spec 4.1), the electrical
  // sensor's exact wire pins and semantics: individual field pushes on
  // cluster 144 (mV/mA/mW/mHz, full-width int64), the batched three-pair
  // per-sample line, and the accumulate-locally-push-the-total energy
  // adders on cluster 145 (mWh). Getters read the last-pushed cache; no
  // wire round trip. On NO_ENERGY the two adders refuse host-side with
  // Hearth error 1 and zero wire traffic (see the header comment).
  bool setVoltage(int64_t mv);
  bool setActiveCurrent(int64_t ma);
  bool setActivePower(int64_t mw);
  bool setFrequency(int64_t mhz);
  bool pushMeasurements(int64_t mv, int64_t ma, int64_t mw);
  bool addEnergyImported(uint64_t mwh);
  bool addEnergyExported(uint64_t mwh);
  int64_t getVoltage();
  int64_t getActiveCurrent();
  int64_t getActivePower();
  int64_t getFrequency();
  uint64_t getEnergyImported();
  uint64_t getEnergyExported();

  // this function is called by Matter internal event processor. A
  // documented no-op: S3.25 promises no +MTATTR URC ever reports the
  // measurement clusters, and an injected one must not move the cache.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

protected:
  // Hearth's own hook (MatterEndPoint.h), on every reconcile: resends
  // nothing, clears only the wire-pushed memory (B229). Delegates to
  // meas.onReconciled().
  void hearthOnReconciled() override;

  bool started = false;
  Variant_t variantSel = FULL;

  // the shared push surface; begin() resets it and sets `enabled`
  // explicitly from the validated variant (never left to the constructor
  // default: the Task 5 ledger note)
  HearthMeasurementPush meas;
};

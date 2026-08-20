/*
 * MatterHeatPump.h - Task 6 (energy round B): the Heat Pump, device type
 * 0x0309. A Hearth original: arduino-esp32's Matter library ships no heat
 * pump class at all (see Hearth.h's umbrella comment), so the public
 * surface below is this port's own design against the firmware's wire
 * contract (AT_MT_SPEC.md S3.9/S3.25) and the round's design spec 4.3.
 *
 * Device type 0x0309 is heat_pump
 * (esp_matter_endpoint.h's ESP_MATTER_HEAT_PUMP_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:149,
 * "#define ESP_MATTER_HEAT_PUMP_DEVICE_TYPE_ID 0x0309"). Single variant.
 *
 * The host surface is the shared measurement-push helper plus identity,
 * NOTHING else, a deliberate near-empty class:
 *
 * - The firmware endpoint carries PowerSource (wired feature) purely at
 *   composition: entirely firmware-derived from the device type, no host
 *   surface, the same reasoning as the electrical sensor's PowerTopology
 *   (its header's own precedent). Nothing to see or set here.
 * - DISCLOSED CONFORMANCE GAP, the variant-1 meter's precedent: the 1.5.1
 *   HeatPump.xml's composedDeviceTypes block mandates a composed
 *   Thermostat device type (0x0301, with User Label) alongside the
 *   Electrical Sensor. This endpoint carries the sensor half and NOT the
 *   thermostat half, matching the SDK's own build (heat_pump's config
 *   has no thermostat member at all); a thermostat graft is a possible
 *   follow-up, not this round (design spec 2.3, ARCHITECTURE.md 8.11).
 *   Consequently this class has no mode, thermostat or boost members of
 *   any kind, and Device Energy Management is deferred whole to Round C
 *   (DE235). A sketch reaching for any of that is a compile error, which
 *   is the honest enforcement.
 *
 * Everything served is Instance-served on the firmware side (S3.25's "no
 * AT+MTATTR path, no +MTATTR URCs" rule): getters read the host-side
 * last-pushed cache, an injected +MTATTR naming cluster 144/145 must not
 * move it, measurements are NOT re-pushed on reconcile (B229: only the
 * wire-pushed memory clears), and the energy adders accumulate locally
 * and push the cumulative total. The class comment on
 * MatterElectricalSensor.h is the behaviour contract; HearthMeasurementPush
 * is the shared mechanism. ActivePower is SIGNED, which matters here more
 * than on any sibling: a heat pump moving energy out (defrost, export)
 * reports negative milliwatts, full-width int64 end to end.
 *
 * Neither measurement cluster has commands, so there is no +MTCMD dispatch
 * path: this class overrides no command virtual and the base class's
 * fail-closed defaults stand (pinned by a probe subclass in
 * test_heatpump.cpp, the electrical sensor's own pattern).
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthMeasurementPush.h"

class MatterHeatPump : public MatterEndPoint {
public:
  MatterHeatPump();
  ~MatterHeatPump();

  // declares the endpoint only (device type 0x0309, variant 0); no initial
  // state to reconcile (measurements are volatile, see the header comment).
  bool begin();
  // this will stop processing Heat Pump Matter events
  void end();

  // The shared measurement surface (design spec 4.1), the electrical
  // sensor's exact wire pins and semantics: individual field pushes on
  // cluster 144 (mV/mA/mW/mHz, full-width int64), the batched three-pair
  // per-sample line, and the accumulate-locally-push-the-total energy
  // adders on cluster 145 (mWh). Getters read the last-pushed cache; no
  // wire round trip.
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

  // the shared push surface; begin() resets it and sets `enabled`
  // explicitly (the heat pump's single variant always carries the energy
  // cluster, but the state is never left implicit: the Task 5 ledger note)
  HearthMeasurementPush meas;
};

/*
 * MatterDeviceEnergyManagement.h - Task 6 (energy round C1): the standalone
 * Device Energy Management endpoint, device type 0x050D. A Hearth original:
 * arduino-esp32's Matter library ships no DEM class at all (see Hearth.h's
 * umbrella comment), so the public surface below is this port's own design
 * against the firmware's wire contract (docs/AT_MT_SPEC.md
 * S3.9/S3.17/S3.25/S3.26) and the round's design spec 4.2.
 *
 * Device type 0x050D is device_energy_management
 * (esp_matter_endpoint.h's ESP_MATTER_DEVICE_ENERGY_MANAGEMENT_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:136,
 * "#define ESP_MATTER_DEVICE_ENERGY_MANAGEMENT_DEVICE_TYPE_ID 0x050D").
 *
 * The endpoint is Task 5's HearthDemControl plus identity, nothing else:
 * the DeviceEnergyManagement cluster (0x0098, 152) and a
 * DeviceEnergyManagementMode ModeBase instance the firmware serves with
 * its conformance default. That mode instance has NO host surface in this
 * release, deliberately: the design spec's class block (4.2) lists none,
 * Mode_DeviceEnergyManagement.xml mandates no tag, and the firmware's
 * accept-list entry carries a kNoOptimization tag-0 default (design spec
 * 3.4), so an unconfigured DEM Mode is already conformant. A host that
 * wants its own mode list drives `AT+MTMODES` directly for now.
 *
 * WHAT THE SURFACE IS: every setter here is one delegation to the shared
 * helper behind this class's own `started` guard. HearthDemControl.h is
 * the contract for all of it (the null-until-pushed discipline on fields
 * 0-5, field 6's event-carrier exception, the accept-pushes-no-state-
 * change semantic, the AT+MTDEMCAP replacement rules and the reconcile
 * split); this class adds the endpoint lifecycle, the variant, and the
 * +MTCMD parse the helper deliberately does not do.
 *
 * VARIANTS (S3.9's 0x050D rows), and the round's ONE adjudicated
 * host-side gate:
 *
 * - CONTROLLABLE (0) builds the cluster with `feature::power_adjustment`
 *   (a ControllableESA): the whole surface below works, and controller
 *   PowerAdjustRequest/CancelPowerAdjustRequest invokes arrive as
 *   adjudicated `+MTCMD` forwards.
 * - REPORT_ONLY (1) builds the same cluster with FeatureMap 0, which is
 *   conformant (the XML's ControllableESA-conditioned choice conformance)
 *   and is a real product shape: an ESA that reports its state and cannot
 *   be told what to do.
 *
 * REPORT_ONLY IS NOT "no DEM surface", and the distinction is the reason
 * this gate lives in THIS class rather than in the helper (Task 5's review
 * adjudication, item 10). The helper's `enabled = false` models "no DEM
 * cluster at all" -- MatterBatteryStorage's NO_DEM, where the firmware
 * answers the cluster-missing code (`+MTERR:3`) for the whole surface.
 * REPORT_ONLY is "the cluster is present, the PowerAdjustment feature is
 * not": the firmware answers the attribute-missing code (`+MTERR:4`) for
 * AT+MTDEMCAP, because PowerAdjustmentCapability is mandatory under the PA
 * feature only. That much is S3.26's own error table, which covers
 * AT+MTDEMCAP and nothing else. That every `AT+MTMEAS` field 0-6 push on
 * cluster 152 still works on this variant is NOT in that table: the spec
 * simply carries no feature-gate column for the 0x0098 field table, and
 * the claim is FIRMWARE-VERIFIED rather than spec-cited -- mt_meas_dem_apply()
 * applies all seven fields with no FeatureMap check of any kind, so the
 * only DEM surface REPORT_ONLY takes away is the capability attribute. So
 * this variant leaves the
 * helper's `enabled` TRUE and refuses exactly ONE method,
 * setPowerAdjustmentCapability(), host-side with Hearth error 1 and zero
 * wire traffic -- the MatterWaterHeater::hearthRefusedOnMinimal() pattern,
 * layered on top of the helper's flag rather than instead of it. The
 * adjust callbacks are not gated because the firmware never forwards them
 * on this variant (FeatureMap 0 means the PA commands are not in
 * AcceptedCommandList at all, so an invoke is answered UnsupportedCommand
 * by CHIP without the host ever being woken); the helper's own fail-closed
 * default (no callback registered denies) remains the backstop.
 *
 * THE REENTRANCY TRAP, worth stating where a sketch author will read it
 * (HearthDemControl.h carries the full version): NEITHER adjust callback
 * may push from inside itself. Both run inside the `+MTCMD` dispatch,
 * where a wire write is refused HEARTH_CMD_REENTRANT. An accepted request
 * needs no push at all (the firmware sets ESAState PowerAdjustActive and
 * emits PowerAdjustStart itself, S3.17), so the callback's whole job is to
 * return a verdict; call pushAdjustmentEnergyUse() and endAdjustment()
 * from ordinary sketch context afterwards, which is exactly what the
 * FullAPI example's load simulator does.
 *
 * The cluster's state is Instance-served (S3.25's "no AT+MTATTR path, no
 * +MTATTR URCs" rule): there are no getters, no attribute callbacks, and
 * an injected +MTATTR naming cluster 152 must move nothing (pinned in
 * test_dem.cpp).
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthDemControl.h"

class MatterDeviceEnergyManagement : public MatterEndPoint {
public:
  // The 0x050D variant byte (AT_MT_SPEC.md S3.9): CONTROLLABLE builds the
  // PowerAdjustment feature, REPORT_ONLY builds FeatureMap 0 (see the
  // header comment for what each one changes about this surface).
  enum Variant_t {
    CONTROLLABLE = 0,
    REPORT_ONLY = 1
  };

  // The AT+MTDEMCAP entry (S3.26), re-exported from the shared helper so a
  // sketch names one type, not two: powers int64 mW, durations uint32 s.
  typedef HearthDemControl::PowerAdjustEntry PowerAdjustEntry;

  MatterDeviceEnergyManagement();
  ~MatterDeviceEnergyManagement();

  // declares the endpoint only (device type 0x050D plus the variant byte)
  // and resets every cache. No wire traffic; the cluster's attributes
  // serve the firmware delegate's defaults until the host pushes.
  bool begin(Variant_t variant = CONTROLLABLE);
  // this will stop processing Device Energy Management Matter events
  void end();

  // Fields 0-5 of the 0x0098 table (S3.25), one AT+MTMEAS line each, with
  // HearthDemControl's null-until-pushed discipline. Enum range checks are
  // the firmware's, answered +MTERR:1.
  bool setESAType(uint8_t t);
  bool setESACanGenerate(bool g);
  bool setESAState(uint8_t s);  // 0..4: Offline, Online, Fault, PowerAdjustActive, Paused
  bool setAbsMinPower(int64_t mw);
  bool setAbsMaxPower(int64_t mw);
  bool setOptOutState(uint8_t s);

  // Field 6: the AdjustmentEnergyUse EVENT CARRIER, not an attribute. It
  // reads back through nothing and is consumed by the next PowerAdjustEnd
  // the firmware emits, so every call is a real wire line (no cache, no
  // no-op suppression).
  bool pushAdjustmentEnergyUse(int64_t mwh);

  // AT+MTDEMCAP (S3.26): replaces the whole PowerAdjustmentCapability
  // list. `n` is 0..4 (0 makes the capability read null and the CHIP
  // server refuse every request); n > 4 is refused host-side with error 1.
  // REFUSED ENTIRELY on the REPORT_ONLY variant, host-side with error 1
  // and zero wire traffic: that variant serves no such attribute (see the
  // header comment's adjudication note).
  bool setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n);

  // Register the host's verdict for a controller-invoked PowerAdjustRequest
  // / CancelPowerAdjustRequest on cluster 152 (S3.17). Plain function
  // pointers, the shared helper's own signature. No callback registered
  // denies by default (fail closed). NEITHER MAY PUSH FROM INSIDE ITSELF
  // (see the header comment's reentrancy note); an accept needs no push at
  // all, since the firmware owns the state transition and the Start event.
  void onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause));
  void onCancelPowerAdjust(bool (*cb)());

  // The sketch's normal-completion path: pushes ESAState Online, which is
  // what makes the firmware emit PowerAdjustEnd(NormalCompletion) with the
  // last field-6 energy figure. Call it from ordinary sketch context
  // (loop()), never from inside a verdict callback.
  bool endAdjustment();

  // this function is called by Matter internal event processor. A
  // documented no-op: cluster 152 is Instance-served (S3.25), so no
  // +MTATTR URC ever reports it and an injected one must move nothing.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h): adjudicates
  // PowerAdjustRequest (152,0) and CancelPowerAdjustRequest (152,1);
  // everything else defers to the fail-closed base default.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  // clang-format off
  /* Wire constants. The cluster and command ids are AT_MT_SPEC.md S3.17's
   * ("cluster 152 command 0 ... command 1"); the field ids live in the
   * shared helper, which owns every AT+MTMEAS line on this cluster. Plain
   * integers, the library's usual pattern: there is no esp-matter or
   * connectedhomeip header on a host build. */
  static const uint32_t kDemClusterId              = 0x0098;  // 152, DeviceEnergyManagement
  static const uint32_t kPowerAdjustRequestCmd     = 0;
  static const uint32_t kCancelPowerAdjustRequestCmd = 1;
  // clang-format on

  // Hearth's own hook (MatterEndPoint.h), on every reconcile: the helper's
  // own split (configuration re-pushed, volatile has-flags cleared without
  // a resend). See HearthDemControl.h.
  void hearthOnReconciled() override;

  // true when the call was refused for the REPORT_ONLY variant (error 1
  // set), the MatterWaterHeater::hearthRefusedOnMinimal() shape. Used by
  // setPowerAdjustmentCapability() and by nothing else, on purpose.
  bool hearthRefusedOnReportOnly();

  bool started = false;
  Variant_t variantSel = CONTROLLABLE;

  // the shared DEM surface (design spec 4.1); begin() resets it and sets
  // `enabled` explicitly, TRUE on both variants (see the header comment)
  HearthDemControl dem;
};

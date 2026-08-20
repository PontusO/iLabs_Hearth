/*
 * MatterBatteryStorage.h - Task 6 (energy round C1): the Battery Storage
 * endpoint, device type 0x0018. A Hearth original: arduino-esp32's Matter
 * library ships no battery storage class at all (see Hearth.h's umbrella
 * comment), so the public surface below is this port's own design against
 * the firmware's wire contract (AT_MT_SPEC.md
 * S3.9/S3.17/S3.25/S3.26) and the round's design spec 4.2.
 *
 * Device type 0x0018 is battery_storage
 * (esp_matter_endpoint.h's ESP_MATTER_BATTERY_STORAGE_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:144,
 * "#define ESP_MATTER_BATTERY_STORAGE_DEVICE_TYPE_ID 0x0018"). THREE
 * host-visible surfaces ride one endpoint, which is why this is a direct
 * MatterEndPoint child embedding two shared helpers rather than a subclass
 * of either:
 *
 * - The composed Electrical Sensor graft (clusters 144/145, S3.25):
 *   the shared HearthMeasurementPush surface with the electrical sensor's
 *   exact semantics. Present on BOTH variants -- mt_devtypes.cpp's
 *   mk_battery_storage() grafts it with its energy flag hard-wired true,
 *   unlike solar's `variant == 0` -- so the energy adders work even on
 *   NO_DEM. ActivePower is SIGNED and carries the whole point of the
 *   device type: charging draws positive milliwatts, discharging feeds
 *   the house or the grid and reports negative.
 * - PowerSource (0x002F, 47), battery|rechargeable: seven EMBER-served
 *   battery attributes, so unlike the measurement surface these ride the
 *   ordinary AT+MTATTR path in both directions and a controller-visible
 *   change arrives back as a +MTATTR URC (design spec 3.5: "no new
 *   surface" -- there is no dedicated command family for any of this).
 * - DeviceEnergyManagement (0x0098, 152) on the FULL variant only: Task
 *   5's HearthDemControl, whole, including the two adjudicated `+MTCMD`
 *   power-adjust forwards.
 *
 * VARIANTS (S3.9's 0x0018 rows): FULL (0) carries the DEM triple (device
 * type 0x050D + the DEM cluster with the PowerAdjustment feature + a DEM
 * Mode instance); NO_DEM (1) omits it and is STILL CONFORMANT
 * (BatteryStorage.xml never requests DEM; the SDK's inclusion is
 * over-delivery, the heat pump shape). On NO_DEM the whole DEM surface
 * refuses HOST-side with Hearth error 1 and zero wire traffic, expressed
 * the way the helper is designed for: `dem.enabled = false`, which is the
 * "no cluster at all" case the firmware answers with the cluster-missing
 * code (+MTERR:3, S3.26). Contrast MatterDeviceEnergyManagement's
 * REPORT_ONLY, which is a different wire fact and is gated differently;
 * that class's header carries the full comparison.
 *
 * THE EMBER ATTRIBUTES, and their two seed rules. The firmware creates
 * them itself (mt_devtypes.cpp's mk_battery_storage(), mirroring
 * esp_matter_endpoint.cpp:1951-1960 with the SDK config's own defaults):
 *
 *   BatVoltage 0x0B (uint32 mV), BatPercentRemaining 0x0C (uint8, HALF
 *   percent 0..200), BatTimeRemaining 0x0D (uint32 s),
 *   BatTimeToFullCharge 0x1B (uint32 s), BatChargingCurrent 0x1D (uint32
 *   mA) are created NULLABLE with no value, so their host cache carries a
 *   has-flag and the FIRST push always writes, even of 0 (the
 *   MatterWaterHeater LocalTemperature precedent for a null-defaulted
 *   ember attribute);
 *   BatCapacity 0x18 (uint32 mAh) is created with 0 and BatChargeState
 *   0x1A (enum8) comes from feature::rechargeable::add()'s config default
 *   0 (kUnknown), both non-nullable, so their caches SEED from those
 *   device values and a write of the same value is a true no-op (the
 *   thermostat-seed precedent: seeding anything else re-creates the
 *   first-write-swallow defect).
 *
 * Attribute ids are PowerSource::Attributes::*::Id from the pinned
 * connectedhomeip checkout's generated header
 * (~/esp/esp-matter/connectedhomeip/connectedhomeip/zzz_generated/
 * app-common/clusters/PowerSource/AttributeIds.h: "namespace BatVoltage {
 * inline constexpr AttributeId Id = 0x0000000B; }" and its siblings),
 * given as plain integers in the .cpp, this library's usual pattern.
 *
 * NO SURFACE FOR THE TWO FAULT LISTS, disclosed: ActiveBatFaults (0x12)
 * and ActiveBatChargeFaults (0x1E) are LISTS, which AT+MTATTR's single
 * integer value cannot carry (S3.25's own scope note). The firmware ships
 * them empty and host-untouched (design spec 3.5), and this class offers
 * no way to change that; a future AT+MTATTRX would be the vehicle.
 * BatChargeLevel, BatReplacementNeeded and the rest of the base
 * PowerSource surface are MatterPowerSource's business on its own
 * endpoint; this class exposes only what its own device type's
 * battery|rechargeable feature set adds.
 *
 * NO GETTERS, the MatterPowerSource precedent: these are host-authoritative
 * readings pushed up to the fabric. Each setter still tracks its own
 * last-written value to skip a redundant wire write (this library's
 * universal setter discipline), and a +MTATTR URC moves that cache, but
 * there is no public accessor. The measurement surface keeps its own
 * getters, which read the push helper's cache.
 *
 * THE RECONCILE SPLIT, three surfaces on one endpoint, every half
 * test-pinned. The DEM configuration (ESAType, ESACanGenerate,
 * AbsMin/MaxPower, the capability list) is re-pushed and the DEM volatile
 * fields' wire memory cleared without a resend (the helper's own split);
 * the measurement fields follow B229 through meas.onReconciled(); and the
 * EMBER attributes get that SAME configuration-versus-sampled split
 * applied to their own surface, per attribute, listed below.
 *
 * THIS LIBRARY HAS BOTH EMBER BEHAVIOURS, and choosing between them is a
 * per-attribute judgement, not a house default. Several classes leave
 * ember attributes alone on reconcile (MatterThermostat and
 * MatterPowerSource override hearthOnReconciled() not at all;
 * MatterWaterHeater overrides it and deliberately skips its thermostat
 * half), and that is right for a value a sketch resamples: the next sample
 * repairs the fabric on its own. But
 * MatterTemperatureControlledCabinet::hearthOnReconciled()
 * (MatterTemperatureControlledCabinet.cpp:214-243, inherited by
 * MatterOvenCavity) re-pushes its four TemperatureControl attributes
 * unconditionally, and the comment above it at :190-213 records WHY: the
 * opposite behaviour was BENCH-OBSERVED to leave the device stale forever,
 * because the setters' skip-if-equal suppressed every later write of a
 * value the host believed it had already set. A value a sketch writes once
 * and never revisits cannot self-heal, so it has to be re-pushed.
 *
 * Per attribute, with the reason each lands where it does:
 *
 *   BatCapacity          CONFIG, re-pushed. A nameplate figure, written
 *                        once at setup() and never again (the FullAPI
 *                        example does exactly that), so after a C6 reboot
 *                        it reverts to its creation default 0 device-side
 *                        and nothing would ever push it back: the
 *                        cabinet's bench bug, one surface over.
 *   BatChargeState       CONFIG, re-pushed. A discrete state the host
 *                        asserts on TRANSITIONS rather than a sample: an
 *                        idle pack can hold IsNotCharging for hours, so
 *                        the next write may be far away. Safe to re-push
 *                        because the firmware derives no event from this
 *                        attribute -- contrast the DEM helper's ESAState,
 *                        which stays volatile precisely because a
 *                        transition out of PowerAdjustActive emits
 *                        PowerAdjustEnd, so a re-push there could
 *                        fabricate an event, while here it can only
 *                        restate a belief the host already holds.
 *   BatVoltage           SAMPLED, B229. Read off the pack every cycle.
 *   BatPercentRemaining  SAMPLED, B229. Ditto, and the headline reading.
 *   BatTimeRemaining     SAMPLED, B229. A derived estimate, recomputed per
 *                        sample while discharging.
 *   BatTimeToFullCharge  SAMPLED, B229. Ditto, while charging.
 *   BatChargingCurrent   SAMPLED, B229. Measured per cycle while charging.
 *
 * B229 here means what it means everywhere else in this round: the
 * wire-pushed memory (the has-flags) is cleared so the very NEXT sample
 * reaches the wire even when it is byte-identical to the last one, while
 * the values themselves are not re-sent, since a stale reading re-reported
 * as fresh would be a lie and the sketch's own loop is one cycle from the
 * truth. Without that clearing a resampling sketch's unchanged value would
 * ALSO be suppressed after a reboot, so the sampled half needs this much
 * even though it needs no re-push.
 *
 * The re-push covers what the HOST has written: each config attribute
 * carries its own "the sketch set this" flag, set by a successful setter
 * call and never by an incoming +MTATTR URC (a URC is the device telling
 * this host something, not this host asserting it), so a reconcile before
 * the sketch has touched either attribute emits nothing on cluster 47.
 *
 * THE REENTRANCY TRAP (HearthDemControl.h carries the full version):
 * neither adjust callback may push from inside itself -- both run inside
 * the `+MTCMD` dispatch, where a wire write is refused
 * HEARTH_CMD_REENTRANT. An accepted request needs no push at all (the
 * firmware sets ESAState PowerAdjustActive and emits PowerAdjustStart
 * itself, S3.17); call pushAdjustmentEnergyUse() and endAdjustment() from
 * ordinary sketch context afterwards.
 *
 * The DEM Mode instance the firmware builds on the FULL variant has no
 * host surface in this release, deliberately: the design spec's class
 * block lists none and the firmware's accept-list entry carries a
 * kNoOptimization tag-0 default (design spec 3.4), so an unconfigured DEM
 * Mode is already conformant. Same disclosure as
 * MatterDeviceEnergyManagement.h's.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthMeasurementPush.h"
#include "HearthDemControl.h"

class MatterBatteryStorage : public MatterEndPoint {
public:
  // The 0x0018 variant byte (AT_MT_SPEC.md S3.9): FULL carries the DEM
  // triple, NO_DEM omits it and stays conformant (see the header comment).
  enum Variant_t {
    FULL = 0,
    NO_DEM = 1
  };

  // The AT+MTDEMCAP entry (S3.26), re-exported from the shared helper so a
  // sketch names one type, not two: powers int64 mW, durations uint32 s.
  typedef HearthDemControl::PowerAdjustEntry PowerAdjustEntry;

  MatterBatteryStorage();
  ~MatterBatteryStorage();

  // declares the endpoint only (device type 0x0018 plus the variant byte)
  // and seeds every cache (see the header comment's two seed rules). No
  // wire traffic.
  bool begin(Variant_t variant = FULL);
  // this will stop processing Battery Storage Matter events
  void end();

  // ---- the measurement-push surface (both variants), the shared helper ----
  // (the electrical sensor's exact wire pins and semantics; ActivePower is
  // signed: positive charging, negative discharging)
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

  // ---- the ember PowerSource battery attributes (both variants) ----
  // Ordinary AT+MTATTR mode-1 writes (reported to subscribers), no-op on
  // an unchanged value, cache committed on success only. The units are the
  // Matter spec's own, NOT converted here (the one place this differs from
  // MatterPowerSource::setBatPercentRemaining(), which takes a percent
  // double: this class takes the raw half-percent wire value the design
  // spec's class block names, so a sketch that already thinks in
  // half-percent steps never round-trips through a float).
  //
  // THE SERVED RANGES, and the one setter that can be refused for range.
  // The firmware creates five of these over the FULL uint32 domain
  // (mt_devtypes.cpp's mk_battery_storage()): BatVoltage, BatTimeRemaining,
  // BatCapacity, BatTimeToFullCharge and BatChargingCurrent, all
  // 0..0xFFFFFFFF. That is deliberately NOT esp-matter's own
  // battery_storage::add(), which caps all five at 0xFFFF; the firmware
  // follows PowerSourceCluster.xml, which types them uint32 with no
  // constraint element, so the type is the bound (firmware fix B263,
  // hardware-verified in round C1 task 8: 13500000 and 4294967295 both
  // write and read back un-truncated). So NO uint32_t a sketch can pass to
  // those five is out of range, and a 13.5 kWh home pack fits comfortably.
  // BatChargeState is created with no min/max at all, so only its enum8
  // width applies. BatPercentRemaining's 0..200 is the sole real range
  // check on this surface, and it is the XML's own.
  //
  // A RANGE REFUSAL CARRIES NO +MTERR CODE (firmware defect B264, open).
  // When a value does land outside an attribute's created min/max,
  // esp-matter's attribute::update() refuses it and the firmware answers a
  // BARE `ERROR` with no +MTERR line, so the setter returns false with
  // Hearth.lastError() == 0 and a sketch must read the BOOL, not the error
  // code. The measured vector, round C1 tasks 7 and 8:
  // AT+MTATTR=1,47,12,201,1 -> ERROR (in width, past BatPercentRemaining's
  // 0..200). Contrast a value too wide for the attribute's TYPE, which is
  // caught earlier and DOES carry a code: AT+MTATTR=1,47,12,1000000 ->
  // +MTERR:1. So lastError() == 1 after one of these means "not even the
  // right width", never "out of range".
  bool setBatVoltage(uint32_t mv);
  bool setBatPercentRemaining(uint8_t halfPercent);  // 0..200 = 0..100%; 201..255 is refused with lastError 0
  bool setBatTimeRemaining(uint32_t seconds);
  bool setBatChargeState(uint8_t state);  // BatChargeStateEnum: 0 Unknown, 1 IsCharging, 2 IsAtFullCharge, 3 IsNotCharging
  bool setBatChargingCurrent(uint32_t ma);
  bool setBatCapacity(uint32_t mah);  // mAh, served over the whole uint32 range (B263)
  bool setBatTimeToFullCharge(uint32_t seconds);

  // ---- the DEM surface (FULL only), the shared helper ----
  // On NO_DEM every one of these refuses host-side with Hearth error 1 and
  // zero wire traffic (the helper's `enabled` gate). See
  // HearthDemControl.h for the null-until-pushed discipline, field 6's
  // event-carrier exception and the accept-pushes-no-state-change rule.
  bool setESAType(uint8_t t);
  bool setESACanGenerate(bool g);
  bool setESAState(uint8_t s);  // 0..4: Offline, Online, Fault, PowerAdjustActive, Paused
  bool setAbsMinPower(int64_t mw);
  bool setAbsMaxPower(int64_t mw);
  bool setOptOutState(uint8_t s);
  bool pushAdjustmentEnergyUse(int64_t mwh);
  bool setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n);
  // register the host's verdict for a controller-invoked
  // PowerAdjustRequest / CancelPowerAdjustRequest (S3.17). Plain function
  // pointers, the shared helper's own signature; no callback registered
  // denies by default. NEITHER MAY PUSH FROM INSIDE ITSELF (see the header
  // comment's reentrancy note).
  void onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause));
  void onCancelPowerAdjust(bool (*cb)());
  // the normal-completion path: pushes ESAState Online, which is what
  // makes the firmware emit PowerAdjustEnd. Call from loop(), never from
  // inside a verdict callback.
  bool endAdjustment();

  // this function is called by Matter internal event processor. Cluster 47
  // drives the ember caches; clusters 144/145/152 are documented ignores
  // (Instance-served, S3.25: an injected +MTATTR naming one must not move
  // any cache).
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h): tells the +MTATTR dispatcher
  // what type each of the seven battery attributes is, so
  // attributeChangeCB() above receives the right union member populated.
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  // Hearth's own addition (MatterEndPoint.h): adjudicates
  // PowerAdjustRequest (152,0) and CancelPowerAdjustRequest (152,1);
  // everything else defers to the fail-closed base default.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  // clang-format off
  /* Wire constants. Cluster and attribute ids are PowerSource::Id and
   * PowerSource::Attributes::*::Id from the pinned connectedhomeip
   * checkout's generated headers (quoted in the header comment above);
   * the DEM cluster and command ids are AT_MT_SPEC.md S3.17's. Plain
   * integers, the library's usual pattern: there is no esp-matter or
   * connectedhomeip header on a host build. */
  static const uint32_t kPowerSourceClusterId   = 0x002F;  // 47, PowerSource
  static const uint32_t kBatVoltageAttrId       = 0x000B;  // uint32 mV, nullable
  static const uint32_t kBatPercentRemainingAttrId = 0x000C;  // uint8 half-percent, nullable
  static const uint32_t kBatTimeRemainingAttrId = 0x000D;  // uint32 s, nullable
  static const uint32_t kBatCapacityAttrId      = 0x0018;  // uint32 mAh, non-nullable, device default 0, served over the whole uint32 range (B263)
  static const uint32_t kBatChargeStateAttrId   = 0x001A;  // enum8, non-nullable, device default 0 (kUnknown)
  static const uint32_t kBatTimeToFullChargeAttrId = 0x001B;  // uint32 s, nullable
  static const uint32_t kBatChargingCurrentAttrId  = 0x001D;  // uint32 mA, nullable
  static const uint32_t kDemClusterId           = 0x0098;  // 152, DeviceEnergyManagement
  static const uint32_t kPowerAdjustRequestCmd  = 0;
  static const uint32_t kCancelPowerAdjustRequestCmd = 1;
  // clang-format on

  // Hearth's own hook (MatterEndPoint.h), on every reconcile: the DEM
  // helper's split, the measurement helper's B229, and the ember
  // attributes' own config/sampled split (the header comment lists all
  // seven with the reason each lands where it does).
  void hearthOnReconciled() override;

  // the shared ember write shape (updateAttributeVal = AT+MTATTR mode 1),
  // cache on success only; `hasFlag` is null for the two attributes whose
  // cache seeds from a real device default instead (see the header
  // comment's two seed rules)
  bool hearthWriteBatteryAttr(uint32_t attribute_id, esp_matter_attr_val_t val, uint32_t value, uint32_t *cache, bool *hasFlag);

  bool started = false;
  Variant_t variantSel = FULL;

  // the ember attribute caches. The five nullable ones carry a has-flag so
  // a first push of 0 still writes; BatCapacity and BatChargeState seed
  // from the device's own creation defaults (both 0) and carry none.
  uint32_t batVoltage = 0;
  uint32_t batPercentRemaining = 0;
  uint32_t batTimeRemaining = 0;
  uint32_t batTimeToFullCharge = 0;
  uint32_t batChargingCurrent = 0;
  uint32_t batCapacity = 0;
  uint32_t batChargeState = 0;
  bool hasBatVoltage = false;
  bool hasBatPercentRemaining = false;
  bool hasBatTimeRemaining = false;
  bool hasBatTimeToFullCharge = false;
  bool hasBatChargingCurrent = false;

  // "the sketch set this", for the two CONFIG attributes' reconcile
  // re-push only (the header comment's split). Deliberately separate from
  // the has-flags above, which model the fabric's null-until-pushed state
  // and decide the no-op: these two seed from real device defaults, so
  // their no-op check stays a plain cache comparison and a first
  // setBatCapacity(0) at boot is still a true no-op. Set by a successful
  // setter call, never by an incoming +MTATTR URC.
  bool wroteBatCapacity = false;
  bool wroteBatChargeState = false;

  // the two shared surfaces; begin() resets both and sets each `enabled`
  // explicitly from the validated variant
  HearthMeasurementPush meas;
  HearthDemControl dem;
};

/*
 * HearthDemControl.h - Task 5 (energy round C1, design spec 4.1): the
 * shared DeviceEnergyManagement (0x0098, cluster 152) surface helper. Built
 * fresh rather than extracted (there is no pre-existing DEM class to pull
 * it out of), but deliberately shaped after HearthMeasurementPush.h's
 * pattern: owned as a member by whichever endpoint class carries the DEM
 * cluster (MatterBatteryStorage on its FULL variant, MatterDeviceEnergyManagement
 * on both variants, Task 6), an `enabled` flag the owner assigns from its
 * variant at begin(), an onReconciled() hook the owner's own
 * hearthOnReconciled() calls, and a reset() the owner's begin() calls before
 * assigning `enabled`.
 *
 * Division of labour with the owner, the same split HearthMeasurementPush.h
 * documents:
 *
 * - The owner keeps its own `started` guard IN FRONT of every delegation:
 *   a not-yet-begun endpoint returns false with no Hearth error and no wire
 *   traffic. This class never checks it.
 * - `enabled` gates EVERY wire-touching method uniformly (unlike
 *   HearthMeasurementPush's `enabled`, which gates only the energy adders):
 *   false refuses host-side with Hearth error 1 and zero wire traffic for
 *   every setter, the capability replace, the energy-use push and
 *   endAdjustment(). The owner sets it from its variant at begin(); a
 *   variant that carries no DEM cluster at all (MatterBatteryStorage's
 *   NO_DEM) sets it false wholesale. A variant whose DEM cluster IS present
 *   but lacks the PowerAdjustment feature (MatterDeviceEnergyManagement's
 *   REPORT_ONLY, design spec 4.2: "the state/identity pushes work in both
 *   variants") leaves this helper's `enabled` TRUE and instead refuses only
 *   its OWN capability-setter call site itself, the
 *   MatterWaterHeater::hearthRefusedOnMinimal() pattern layered on top of,
 *   not instead of, this flag: that per-variant nuance is Task 6's, not
 *   this helper's, since this helper cannot see which variant its owner is.
 * - reset() is the owner's begin()-time re-initialisation (every cache and
 *   has-flag back to the fresh state, the registered callbacks left alone:
 *   no existing endpoint class clears a callback registration on begin(),
 *   see MatterWaterHeater::begin()). It does not touch `enabled`: the owner
 *   assigns that right after, from the variant it validated.
 * - onReconciled() is the reconcile split (design spec 4.1's own words,
 *   below).
 *
 * THE WIRE SURFACE (AT_MT_SPEC.md S3.25's 0x0098 table, S3.26, S3.17):
 * fields 0-5 push through "AT+MTMEAS=<ep>,152,<field>,<value>", one pair per
 * call, the WaterHeaterManagement/hearthSendWhmPair shape; the capability
 * list replaces wholesale through "AT+MTDEMCAP=<ep>,<cause>,<n>[,entry]{n}"
 * (S3.26, its own command family, not AT+MTMEAS's field=value grammar); the
 * two PowerAdjust command forwards arrive over +MTCMD on cluster 152,
 * commands 0 and 1 (S3.17), and this class exposes the two entry points
 * (hearthOnPowerAdjustRequest()/hearthOnCancelPowerAdjustRequest()) an
 * owner's own hearthOnForwardedCommandFields() override calls after parsing
 * the raw HearthCmdFields itself -- this class touches HearthCmdFields
 * nowhere, the same reason HearthMeasurementPush touches +MTCMD nowhere:
 * command dispatch is the owner's job (every existing endpoint type
 * hand-rolls its own cluster_id/command_id if-chain, MatterWaterHeater's
 * Boost/CancelBoost being the closest precedent for a same-cluster
 * payload/payload-less pair), this helper only owns what happens once a
 * verdict is needed.
 *
 * FIELD 0-5 NULL-UNTIL-PUSHED DISCIPLINE (the electrical/WHM shape): a
 * per-field has-flag gates a no-op (has-flag set AND cache equal to the new
 * value is the ONLY no-op condition, so the zero-initialised cache never
 * suppresses a first push of 0), and the cache commits only on a
 * successful wire write. Field 6 (AdjustmentEnergyUse,
 * pushAdjustmentEnergyUse()) is the one deliberate exception: it is an
 * EVENT CARRIER, not an attribute (S3.25: "reads back through nothing...
 * and is consumed (reset to 0) by each PowerAdjustEnd emission"), so it
 * keeps no cache at all and ALWAYS writes, the fire-and-forget shape a
 * repeat-suppressing no-op would silently break (two adjustments in a row
 * reporting the identical energy figure must both reach the wire).
 *
 * ACCEPT PUSHES NO STATE CHANGE; endAdjustment() DOES (design spec 4.1,
 * the round's headline DEM semantic, contrast Round B's Boost): on an
 * accepted PowerAdjustRequest the FIRMWARE sets ESAState PowerAdjustActive
 * itself and emits the fieldless PowerAdjustStart (S3.17); this class must
 * NOT push a state change from hearthOnPowerAdjustRequest(), so an accept
 * answers AT+MTCMDRESP alone, no follow-up AT+MTMEAS. It does, however,
 * update its OWN ESAState cache to PowerAdjustActive without any wire
 * traffic, mirroring what the firmware just did silently: without this,
 * the null-until-pushed no-op check on a LATER endAdjustment() call would
 * compare against a stale "Online" belief (whatever the host last
 * explicitly pushed, or never-pushed) and could wrongly no-op instead of
 * sending the Online push that is the whole point of endAdjustment() (S3.17:
 * "it pushes ESAState back to Online, which is what makes the firmware emit
 * PowerAdjustEnd"). The same applies to an accepted
 * CancelPowerAdjustRequest, which the firmware resolves by resetting
 * ESAState to Online itself (S3.17): the cache is updated to Online
 * silently there too, so a subsequent explicit setESAState(Online) call
 * correctly no-ops against a device state it now honestly agrees with.
 * `endAdjustment()` is a pure `setESAState(<Online>)` shorthand: the
 * request/cancel handlers are what make the FOLLOWING call to it (or to
 * setESAState directly) reach the wire honestly, not a special case
 * inside endAdjustment() itself.
 *
 * THE RECONCILE SPLIT (design spec 4.1, both halves test-pinned):
 * ESAType, ESACanGenerate, AbsMinPower, AbsMaxPower and the
 * PowerAdjustmentCapability list are CONFIGURATION the C6 does not persist
 * and are RE-pushed on every reconcile (the cabinet-labels precedent,
 * MatterWaterHeater's HeaterTypes/TankVolume being the closest AT+MTMEAS
 * analogue). ESAState, OptOutState and the AdjustmentEnergyUse carrier are
 * VOLATILE: the wire-pushed memory (has-flags) is cleared so a repeated
 * value reaches the wire again, but the values are NOT re-sent (the B229
 * rule -- field 6 keeps no cache to begin with, so there is nothing for it
 * to clear or resend either way, and it never appears in the reconcile
 * re-push list).
 */
#pragma once

#include <stdint.h>

class MatterEndPoint;

class HearthDemControl {
public:
  // AT+MTDEMCAP entry (AT_MT_SPEC.md S3.26): a PowerAdjustStruct. Powers are
  // the full 64-bit pipeline (mW, minus accepted, the "5 kW above what 32
  // bits hold" S3.25 example 4 vector); durations are uint32 seconds, the
  // wire's own width.
  struct PowerAdjustEntry {
    int64_t minPowerMw, maxPowerMw;
    uint32_t minDurationS, maxDurationS;
  };

  // Stores the pointer only; the owner passes `this` from its own
  // constructor's init list, and the endpoint id is read per wire call (it
  // is 0 until the owner's declaration reconciles), the
  // HearthMeasurementPush shape.
  explicit HearthDemControl(MatterEndPoint *owner);

  // Fields 0-5, AT_MT_SPEC.md S3.25's 0x0098 table (field ids mirrored from
  // the firmware's main/include/mt_matter.h MT_DEM_F_* below). Each is one
  // "AT+MTMEAS=<ep>,152,<field>,<value>" line, null-until-pushed discipline
  // (this header's own comment above).
  bool setESAType(uint8_t t);
  bool setESACanGenerate(bool g);
  bool setESAState(uint8_t s);  // push; leaving PowerAdjustActive fires PowerAdjustEnd (firmware-derived, S3.25)
  bool setAbsMinPower(int64_t mw);
  bool setAbsMaxPower(int64_t mw);
  bool setOptOutState(uint8_t s);

  // Field 6: event carrier, not an attribute. ALWAYS writes (no cache, no
  // no-op, this header's own comment above); the value it carries is
  // consumed by the next PowerAdjustEnd the firmware emits.
  bool pushAdjustmentEnergyUse(int64_t mwh);

  // AT+MTDEMCAP (S3.26): full replacement of the PowerAdjustmentCapability
  // struct list, `<cause>` the resting PowerAdjustReasonEnum, `n` 0-4
  // entries (`entries` may be nullptr when n is 0). n > 4 is refused
  // HOST-SIDE with Hearth error 1 and zero wire traffic: the wire's own
  // bound (MT_DEM_CAP_MAX_ENTRIES below) is already known without spending
  // the round trip, the POWER_ONLY precedent. Always writes on a legal n
  // (S3.26: "Set-only, full replacement per call", not a null-until-pushed
  // field); the cache this class keeps is for the reconcile re-push only,
  // never for a no-op comparison.
  bool setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n);

  // Registered callbacks: PLAIN function pointers per the design spec's
  // class block verbatim, not the std::function every other adjudicated
  // command in this library uses (MatterWaterHeater's onBoost/
  // onCancelBoost, MatterDoorLock's onLock/onUnlock) -- a deliberate
  // departure this task follows exactly rather than silently
  // "regularising" to the existing convention. No callback registered
  // denies by default, the library-wide fail-closed shape.
  void onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause));
  void onCancelPowerAdjust(bool (*cb)());

  // The owner's own hearthOnForwardedCommandFields() override calls these
  // after parsing cluster 152's raw HearthCmdFields itself (this class
  // never sees HearthCmdFields, this header's own comment above): the
  // owner already knows which command matched by the time either is
  // called. Returns the verdict the owner's override should itself return
  // (the dispatcher, not this class or the owner, sends AT+MTCMDRESP).
  bool hearthOnPowerAdjustRequest(int64_t powerMw, uint32_t durationS, uint8_t cause);
  bool hearthOnCancelPowerAdjustRequest();

  // setESAState(<Online>) shorthand, the normal-completion path (this
  // header's own comment above).
  bool endAdjustment();

  // B229 + cabinet-labels split (this header's own comment above): clears
  // the volatile has-flags (ESAState, OptOutState) WITHOUT resending them,
  // and RE-pushes the configuration fields (ESAType, ESACanGenerate,
  // AbsMinPower, AbsMaxPower, the capability list) that were ever
  // successfully set. Called from the owner's hearthOnReconciled().
  void onReconciled();

  // begin()-time re-initialisation: every cache and has-flag back to the
  // fresh state. Leaves `enabled` and the registered callbacks alone (the
  // owner assigns `enabled` right after, from the variant it validated; no
  // existing endpoint class clears a callback on begin() either).
  void reset();

  // false = every wire-touching method refuses with hearthSetError(1) and
  // zero wire traffic (this header's own comment above on the departure
  // from HearthMeasurementPush's narrower gate). Set by the owner from its
  // variant at begin().
  bool enabled = true;

private:
  // clang-format off
  /* Wire constants (AT_MT_SPEC.md S3.25/S3.26), mirrored from the
   * firmware's main/include/mt_matter.h MT_DEM_F_* / MT_DEM_CAP_MAX_ENTRIES
   * (read from that source, main/include/mt_matter.h lines 958-964 and 975,
   * not transcribed from memory). */
  static const uint32_t kDemClusterId        = 0x0098;  // 152, DeviceEnergyManagement
  static const uint8_t  kFieldESAType        = 0;  // MT_DEM_F_ESA_TYPE, enum8 unsigned
  static const uint8_t  kFieldESACanGenerate = 1;  // MT_DEM_F_ESA_CAN_GEN, bool unsigned
  static const uint8_t  kFieldESAState       = 2;  // MT_DEM_F_ESA_STATE, enum8 unsigned
  static const uint8_t  kFieldAbsMinPower    = 3;  // MT_DEM_F_ABS_MIN_POWER, int64 mW signed
  static const uint8_t  kFieldAbsMaxPower    = 4;  // MT_DEM_F_ABS_MAX_POWER, int64 mW signed
  static const uint8_t  kFieldOptOutState    = 5;  // MT_DEM_F_OPT_OUT_STATE, enum8 unsigned
  static const uint8_t  kFieldAdjEnergyUse   = 6;  // MT_DEM_F_ADJ_ENERGY_USE, int64 mWh signed, event carrier
  static const uint8_t  kCapMaxEntries       = 4;  // MT_DEM_CAP_MAX_ENTRIES, the AT+MTDEMCAP n<=4 bound
  // ESAStateEnum (AT_MT_SPEC.md S3.25: "0..4, Offline, Online, Fault,
  // PowerAdjustActive, Paused"); the firmware defines no named macros for
  // these, only the field ids above, so these two are cited to the spec
  // text, not to mt_matter.h.
  static const uint8_t  kESAStateOnline         = 1;
  static const uint8_t  kESAStatePowerAdjustActive = 3;
  // clang-format on

  // wire-only writes; the caller decides whether/what to commit to the
  // cache (house discipline: a failed write must not update it).
  bool hearthSendDemPair(uint8_t field, int64_t value);
  bool hearthSendCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n);
  bool hearthSetDemField(uint8_t field, int64_t value, int64_t *cache, bool *hasFlag);

  MatterEndPoint *owner;

  // fields 0-5's last-pushed cache; the has-flags model the fabric's
  // null-until-pushed state.
  uint8_t esaType = 0;
  bool esaCanGenerate = false;
  uint8_t esaState = 0;
  int64_t absMinPower = 0;
  int64_t absMaxPower = 0;
  uint8_t optOutState = 0;
  bool hasESAType = false;
  bool hasESACanGenerate = false;
  bool hasESAState = false;
  bool hasAbsMinPower = false;
  bool hasAbsMaxPower = false;
  bool hasOptOutState = false;

  // the capability list cache: kept for the reconcile re-push only (S3.26
  // has no no-op discipline of its own, every legal call writes), so
  // `hasCapability` distinguishes "never set" from "explicitly set to
  // null" (n=0 is itself a real, re-pushable configuration, S3.26: "0 means
  // the capability reads null").
  bool hasCapability = false;
  uint8_t capCause = 0;
  uint8_t capCount = 0;
  PowerAdjustEntry capEntries[kCapMaxEntries];

  bool (*_onPowerAdjustCB)(int64_t powerMw, uint32_t durationS, uint8_t cause) = nullptr;
  bool (*_onCancelPowerAdjustCB)() = nullptr;
};

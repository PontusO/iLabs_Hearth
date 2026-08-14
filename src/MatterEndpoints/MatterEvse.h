/*
 * MatterEvse.h - Task 11 (energy round C2): the Energy EVSE endpoint, device
 * type 0x050C. A Hearth original: arduino-esp32's Matter library ships no
 * EVSE class at all (see Hearth.h's umbrella comment), so the public surface
 * below is this port's own design against the firmware's wire contract
 * (main/mt_evse.cpp, main/mt_at.c's cmd_mtrow* family, main/include/
 * mt_matter.h's MT_EVSE_F_* table) and the round's design spec sections 2
 * and 5.5. Every wire fact cited here was verified directly against the
 * firmware source, not transcribed from a document. `docs/AT_MT_SPEC.md`
 * now describes EnergyEvse extensively (Task 14 shipped it: the AT+MTROW
 * family, the AT+MTMEAS 0x0099 field table, the +MTCMD forward shapes), so
 * it is a second, authoritative source to check against, not the absence
 * this comment used to report.
 *
 * THIS IS THE ROUND'S INTEGRATION POINT: a controller's charging schedule
 * reaches a sketch, and the sketch's verdict reaches the fabric, through the
 * machinery below.
 *
 * VARIANTS (mt_devtypes.cpp / task 6-7's own bench notes): FULL (0) builds
 * the SOC feature (StateOfCharge, BatteryCapacity attributes present; every
 * charging target MUST carry a target state of charge, enforced both by
 * CHIP's own ValidateTargets() on the fabric path and, host-side, by
 * setChargingSchedule() below before any wire traffic). NO_SOC (1) omits the
 * SOC feature; a target's SoC must then be absent or exactly 100 (the XML's
 * "charge it fully" spelling for a device that cannot report state of
 * charge).
 *
 * WHICH LAYER GUARDS WHICH PATH (corrected by round C2's final review; the
 * previous wording had it backwards). There are three checks, and they do
 * not overlap the way the old text implied:
 *
 *   - CHIP's own Instance::ValidateTargets() guards the FABRIC path. It
 *     runs inside the SDK, before HearthEvseDelegate::SetTargets() is ever
 *     called, so a controller's malformed schedule never reaches this
 *     firmware's code at all.
 *   - mt_evse_targets_apply_locked()'s pass 1 (mt_evse.cpp, the
 *     `soc_feature` check) exists because NOTHING guarded the AT path.
 *     Without it a host could install over AT exactly the schedule a
 *     controller's SetTargets would have been refused, and GetTargets would
 *     then hand that schedule back to the controller. That function's own
 *     comment says so in as many words. It reads the endpoint's own
 *     metadata (StateOfCharge present or not) rather than a stored variant,
 *     which is the same fact ValidateTargets reads from the Instance's
 *     FeatureMap snapshot, so the two can never disagree.
 *   - This class's host-side check in setChargingSchedule() is neither of
 *     those: it is a convenience, so a sketch discovers a malformed
 *     schedule at the call that built it rather than as a wire refusal
 *     three commands later.
 *
 * THE COMMAND SURFACE, verified against main/mt_evse.cpp's HearthEvseDelegate
 * (cluster 0x0099 = 153 decimal):
 *
 * - Disable (command 1) and EnableCharging (command 2) forward as ORDINARY
 *   SCALAR `+MTCMD` commands on the unchanged 1000 ms verdict window,
 *   MatterWaterHeater's onBoost/onCancelBoost shape: the callback returns a
 *   verdict synchronously and hearthOnForwardedCommandFieldsSeq() hands it
 *   straight back. Neither accept writes an EVSE attribute of its own; the
 *   firmware deliberately leaves SupplyState/State/ChargingEnabledUntil to
 *   the host's own AT+MTMEAS pushes (mt_evse.cpp's own comment: "the host is
 *   the single authority on what the hardware actually did"), so a sketch's
 *   onDisableCharging()/onEnableCharging() accept must be followed by its
 *   own setSupplyState() call from ordinary loop() context, not from inside
 *   the callback (a wire write there would be refused HEARTH_CMD_REENTRANT
 *   the same way every other verdict callback in this library is).
 *   EnableCharging's tail is "<chargingEnabledUntil|empty>,<min>,<max>": the
 *   first field is nullable and renders as an EMPTY WIRE TOKEN when null
 *   (mt_evse.cpp's EnableCharging() override, not a presence mask -- there
 *   are no booleans here, so the mask machinery Boost needed has nothing to
 *   carry).
 *
 * - SetTargets (command 5) is the adjudicated, ROW-BEARING forward that
 *   makes this class different from every verdict callback that came before
 *   it in this library. The wire line is
 *   "+MTCMD:<seq>,<ep>,153,5,<rowcount>,<daymask>" (mt_evse.cpp's SetTargets
 *   override, fix round 1: the affected-day mask is a SECOND tail field
 *   because a controller can clear a day by sending it with an EMPTY target
 *   list, which produces NO rows at all, so <rowcount> alone cannot tell a
 *   host that day is even in play). The verdict CANNOT be produced from the
 *   four fields on the wire line: the host must first PULL the proposed
 *   rows with `AT+MTROWGET=<ep>,1,,<seq>` (HearthRowTransfer::
 *   getAllProposed(), the seq-qualified proposal read), and a wire command
 *   from inside a `+MTCMD` dispatch is refused HEARTH_CMD_REENTRANT.
 *
 *   So this class defers the whole thing. hearthOnForwardedCommandFieldsSeq()
 *   records (seq, dayMask), calls Hearth.hearthDeferCurrentCmdResp() (Task
 *   11's own addition to the shared dispatcher, MatterEndPoint.h/Hearth.h)
 *   so no immediate AT+MTCMDRESP is sent, and calls
 *   Hearth.hearthRequestDeferredWork() to arm hearthOnDeferredWork(), which
 *   runs once the busy gate the +MTCMD arrived under has been released --
 *   the WaterHeater Boost-push precedent, one step earlier in the sequence:
 *   there the deferral follows the verdict, here it REPLACES it, because the
 *   verdict itself cannot exist yet.
 *
 *   hearthOnDeferredWork() fetches the proposed rows, builds a
 *   HearthChargingSchedule from them, calls the sketch's onSetTargets()
 *   handler, and sends `AT+MTCMDRESP=<seq>,<0|1>` itself -- an ordinary wire
 *   write, safe here because the busy gate is down (this is exactly the
 *   WaterHeater BoostState-push precedent's own reasoning, reused for the
 *   verdict itself rather than a push that follows it).
 *
 * THE SLOW-LOOP CASE, stated because the brief asked for it explicitly:
 * hearthOnDeferredWork() runs on the sketch's OWN next call into the
 * library (any Hearth.poll() or Hearth.hearthCommand()), not on a timer of
 * this library's own. The firmware's verdict window for a row-bearing
 * forward is 3000 ms (mt_evse.cpp / task 6's report; the scalar window
 * above is 1000 ms). If a sketch's loop() takes long enough that this
 * host's own deferred fetch does not run until AFTER the firmware's 3000 ms
 * has already elapsed, the firmware has ALREADY default-denied the request
 * on its own and raised `+MTCMDTO:<seq>` (which this library does not
 * currently correlate back to a specific pending seq -- Hearth.cpp's
 * hearthDispatchCmdTimeout() ignores its own <seq> argument today). The
 * fetch this class then attempts, `AT+MTROWGET=<ep>,1,,<seq>` against a seq
 * the firmware no longer recognises, answers `+MTERR:1` (HearthRowTransfer.h:
 * "a stale or invented seq answers +MTERR:1"). This class treats that
 * outcome as final: it does NOT call onSetTargets() (there is no trustworthy
 * proposal to show it) and does NOT send any AT+MTCMDRESP (the firmware has
 * already resolved this seq on its own, and answering a resolved seq would
 * only earn a harmless-but-pointless `+MTERR:1` of its own). A sketch whose
 * loop() cannot reliably run within the 3 second budget will see its
 * onSetTargets() handler simply never fire for that particular proposal,
 * with the controller observing FAILURE -- the same outcome as an explicit
 * deny, just decided by the firmware's clock instead of the sketch's.
 *
 * A SECOND SetTargets ARRIVING WHILE ONE IS ALREADY PENDING HOST-SIDE cannot
 * happen through the firmware's own serialization (mt_evse.cpp only ever
 * has one command forward open at a time), but this class refuses one
 * defensively anyway (deny, no state change) rather than clobbering the
 * pending (seq, dayMask) of the first.
 *
 * THE ROW <-> HearthChargingSchedule TRANSLATION uses MT_ROW_KIND_EVSE_TARGET
 * (kind 1)'s field order verified against main/mt_rows.c's
 * s_evse_target_fields[] table and main/mt_evse.cpp's kRowDay/kRowTime/
 * kRowSoC/kRowEnergy constants: index 0 day bitmap, 1 minutes past midnight,
 * 2 target SoC (optional), 3 added energy mWh (optional). One row maps to
 * exactly one HearthChargingSchedule::addTarget(dayBitmap, target) call in
 * either direction; no grouping step is needed; a day GROUP is simply every
 * row sharing the same bitmap value, exactly how HearthChargingSchedule
 * already models it.
 *
 * THE MERGE-BY-DAY CACHE (design spec, mt_evse.cpp's own file comment: "an
 * apply MERGES BY DAY... days present in the applied set replace those days
 * ENTIRELY, days absent are UNCHANGED"). Rather than re-reading the live
 * store after every accepted write (a second wire round trip with a real,
 * if probably small, race against the firmware's own post-verdict merge),
 * this class reproduces the firmware's own subtract_days()-then-append
 * algorithm host-side in hearthMergeByDay(): narrow every EXISTING cached
 * entry's day bitmap by clearing the bits the new dayMask claims (dropping
 * the entry outright if nothing is left), then append every entry of the
 * new/incoming set verbatim. This is deterministic and race-free because it
 * needs no fact the firmware has not already handed this host: the OLD
 * cache (this class's own prior state) and the NEW rows (just fetched or
 * just staged). Used by both write paths: setChargingSchedule() (the
 * upward, host-authored path, dayMask = the union of every dayBitmap in the
 * schedule just pushed) and an ALLOWED SetTargets (the controller-authored
 * path, dayMask = the wire's own second tail field, which is what makes an
 * emptied day -- rowcount 0, dayMask non-zero -- visible to the merge at
 * all: the incoming set contributes nothing for that day, so the day is
 * narrowed away and never re-appended, exactly mirroring the firmware's own
 * "day emptied" outcome).
 *
 * INSTANCE-OWNED, NEVER RAISES +MTATTR (C1's rule, restated here because
 * cluster 0x0099 is delegate-served for every one of its 19 attributes,
 * task 7's report): the host cache updates on successful PUSHES only for
 * the three scalar setters, and the schedule cache updates only when an
 * adjudicated SetTargets is ALLOWED, never on the bare proposal -- a denied
 * or timed-out proposal leaves chargingSchedule() untouched, by construction
 * (hearthMergeByDay() is only ever called on the allow path).
 *
 * THE B229 PATTERN, hearthOnReconciled(): CircuitCapacity is CONFIGURATION
 * (an installation fact -- the circuit this EVSE is wired to -- that does
 * not change with the weather) and is re-pushed if it was ever set.
 * SupplyState and FaultState are VOLATILE live status and follow B229:
 * their has-flags are cleared WITHOUT a resend, so a post-reboot setter
 * pushing the identical value again is not silently swallowed by a cache
 * the device no longer agrees with. The charging schedule is neither: it
 * lives in the FIRMWARE's own NVS (main/mt_evse.cpp's file comment: "the
 * schedule ... is USER DATA: it survives AT+MTRESET"), so this host's own
 * cache is rebuilt from the LIVE store (HearthRowTransfer::getAll(), the
 * unqualified read) on every reconcile instead of assumed empty or carried
 * over -- the only way chargingSchedule() can ever be accurate the first
 * time a sketch starts against a C6 that already has a schedule from a
 * previous session or a controller's own prior SetTargets.
 *
 * THE ROW BUFFER is a class MEMBER, not a stack array: HearthRowTransfer.h's
 * own Task 10 guidance is to size a bulk read's buffer at the kind's known
 * ceiling (HearthChargingSchedule::kMaxEntries, 70) so `returned` can never
 * fall short of `total`, and 70 Row structures is on the order of five
 * kilobytes -- exactly the stack-budget concern main/mt_evse.cpp's own file
 * comment raises about the firmware's mirror-image problem ("no target
 * storage is built here... the merge writes straight into the member store
 * instead"). One buffer serves both the reconcile-time live sync and the
 * deferred proposal fetch; the two never run concurrently (nothing in
 * Arduino's cooperative loop() model preempts either), so a shared buffer
 * costs no more than one member array while halving what a naive
 * implementation would carry permanently.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "HearthRowTransfer.h"
#include "HearthChargingSchedule.h"

class MatterEvse : public MatterEndPoint {
public:
  // The 0x050C variant byte: FULL builds the SOC feature (target SoC
  // mandatory on every charging target), NO_SOC omits it (target SoC must
  // be absent or exactly 100). See the header comment.
  enum Variant_t {
    FULL = 0,
    NO_SOC = 1
  };

  // EnableCharging's unpacked payload (mt_evse.cpp's EnableCharging()
  // override): chargingEnabledUntil is nullable and travels as an empty
  // wire token when absent, hasChargingEnabledUntil records which.
  struct EnableChargingInfo {
    bool hasChargingEnabledUntil;
    uint32_t chargingEnabledUntil;
    int64_t minimumChargeCurrent;
    int64_t maximumChargeCurrent;
  };

  MatterEvse();
  ~MatterEvse();

  // declares the endpoint only (device type 0x050C plus the variant byte)
  // and resets every cache. No wire traffic.
  bool begin(Variant_t variant = FULL);
  // this will stop processing EnergyEvse Matter events
  void end();

  // The three scalar setters over AT+MTMEAS cluster 0x0099 (MT_EVSE_F_1,
  // MT_EVSE_F_2, MT_EVSE_F_4 in main/include/mt_matter.h): null-until-pushed
  // has-flag discipline, cache on success only, no-op when unchanged and
  // already pushed once (the MatterWaterHeater hearthSetWhmField() shape).
  // Enum/range checks are the firmware's, answered +MTERR:1.
  bool setSupplyState(uint8_t state);
  bool setFaultState(uint8_t state);
  bool setCircuitCapacity(int64_t milliamps);

  // The upward path: replace the days named in `schedule` on the device's
  // charging-target store (AT+MTROW/AT+MTROWAPPLY), merging by day (see the
  // header comment). `schedule.count() == 0` is the documented clear-
  // everything request (AT+MTROWAPPLY's own count-0 verb): it empties the
  // WHOLE stored payload, not merely the days named (there are none named).
  // Host-side variant enforcement runs BEFORE any wire traffic: on FULL
  // every target must carry a target SoC, on NO_SOC every target's SoC
  // must be absent or exactly 100; a violation answers Hearth error 1 with
  // zero wire traffic rather than a wire refusal three commands later.
  bool setChargingSchedule(const HearthChargingSchedule &schedule);

  // The cached view: the live store as last known to this host, updated on
  // a successful setChargingSchedule(), an ALLOWED SetTargets, and every
  // reconcile (resynced from the device, since the schedule survives
  // AT+MTRESET and this host may not be the only author of it).
  const HearthChargingSchedule &chargingSchedule() const {
    return _schedule;
  }

  // Register the host's verdict for a controller-invoked SetTargets
  // (cluster 0x0099, command 5). The handler receives the PROPOSED
  // schedule (never applied yet) AND the affected-day mask, and returns
  // true to allow. No callback registered denies by default (fail
  // closed), the library's usual precedent. See the header comment for
  // the full deferred-fetch sequence and the slow-loop case.
  //
  // WHY THE MASK IS A SECOND ARGUMENT, not derivable from `proposed`
  // (round C2 final review; this is a PUBLIC signature, so it had to be
  // right before the tag rather than after it). The firmware derives the
  // mask from the controller's schedule ENTRIES, not from its rows,
  // precisely so a host can see a day being EMPTIED: a controller clears
  // a day by sending it with an empty chargingTargets list, which
  // produces no rows at all. Two shapes are indistinguishable without
  // the mask:
  //
  //   - Monday-with-no-targets plus Tuesday-with-one-target arrives as
  //     ONE Tuesday row and mask 6. Given the schedule alone, a sketch
  //     sees a Tuesday row and no indication whatsoever that Monday is
  //     about to be deleted.
  //   - Rowcount 0 with a NON-ZERO mask (clear these days) versus
  //     rowcount 0 with a ZERO mask (the fabric-only empty-SetTargets
  //     no-op) both arrive as an empty HearthChargingSchedule.
  //
  // AT_MT_SPEC.md states this outright: it is why the mask exists at
  // all. The mask is the same value hearthMergeByDay() uses on an allow,
  // so a sketch that wants to predict the post-merge store can apply the
  // identical rule: narrow every cached entry by `~mask`, then append
  // `proposed`. A `mask` of 0 means nothing is being replaced.
  void onSetTargets(bool (*handler)(const HearthChargingSchedule &,
                                    uint8_t affectedDayMask));

  // Register the host's verdict for a controller-invoked Disable /
  // EnableCharging (cluster 0x0099, commands 1 and 2). Ordinary scalar
  // forwards, 1000 ms window, synchronous verdicts. Neither accept writes
  // an EVSE attribute on its own; push setSupplyState()/setFaultState()
  // afterwards, from ordinary loop() context, never from inside the
  // callback (see the header comment).
  void onDisableCharging(bool (*cb)());
  void onEnableCharging(bool (*cb)(const EnableChargingInfo &info));

  // this function is called by Matter internal event processor. Cluster
  // 0x0099 is Instance-served (every attribute delegate-owned): no
  // +MTATTR URC is ever raised for it, and an injected one must move
  // nothing, the MatterDeviceEnergyManagement shape.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h, Task 11): adjudicates Disable
  // (153,1) and EnableCharging (153,2) synchronously, and DEFERS SetTargets
  // (153,5) via Hearth.hearthDeferCurrentCmdResp() (see the header
  // comment); everything else defers to the fail-closed base default.
  bool hearthOnForwardedCommandFieldsSeq(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields, uint32_t seq) override;

protected:
  // clang-format off
  /* Wire constants, verified against main/mt_evse.cpp and main/include/
   * mt_matter.h's MT_EVSE_F_* table, not transcribed from the design spec
   * (task 7's report found the spec's own field count wrong). Plain
   * integers, the library's usual pattern. */
  static const uint32_t kEvseClusterId          = 0x0099;  // 153, EnergyEvse
  static const uint32_t kDisableCommandId       = 1;
  static const uint32_t kEnableChargingCommandId = 2;
  static const uint32_t kSetTargetsCommandId    = 5;
  static const uint8_t  kFieldSupplyState       = 1;  // MT_EVSE_F_SUPPLY_STATE, enum8 0..5
  static const uint8_t  kFieldFaultState        = 2;  // MT_EVSE_F_FAULT_STATE, enum8 0..15
  static const uint8_t  kFieldCircuitCapacity   = 4;  // MT_EVSE_F_CIRCUIT_CAPACITY, int64 mA, min 0
  // MT_ROW_KIND_EVSE_TARGET (main/include/mt_rows.h) and its field order
  // (main/mt_rows.c's s_evse_target_fields[]).
  static const uint8_t  kRowKindEvseTarget      = 1;
  static const uint8_t  kRowFields              = 4;
  static const uint8_t  kRowDay                 = 0;
  static const uint8_t  kRowTime                = 1;
  static const uint8_t  kRowSoc                 = 2;
  static const uint8_t  kRowEnergy              = 3;
  // clang-format on

  // Hearth's own hook (MatterEndPoint.h): re-pushes CircuitCapacity if set,
  // clears SupplyState/FaultState's has-flags without a resend (B229), and
  // resyncs the charging schedule cache from the live store. See the
  // header comment.
  void hearthOnReconciled() override;

  // Hearth's own hook (MatterEndPoint.h, Task 6, energy round B; Task 11's
  // consumer): runs the deferred SetTargets sequence once the busy gate the
  // +MTCMD arrived under has released. See the header comment.
  void hearthOnDeferredWork() override;

  bool started = false;
  Variant_t variantSel = FULL;

  bool hasSupplyState = false;
  uint8_t supplyState = 0;
  bool hasFaultState = false;
  uint8_t faultState = 0;
  bool hasCircuitCapacity = false;
  int64_t circuitCapacity = 0;

  // "AT+MTMEAS=<ep>,153,<field>,<value>" (task 7's dispatch), wire-only;
  // the caller commits the cache (house discipline: a failed write must not
  // update it).
  bool hearthSendEvsePair(uint8_t field, int64_t value);

  HearthChargingSchedule _schedule;
  HearthRowTransfer rows;

  // the deferred SetTargets state (header comment's sequence); pendingSeq
  // is only meaningful while pendingSetTargets is true.
  bool pendingSetTargets = false;
  uint32_t pendingSeq = 0;
  uint8_t pendingDayMask = 0;

  bool (*_onSetTargetsCB)(const HearthChargingSchedule &, uint8_t) = nullptr;
  bool (*_onDisableChargingCB)() = nullptr;
  bool (*_onEnableChargingCB)(const EnableChargingInfo &) = nullptr;

  // the shared row buffer (header comment: a member, not a stack array),
  // used by both hearthOnReconciled()'s live resync and
  // hearthOnDeferredWork()'s proposal fetch.
  HearthRowTransfer::Row _rowBuf[HearthChargingSchedule::kMaxEntries];

  // one HearthRowTransfer::Row -> HearthChargingTarget, the kRowKindEvseTarget
  // field order (header comment); shared by the reconcile resync and the
  // deferred proposal fetch so the mapping exists in exactly one place.
  static HearthChargingTarget hearthTargetFromRow(const HearthRowTransfer::Row &row);

  // The merge-by-day algorithm (header comment): narrows every cached entry
  // whose bitmap overlaps dayMask, dropping it if nothing is left, then
  // appends every entry of `incoming` verbatim. Returns false (cache left
  // untouched) only if HearthChargingSchedule::addTarget() ever refuses a
  // reconstruction step, which the header comment argues cannot happen
  // given the invariants both callers already hold; kept as a defensive
  // guard rather than assumed infallible.
  bool hearthMergeByDay(uint8_t dayMask, const HearthChargingSchedule &incoming);
};

/*
 * MatterEvse.cpp - implementation. See the header for the design notes: the
 * variant SoC rule, the three command shapes, the deferred SetTargets
 * sequence and the slow-loop case, the row <-> schedule translation, the
 * merge-by-day cache algorithm and the B229 reconcile split.
 */
#include "MatterEndpoints/MatterEvse.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

namespace {
/* energy_evse (ESP_MATTER_ENERGY_EVSE_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:130,
 * "#define ESP_MATTER_ENERGY_EVSE_DEVICE_TYPE_ID 0x050C", 1292 decimal, the
 * value task 6/7's bench notes both stage against). Given as a plain
 * integer: there is no esp-matter header on a host build. */
const uint32_t kEvseDeviceType = 0x050C;
}  // namespace

MatterEvse::MatterEvse() : rows(this, kRowKindEvseTarget, kRowFields) {}

MatterEvse::~MatterEvse() {
  end();
}

bool MatterEvse::begin(Variant_t variant) {
  /* An enum parameter does not stop a cast from smuggling in a third value,
   * and the variant byte travels the wire verbatim, so validate it
   * host-side, the electrical sensor's shape (also WaterHeater's,
   * MatterDeviceEnergyManagement's). */
  if (variant != FULL && variant != NO_SOC) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthDeclare(this, kEvseDeviceType, (uint8_t)variant)) {
    return false;
  }
  variantSel = variant;
  hasSupplyState = false;
  supplyState = 0;
  hasFaultState = false;
  faultState = 0;
  hasCircuitCapacity = false;
  circuitCapacity = 0;
  _schedule.clear();
  pendingSetTargets = false;
  pendingSeq = 0;
  pendingDayMask = 0;
  started = true;
  return true;
}

void MatterEvse::end() {
  started = false;
  pendingSetTargets = false;
}

/*
 * "AT+MTMEAS=<ep>,153,<field>,<value>" (task 7's dispatch wiring). Signed
 * rendering unconditionally: CircuitCapacity is the one signed field this
 * class pushes (int64 mA, XML min 0, kept signed for 64-bit pipeline
 * symmetry per task 7's report), and SupplyState/FaultState are small
 * non-negative enums that render identically either way.
 */
bool MatterEvse::hearthSendEvsePair(uint8_t field, int64_t value) {
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[64];
  snprintf(
    cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu,%u,%lld", (unsigned)getEndPointId(), (unsigned long)kEvseClusterId, (unsigned)field, (long long)value
  );
  return Hearth.hearthCommand(cmd) == 0;
}

bool MatterEvse::setSupplyState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (hasSupplyState && supplyState == state) {
    return true;
  }
  if (!hearthSendEvsePair(kFieldSupplyState, state)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  supplyState = state;
  hasSupplyState = true;
  return true;
}

bool MatterEvse::setFaultState(uint8_t state) {
  if (!started) {
    return false;
  }
  if (hasFaultState && faultState == state) {
    return true;
  }
  if (!hearthSendEvsePair(kFieldFaultState, state)) {
    return false;
  }
  faultState = state;
  hasFaultState = true;
  return true;
}

bool MatterEvse::setCircuitCapacity(int64_t milliamps) {
  if (!started) {
    return false;
  }
  if (hasCircuitCapacity && circuitCapacity == milliamps) {
    return true;
  }
  if (!hearthSendEvsePair(kFieldCircuitCapacity, milliamps)) {
    return false;
  }
  circuitCapacity = milliamps;
  hasCircuitCapacity = true;
  return true;
}

/*
 * One HearthRowTransfer::Row -> HearthChargingTarget, kRowKindEvseTarget's
 * field order (header comment). Shared by hearthOnReconciled()'s live
 * resync and hearthOnDeferredWork()'s proposal fetch.
 */
HearthChargingTarget MatterEvse::hearthTargetFromRow(const HearthRowTransfer::Row &row) {
  HearthChargingTarget t;
  t.minutesPastMidnight = (uint16_t)row.value[kRowTime];
  t.hasTargetSoC = row.present[kRowSoc];
  t.targetSoC = (uint8_t)row.value[kRowSoc];
  t.hasAddedEnergy = row.present[kRowEnergy];
  t.addedEnergy = row.value[kRowEnergy];
  return t;
}

/*
 * The merge-by-day algorithm (header comment): narrow every existing cached
 * entry by clearing the bits dayMask claims, dropping it if nothing is left,
 * then append every entry of `incoming` verbatim. Built into a fresh
 * HearthChargingSchedule and swapped in only if every step succeeds, so a
 * failure (should not happen, see the header comment) leaves the live cache
 * exactly as it was rather than half-rewritten.
 */
bool MatterEvse::hearthMergeByDay(uint8_t dayMask, const HearthChargingSchedule &incoming) {
  HearthChargingSchedule merged;
  for (uint8_t i = 0; i < _schedule.count(); i++) {
    uint8_t bits = _schedule.dayBitmapAt(i);
    uint8_t narrowed = (uint8_t)(bits & (uint8_t)~dayMask);
    if (narrowed == 0) {
      continue;  // every day this entry claimed was replaced
    }
    if (!merged.addTarget(narrowed, _schedule.targetAt(i))) {
      return false;
    }
  }
  for (uint8_t i = 0; i < incoming.count(); i++) {
    if (!merged.addTarget(incoming.dayBitmapAt(i), incoming.targetAt(i))) {
      return false;
    }
  }
  _schedule = merged;
  return true;
}

/*
 * The upward path (header comment): host-side variant enforcement first
 * (zero wire traffic on a violation), then stage every row and apply, then
 * update the cache via the same merge-by-day algorithm the adjudicated path
 * uses, with dayMask = the union of every dayBitmap in `schedule`.
 *
 * count() == 0 is the documented clear-everything request
 * (AT+MTROWAPPLY=<ep>,1,0): it empties the WHOLE stored payload, not merely
 * the days named (there are none), so the cache is cleared outright rather
 * than merged.
 */
bool MatterEvse::setChargingSchedule(const HearthChargingSchedule &schedule) {
  if (!started) {
    return false;
  }
  for (uint8_t i = 0; i < schedule.count(); i++) {
    const HearthChargingTarget &t = schedule.targetAt(i);
    if (variantSel == FULL) {
      if (!t.hasTargetSoC) {
        Hearth.hearthSetError(1);
        return false;
      }
    } else {  // NO_SOC
      if (t.hasTargetSoC && t.targetSoC != 100) {
        Hearth.hearthSetError(1);
        return false;
      }
    }
  }
  if (getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  if (schedule.count() == 0) {
    if (!rows.apply(0)) {
      return false;
    }
    _schedule.clear();
    return true;
  }

  uint8_t dayMask = 0;
  for (uint8_t i = 0; i < schedule.count(); i++) {
    HearthRowTransfer::Row row;
    for (uint8_t f = 0; f < HearthRowTransfer::kMaxFields; f++) {
      row.present[f] = false;
      row.value[f] = 0;
    }
    uint8_t bits = schedule.dayBitmapAt(i);
    const HearthChargingTarget &t = schedule.targetAt(i);
    row.present[kRowDay] = true;
    row.value[kRowDay] = bits;
    row.present[kRowTime] = true;
    row.value[kRowTime] = t.minutesPastMidnight;
    if (t.hasTargetSoC) {
      row.present[kRowSoc] = true;
      row.value[kRowSoc] = t.targetSoC;
    }
    if (t.hasAddedEnergy) {
      row.present[kRowEnergy] = true;
      row.value[kRowEnergy] = t.addedEnergy;
    }
    if (!rows.stage(i, row)) {
      return false;  // nothing applied yet; the device's stage is whatever
                      // the wire refusal left it as
    }
    dayMask = (uint8_t)(dayMask | bits);
  }
  if (!rows.apply((uint16_t)schedule.count())) {
    return false;
  }
  if (!hearthMergeByDay(dayMask, schedule)) {
    /* Should not happen (see hearthMergeByDay()'s own comment): the wire
     * write already succeeded, so the DEVICE's schedule is correct and only
     * this host's cache failed to rebuild. Left stale rather than guessed
     * at; the next reconcile resyncs it from the live store. */
    return false;
  }
  return true;
}

void MatterEvse::onSetTargets(bool (*handler)(const HearthChargingSchedule &)) {
  _onSetTargetsCB = handler;
}

void MatterEvse::onDisableCharging(bool (*cb)()) {
  _onDisableChargingCB = cb;
}

void MatterEvse::onEnableCharging(bool (*cb)(const EnableChargingInfo &)) {
  _onEnableChargingCB = cb;
}

/*
 * The reconcile split (header comment): CircuitCapacity is configuration,
 * re-pushed if ever set; SupplyState/FaultState are volatile and follow
 * B229 (has-flags cleared without a resend, so a post-reboot identical
 * setter reaches the fabric instead of being swallowed by a cache the
 * device no longer agrees with). The charging schedule lives in the
 * firmware's own NVS and survives AT+MTRESET, so it is resynced from the
 * LIVE store rather than assumed empty or carried over: this is the only
 * place chargingSchedule() can become accurate the first time a sketch
 * starts against a C6 that already has a schedule.
 *
 * A read failure, or one that returns fewer rows than it claims exist
 * (returned < total, which should not happen since _rowBuf is sized to
 * HearthChargingSchedule::kMaxEntries, the kind's own ceiling), leaves the
 * cache empty: a confidently wrong partial schedule would be worse than an
 * honestly empty one, and the next reconcile gets another chance.
 */
void MatterEvse::hearthOnReconciled() {
  if (!started) {
    return;
  }
  if (hasCircuitCapacity) {
    hearthSendEvsePair(kFieldCircuitCapacity, circuitCapacity);
  }
  hasSupplyState = false;
  hasFaultState = false;

  _schedule.clear();
  uint16_t total = 0, returned = 0;
  if (rows.getAll(_rowBuf, HearthChargingSchedule::kMaxEntries, total, returned) && returned == total) {
    for (uint16_t i = 0; i < returned; i++) {
      uint8_t bits = (uint8_t)_rowBuf[i].value[kRowDay];
      if (!_schedule.addTarget(bits, hearthTargetFromRow(_rowBuf[i]))) {
        _schedule.clear();  // a live store that fails this class's own
                             // shape rules would be a firmware/library
                             // mismatch; fail safe to empty rather than a
                             // partially-rebuilt schedule
        break;
      }
    }
  }
}

/*
 * The deferred SetTargets sequence (header comment): pull the proposed
 * rows (seq-qualified, HearthRowTransfer::getAllProposed()), build a
 * HearthChargingSchedule from them, ask the sketch, send AT+MTCMDRESP, and
 * on an allow with a non-zero dayMask, merge the cache. All ordinary wire
 * traffic: the busy gate the +MTCMD arrived under has been released by the
 * time this runs (Hearth.cpp's hearthDrainDeferredWork(), called after
 * hearthDrainCmdRespQueue(), both after the outer _link call has returned),
 * the exact precondition MatterWaterHeater's own BoostState push already
 * relies on.
 *
 * The pending state is claimed (copied out, then cleared) BEFORE any wire
 * traffic, the WaterHeater hearthOnDeferredWork() precedent: a firmware
 * refused write is not retried blind, and the drain's own busy check means
 * this call cannot itself be refused re-entrant.
 *
 * getAllProposed() failing, or returning fewer rows than it claims exist,
 * is treated as "the firmware has already resolved this seq without us"
 * (the slow-loop case, header comment): no callback is consulted and no
 * AT+MTCMDRESP is sent, since a stale/unknown seq answers +MTERR:1 on the
 * firmware's own side and sending one deliberately would only spend a
 * pointless round trip.
 */
void MatterEvse::hearthOnDeferredWork() {
  if (!pendingSetTargets) {
    return;
  }
  uint32_t seq = pendingSeq;
  uint8_t dayMask = pendingDayMask;
  pendingSetTargets = false;
  pendingSeq = 0;
  pendingDayMask = 0;

  uint16_t total = 0, returned = 0;
  bool wireOk = rows.getAllProposed(seq, _rowBuf, HearthChargingSchedule::kMaxEntries, total, returned);
  if (!wireOk || returned != total) {
    return;
  }

  HearthChargingSchedule proposed;
  bool built = true;
  for (uint16_t i = 0; i < returned && built; i++) {
    uint8_t bits = (uint8_t)_rowBuf[i].value[kRowDay];
    built = proposed.addTarget(bits, hearthTargetFromRow(_rowBuf[i]));
  }

  bool allow = (built && _onSetTargetsCB) ? _onSetTargetsCB(proposed) : false;

  /* "AT+MTCMDRESP=" (13) + seq (up to 10, u32) + "," (1) + verdict (1) + NUL
   * = 26; 40 matches Hearth.cpp's own hearthDrainCmdRespQueue() buffer for
   * the identical format, rather than trimming to the proven minimum. */
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTCMDRESP=%lu,%d", (unsigned long)seq, allow ? 1 : 0);
  Hearth.hearthCommand(cmd);

  if (allow && dayMask != 0) {
    hearthMergeByDay(dayMask, proposed);
  }
  /* dayMask == 0 with an allow is the fabric-only empty-SetTargets no-op
   * (mt_evse.cpp: "n == 0 && affected == 0" skips the merge entirely on
   * the firmware side too): nothing to merge, cache left exactly as it
   * was. */
}

/*
 * Cluster 0x0099 is Instance-served for every one of its 19 attributes
 * (task 7's report): no +MTATTR URC is ever raised for it, and an injected
 * one must move nothing. This class keeps no attribute cache reachable from
 * attributeChangeCB() at all, so the body is a documented no-op returning
 * `started`, the MatterDeviceEnergyManagement shape.
 */
bool MatterEvse::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

/*
 * main/mt_evse.cpp's HearthEvseDelegate, verified: Disable (153,1) carries
 * no tail (the four-field line). EnableCharging (153,2) carries exactly
 * three fields, "<until|empty>,<min>,<max>"; a short tail is malformed and
 * denied WITHOUT consulting the callback (fields.value[1]/[2] would
 * otherwise read as fabricated zeros), the WaterHeater Boost / DEM
 * PowerAdjustRequest precedent. SetTargets (153,5) carries exactly two
 * mandatory fields, "<rowcount>,<daymask>"; a malformed line is denied the
 * same way, and a well-formed one is DEFERRED rather than answered: see the
 * header comment and hearthOnDeferredWork() above.
 *
 * A second SetTargets arriving while one is already pending host-side
 * cannot happen through the firmware's own serialization (mt_evse.cpp
 * forwards only one command at a time) but is refused defensively anyway,
 * without touching the first one's pending state.
 */
bool MatterEvse::hearthOnForwardedCommandFieldsSeq(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields, uint32_t seq) {
  if (started && cluster_id == kEvseClusterId && command_id == kDisableCommandId) {
    return _onDisableChargingCB ? _onDisableChargingCB() : false;
  }
  if (started && cluster_id == kEvseClusterId && command_id == kEnableChargingCommandId) {
    if (fields.count < 3 || !fields.present[1] || !fields.present[2]) {
      return false;  // malformed: the two currents are mandatory
    }
    EnableChargingInfo info;
    info.hasChargingEnabledUntil = fields.present[0];
    info.chargingEnabledUntil = (uint32_t)fields.value[0];
    info.minimumChargeCurrent = fields.value[1];
    info.maximumChargeCurrent = fields.value[2];
    return _onEnableChargingCB ? _onEnableChargingCB(info) : false;
  }
  if (started && cluster_id == kEvseClusterId && command_id == kSetTargetsCommandId) {
    if (fields.count < 2 || !fields.present[0] || !fields.present[1]) {
      return false;  // malformed: rowcount and daymask are both mandatory
    }
    if (!_onSetTargetsCB) {
      /* No callback registered denies by default (the library's usual
       * precedent), decided HERE rather than after a pointless fetch: with
       * no handler to consult there is nothing AT+MTROWGET could change
       * about the answer, so this stays a synchronous deny with zero extra
       * wire traffic, exactly like every other unregistered verdict
       * callback in this library. */
      return false;
    }
    if (pendingSetTargets) {
      return false;  // defensive: the firmware never overlaps two forwards
    }
    pendingSetTargets = true;
    pendingSeq = seq;
    pendingDayMask = (uint8_t)fields.value[1];
    Hearth.hearthDeferCurrentCmdResp();
    Hearth.hearthRequestDeferredWork();
    return false;  // placeholder: the dispatcher ignores this return value
                    // once the dispatch has been deferred
  }
  return MatterEndPoint::hearthOnForwardedCommandFieldsSeq(cluster_id, command_id, fields, seq);
}

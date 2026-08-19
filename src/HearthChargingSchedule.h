/*
 * HearthChargingSchedule.h - Task 10 (energy round C2, design spec section
 * 7.2): the typed EVSE charging-target schedule a sketch builds and reads
 * through MatterEvse (Task 11). Pure value object, no wire knowledge and no
 * dependency on HearthLink/Hearth: it holds and validates the same shape
 * `AT+MTROW` kind 1 carries (AT_MT_SPEC.md, design spec section 2.4), but
 * Task 11 is what turns it into `HearthRowTransfer::Row`s and back, not
 * this class.
 *
 * ONE ENTRY PER STORED ROW, NOT ONE PER DAY: `HearthChargingSchedule` holds
 * up to 7 day-GROUP entries (one group per distinct day bitmap actually
 * used, never more than 7 since only 7 day bits exist) of up to 10 targets
 * each, 70 entries total, mirroring the firmware's own 7 x 10 storage bound
 * (mt_rows.h's MT_EVSE_TARGET_MAX_ROWS) and CHIP's own `ValidateTargets()`
 * limits (design spec section 3.2, point 1: "more than 10 targets in a
 * day"). Two rows sharing the identical day bitmap are two targets for the
 * same day-group, exactly the wire's own "rows sharing the same <day>
 * bitmap group into one ChargingTargetScheduleStruct at apply time" rule
 * (design spec 2.4).
 *
 * THE DAY-OWNERSHIP RULE, ENFORCED HERE AT ADD TIME rather than left to a
 * later apply/validate step: a day bit may belong to only one distinct
 * bitmap across the whole schedule (design spec 2.4, `mt_rows.c`'s
 * `mt_rows_validate()`). Two entries with bitmap {Mon,Tue} and {Tue,Wed}
 * both claim Tuesday under a DIFFERENT bitmap and are refused; two entries
 * that both carry bitmap {Mon,Tue} are fine (two targets on the same
 * Monday+Tuesday group) and count against that group's own 10-target cap.
 * Enforcing this on `addTarget()` rather than only at commit time means a
 * sketch discovers a malformed schedule immediately, at the call that
 * caused it, rather than building 70 entries and learning about a conflict
 * only when it tries to push the whole thing.
 *
 * WHAT THIS CLASS GUARANTEES, AND WHAT IT DOES NOT (fix round 1 finding 2:
 * an earlier version of this comment overclaimed the second half). This
 * class enforces exactly the SHAPE rules listed above and nothing past
 * them: the field ranges, the choice-of-optionals rule, the day-ownership
 * rule and the two ceilings (design spec 2.5's error table, `mt_rows.c`'s
 * field table). It does NOT know, and cannot know, about apply-time rules
 * that depend on data this class never sees: `mt_evse.cpp`'s SoC-mandatory-
 * per-variant check (around line 1521-1531) requires every target to carry
 * a target state of charge on a variant-0 (FULL) EVSE endpoint, and
 * requires it be ABSENT or exactly 100 on a variant-1 (NO_SOC) endpoint --
 * a rule keyed on the EVSE's own variant, which belongs to `MatterEvse`
 * (Task 11), not this class. A schedule built entirely through
 * `addTarget()` can therefore still be refused by `AT+MTROWAPPLY` on a real
 * device over that variant rule: that is not evidence of a bug in this
 * class, it is the check Task 11 must apply (or pre-filter for) before
 * staging, and Task 11 is where a reader should look for it.
 */
#pragma once

#include <stdint.h>

/*
 * One EVSE charging target: a time of day plus up to two optional payload
 * fields, at least one of which must be present (design spec 2.4's "choice
 * a min 1"). The has-flags spell absence explicitly rather than using a
 * sentinel value, since 0 is a legal SoC and a legal added-energy figure.
 */
class HearthChargingTarget {
public:
  uint16_t minutesPastMidnight = 0;  // 0..1439
  bool hasTargetSoC = false;
  uint8_t targetSoC = 0;  // 0..100 percent, meaningful only if hasTargetSoC
  bool hasAddedEnergy = false;
  int64_t addedEnergy = 0;  // >= 0 milliwatt-hours, meaningful only if hasAddedEnergy
};

class HearthChargingSchedule {
public:
  // The wire's own bound (mt_rows.h MT_EVSE_TARGET_MAX_ROWS): 7 days x 10
  // targets per day.
  static const uint8_t kMaxDays = 7;
  static const uint8_t kMaxTargetsPerDay = 10;
  static const uint8_t kMaxEntries = kMaxDays * kMaxTargetsPerDay;  // 70
  // kMaxEntries is unreachable as an INDEPENDENT condition given the other
  // two rules (only kMaxDays bits exist, so at most kMaxDays groups of at
  // most kMaxTargetsPerDay can ever form: 7 * 10 = 70 exactly). Keep the
  // check anyway (review round 1): it is not mere defence in depth, it is
  // `_dayBitmap[kMaxEntries]`/`_target[kMaxEntries]`'s OWN bounds guard.
  // addTarget() writes to `_dayBitmap[_count]`/`_target[_count]` on every
  // accepted call; if the day-ownership or per-day-ceiling logic above it
  // ever regresses and stops actually capping `_count` at 70, this is the
  // one line standing between that regression and an out-of-bounds write.
  // Bits 0..6 = Sunday..Saturday (design spec 2.4); 0x7F is all seven set.
  static const uint8_t kDayBitmapMask = 0x7F;

  HearthChargingSchedule() { clear(); }

  /*
   * Validates and appends one (dayBitmap, target) entry. Returns false and
   * changes nothing on any of:
   *   - dayBitmap is 0 or carries a bit outside 0..6 (kDayBitmapMask)
   *   - target.minutesPastMidnight > 1439
   *   - target.hasTargetSoC && target.targetSoC > 100
   *   - target.hasAddedEnergy && target.addedEnergy < 0
   *   - neither optional is present
   *   - the schedule is already at kMaxEntries (70)
   *   - a day bit in dayBitmap is already owned by a DIFFERENT bitmap
   *     already stored (the day-ownership rule, this header's own comment)
   *   - the day-group named by dayBitmap already holds kMaxTargetsPerDay
   *     (10) entries
   * A rejected call is a pure no-op: the schedule is left exactly as it was
   * before the call, so a sketch can retry with a corrected target.
   */
  bool addTarget(uint8_t dayBitmap, const HearthChargingTarget &t);

  uint8_t count() const {
    return _count;
  }

  // Out-of-range (i >= count()) answers 0, the empty bitmap, matching
  // MatterEndPoint::hearthDeclaredTypeAt()'s "safe default" convention for
  // an out-of-range index elsewhere in this library.
  uint8_t dayBitmapAt(uint8_t i) const;

  // Out-of-range (i >= count()) answers a reference to a fresh, all-absent
  // target rather than one of the stored entries, the same "safe default,
  // not a crash" convention dayBitmapAt() follows.
  const HearthChargingTarget &targetAt(uint8_t i) const;

  void clear() {
    _count = 0;
  }

private:
  bool dayOwnedByDifferentBitmap(uint8_t dayBitmap) const;
  uint8_t countInGroup(uint8_t dayBitmap) const;

  /*
   * THE MERGE-BY-DAY ALGORITHM, IN PLACE (T307). Narrows every stored
   * entry's bitmap by clearing the bits `dayMask` claims (dropping the
   * entry outright if nothing is left), then appends every entry of
   * `incoming` verbatim: the firmware's own subtract-days-then-append rule
   * (main/mt_evse.cpp), reproduced host-side. MatterEvse::hearthMergeByDay()
   * is the only caller, and MatterEvse.h is where the wire semantics are
   * documented.
   *
   * PRIVATE, WITH ONE FRIEND, DELIBERATELY. This is EVSE wire semantics,
   * not an operation a sketch should reach for on a value object, so it
   * adds nothing to the public surface. It lives here rather than in
   * MatterEvse.cpp because pass 2 writes `_dayBitmap`/`_target`/`_count`
   * directly, and because pass 1 MIRRORS addTarget()'s own acceptance rules
   * and must sit where a reader changing those rules cannot miss it.
   * MatterOvenCavity/MatterCookSurface friend their owner classes for the
   * same reason.
   *
   * VALIDATE THEN APPLY, and that shape is the whole point of the function.
   * It used to build a whole second HearthChargingSchedule and swap it in on
   * success, which made atomicity FREE but put 1192 bytes of stack on top of
   * a caller that already holds a schedule of its own, at the deepest point
   * of this library's deepest call chain. In place, atomicity has to be
   * DELIBERATE:
   *
   *   - mergeByDayFits() (pass 1) mutates nothing and answers whether every
   *     entry of the merged result would be accepted. It costs a couple of
   *     dozen bytes of stack, not 1192, because the only NEW information is
   *     the merged GROUP structure (at most kMaxDays groups), not the
   *     entries: every entry involved already passed addTarget()'s field
   *     checks when it was stored, and narrowing a bitmap touches no field.
   *   - pass 2 then narrows, compacts and appends with raw array writes, and
   *     CANNOT fail, because pass 1 established every bound it relies on.
   *
   * Returns false and leaves this schedule byte-identical on a refusal, and
   * refuses `&incoming == this` outright: an in-place merge cannot read a
   * source it is simultaneously rewriting, and a self-merge is meaningless
   * (no caller does one, and the alternative to refusing it is a silently
   * wrong answer). That hazard did not exist while the temporary did, so the
   * guard is part of the same change rather than an old check moved.
   */
  bool mergeByDay(uint8_t dayMask, const HearthChargingSchedule &incoming);
  bool mergeByDayFits(uint8_t dayMask, const HearthChargingSchedule &incoming) const;
  friend class MatterEvse;

  uint8_t _count;
  uint8_t _dayBitmap[kMaxEntries];
  HearthChargingTarget _target[kMaxEntries];
};

/*
 * HearthChargingSchedule.cpp - implementation. See HearthChargingSchedule.h
 * for the day-ownership rule and the validation this class mirrors from the
 * wire (design spec 2.4/2.5, mt_rows.c).
 */
#include "HearthChargingSchedule.h"

namespace {
const HearthChargingTarget kEmptyTarget;
}  // namespace

/*
 * True if any bit of dayBitmap is already set on a STORED entry whose own
 * bitmap is not IDENTICAL to dayBitmap. Two entries sharing the exact same
 * bitmap are the same day-group and are fine (that is countInGroup()'s
 * concern, not this one); this only catches a day bit split across two
 * DIFFERENT bitmaps, e.g. {Mon,Tue} then {Tue,Wed}.
 */
bool HearthChargingSchedule::dayOwnedByDifferentBitmap(uint8_t dayBitmap) const {
  for (uint8_t i = 0; i < _count; i++) {
    if (_dayBitmap[i] == dayBitmap) {
      continue;  // same group: not a conflict
    }
    if (_dayBitmap[i] & dayBitmap) {
      return true;  // some day bit is claimed by a different bitmap
    }
  }
  return false;
}

uint8_t HearthChargingSchedule::countInGroup(uint8_t dayBitmap) const {
  uint8_t n = 0;
  for (uint8_t i = 0; i < _count; i++) {
    if (_dayBitmap[i] == dayBitmap) {
      n++;
    }
  }
  return n;
}

bool HearthChargingSchedule::addTarget(uint8_t dayBitmap, const HearthChargingTarget &t) {
  if (dayBitmap == 0 || (dayBitmap & ~kDayBitmapMask) != 0) {
    return false;
  }
  if (t.minutesPastMidnight > 1439) {
    return false;
  }
  if (t.hasTargetSoC && t.targetSoC > 100) {
    return false;
  }
  if (t.hasAddedEnergy && t.addedEnergy < 0) {
    return false;
  }
  if (!t.hasTargetSoC && !t.hasAddedEnergy) {
    return false;
  }
  if (_count >= kMaxEntries) {
    // Unreachable given the two checks below (day-ownership plus the
    // per-day ceiling already cap `_count` at kMaxDays * kMaxTargetsPerDay
    // == kMaxEntries), but this is `_dayBitmap[]`/`_target[]`'s own bounds
    // guard for the write a few lines down, not mere defence in depth: see
    // HearthChargingSchedule.h's own comment on kMaxEntries.
    return false;
  }
  if (dayOwnedByDifferentBitmap(dayBitmap)) {
    return false;
  }
  if (countInGroup(dayBitmap) >= kMaxTargetsPerDay) {
    return false;
  }

  _dayBitmap[_count] = dayBitmap;
  _target[_count] = t;
  _count++;
  return true;
}

/*
 * PASS 1 of mergeByDay() (T307): would every entry of the merged result be
 * accepted by addTarget() above? Mutates nothing, allocates nothing, and is
 * the ONLY reason pass 2 is allowed to write in place.
 *
 * WHAT IT CHECKS, AND WHY THAT IS THE COMPLETE LIST. addTarget() refuses on
 * seven conditions. Five of them are per-entry FIELD rules (bitmap zero or
 * outside kDayBitmapMask, minutes past 1439, SoC above 100, negative added
 * energy, neither optional present) and none of them can be newly violated
 * here, because of a class invariant: every entry stored in a
 * HearthChargingSchedule got there through addTarget(), so it already
 * satisfies the field rules as they stand TODAY, and the merge changes no
 * field of any entry. The one bitmap it does change is narrowed, i.e. a
 * SUBSET of a bitmap that already passed, and the empty result is dropped
 * rather than stored. Relying on the invariant rather than re-checking the
 * fields here is deliberate and is the safer of the two: a re-check would be
 * a second copy of the field rules that a future change to addTarget() could
 * leave behind, while the invariant re-states itself automatically.
 *
 * That leaves exactly the three rules whose inputs ARE new, because they are
 * properties of the COMBINED set: the kMaxEntries bound, the day-ownership
 * rule, and the per-day ceiling. All three are properties of the merged
 * GROUP structure, and there are at most kMaxDays groups (a group needs at
 * least one day bit of its own and only kMaxDays bits exist), so the whole
 * simulation fits in two 7-byte arrays instead of a second schedule.
 *
 * The per-day ceiling is the one a real, well-formed device can trip: an
 * existing {Mon,Tue} group of 10 targets narrowed by Monday becomes a
 * {Tue} group of 10, and one incoming Tuesday target then makes 11.
 */
bool HearthChargingSchedule::mergeByDayFits(uint8_t dayMask, const HearthChargingSchedule &incoming) const {
  uint8_t groupBits[kMaxDays] = {0};
  uint8_t groupCount[kMaxDays] = {0};
  uint8_t groups = 0;
  uint8_t total = 0;

  /* One accumulator, two sources, in the order pass 2 writes them: src 0
   * walks THIS schedule narrowed by ~dayMask, src 1 walks `incoming`
   * verbatim. */
  for (uint8_t src = 0; src < 2; src++) {
    const HearthChargingSchedule &from = (src == 0) ? *this : incoming;
    for (uint8_t i = 0; i < from._count; i++) {
      uint8_t bits = from._dayBitmap[i];
      if (src == 0) {
        bits = (uint8_t)(bits & (uint8_t)~dayMask);
        if (bits == 0) {
          continue;  // every day this entry claimed is being replaced
        }
      }
      if (total >= kMaxEntries) {
        return false;
      }
      uint8_t g = 0;
      while (g < groups && groupBits[g] != bits) {
        g++;
      }
      if (g < groups) {
        if (groupCount[g] >= kMaxTargetsPerDay) {
          return false;  // the per-day ceiling, on the COMBINED counts
        }
        groupCount[g] = (uint8_t)(groupCount[g] + 1);
      } else {
        for (uint8_t k = 0; k < groups; k++) {
          if ((groupBits[k] & bits) != 0) {
            return false;  // a day bit split across two different bitmaps
          }
        }
        if (groups >= kMaxDays) {
          /* Unreachable: kMaxDays pairwise-disjoint non-empty bitmaps
           * already account for all kMaxDays day bits, so no further
           * disjoint bitmap exists to reach here. Kept as groupBits[]'s
           * own bounds guard for the write below, the same reasoning
           * addTarget()'s kMaxEntries check is kept under. */
          return false;
        }
        groupBits[groups] = bits;
        groupCount[groups] = 1;
        groups = (uint8_t)(groups + 1);
      }
      total = (uint8_t)(total + 1);
    }
  }
  return true;
}

/*
 * Validate, then apply (T307, and see the header for the whole rationale).
 * Nothing below pass 1 can fail, so a refusal leaves this schedule
 * byte-identical to what it was.
 */
bool HearthChargingSchedule::mergeByDay(uint8_t dayMask, const HearthChargingSchedule &incoming) {
  if (&incoming == this) {
    /* Aliasing: pass 2 rewrites the very array it would then have to read
     * `incoming` from. Refused rather than half-answered; see the header. */
    return false;
  }
  if (!mergeByDayFits(dayMask, incoming)) {
    return false;
  }

  /* Pass 2, in place. Narrow and compact first: `w` never runs ahead of
   * `r`, so this only ever reads an entry it has not yet overwritten. */
  uint8_t w = 0;
  for (uint8_t r = 0; r < _count; r++) {
    uint8_t narrowed = (uint8_t)(_dayBitmap[r] & (uint8_t)~dayMask);
    if (narrowed == 0) {
      continue;
    }
    _dayBitmap[w] = narrowed;
    if (w != r) {
      _target[w] = _target[r];
    }
    w++;
  }
  _count = w;

  /* Then append. addTarget() is deliberately NOT used: it would re-run
   * checks pass 1 has already settled, and a refusal from it at THIS point
   * would be exactly the half-merged cache this shape exists to prevent.
   * The write is bounded by pass 1's own kMaxEntries count, which counted
   * these same entries, AND by the alias refusal above: `incoming._count` is
   * re-read on every iteration, so a source that is also the destination
   * would grow exactly as fast as this loop consumes it.
   *
   * The kMaxEntries break is that second dependency's bounds guard, kept for
   * the same reason addTarget()'s own kMaxEntries check is kept: unreachable
   * with pass 1 and the alias refusal in front of it, and the one line
   * standing between a regression in either of them and a write past the end
   * of `_dayBitmap`/`_target`.
   *
   * What that write actually does is worse than a plain overrun, and it was
   * measured rather than assumed (fix round 1). With this break removed and
   * the alias guard deleted, `_count` runs to its uint8_t wrap, and
   * `_target[255]` is 4080 bytes into a 1120-byte array: the writes land
   * INSIDE the enclosing MatterEvse and quietly overwrite `rows` and
   * `_rowBuf`. AddressSanitizer reports NOTHING, because intra-object
   * overflow is not a wild pointer. Silent corruption of a neighbouring
   * member is precisely the failure no test and no sanitizer would ever
   * attribute back to this loop. */
  for (uint8_t i = 0; i < incoming._count; i++) {
    if (_count >= kMaxEntries) {
      break;
    }
    _dayBitmap[_count] = incoming._dayBitmap[i];
    _target[_count] = incoming._target[i];
    _count++;
  }
  return true;
}

uint8_t HearthChargingSchedule::dayBitmapAt(uint8_t i) const {
  if (i >= _count) {
    return 0;
  }
  return _dayBitmap[i];
}

const HearthChargingTarget &HearthChargingSchedule::targetAt(uint8_t i) const {
  if (i >= _count) {
    return kEmptyTarget;
  }
  return _target[i];
}

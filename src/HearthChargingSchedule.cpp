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

/*
 * test_chargingschedule.cpp - Task 10 (energy round C2): HearthChargingSchedule,
 * the typed EVSE charging-target value object. Pure logic, no link and no
 * endpoint: this file exercises addTarget()'s validation and the
 * day-ownership/per-day-ceiling rules directly, independent of the wire.
 */
#include <stdio.h>
#include "HearthChargingSchedule.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

namespace {
HearthChargingTarget socOnly(uint16_t minutes, uint8_t soc) {
  HearthChargingTarget t;
  t.minutesPastMidnight = minutes;
  t.hasTargetSoC = true;
  t.targetSoC = soc;
  return t;
}
HearthChargingTarget energyOnly(uint16_t minutes, int64_t mwh) {
  HearthChargingTarget t;
  t.minutesPastMidnight = minutes;
  t.hasAddedEnergy = true;
  t.addedEnergy = mwh;
  return t;
}
HearthChargingTarget both(uint16_t minutes, uint8_t soc, int64_t mwh) {
  HearthChargingTarget t;
  t.minutesPastMidnight = minutes;
  t.hasTargetSoC = true;
  t.targetSoC = soc;
  t.hasAddedEnergy = true;
  t.addedEnergy = mwh;
  return t;
}
}  // namespace

/* ===== absent optionals survive a round trip in both directions ===== */

static void test_soc_only_round_trips_absent_energy(void) {
  HearthChargingSchedule s;
  check("add SoC-only target", s.addTarget(0x01 /* Sunday */, socOnly(480, 80)));
  check("count is 1", s.count() == 1);
  const HearthChargingTarget &t = s.targetAt(0);
  check("hasTargetSoC true", t.hasTargetSoC);
  check("targetSoC 80", t.targetSoC == 80);
  check("hasAddedEnergy false: the absent optional survived", !t.hasAddedEnergy);
  check("minutes 480", t.minutesPastMidnight == 480);
}

static void test_energy_only_round_trips_absent_soc(void) {
  HearthChargingSchedule s;
  check("add energy-only target", s.addTarget(0x02 /* Monday */, energyOnly(1080, 15000)));
  const HearthChargingTarget &t = s.targetAt(0);
  check("hasAddedEnergy true", t.hasAddedEnergy);
  check("addedEnergy 15000", t.addedEnergy == 15000);
  check("hasTargetSoC false: the absent optional survived", !t.hasTargetSoC);
}

/* ===== refusals, each a pure no-op ===== */

static void test_refuses_target_with_neither_optional(void) {
  HearthChargingSchedule s;
  HearthChargingTarget t;
  t.minutesPastMidnight = 480;
  /* hasTargetSoC and hasAddedEnergy both default false */
  check("neither optional present is refused", !s.addTarget(0x01, t));
  check("schedule left empty", s.count() == 0);
}

static void test_refuses_time_past_1439(void) {
  HearthChargingSchedule s;
  check("minute 1439 (the boundary) is accepted", s.addTarget(0x01, socOnly(1439, 50)));
  check("minute 1440 is refused", !s.addTarget(0x02, socOnly(1440, 50)));
  check("the refused add did not grow the schedule", s.count() == 1);
}

static void test_refuses_soc_above_100(void) {
  HearthChargingSchedule s;
  check("SoC 100 (the boundary) is accepted", s.addTarget(0x01, socOnly(480, 100)));
  check("SoC 101 is refused", !s.addTarget(0x02, socOnly(480, 101)));
  check("the refused add did not grow the schedule", s.count() == 1);
}

static void test_refuses_negative_added_energy(void) {
  HearthChargingSchedule s;
  check("addedEnergy 0 is accepted", s.addTarget(0x01, energyOnly(480, 0)));
  check("addedEnergy -1 is refused", !s.addTarget(0x02, energyOnly(480, -1)));
  check("the refused add did not grow the schedule", s.count() == 1);
}

static void test_refuses_empty_day_bitmap(void) {
  HearthChargingSchedule s;
  check("bitmap 0 is refused", !s.addTarget(0x00, socOnly(480, 80)));
  check("schedule left empty", s.count() == 0);
}

static void test_refuses_day_bitmap_outside_seven_bits(void) {
  HearthChargingSchedule s;
  check("bitmap 0x80 (bit 7, outside Sun..Sat) is refused", !s.addTarget(0x80, socOnly(480, 80)));
  check("bitmap 0xFF is refused", !s.addTarget(0xFF, socOnly(480, 80)));
  check("schedule left empty", s.count() == 0);
}

/* ===== the day-ownership rule ===== */

static void test_same_bitmap_repeated_groups_into_one_day(void) {
  HearthChargingSchedule s;
  check("first Monday target", s.addTarget(0x02, socOnly(480, 60)));
  check("second Monday target, same bitmap", s.addTarget(0x02, socOnly(1080, 90)));
  check("both stored", s.count() == 2);
  check("both carry bitmap 0x02", s.dayBitmapAt(0) == 0x02 && s.dayBitmapAt(1) == 0x02);
}

static void test_day_reused_under_a_different_bitmap_is_refused(void) {
  HearthChargingSchedule s;
  /* bit 0 = Sunday, bit 1 = Monday, bit 2 = Tuesday (design spec 2.4). */
  check("Sun+Mon target", s.addTarget(0x03, socOnly(480, 60)));
  /* 0x06 = Mon+Tue: bit 1 (Monday) is already owned by bitmap 0x03. */
  check("Mon+Tue conflicting with Sun+Mon is refused", !s.addTarget(0x06, socOnly(600, 70)));
  check("the refused add did not grow the schedule", s.count() == 1);
}

static void test_disjoint_bitmaps_both_succeed(void) {
  HearthChargingSchedule s;
  check("Sun+Mon target", s.addTarget(0x03, socOnly(480, 60)));
  check("Tue+Wed target, no shared day", s.addTarget(0x0C, socOnly(600, 70)));
  check("both stored", s.count() == 2);
}

/* ===== the 10-target-per-day and 7-day/70-entry ceilings ===== */

static void test_ten_targets_per_day_ceiling(void) {
  HearthChargingSchedule s;
  for (int i = 0; i < 10; i++) {
    char name[64];
    snprintf(name, sizeof(name), "Monday target %d/10 accepted", i + 1);
    check(name, s.addTarget(0x02, socOnly((uint16_t)(i * 10), 50)));
  }
  check("10 stored", s.count() == 10);
  check("an 11th Monday target is refused", !s.addTarget(0x02, socOnly(999, 50)));
  check("the refused 11th did not grow the schedule", s.count() == 10);
}

static void test_seven_days_seventy_entries_ceiling(void) {
  HearthChargingSchedule s;
  const uint8_t days[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40};
  for (int d = 0; d < 7; d++) {
    for (int i = 0; i < 10; i++) {
      check("fill one of the 70 slots", s.addTarget(days[d], socOnly((uint16_t)(i * 10), 50)));
    }
  }
  check("the schedule is at its 70-entry ceiling", s.count() == HearthChargingSchedule::kMaxEntries);
  check("kMaxEntries is 70 (7 days x 10 targets)", HearthChargingSchedule::kMaxEntries == 70);
  /* every day bit is already owned, so ANY further bitmap conflicts with
   * one of the seven already stored: there is no way to name an eighth
   * distinct day-group even before the raw 70-entry cap is consulted. */
  check("a 71st entry (any bitmap) is refused", !s.addTarget(0x01, socOnly(999, 60)));
  check("still exactly 70", s.count() == 70);
}

/* ===== clear() and out-of-range accessors ===== */

static void test_clear_resets_the_schedule(void) {
  HearthChargingSchedule s;
  check("baseline add", s.addTarget(0x01, both(480, 80, 5000)));
  check("baseline count 1", s.count() == 1);
  s.clear();
  check("count is 0 after clear", s.count() == 0);
  check("a fresh add after clear starts at index 0", s.addTarget(0x02, socOnly(600, 40)));
  check("dayBitmapAt(0) is the post-clear entry, not the pre-clear one", s.dayBitmapAt(0) == 0x02);
}

/*
 * The out-of-range guard is only observable if the slot being probed once
 * held REAL data: HearthChargingTarget's own default member initializers
 * make a never-written array slot indistinguishable from the guard's
 * fallback (both read back as all-zero/all-absent), so a naive version of
 * this test that only ever probes a fresh, never-written slot would pass
 * whether or not the bounds check in dayBitmapAt()/targetAt() exists at
 * all -- exactly the "check that cannot fail" this project's mutation
 * testing exists to catch. clear() resets `count()` but does not wipe the
 * backing arrays (HearthChargingSchedule.h/.cpp), so writing targets,
 * clearing, then writing FEWER targets leaves genuine stale data sitting
 * in the now-out-of-range slots: a broken guard would leak it back out.
 */
static void test_out_of_range_access_is_a_safe_default(void) {
  HearthChargingSchedule s;
  check("first pre-clear target", s.addTarget(0x01, socOnly(111, 11)));
  check("second pre-clear target, at what will become the stale index 1", s.addTarget(0x02, socOnly(222, 22)));
  s.clear();
  check("one post-clear target: count is 1, index 1 is now out of range but still holds the old data",
        s.addTarget(0x04, socOnly(333, 33)));

  check("dayBitmapAt(count()) answers 0, not the stale 0x02", s.dayBitmapAt(s.count()) == 0);
  check("dayBitmapAt(255) answers 0", s.dayBitmapAt(255) == 0);
  const HearthChargingTarget &oob = s.targetAt(s.count());
  check("targetAt(count()) answers an all-absent target, not the stale minutes=222/soc=22",
        !oob.hasTargetSoC && !oob.hasAddedEnergy && oob.minutesPastMidnight == 0);
}

int main(void) {
  printf("\n===== HearthChargingSchedule tests =====\n");
  test_soc_only_round_trips_absent_energy();
  test_energy_only_round_trips_absent_soc();
  test_refuses_target_with_neither_optional();
  test_refuses_time_past_1439();
  test_refuses_soc_above_100();
  test_refuses_negative_added_energy();
  test_refuses_empty_day_bitmap();
  test_refuses_day_bitmap_outside_seven_bits();
  test_same_bitmap_repeated_groups_into_one_day();
  test_day_reused_under_a_different_bitmap_is_refused();
  test_disjoint_bitmaps_both_succeed();
  test_ten_targets_per_day_ceiling();
  test_seven_days_seventy_entries_ceiling();
  test_clear_resets_the_schedule();
  test_out_of_range_access_is_a_safe_default();

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

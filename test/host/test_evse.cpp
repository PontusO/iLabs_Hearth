/*
 * test_evse.cpp - Task 11 (energy round C2): MatterEvse, device type 0x050C,
 * the round's integration point.
 *
 * THE CENTRAL PROOF THIS FILE EXISTS TO CARRY: onSetTargets() must be shown
 * to fire through the REAL dispatch path, not a mock that hands the input
 * straight to the handler under test. Every test in the "SetTargets: the
 * deferred adjudication path" section below drives the whole chain a real
 * device would: s.injectURC() queues the exact "+MTCMD:..." line
 * main/mt_evse.cpp emits, Hearth.poll() is what discovers and dispatches
 * it (through hearthDispatchCmd() -> MatterEvse::
 * hearthOnForwardedCommandFieldsSeq() -> Hearth.hearthDeferCurrentCmdResp()
 * / hearthRequestDeferredWork() -> Hearth.cpp's own drain machinery ->
 * MatterEvse::hearthOnDeferredWork() -> HearthRowTransfer::
 * getAllProposed() -> the sketch's registered callback -> a real
 * "AT+MTCMDRESP=..." write), and MockStream's scripted expectations are
 * what prove each wire line actually went out in the right order with the
 * right bytes. Nothing here calls hearthOnForwardedCommandFieldsSeq() or
 * hearthOnDeferredWork() directly.
 *
 * test_set_targets_wiring_removed_would_be_caught (bottom of the deferred
 * section) documents the two mutations run against
 * test_set_targets_allowed_full_real_dispatch to prove the wiring is what
 * actually carries the test, not an accident of how MockStream happens to
 * behave: removing Hearth.hearthDeferCurrentCmdResp() from MatterEvse.cpp's
 * SetTargets branch, and separately removing
 * Hearth.hearthRequestDeferredWork() from the same branch. Both were run by
 * hand against this suite (see the task report for the transcripts) and
 * both turned this file's own tests red; this comment records the claim so
 * a future reader can re-run the same two mutations rather than trusting
 * the report alone.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterEvse.h"

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

/*
 * bringUp() runs the real Matter.begin() reconcile, exactly as every other
 * endpoint suite's own bringUp() does. MatterEvse's hearthOnReconciled()
 * always resyncs the schedule cache from the live store (the header
 * comment's own reasoning: the schedule survives AT+MTRESET and this host
 * may not be its sole author), so unlike MatterWaterHeater's conditional
 * reconcile push, EVERY bringUp() here must script the AT+MTROWGET=<ep>,1
 * reply too, not only AT+MTEP?. A bare "OK" (no +MTROW lines) is a fresh
 * device with no stored schedule, the common case for a smoke test.
 */
static void bringUp(MockStream &s, MatterEvse &dev, MatterEvse::Variant_t v = MatterEvse::FULL) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dev.begin(v);
  s.expect("AT+MTEP?", v == MatterEvse::FULL ? "+MTEP:0,1,0x050C\r\nOK\r\n" : "+MTEP:0,1,0x050C,1\r\nOK\r\n");
  s.expect("AT+MTROWGET=1,1", "OK\r\n");
  Matter.begin();
}

static void reconcile(MatterEndPoint &ep) {
  ep.hearthOnReconciled();
}

/* ===== declaration ===== */

static void test_begin_declares_0x050c_full(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);
  check("declared as device type 0x050C", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050C);
  check("FULL declares variant 0", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dev.getEndPointId() == 1);
  check("bringUp() issued no traffic beyond declaration and reconcile", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_begin_no_soc_declares_variant_1(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);
  check("NO_SOC still declares 0x050C", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x050C);
  check("NO_SOC declares variant 1", MatterEndPoint::hearthDeclaredVariantAt(0) == 1);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_out_of_range_variant_refused(void) {
  MockStream s;
  MatterEvse dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  Hearth.hearthSetError(0);
  check("a smuggled-in variant 2 is refused", !dev.begin((MatterEvse::Variant_t)2));
  check("with error 1", Hearth.lastError() == 1);
  check("and nothing was declared", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);
  check("a second begin() after Matter.begin() is refused", !dev.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_setters_before_begin_and_before_reconcile(void) {
  MockStream s;
  MatterEvse dev;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("setSupplyState() before begin() fails", !dev.setSupplyState(1));
  check("setFaultState() before begin() fails", !dev.setFaultState(1));
  check("setCircuitCapacity() before begin() fails", !dev.setCircuitCapacity(1000));
  HearthChargingSchedule empty;
  check("setChargingSchedule() before begin() fails", !dev.setChargingSchedule(empty));
  check("begin() declares", dev.begin());
  check("setSupplyState() before reconcile fails", !dev.setSupplyState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained() && s.unexpected().empty());
}

/* ===== the three scalar setters over AT+MTMEAS cluster 0x0099 ===== */

static void test_supply_state_pushes_no_ops_and_refuses_on_wire_error(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);

  s.expect("AT+MTMEAS=1,153,1,2", "OK\r\n");
  check("setSupplyState(2) pushes", dev.setSupplyState(2));
  check("repeat setSupplyState(2) is a no-op", dev.setSupplyState(2));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());

  s.expect("AT+MTMEAS=1,153,1,9", "+MTERR:1\r\nERROR\r\n");
  check("an out-of-range push is refused", !dev.setSupplyState(9));
  s.expect("AT+MTMEAS=1,153,1,9", "+MTERR:1\r\nERROR\r\n");
  check("cache untouched: the SAME refused value is retried on the wire again", !dev.setSupplyState(9));
  check("no unexpected commands", s.unexpected().empty());
}

static void test_fault_state_pushes_and_no_ops(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);

  s.expect("AT+MTMEAS=1,153,2,3", "OK\r\n");
  check("setFaultState(3) pushes", dev.setFaultState(3));
  check("repeat setFaultState(3) is a no-op", dev.setFaultState(3));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

static void test_circuit_capacity_pushes_signed(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);

  s.expect("AT+MTMEAS=1,153,4,32000", "OK\r\n");
  check("setCircuitCapacity(32000) pushes", dev.setCircuitCapacity(32000));
  check("repeat is a no-op", dev.setCircuitCapacity(32000));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());
}

/* ===== setChargingSchedule(): host-side variant enforcement ===== */

static void test_full_variant_requires_soc_on_every_target(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::FULL);
  Hearth.hearthSetError(0);

  HearthChargingSchedule sched;
  check("add an energy-only (no SoC) target", sched.addTarget(0x02 /* Monday */, energyOnly(480, 15000)));
  check("FULL refuses a schedule with a target missing SoC", !dev.setChargingSchedule(sched));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic", s.scriptDrained() && s.unexpected().empty());
}

static void test_no_soc_variant_requires_absent_or_100(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);
  Hearth.hearthSetError(0);

  HearthChargingSchedule bad;
  check("add a target with SoC 80 (not 100)", bad.addTarget(0x02, socOnly(480, 80)));
  check("NO_SOC refuses a target whose SoC is present and not 100", !dev.setChargingSchedule(bad));
  check("with error 1", Hearth.lastError() == 1);
  check("zero wire traffic on the refusal", s.scriptDrained() && s.unexpected().empty());

  HearthChargingSchedule ok100;
  check("add a target with SoC exactly 100", ok100.addTarget(0x02, socOnly(480, 100)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,100,", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,1", "OK\r\n");
  check("NO_SOC accepts SoC == 100", dev.setChargingSchedule(ok100));
  check("cache reflects the accepted schedule", dev.chargingSchedule().count() == 1);
}

/* FULL variant, a target carrying BOTH optionals at once: every field of
 * the row present, no absent tokens anywhere in the line. */
static void test_full_variant_soc_and_energy_both_present(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::FULL);

  HearthChargingSchedule sched;
  check("a target with SoC and added energy both set", sched.addTarget(0x08 /* Wednesday */, both(600, 90, 5000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,8,600,90,5000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,1", "OK\r\n");
  check("FULL accepts a target with SoC and energy both present", dev.setChargingSchedule(sched));
  check("script drained", s.scriptDrained());
}

/* ===== setChargingSchedule(): the wire, the cache, and the merge ===== */

static void test_set_schedule_stages_applies_and_caches(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule sched;
  check("Monday target", sched.addTarget(0x02, energyOnly(480, 25000000)));
  check("Tuesday target", sched.addTarget(0x04, energyOnly(420, 30000000)));

  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,25000000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,30000000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("setChargingSchedule() stages both rows and applies count 2", dev.setChargingSchedule(sched));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cache count is 2", dev.chargingSchedule().count() == 2);
  check("cache entry 0 is Monday", dev.chargingSchedule().dayBitmapAt(0) == 0x02);
  check("cache entry 1 is Tuesday", dev.chargingSchedule().dayBitmapAt(1) == 0x04);
}

static void test_set_schedule_empty_clears_everything(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule sched;
  check("one target", sched.addTarget(0x02, energyOnly(480, 25000000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,25000000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,1", "OK\r\n");
  check("populate the cache first", dev.setChargingSchedule(sched));
  check("cache has one entry", dev.chargingSchedule().count() == 1);

  HearthChargingSchedule empty;
  s.expect("AT+MTROWAPPLY=1,1,0", "OK\r\n");
  check("an empty schedule sends the count-0 clear request", dev.setChargingSchedule(empty));
  check("no AT+MTROW staging line for an empty schedule", s.scriptDrained());
  check("cache is empty", dev.chargingSchedule().count() == 0);
}

static void test_set_schedule_stage_failure_applies_nothing(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule sched;
  check("one target", sched.addTarget(0x02, energyOnly(480, 25000000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,25000000", "+MTERR:1\r\nERROR\r\n");
  check("a refused stage fails the whole call", !dev.setChargingSchedule(sched));
  check("no AT+MTROWAPPLY was ever sent", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cache stays empty", dev.chargingSchedule().count() == 0);
}

/*
 * The merge-by-day algorithm (header comment's central claim): pushing a
 * new schedule for Monday only must narrow the cached Monday entry away and
 * replace it, while Tuesday's cached entry -- named nowhere in this second
 * push -- survives untouched.
 */
static void test_set_schedule_merges_by_day(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule first;
  check("Monday", first.addTarget(0x02, energyOnly(480, 1000)));
  check("Tuesday", first.addTarget(0x04, energyOnly(420, 2000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("first push installs both days", dev.setChargingSchedule(first));

  HearthChargingSchedule second;
  check("a NEW Monday target", second.addTarget(0x02, energyOnly(500, 9999)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,500,,9999", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,1", "OK\r\n");
  check("second push replaces only Monday", dev.setChargingSchedule(second));

  check("cache still has 2 entries (Monday replaced, Tuesday kept)", dev.chargingSchedule().count() == 2);
  bool sawNewMonday = false, sawOldTuesday = false;
  for (uint8_t i = 0; i < dev.chargingSchedule().count(); i++) {
    if (dev.chargingSchedule().dayBitmapAt(i) == 0x02) {
      check("Monday's target is the NEW one (minutes 500)", dev.chargingSchedule().targetAt(i).minutesPastMidnight == 500);
      sawNewMonday = true;
    }
    if (dev.chargingSchedule().dayBitmapAt(i) == 0x04) {
      check("Tuesday's target is UNCHANGED (minutes 420)", dev.chargingSchedule().targetAt(i).minutesPastMidnight == 420);
      sawOldTuesday = true;
    }
  }
  check("both days accounted for", sawNewMonday && sawOldTuesday);
}

/* ===== SetTargets: the deferred adjudication path ===== */

/*
 * THE central proof (see this file's own header comment): a real
 * "+MTCMD:..." line injected exactly as main/mt_evse.cpp emits it, dispatched
 * by the real Hearth.poll(), pulled with a real seq-qualified
 * AT+MTROWGET, adjudicated by the sketch's own registered callback, and
 * answered with a real AT+MTCMDRESP -- MockStream's script is what proves
 * every one of those wire lines actually happened, in order.
 */
static void test_set_targets_allowed_full_real_dispatch(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  /* MatterEvse follows MatterDeviceEnergyManagement's plain-function-
   * pointer convention (not std::function), so a capturing lambda cannot
   * convert to the callback type; the file-scope g_evse_test_* cells below
   * are what a capture would have closed over. */
  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern uint8_t g_evse_test_seenMask;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  g_evse_test_seenMask = 0xFF;
  dev.onSetTargets([](const HearthChargingSchedule &proposed,
                      uint8_t affectedDayMask) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    g_evse_test_seenMask = affectedDayMask;
    return g_evse_test_verdict;
  });

  /* mt_evse.cpp's own worked example (task 6 report section 8.3): three
   * rows, day mask 6 = Monday (0x02) | Tuesday (0x04). NO_SOC variant, so
   * every row's SoC field is the empty token. */
  s.expect(
    "AT+MTROWGET=1,1,,7", "+MTROW:0,3,2,480,,25000000\r\n+MTROW:1,3,2,1080,,10000000\r\n+MTROW:2,3,4,420,,30000000\r\nOK\r\n"
  );
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,153,5,3,6");
  Hearth.poll();

  check("the onSetTargets handler fired exactly once", g_evse_test_calls == 1);
  check("the handler was handed the wire's own day mask (6 = Mon|Tue)", g_evse_test_seenMask == 0x06);
  check("the proposal carries 3 targets", g_evse_test_seenSchedule.count() == 3);
  check("row 0 is Monday", g_evse_test_seenSchedule.dayBitmapAt(0) == 0x02);
  check("row 0 has no SoC (NO_SOC variant, empty token)", !g_evse_test_seenSchedule.targetAt(0).hasTargetSoC);
  check("row 0 carries the added energy", g_evse_test_seenSchedule.targetAt(0).addedEnergy == 25000000);
  check("row 2 is Tuesday", g_evse_test_seenSchedule.dayBitmapAt(2) == 0x04);
  check("the AT+MTROWGET pull and the AT+MTCMDRESP verdict both went out, in order", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("an ALLOW merged the proposal into the cache", dev.chargingSchedule().count() == 3);
}

static void test_set_targets_denied_cache_untouched(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern uint8_t g_evse_test_seenMask;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = false;
  g_evse_test_seenMask = 0xFF;
  dev.onSetTargets([](const HearthChargingSchedule &proposed,
                      uint8_t affectedDayMask) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    g_evse_test_seenMask = affectedDayMask;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTROWGET=1,1,,8", "+MTROW:0,1,8,60,,99000000\r\nOK\r\n");
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,153,5,1,8");
  Hearth.poll();

  check("the handler ran", g_evse_test_calls == 1);
  check("the deny verdict was answered", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("a DENY leaves the cache untouched (still empty)", dev.chargingSchedule().count() == 0);
}

/*
 * Wire fact: a row-bearing proposal whose row count is zero but whose day
 * mask is not is a real request (a controller emptying a day with no
 * targets left in it), not a malformed line, and it must be shown to the
 * handler as a genuinely empty proposal against a non-zero day mask -- an
 * allow must clear that day from the cache even though nothing was
 * appended for it.
 */
static void test_set_targets_zero_rows_nonzero_daymask_clears_the_day(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule seedSched;
  check("seed Monday", seedSched.addTarget(0x02, energyOnly(480, 1000)));
  check("seed Tuesday", seedSched.addTarget(0x04, energyOnly(420, 2000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("seed the cache with two days", dev.setChargingSchedule(seedSched));

  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern uint8_t g_evse_test_seenMask;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  g_evse_test_seenMask = 0xFF;
  dev.onSetTargets([](const HearthChargingSchedule &proposed,
                      uint8_t affectedDayMask) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    g_evse_test_seenMask = affectedDayMask;
    return g_evse_test_verdict;
  });

  /* rowcount 0, daymask 2 (Monday): Monday is being emptied, no row exists
   * to pull for it. */
  s.expect("AT+MTROWGET=1,1,,9", "OK\r\n");
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,153,5,0,2");
  Hearth.poll();

  check("the handler saw an EMPTY proposal", g_evse_test_seenSchedule.count() == 0);
  check("but a NON-ZERO mask (2 = Monday), the only signal that a day is being cleared",
        g_evse_test_seenMask == 0x02);
  check("the verdict was answered", s.scriptDrained());
  check("Monday is gone from the cache", dev.chargingSchedule().count() == 1);
  check("the survivor is Tuesday, untouched", dev.chargingSchedule().dayBitmapAt(0) == 0x04);
}

/*
 * Round C2 final review, the finding this callback signature changed for.
 * A controller sends Monday with an EMPTY chargingTargets list (clear it)
 * together with Tuesday carrying one target. The firmware derives the mask
 * from the controller's ENTRIES, so the wire line is rowcount 1, mask 6:
 * one Tuesday row, and Monday named nowhere in the rows at all.
 *
 * Before the fix the handler received `proposed` only and could not
 * possibly have known Monday was about to be deleted: one Tuesday row is
 * exactly what an ordinary Tuesday-only proposal looks like. This test
 * fails to compile against the old signature and fails its mask assertion
 * against any implementation that passes a mask derived from the rows.
 */
static void test_set_targets_mask_names_a_day_with_no_row(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule seedSched;
  check("seed Monday", seedSched.addTarget(0x02, energyOnly(480, 1000)));
  check("seed Tuesday", seedSched.addTarget(0x04, energyOnly(420, 2000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("seed the cache with two days", dev.setChargingSchedule(seedSched));

  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern uint8_t g_evse_test_seenMask;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  g_evse_test_seenMask = 0xFF;
  dev.onSetTargets([](const HearthChargingSchedule &proposed,
                      uint8_t affectedDayMask) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    g_evse_test_seenMask = affectedDayMask;
    return g_evse_test_verdict;
  });

  /* rowcount 1, mask 6: the single row is TUESDAY's replacement, and
   * Monday is in the mask only because the controller sent it empty. */
  s.expect("AT+MTROWGET=1,1,,12", "+MTROW:0,1,4,600,,7777\r\nOK\r\n");
  s.expect("AT+MTCMDRESP=12,1", "OK\r\n");
  s.injectURC("+MTCMD:12,1,153,5,1,6");
  Hearth.poll();

  check("the handler ran", g_evse_test_calls == 1);
  check("the proposal carries exactly one row", g_evse_test_seenSchedule.count() == 1);
  check("and that row is TUESDAY, not Monday", g_evse_test_seenSchedule.dayBitmapAt(0) == 0x04);
  /* THE assertion: the mask names Monday, which no row does. Deriving a
   * mask from the rows would give 4 here, not 6. */
  check("the mask names BOTH days (6), including the row-less Monday", g_evse_test_seenMask == 0x06);
  check("Monday is not represented in the proposal at all",
        (g_evse_test_seenSchedule.dayBitmapAt(0) & 0x02) == 0);
  check("the verdict was answered", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  /* And the allow really does delete Monday, which is what the sketch
   * could not have predicted without the mask. */
  check("after the allow the cache holds one entry", dev.chargingSchedule().count() == 1);
  check("and it is the new Tuesday", dev.chargingSchedule().dayBitmapAt(0) == 0x04);
  check("Monday's stored target is gone", dev.chargingSchedule().targetAt(0).minutesPastMidnight == 600);
}

/*
 * Round C2 final review, the "partial upload wedges every shorter one"
 * finding. mt_rows.c makes the firmware's staged `count` a HIGH-WATER
 * MARK, and mt_at.c's cmd_mtrowapply() returns on a failed apply BEFORE
 * the reset that would clear it. So a two-row push that fails at the
 * APPLY leaves a stage of count 2 behind, and the next one-row push used
 * to stage index 0, apply count 1, be compared against the stale 2, and
 * answer +MTERR:1 -- forever, for every shorter schedule, for the life of
 * the boot. The library never issued AT+MTROWCLEAR (clearStaged() had
 * zero call sites and `rows` is protected, so a sketch could not either).
 *
 * This test scripts the exact sequence and fails without the speculative
 * discard: with no AT+MTROWCLEAR in the second push, the third command
 * MockStream sees is AT+MTROW rather than AT+MTROWCLEAR, the script
 * mismatches, and the recovery is never proven.
 */
static void test_failed_apply_does_not_wedge_a_shorter_push(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  /* Push 1: two rows staged fine, then the APPLY is refused (on a real
   * device this is the SOC-variant rule, or any other apply-time check).
   * The firmware's stage is now count 2 and still active. */
  HearthChargingSchedule two;
  check("Monday", two.addTarget(0x02, energyOnly(480, 1000)));
  check("Tuesday", two.addTarget(0x04, energyOnly(420, 2000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "+MTERR:1\r\nERROR\r\n");
  check("the refused apply fails the call", !dev.setChargingSchedule(two));
  check("with the wire's error code", Hearth.lastError() == 1);
  check("the cache was not updated", dev.chargingSchedule().count() == 0);

  /* Push 2: ONE row. The speculative discard is what makes this work; the
   * device's stale count-2 stage is dropped and the apply(1) matches.
   * Note the AT+MTROWCLEAR now SUCCEEDS, because something really is
   * staged this time. */
  HearthChargingSchedule one;
  check("a single Monday target", one.addTarget(0x02, energyOnly(500, 9999)));
  s.expect("AT+MTROWCLEAR=1,1", "OK\r\n");
  s.expect("AT+MTROW=1,1,0,2,500,,9999", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,1", "OK\r\n");
  check("the shorter push is NOT wedged by the previous failure", dev.setChargingSchedule(one));
  check("the discard, the stage and the apply all went out, in order", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("the cache holds the new schedule", dev.chargingSchedule().count() == 1);
  check("lastError() is clean after a successful push", Hearth.lastError() == 0);
}

static void test_set_targets_no_callback_denies_synchronously_zero_traffic(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,1,153,5,1,2");
  Hearth.poll();

  check("no registered callback denies immediately", s.scriptDrained());
  check("NO AT+MTROWGET was ever sent: the answer needed no fetch", s.unexpected().empty());
}

/*
 * The slow-loop case (header comment): by the time this host's deferred
 * fetch runs, the firmware's own 3000 ms window has already closed and
 * default-denied the request on its own, so the pull answers +MTERR:1 for
 * a seq the firmware no longer recognises. No callback is consulted (there
 * is no trustworthy proposal to show it) and no AT+MTCMDRESP is sent (the
 * firmware has already resolved this seq without us).
 */
static void test_set_targets_stale_seq_sends_no_verdict(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern int g_evse_test_calls;
  g_evse_test_calls = 0;
  dev.onSetTargets([](const HearthChargingSchedule &, uint8_t) -> bool {
    g_evse_test_calls++;
    return true;
  });

  s.expect("AT+MTROWGET=1,1,,11", "+MTERR:1\r\nERROR\r\n");
  s.injectURC("+MTCMD:11,1,153,5,2,4");
  Hearth.poll();

  check("the handler never ran: no trustworthy proposal to show it", g_evse_test_calls == 0);
  check("the fetch attempt is the only traffic; no AT+MTCMDRESP followed it", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("cache untouched", dev.chargingSchedule().count() == 0);
}

/*
 * Wiring proof (this file's own header comment): the two mutations below
 * were each run by hand against MatterEvse.cpp and confirmed to turn
 * test_set_targets_allowed_full_real_dispatch() red before being restored.
 * This function documents the exact edits for a future re-run; it makes no
 * assertions of its own (there is nothing left to mutate at test-run time
 * without editing source between builds).
 *
 * Mutation A: delete "Hearth.hearthDeferCurrentCmdResp();" from
 * MatterEvse::hearthOnForwardedCommandFieldsSeq()'s SetTargets branch.
 * Effect: hearthDispatchCmd() enqueues the placeholder `false` return value
 * immediately, so the FIRST command the mock sees after the injected URC is
 * "AT+MTCMDRESP=7,0" instead of the scripted "AT+MTROWGET=1,1,,7" --
 * MockStream records it unexpected, the scripted AT+MTROWGET is never
 * consumed, and s.scriptDrained() together with the handler call count both
 * go red.
 *
 * Mutation B: delete "Hearth.hearthRequestDeferredWork();" from the same
 * branch (Mutation A left in place). Effect: the deferral flag suppresses
 * the immediate verdict correctly, but hearthOnDeferredWork() is never
 * armed, so NEITHER the AT+MTROWGET pull NOR the AT+MTCMDRESP ever happens.
 * s.scriptDrained() is false (both scripted exchanges sit unconsumed) and
 * the handler call count stays 0.
 */
static void test_set_targets_wiring_removed_would_be_caught(void) {
  check("see this function's own comment: two hand-run mutations, transcribed in the task report", true);
}

/* ===== the merge is ATOMIC (T307) =====
 *
 * WHY THIS SECTION EXISTS AT ALL, and why it did not before. Until 0.12.1
 * hearthMergeByDay() built a whole second HearthChargingSchedule and swapped
 * it in on success, so "a refused merge leaves the cache untouched" was FREE:
 * nothing could be half-written because nothing was written until the end.
 * That temporary is gone (it cost 1192 bytes of stack at the deepest point of
 * the library's deepest call chain), and the merge now runs IN PLACE on the
 * cache. Atomicity is therefore no longer a property of the shape; it is a
 * property of HearthChargingSchedule::mergeByDayFits(), the validate pass
 * that runs before a single byte is touched. A property that used to be free
 * and is now deliberate needs a test that goes red when the deliberate part
 * is removed, which is exactly what the five mutations recorded in
 * test_merge_atomicity_mutations_would_be_caught() below do.
 *
 * Both refusal cases are driven through the REAL +MTCMD dispatch, this
 * file's own standard: nothing here calls hearthMergeByDay() directly except
 * the aliasing test, which cannot be reached any other way (that is the
 * point of it).
 */

namespace {
/*
 * hearthMergeByDay() is protected, and NEITHER shipped call site can alias
 * `incoming` with the cache (setChargingSchedule() passes its own parameter,
 * hearthOnDeferredWork() passes its local `proposed`). A subclass is the only
 * way to construct the aliased call at all: the hazard is created by the
 * in-place rewrite, so the guard against it needs a test even though no
 * caller can trip it today.
 */
class AliasProbeEvse : public MatterEvse {
public:
  bool mergeWithOwnCache(uint8_t dayMask) {
    return hearthMergeByDay(dayMask, _schedule);
  }
};
}  // namespace

/*
 * THE PER-DAY CEILING, the one rule a merge can genuinely fail on: an
 * existing {Mon,Tue} group holding the full ten targets, narrowed by a
 * Monday-only mask, becomes a {Tue} group still holding ten, and one
 * incoming Tuesday target would make eleven.
 *
 * The +MTCMD line below is deliberately one a correct firmware would not
 * emit (the mask names Monday while the row names Tuesday; mt_evse.cpp
 * derives the mask from the controller's own entries, so a Tuesday row
 * implies Tuesday in the mask). That is the point: this is the defensive
 * path, and the assertion is about what the cache looks like AFTERWARDS.
 */
static void test_merge_refused_by_per_day_ceiling_leaves_cache_untouched(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  /* Ten stored targets, all in the one {Mon,Tue} (0x06) day group: the
   * per-day ceiling exactly. */
  s.expect(
    "AT+MTROWGET=1,1",
    "+MTROW:0,10,6,400,,1000\r\n"
    "+MTROW:1,10,6,401,,1001\r\n"
    "+MTROW:2,10,6,402,,1002\r\n"
    "+MTROW:3,10,6,403,,1003\r\n"
    "+MTROW:4,10,6,404,,1004\r\n"
    "+MTROW:5,10,6,405,,1005\r\n"
    "+MTROW:6,10,6,406,,1006\r\n"
    "+MTROW:7,10,6,407,,1007\r\n"
    "+MTROW:8,10,6,408,,1008\r\n"
    "+MTROW:9,10,6,409,,1009\r\nOK\r\n"
  );
  reconcile(dev);
  check("the cache holds ten {Mon,Tue} targets", dev.chargingSchedule().count() == 10);

  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern uint8_t g_evse_test_seenMask;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  g_evse_test_seenMask = 0xFF;
  dev.onSetTargets([](const HearthChargingSchedule &proposed,
                      uint8_t affectedDayMask) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    g_evse_test_seenMask = affectedDayMask;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTROWGET=1,1,,20", "+MTROW:0,1,4,600,,7777\r\nOK\r\n");
  s.expect("AT+MTCMDRESP=20,1", "OK\r\n");
  s.injectURC("+MTCMD:20,1,153,5,1,2");
  Hearth.poll();

  check("the handler still ran", g_evse_test_calls == 1);
  check("the ALLOW verdict still went out: the merge is a cache concern, not a wire one", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  /* THE assertion. A naive in-place merge narrows all ten entries to 0x04
   * and appends the eleventh before discovering the ceiling; the cache
   * would then hold 11 entries with bitmap 4. Byte-identical is the
   * contract. */
  check("the refused merge left the count alone", dev.chargingSchedule().count() == 10);
  bool bitmapsIntact = true, targetsIntact = true;
  for (uint8_t i = 0; i < dev.chargingSchedule().count(); i++) {
    if (dev.chargingSchedule().dayBitmapAt(i) != 0x06) {
      bitmapsIntact = false;
    }
    if (dev.chargingSchedule().targetAt(i).minutesPastMidnight != (uint16_t)(400 + i) ||
        dev.chargingSchedule().targetAt(i).addedEnergy != (int64_t)(1000 + i)) {
      targetsIntact = false;
    }
  }
  check("every cached bitmap is still {Mon,Tue}, not narrowed to {Tue}", bitmapsIntact);
  check("every cached target is still its own, in its own slot", targetsIntact);
}

/*
 * The ceiling is INCLUSIVE, and a merge that exactly reaches it must still
 * go through. Without this, a pass 1 that refused everything (or refused at
 * ten rather than eleven) would look just as green as a correct one on the
 * refusal test above.
 */
static void test_merge_that_exactly_fills_the_per_day_ceiling_succeeds(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  /* Nine this time, so the incoming tenth fits exactly. */
  s.expect(
    "AT+MTROWGET=1,1",
    "+MTROW:0,9,6,400,,1000\r\n"
    "+MTROW:1,9,6,401,,1001\r\n"
    "+MTROW:2,9,6,402,,1002\r\n"
    "+MTROW:3,9,6,403,,1003\r\n"
    "+MTROW:4,9,6,404,,1004\r\n"
    "+MTROW:5,9,6,405,,1005\r\n"
    "+MTROW:6,9,6,406,,1006\r\n"
    "+MTROW:7,9,6,407,,1007\r\n"
    "+MTROW:8,9,6,408,,1008\r\nOK\r\n"
  );
  reconcile(dev);
  check("the cache holds nine {Mon,Tue} targets", dev.chargingSchedule().count() == 9);

  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onSetTargets([](const HearthChargingSchedule &, uint8_t) -> bool {
    g_evse_test_calls++;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTROWGET=1,1,,21", "+MTROW:0,1,4,600,,7777\r\nOK\r\n");
  s.expect("AT+MTCMDRESP=21,1", "OK\r\n");
  s.injectURC("+MTCMD:21,1,153,5,1,2");
  Hearth.poll();

  check("the handler ran", g_evse_test_calls == 1);
  check("the merge went through: nine narrowed plus one appended", dev.chargingSchedule().count() == 10);
  bool allTuesday = true;
  for (uint8_t i = 0; i < dev.chargingSchedule().count(); i++) {
    if (dev.chargingSchedule().dayBitmapAt(i) != 0x04) {
      allTuesday = false;
    }
  }
  check("Monday was narrowed away from all nine, leaving {Tue}", allTuesday);
  check("the first nine kept their own targets, in order", dev.chargingSchedule().targetAt(0).minutesPastMidnight == 400 &&
                                                            dev.chargingSchedule().targetAt(8).minutesPastMidnight == 408);
  check("the incoming target is appended last", dev.chargingSchedule().targetAt(9).minutesPastMidnight == 600);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The DAY-OWNERSHIP rule, the second way a merge can be refused: a cached
 * {Mon,Tue} entry narrowed by a Monday-only mask becomes {Tue}, and an
 * incoming {Tue,Wed} entry then claims Tuesday under a DIFFERENT bitmap,
 * which HearthChargingSchedule refuses by construction. Left half-merged
 * this is worse than a lost update: the cache would hold a day split across
 * two bitmaps, a state addTarget() cannot produce and nothing downstream
 * expects.
 */
static void test_merge_refused_by_day_ownership_leaves_cache_untouched(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTROWGET=1,1", "+MTROW:0,1,6,480,,1000\r\nOK\r\n");
  reconcile(dev);
  check("the cache holds one {Mon,Tue} target", dev.chargingSchedule().count() == 1);

  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onSetTargets([](const HearthChargingSchedule &, uint8_t) -> bool {
    g_evse_test_calls++;
    return g_evse_test_verdict;
  });

  /* mask 2 (Monday), one row for {Tue,Wed} (0x0C). */
  s.expect("AT+MTROWGET=1,1,,22", "+MTROW:0,1,12,600,,7777\r\nOK\r\n");
  s.expect("AT+MTCMDRESP=22,1", "OK\r\n");
  s.injectURC("+MTCMD:22,1,153,5,1,2");
  Hearth.poll();

  check("the handler ran and the verdict went out", g_evse_test_calls == 1 && s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("the refused merge left the count alone", dev.chargingSchedule().count() == 1);
  check("the cached bitmap is still {Mon,Tue}, not narrowed to {Tue}", dev.chargingSchedule().dayBitmapAt(0) == 0x06);
  check("the cached target is still the old one", dev.chargingSchedule().targetAt(0).minutesPastMidnight == 480);
  check("the incoming {Tue,Wed} entry did not land", dev.chargingSchedule().dayBitmapAt(1) == 0);
}

/*
 * ALIASING, a hazard the in-place rewrite CREATED. With a temporary, passing
 * the cache as `incoming` was merely wasteful; in place, pass 2 rewrites the
 * array it would then have to read the incoming entries from. Refused
 * outright, and the cache is left exactly as it was.
 *
 * The refusal is the VALUE CLASS's answer, and it is the right one there.
 * What an OWNER should do with it is a separate question, answered by
 * test_set_schedule_repushing_the_cache_itself_succeeds() below: the one
 * aliased call a sketch can actually make arrives with a mask that makes
 * the merge the identity, so it is a success, not a failure.
 */
static void test_merge_with_the_cache_itself_is_refused(void) {
  MockStream s;
  AliasProbeEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTROWGET=1,1", "+MTROW:0,2,2,480,,1000\r\n+MTROW:1,2,4,420,,2000\r\nOK\r\n");
  reconcile(dev);
  check("the cache holds Monday and Tuesday", dev.chargingSchedule().count() == 2);

  check("merging the cache into itself is refused", !dev.mergeWithOwnCache(0x02));
  check("count unchanged", dev.chargingSchedule().count() == 2);
  /* Unguarded, pass 2 compacts Monday away first, so entry 0 becomes
   * Tuesday's target and the append then re-reads the slot it just
   * overwrote: the cache ends up holding Tuesday twice and Monday not at
   * all, with the same count. Checking count alone would miss it. */
  check("entry 0 is still Monday", dev.chargingSchedule().dayBitmapAt(0) == 0x02);
  check("entry 0 still carries Monday's own target", dev.chargingSchedule().targetAt(0).minutesPastMidnight == 480);
  check("entry 1 is still Tuesday", dev.chargingSchedule().dayBitmapAt(1) == 0x04);
  check("entry 1 still carries Tuesday's own target", dev.chargingSchedule().targetAt(1).minutesPastMidnight == 420);
}

/*
 * THE OWNER'S SIDE OF THE ALIAS GUARD (fix round 1, review finding 1).
 * `chargingSchedule()` hands out a const reference to the cache, so
 * `setChargingSchedule(chargingSchedule())` is a legal call a sketch can
 * make -- re-push what I believe the device already has -- and it arrives
 * at the merge aliased. The value class refuses that merge, correctly. The
 * owner must NOT turn the refusal into a failed return: every wire line
 * went out and the device accepted them, and the merge is the identity in
 * this case anyway (the mask is the union of the schedule's own bitmaps,
 * so every entry is narrowed away and re-appended verbatim).
 *
 * 0.12.0 answered true here with all four lines on the wire; so does
 * 0.12.1 after this fix. The push is deliberately NOT short-circuited:
 * suppressing the wire traffic would turn a deliberate re-push into a
 * silent no-op, which is a worse lie than the one being fixed.
 */
static void test_set_schedule_repushing_the_cache_itself_succeeds(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  HearthChargingSchedule first;
  check("Monday", first.addTarget(0x02, energyOnly(480, 1000)));
  check("Tuesday", first.addTarget(0x04, energyOnly(420, 2000)));
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");  // speculative discard, refused when nothing is staged
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("seed the cache with two days", dev.setChargingSchedule(first));

  /* The re-push, through the public API only: the same four lines again. */
  s.expect("AT+MTROWCLEAR=1,1", "+MTERR:1\r\nERROR\r\n");
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("re-pushing the cache to the device REPORTS SUCCESS", dev.setChargingSchedule(dev.chargingSchedule()));
  check("and every wire line went out: the push is not short-circuited", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("lastError() is clean after a successful re-push", Hearth.lastError() == 0);

  check("the cache is unchanged: count", dev.chargingSchedule().count() == 2);
  check("the cache is unchanged: Monday first", dev.chargingSchedule().dayBitmapAt(0) == 0x02 &&
                                                 dev.chargingSchedule().targetAt(0).minutesPastMidnight == 480);
  check("the cache is unchanged: Tuesday second", dev.chargingSchedule().dayBitmapAt(1) == 0x04 &&
                                                   dev.chargingSchedule().targetAt(1).minutesPastMidnight == 420);
}

/*
 * Mutation record for the section above (this file's own standard, the same
 * shape as test_set_targets_wiring_removed_would_be_caught): the checks are
 * only evidence if they can be shown to fail. Each mutation below was run by
 * hand against src/, rebuilt, run and reverted, all against the FINAL code
 * (fix round 1 included, suite baseline 204/0 for this binary); the task
 * report carries the transcripts.
 *
 * Mutation C: delete the "if (!mergeByDayFits(...)) return false;" guard
 * from HearthChargingSchedule::mergeByDay(), i.e. remove pass 1 and keep the
 * in-place apply. Observed 198/6: six checks red, all of them cache
 * assertions and none of them wire assertions (the verdict still goes out
 * either way, which is correct). The per-day-ceiling test sees 11 entries
 * all narrowed to bitmap 4; the ownership test sees 2 entries with bitmaps 4
 * and 12, a day split across two bitmaps, a state addTarget() cannot
 * produce. Note that the ownership test's "the cached target is still the
 * old one" check stays GREEN under this mutation (the narrowed entry keeps
 * its target where it was), which is why the BITMAP assertion is the
 * load-bearing one.
 * test_merge_that_exactly_fills_the_per_day_ceiling_succeeds() also stays
 * green, which is the point of having it: it is the control that stops a
 * pass 1 which simply refuses everything from looking correct.
 *
 * Mutations F and G (fix round 1, from the review): delete ONLY the
 * day-ownership check, or ONLY the per-day-ceiling check, from
 * mergeByDayFits(). Each observed 201/3, and in each case the three red
 * lines are exactly the corresponding refusal test's. So the two refusal
 * tests are independently load-bearing, not a pair that only catches the
 * wholesale removal of pass 1.
 *
 * Mutation D: delete the "if (&incoming == this) return false;" guard from
 * mergeByDay(). Observed 200/4, all in
 * test_merge_with_the_cache_itself_is_refused(). The merge returns true;
 * entry 0 becomes Tuesday's 420-minute target (Monday was compacted away
 * first and never re-appended); and count() reads 70, not 2, because the
 * append loop re-reads `incoming._count` -- which IS the cache's own count
 * when the two alias -- so the source grows exactly as fast as the loop
 * consumes it, and only the kMaxEntries break stops it. Remove that break
 * as well and `_count` runs to its uint8_t wrap, writing far past the end
 * of both arrays; but the writes land INSIDE the enclosing MatterEvse and
 * silently corrupt `rows` and `_rowBuf`, so AddressSanitizer reports
 * nothing (intra-object overflow is not a wild pointer). That makes the
 * guard more important than a catchable overrun, not less.
 *
 * Mutation E (fix round 1): delete the "if (&schedule == &_schedule) return
 * true;" identity case from MatterEvse::setChargingSchedule(). Observed
 * 203/1: exactly "re-pushing the cache to the device REPORTS SUCCESS" goes
 * red, while "and every wire line went out" stays green. That pairing is
 * the whole point of the finding it fixes: the device accepted all four
 * lines and the call reported failure anyway.
 */
static void test_merge_atomicity_mutations_would_be_caught(void) {
  check("see this function's own comment: five hand-run mutations, transcribed in the task report", true);
}

/* ===== Disable / EnableCharging: ordinary scalar forwards ===== */

static void test_disable_charging_dispatches_and_answers(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onDisableCharging([]() -> bool {
    g_evse_test_calls++;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTCMDRESP=12,1", "OK\r\n");
  s.injectURC("+MTCMD:12,1,153,1");
  Hearth.poll();

  check("the Disable handler ran", g_evse_test_calls == 1);
  check("the accept verdict was answered", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_disable_charging_no_callback_denies(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTCMDRESP=13,0", "OK\r\n");
  s.injectURC("+MTCMD:13,1,153,1");
  Hearth.poll();

  check("no registered callback denies by default", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_enable_charging_null_until_is_empty_token(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern MatterEvse::EnableChargingInfo g_evse_test_seenInfo;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onEnableCharging([](const MatterEvse::EnableChargingInfo &info) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenInfo = info;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTCMDRESP=14,1", "OK\r\n");
  /* mt_evse.cpp's own EnableCharging() rendering of a null
   * chargingEnabledUntil: an empty token between the leading commas. */
  s.injectURC("+MTCMD:14,1,153,2,,6000,32000");
  Hearth.poll();

  check("the handler ran", g_evse_test_calls == 1);
  check("chargingEnabledUntil is absent (null)", !g_evse_test_seenInfo.hasChargingEnabledUntil);
  check("minimumChargeCurrent parsed", g_evse_test_seenInfo.minimumChargeCurrent == 6000);
  check("maximumChargeCurrent parsed", g_evse_test_seenInfo.maximumChargeCurrent == 32000);
  check("the accept verdict was answered", s.scriptDrained());
}

static void test_enable_charging_non_null_until(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern MatterEvse::EnableChargingInfo g_evse_test_seenInfo;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onEnableCharging([](const MatterEvse::EnableChargingInfo &info) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenInfo = info;
    return g_evse_test_verdict;
  });

  s.expect("AT+MTCMDRESP=15,1", "OK\r\n");
  s.injectURC("+MTCMD:15,1,153,2,1800,6000,32000");
  Hearth.poll();

  check("chargingEnabledUntil is present", g_evse_test_seenInfo.hasChargingEnabledUntil);
  check("chargingEnabledUntil value is 1800", g_evse_test_seenInfo.chargingEnabledUntil == 1800);
  check("script drained", s.scriptDrained());
}

static void test_enable_charging_short_tail_denies_without_callback(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  extern int g_evse_test_calls;
  g_evse_test_calls = 0;
  dev.onEnableCharging([](const MatterEvse::EnableChargingInfo &) -> bool {
    g_evse_test_calls++;
    return true;
  });

  /* Only one of the two mandatory currents present: malformed, denied
   * without consulting the callback (the WaterHeater Boost / DEM
   * PowerAdjustRequest precedent). Unlike a header-level parse failure
   * (Hearth.cpp's own seq/ep/cluster/command checks, which drop the whole
   * dispatch with no reply at all), THIS malformed tail is a semantic
   * decision made inside the target's own override, which still returns an
   * ordinary verdict (false) to hearthDispatchCmd() -- so a deny IS still
   * answered, exactly as test_waterheater.cpp's own malformed-Boost-tail
   * case (test_boost_denies_when_mask_promises_more_than_the_tail_carries)
   * expects "AT+MTCMDRESP=...,0". */
  s.expect("AT+MTCMDRESP=16,0", "OK\r\n");
  s.injectURC("+MTCMD:16,1,153,2,,6000");
  Hearth.poll();

  check("the callback never ran", g_evse_test_calls == 0);
  check("a deny was still answered for the malformed tail", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== attributeChangeCB: Instance-served, documented no-op ===== */

static void test_attribute_change_cb_is_a_documented_noop(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev);

  check("attributeChangeCB() returns true (started) and touches nothing observable", dev.attributeChangeCB(1, 0x0099, 0, nullptr));
}

/* ===== the B229 reconcile split, and the schedule live-resync ===== */

static void test_reconcile_repushes_config_not_volatile(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTMEAS=1,153,4,32000", "OK\r\n");
  check("setCircuitCapacity", dev.setCircuitCapacity(32000));
  s.expect("AT+MTMEAS=1,153,1,1", "OK\r\n");
  check("setSupplyState", dev.setSupplyState(1));
  s.expect("AT+MTMEAS=1,153,2,2", "OK\r\n");
  check("setFaultState", dev.setFaultState(2));

  s.expect("AT+MTMEAS=1,153,4,32000", "OK\r\n");  // config: re-pushed
  s.expect("AT+MTROWGET=1,1", "OK\r\n");           // the schedule resync, every reconcile
  reconcile(dev);
  check("reconcile re-pushed CircuitCapacity and resynced the (empty) schedule, nothing else", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());

  check("repeat setCircuitCapacity(32000) after reconcile is still a no-op", dev.setCircuitCapacity(32000));
  check("no traffic for the repeat", s.scriptDrained() && s.unexpected().empty());

  s.expect("AT+MTMEAS=1,153,1,1", "OK\r\n");
  check("setSupplyState(1) after reconcile reaches the wire again (B229)", dev.setSupplyState(1));
  s.expect("AT+MTMEAS=1,153,2,2", "OK\r\n");
  check("setFaultState(2) after reconcile reaches the wire again (B229)", dev.setFaultState(2));
}

static void test_reconcile_resyncs_schedule_from_live_store(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);
  check("cache starts empty (fresh device, bringUp's own empty reconcile)", dev.chargingSchedule().count() == 0);

  /* A schedule already sits on the device -- from a previous session, or a
   * controller's own prior SetTargets -- that THIS host never itself
   * pushed. */
  s.expect("AT+MTROWGET=1,1", "+MTROW:0,2,2,480,,1000\r\n+MTROW:1,2,4,420,,2000\r\nOK\r\n");
  reconcile(dev);

  check("the cache now reflects the live store", dev.chargingSchedule().count() == 2);
  check("entry 0 is Monday", dev.chargingSchedule().dayBitmapAt(0) == 0x02);
  check("entry 1 is Tuesday", dev.chargingSchedule().dayBitmapAt(1) == 0x04);
  check("script drained", s.scriptDrained());
}

/*
 * A live read that returns fewer rows than its own <total> claims
 * (returned < total) is treated as untrustworthy: a partially-rebuilt
 * schedule would be a confidently wrong picture, worse than an honestly
 * empty one. Provoked here by a single +MTROW line whose OWN <total> field
 * says 5: HearthRowTransfer::getAll() takes `total` from the line's own
 * report (authoritative, HearthRowTransfer.h) while `returned` counts what
 * actually arrived, so one line claiming a total of 5 produces exactly the
 * returned(1) != total(5) mismatch this class's own reconcile guard exists
 * to catch, with no wire error needed at all.
 */
static void test_reconcile_truncated_read_leaves_cache_empty(void) {
  MockStream s;
  MatterEvse dev;
  bringUp(s, dev, MatterEvse::NO_SOC);

  s.expect("AT+MTROWGET=1,1", "+MTROW:0,5,2,480,,1000\r\nOK\r\n");
  reconcile(dev);

  check("a truncated read (returned 1 != claimed total 5) leaves the cache empty", dev.chargingSchedule().count() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterEvse tests =====\n");
  g_yieldAdvanceMs = 1;
  test_begin_declares_0x050c_full();
  test_begin_no_soc_declares_variant_1();
  test_out_of_range_variant_refused();
  test_rebegin_after_reconcile_refused();
  test_setters_before_begin_and_before_reconcile();

  test_supply_state_pushes_no_ops_and_refuses_on_wire_error();
  test_fault_state_pushes_and_no_ops();
  test_circuit_capacity_pushes_signed();

  test_full_variant_requires_soc_on_every_target();
  test_no_soc_variant_requires_absent_or_100();
  test_full_variant_soc_and_energy_both_present();

  test_set_schedule_stages_applies_and_caches();
  test_set_schedule_empty_clears_everything();
  test_set_schedule_stage_failure_applies_nothing();
  test_set_schedule_merges_by_day();

  test_set_targets_allowed_full_real_dispatch();
  test_set_targets_denied_cache_untouched();
  test_set_targets_zero_rows_nonzero_daymask_clears_the_day();
  test_set_targets_mask_names_a_day_with_no_row();
  test_failed_apply_does_not_wedge_a_shorter_push();
  test_set_targets_no_callback_denies_synchronously_zero_traffic();
  test_set_targets_stale_seq_sends_no_verdict();
  test_set_targets_wiring_removed_would_be_caught();

  test_merge_refused_by_per_day_ceiling_leaves_cache_untouched();
  test_merge_that_exactly_fills_the_per_day_ceiling_succeeds();
  test_merge_refused_by_day_ownership_leaves_cache_untouched();
  test_merge_with_the_cache_itself_is_refused();
  test_set_schedule_repushing_the_cache_itself_succeeds();
  test_merge_atomicity_mutations_would_be_caught();

  test_disable_charging_dispatches_and_answers();
  test_disable_charging_no_callback_denies();
  test_enable_charging_null_until_is_empty_token();
  test_enable_charging_non_null_until();
  test_enable_charging_short_tail_denies_without_callback();

  test_attribute_change_cb_is_a_documented_noop();

  test_reconcile_repushes_config_not_volatile();
  test_reconcile_resyncs_schedule_from_live_store();
  test_reconcile_truncated_read_leaves_cache_empty();
  g_yieldAdvanceMs = 0;

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

/* File-scope capture cells for the raw-function-pointer callbacks under
 * test (this class follows MatterDeviceEnergyManagement's plain-
 * function-pointer convention, not std::function, so a lambda with
 * captures cannot convert to the callback type; these are what every
 * capturing lambda above would have closed over instead). */
HearthChargingSchedule g_evse_test_seenSchedule;
MatterEvse::EnableChargingInfo g_evse_test_seenInfo;
int g_evse_test_calls = 0;
bool g_evse_test_verdict = true;
/* The affected-day mask onSetTargets() now carries (round C2 final
 * review). Seeded to a value NO test ever legitimately produces, so a
 * check that passes only because the cell was never written is
 * impossible: 0xFF is not a valid day mask (only bits 0..6 exist). */
uint8_t g_evse_test_seenMask = 0xFF;

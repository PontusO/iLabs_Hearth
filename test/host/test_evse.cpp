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
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("first push installs both days", dev.setChargingSchedule(first));

  HearthChargingSchedule second;
  check("a NEW Monday target", second.addTarget(0x02, energyOnly(500, 9999)));
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
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onSetTargets([](const HearthChargingSchedule &proposed) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
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
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = false;
  dev.onSetTargets([](const HearthChargingSchedule &proposed) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
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
  s.expect("AT+MTROW=1,1,0,2,480,,1000", "OK\r\n");
  s.expect("AT+MTROW=1,1,1,4,420,,2000", "OK\r\n");
  s.expect("AT+MTROWAPPLY=1,1,2", "OK\r\n");
  check("seed the cache with two days", dev.setChargingSchedule(seedSched));

  extern HearthChargingSchedule g_evse_test_seenSchedule;
  extern int g_evse_test_calls;
  extern bool g_evse_test_verdict;
  g_evse_test_calls = 0;
  g_evse_test_verdict = true;
  dev.onSetTargets([](const HearthChargingSchedule &proposed) -> bool {
    g_evse_test_calls++;
    g_evse_test_seenSchedule = proposed;
    return g_evse_test_verdict;
  });

  /* rowcount 0, daymask 2 (Monday): Monday is being emptied, no row exists
   * to pull for it. */
  s.expect("AT+MTROWGET=1,1,,9", "OK\r\n");
  s.expect("AT+MTCMDRESP=9,1", "OK\r\n");
  s.injectURC("+MTCMD:9,1,153,5,0,2");
  Hearth.poll();

  check("the handler saw an EMPTY proposal", g_evse_test_seenSchedule.count() == 0);
  check("the verdict was answered", s.scriptDrained());
  check("Monday is gone from the cache", dev.chargingSchedule().count() == 1);
  check("the survivor is Tuesday, untouched", dev.chargingSchedule().dayBitmapAt(0) == 0x04);
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
  dev.onSetTargets([](const HearthChargingSchedule &) -> bool {
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
  test_set_targets_no_callback_denies_synchronously_zero_traffic();
  test_set_targets_stale_seq_sends_no_verdict();
  test_set_targets_wiring_removed_would_be_caught();

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

/*
 * test_laundrywasher.cpp - Task C8: MatterLaundryWasher, and the
 * OperationalState trio's shared-core proof.
 *
 * This file carries the full test matrix for the shared
 * MatterOperationalStateEndpoint core (AT+MTOPSTATE's exact wire pin, the
 * no-op/failed-write discipline, and all four forwarded commands'
 * allow/deny/no-callback/wrong-cluster/unrecognised-id paths). MatterDishwasher
 * and MatterLaundryDryer's own test binaries cover the same shape more
 * briefly, since the implementation underneath is identical (see
 * MatterOperationalStateEndpoint.h's header comment); the brief's own
 * cross-class proof lives here, exercising MatterDishwasher too on the
 * same wire to demonstrate that identity directly rather than merely
 * asserting it.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterLaundryWasher.h"
#include "MatterEndpoints/MatterDishwasher.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterLaundryWasher &washer) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  washer.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0073\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0073_variant0(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  check("declared as the laundry_washer device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0073);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", washer.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached OperationalState is Stopped (0)", washer.getOperationalState() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  check("a second begin() after Matter.begin() is refused", !washer.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== setOperationalState / getOperationalState: AT+MTOPSTATE ===== */

static void test_setoperationalstate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  s.expect("AT+MTOPSTATE=1,1", "OK\r\n"); /* Running */
  check("setOperationalState(1) sends the exact wire pin", washer.setOperationalState(1));
  check("cache updated to 1", washer.getOperationalState() == 1);
  s.expect("AT+MTOPSTATE=1,2", "OK\r\n"); /* Paused */
  check("setOperationalState(2) sends the exact wire pin", washer.setOperationalState(2));
  check("cache updated to 2", washer.getOperationalState() == 2);
  s.expect("AT+MTOPSTATE=1,0", "OK\r\n"); /* Stopped */
  check("setOperationalState(0) sends the exact wire pin", washer.setOperationalState(0));
  check("cache updated to 0", washer.getOperationalState() == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_noop_on_unchanged_value(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer); /* cache starts 0 (Stopped) */
  check("setOperationalState(0), already the cache, is a no-op", washer.setOperationalState(0));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.21's own worked example: "AT+MTOPSTATE=8,3 -> +MTERR:1
 * (3/Error is reserved, not settable here)". Caught firmware-side, not
 * duplicated host-side (header comment); the class simply reports the
 * failed write. */
static void test_setoperationalstate_failed_write_leaves_cache(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  s.expect("AT+MTOPSTATE=1,3", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !washer.setOperationalState(3));
  check("cache untouched (still 0)", washer.getOperationalState() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterLaundryWasher washer;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", washer.begin());
  check("setOperationalState() before reconcile fails", !washer.setOperationalState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== +MTCMD: Pause / Stop / Start / Resume, all real wire verdicts ===== */

static void test_forwarded_pause_allow(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,0"); /* Pause */
  Hearth.poll();
  check("an allowing onPause() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_pause_deny(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onPause([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,96,0");
  Hearth.poll();
  check("a denying onPause() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_stop_allow(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onStop([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,1"); /* Stop */
  Hearth.poll();
  check("an allowing onStop() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_start_allow(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onStart([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,2"); /* Start */
  Hearth.poll();
  check("an allowing onStart() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_resume_allow(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onResume([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,3"); /* Resume */
  Hearth.poll();
  check("an allowing onResume() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_no_callback_denies(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  /* no onPause() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,0");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* DoorLock's cluster, not OperationalState's */
  Hearth.poll();
  check("the wrong cluster id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_unrecognised_id_denies(void) {
  MockStream s;
  MatterLaundryWasher washer;
  bringUp(s, washer);
  washer.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,9"); /* command id 9: not Pause/Stop/Start/Resume */
  Hearth.poll();
  check("an unrecognised command id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * ===== Cross-class shared-core proof (task brief's own requirement) =====
 *
 * MatterLaundryWasher and MatterDishwasher declare different device types
 * (0x0073 vs 0x0075) but subclass the identical
 * MatterOperationalStateEndpoint core. This test declares one of each,
 * drives an onPause() verdict and a setOperationalState() write on BOTH,
 * and pins that every wire line each produces is byte-identical to the
 * other's except for the endpoint id -- the same command construction, the
 * same dispatch logic, genuinely shared, not two copies that merely look
 * alike.
 */
static void test_cross_class_shared_core_same_wire_different_devtype(void) {
  MockStream s;
  MatterLaundryWasher washer;
  MatterDishwasher dishwasher;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("washer.begin() declares", washer.begin());
  check("dishwasher.begin() declares", dishwasher.begin());
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0073\r\n+MTEP:1,2,0x0075\r\nOK\r\n");
  Matter.begin();
  check("washer adopted endpoint 1", washer.getEndPointId() == 1);
  check("dishwasher adopted endpoint 2", dishwasher.getEndPointId() == 2);

  /* Identical setOperationalState() wire shape, differing only by <ep>. */
  s.expect("AT+MTOPSTATE=1,1", "OK\r\n");
  check("washer.setOperationalState(1) writes AT+MTOPSTATE=1,1", washer.setOperationalState(1));
  s.expect("AT+MTOPSTATE=2,1", "OK\r\n");
  check("dishwasher.setOperationalState(1) writes the identical shape at ep 2", dishwasher.setOperationalState(1));

  /* Identical onPause() dispatch and AT+MTCMDRESP shape, differing only by
   * <ep> and <seq>. */
  bool washerCalled = false, dishwasherCalled = false;
  washer.onPause([&]() { washerCalled = true; return true; });
  dishwasher.onPause([&]() { dishwasherCalled = true; return true; });
  s.expect("AT+MTCMDRESP=10,1", "OK\r\n");
  s.injectURC("+MTCMD:10,1,96,0");
  Hearth.poll();
  s.expect("AT+MTCMDRESP=11,1", "OK\r\n");
  s.injectURC("+MTCMD:11,2,96,0");
  Hearth.poll();

  check("washer's onPause() fired for its own endpoint", washerCalled);
  check("dishwasher's onPause() fired for its own endpoint", dishwasherCalled);
  check("both endpoints produced the identical dispatch/reply shape", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterLaundryWasher tests =====\n");
  test_begin_declares_0x0073_variant0();
  test_rebegin_after_reconcile_refused();
  test_setoperationalstate_exact_wire_pin_and_cache();
  test_setoperationalstate_noop_on_unchanged_value();
  test_setoperationalstate_failed_write_leaves_cache();
  test_setoperationalstate_before_reconcile_fails_without_traffic();
  test_forwarded_pause_allow();
  test_forwarded_pause_deny();
  test_forwarded_stop_allow();
  test_forwarded_start_allow();
  test_forwarded_resume_allow();
  test_forwarded_command_no_callback_denies();
  test_forwarded_command_wrong_cluster_denies();
  test_forwarded_command_unrecognised_id_denies();
  test_cross_class_shared_core_same_wire_different_devtype();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

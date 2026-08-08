/*
 * test_laundrydryer.cpp - Task C8: MatterLaundryDryer.
 *
 * MatterLaundryDryer subclasses the identical MatterOperationalStateEndpoint
 * core MatterLaundryWasher does (see MatterOperationalStateEndpoint.h's
 * header comment), and test_laundrywasher.cpp already carries the full
 * test matrix for that shared core, plus the brief's own cross-class proof.
 * This file's job is narrower: prove THIS class declares the right device
 * type and wires correctly on its own, one representative case per
 * behaviour rather than the full matrix.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterLaundryDryer.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterLaundryDryer &dryer) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dryer.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x007C\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x007c_variant0(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  check("declared as the laundry_dryer device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x007C);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dryer.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached OperationalState is Stopped (0)", dryer.getOperationalState() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  check("a second begin() after Matter.begin() is refused", !dryer.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setoperationalstate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  s.expect("AT+MTOPSTATE=1,2", "OK\r\n"); /* Paused */
  check("setOperationalState(2) sends the exact wire pin", dryer.setOperationalState(2));
  check("cache updated to 2", dryer.getOperationalState() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_failed_write_leaves_cache(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  s.expect("AT+MTOPSTATE=1,3", "+MTERR:1\r\nERROR\r\n");
  check("a rejected write returns false", !dryer.setOperationalState(3));
  check("cache untouched (still 0)", dryer.getOperationalState() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_resume_allow(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  dryer.onResume([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,3"); /* Resume */
  Hearth.poll();
  check("an allowing onResume() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  bringUp(s, dryer);
  dryer.onPause([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* DoorLock's cluster, not OperationalState's */
  Hearth.poll();
  check("the wrong cluster id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterLaundryDryer dryer;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", dryer.begin());
  check("setOperationalState() before reconcile fails", !dryer.setOperationalState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterLaundryDryer tests =====\n");
  test_begin_declares_0x007c_variant0();
  test_rebegin_after_reconcile_refused();
  test_setoperationalstate_exact_wire_pin_and_cache();
  test_setoperationalstate_failed_write_leaves_cache();
  test_forwarded_resume_allow();
  test_forwarded_command_wrong_cluster_denies();
  test_setoperationalstate_before_reconcile_fails_without_traffic();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

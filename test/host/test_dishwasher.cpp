/*
 * test_dishwasher.cpp - Task C8: MatterDishwasher.
 *
 * MatterDishwasher subclasses the identical MatterOperationalStateEndpoint
 * core MatterLaundryWasher does (see MatterOperationalStateEndpoint.h's
 * header comment), and test_laundrywasher.cpp already carries the full
 * test matrix for that shared core, plus the brief's own cross-class proof
 * exercising this class directly. This file's job is narrower: prove THIS
 * class declares the right device type and wires correctly on its own,
 * one representative case per behaviour rather than the full matrix.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterDishwasher.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterDishwasher &dishwasher) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  dishwasher.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0075\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0075_variant0(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  check("declared as the dish_washer device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0075);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", dishwasher.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached OperationalState is Stopped (0)", dishwasher.getOperationalState() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  check("a second begin() after Matter.begin() is refused", !dishwasher.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setoperationalstate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  s.expect("AT+MTOPSTATE=1,1", "OK\r\n");
  check("setOperationalState(1) sends the exact wire pin", dishwasher.setOperationalState(1));
  check("cache updated to 1", dishwasher.getOperationalState() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_noop_on_unchanged_value(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  check("setOperationalState(0), already the cache, is a no-op", dishwasher.setOperationalState(0));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_start_allow(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  dishwasher.onStart([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,2"); /* Start */
  Hearth.poll();
  check("an allowing onStart() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_stop_deny(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  dishwasher.onStop([]() { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,96,1"); /* Stop */
  Hearth.poll();
  check("a denying onStop() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_no_callback_denies(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  bringUp(s, dishwasher);
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,96,0"); /* Pause, no onPause() registered */
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setoperationalstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterDishwasher dishwasher;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", dishwasher.begin());
  check("setOperationalState() before reconcile fails", !dishwasher.setOperationalState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterDishwasher tests =====\n");
  test_begin_declares_0x0075_variant0();
  test_rebegin_after_reconcile_refused();
  test_setoperationalstate_exact_wire_pin_and_cache();
  test_setoperationalstate_noop_on_unchanged_value();
  test_forwarded_start_allow();
  test_forwarded_stop_deny();
  test_forwarded_command_no_callback_denies();
  test_setoperationalstate_before_reconcile_fails_without_traffic();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

/*
 * test_powersource.cpp - Task C8: MatterPowerSource.
 *
 * Covers: the three setters' exact AT+MTATTR wire pins (BatChargeLevel
 * enum8, BatPercentRemaining uint8, BatReplacementNeeded boolean), the
 * no-op-on-unchanged and failed-write-leaves-cache discipline every
 * sibling class follows, and -- the class's one genuinely novel piece --
 * setBatPercentRemaining()'s percent-to-half-percent doubling, pinned for
 * several values including the two clamp edges.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterPowerSource.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterPowerSource &ps) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  ps.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0011\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0011_variant0(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  check("declared as the power_source device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0011);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", ps.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  check("a second begin() after Matter.begin() is refused", !ps.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== setBatChargeLevel: cluster 47 attr 14, enum8, mode 1 ===== */

static void test_setbatchargelevel_exact_wire_pin(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,14,1,1", "+MTATTR:1,47,14,1\r\nOK\r\n"); /* 1 == Warning */
  check("setBatChargeLevel(1) sends the exact wire pin", ps.setBatChargeLevel(1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatchargelevel_noop_on_unchanged_value(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps); /* cache starts 0 (Ok) */
  check("setBatChargeLevel(0), already the cache, is a no-op", ps.setBatChargeLevel(0));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatchargelevel_failed_write_leaves_cache(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,14,2,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !ps.setBatChargeLevel(2));
  /* cache untouched (still 0): setBatChargeLevel(0) must still be a no-op */
  check("cache still reads as unchanged (0)", ps.setBatChargeLevel(0));
  check("no further AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== setBatReplacementNeeded: cluster 47 attr 15, boolean, mode 1 ===== */

static void test_setbatreplacementneeded_exact_wire_pin_and_toggle(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,15,1,1", "+MTATTR:1,47,15,1\r\nOK\r\n");
  check("setBatReplacementNeeded(true) sends the exact wire pin", ps.setBatReplacementNeeded(true));
  s.expect("AT+MTATTR=1,47,15,0,1", "+MTATTR:1,47,15,0\r\nOK\r\n");
  check("setBatReplacementNeeded(false) sends the exact wire pin", ps.setBatReplacementNeeded(false));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatreplacementneeded_noop_on_unchanged_value(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps); /* cache starts false */
  check("setBatReplacementNeeded(false), already the cache, is a no-op", ps.setBatReplacementNeeded(false));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== setBatPercentRemaining: cluster 47 attr 12, uint8, mode 1, x2 ===== */

static void test_setbatpercentremaining_doubles_50_to_100(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,100,1", "+MTATTR:1,47,12,100\r\nOK\r\n");
  check("setBatPercentRemaining(50.0) writes wire value 100 (half-percent units)", ps.setBatPercentRemaining(50.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_doubles_fractional_37_5_to_75(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,75,1", "+MTATTR:1,47,12,75\r\nOK\r\n");
  check("setBatPercentRemaining(37.5) writes wire value 75, an exact half-percent step", ps.setBatPercentRemaining(37.5));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_doubles_100_to_200(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,200,1", "+MTATTR:1,47,12,200\r\nOK\r\n");
  check("setBatPercentRemaining(100.0) writes wire value 200, the top of the range", ps.setBatPercentRemaining(100.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_doubles_0_to_0(void) {
  /* The cache already starts at wire value 0 (begin()'s own default), so
   * setBatPercentRemaining(0.0) against a fresh object would be a no-op
   * for that reason alone, not evidence the doubling math produced 0. Set
   * a non-zero value first so the drop back to 0.0 is a genuine change. */
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,100,1", "+MTATTR:1,47,12,100\r\nOK\r\n");
  check("prime a non-zero value first", ps.setBatPercentRemaining(50.0));
  s.expect("AT+MTATTR=1,47,12,0,1", "+MTATTR:1,47,12,0\r\nOK\r\n");
  check("setBatPercentRemaining(0.0) writes wire value 0", ps.setBatPercentRemaining(0.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_clamps_above_100(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,200,1", "+MTATTR:1,47,12,200\r\nOK\r\n");
  check("setBatPercentRemaining(150.0) clamps to 100 percent, wire value 200", ps.setBatPercentRemaining(150.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_clamps_below_0(void) {
  /* Same reasoning as the 0.0 test above: prime a non-zero value first so
   * the clamp-to-0 write is a genuine change, not a cache no-op. */
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,100,1", "+MTATTR:1,47,12,100\r\nOK\r\n");
  check("prime a non-zero value first", ps.setBatPercentRemaining(50.0));
  s.expect("AT+MTATTR=1,47,12,0,1", "+MTATTR:1,47,12,0\r\nOK\r\n");
  check("setBatPercentRemaining(-10.0) clamps to 0 percent, wire value 0", ps.setBatPercentRemaining(-10.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatpercentremaining_noop_on_unchanged_wire_value(void) {
  MockStream s;
  MatterPowerSource ps;
  bringUp(s, ps);
  s.expect("AT+MTATTR=1,47,12,100,1", "+MTATTR:1,47,12,100\r\nOK\r\n");
  check("first setBatPercentRemaining(50.0) reaches the wire", ps.setBatPercentRemaining(50.0));
  check("a second call at the identical wire value is a no-op", ps.setBatPercentRemaining(50.0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoint 0 (not yet reconciled): every attribute write path must fail
 * without ever reaching the wire. */
static void test_setter_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterPowerSource ps;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", ps.begin());
  check("setBatChargeLevel() before reconcile fails", !ps.setBatChargeLevel(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterPowerSource tests =====\n");
  test_begin_declares_0x0011_variant0();
  test_rebegin_after_reconcile_refused();
  test_setbatchargelevel_exact_wire_pin();
  test_setbatchargelevel_noop_on_unchanged_value();
  test_setbatchargelevel_failed_write_leaves_cache();
  test_setbatreplacementneeded_exact_wire_pin_and_toggle();
  test_setbatreplacementneeded_noop_on_unchanged_value();
  test_setbatpercentremaining_doubles_50_to_100();
  test_setbatpercentremaining_doubles_fractional_37_5_to_75();
  test_setbatpercentremaining_doubles_100_to_200();
  test_setbatpercentremaining_doubles_0_to_0();
  test_setbatpercentremaining_clamps_above_100();
  test_setbatpercentremaining_clamps_below_0();
  test_setbatpercentremaining_noop_on_unchanged_wire_value();
  test_setter_before_reconcile_fails_without_traffic();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

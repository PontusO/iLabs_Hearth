/*
 * test_smokecoalarm.cpp - Task C8: MatterSmokeCOAlarm.
 *
 * Covers: AT+MTALARM's eleven fields pinned exactly (AT_MT_SPEC.md S3.22's
 * own table, including completeSelfTest() = field 5 value 0), the
 * notify-only SelfTestRequest dispatch (seq 0: the callback fires but no
 * AT+MTCMDRESP is ever sent), cached getters for the ten Set*-backed
 * states, and getExpressedState()'s live AT+MTATTR read.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterSmokeCOAlarm.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterSmokeCOAlarm &alarm) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  alarm.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0076\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0076_variant0(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  check("declared as the smoke_co_alarm device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0076);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", alarm.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("every cached state starts at 0", alarm.getSmokeState() == 0 && alarm.getCOState() == 0 && alarm.getBatteryAlert() == 0);
  check("HardwareFaultAlert starts false", !alarm.getHardwareFaultAlert());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  check("a second begin() after Matter.begin() is refused", !alarm.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== AT+MTALARM: exact wire pins, one per S3.22 field, plus cache ===== */

static void test_setsmokestate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,1,1", "OK\r\n");
  check("setSmokeState(1) sends field 1", alarm.setSmokeState(1));
  check("cache updated", alarm.getSmokeState() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setcostate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,2,2", "OK\r\n");
  check("setCOState(2) sends field 2", alarm.setCOState(2));
  check("cache updated", alarm.getCOState() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setbatteryalert_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,3,1", "OK\r\n");
  check("setBatteryAlert(1) sends field 3", alarm.setBatteryAlert(1));
  check("cache updated", alarm.getBatteryAlert() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setdevicemuted_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,4,1", "OK\r\n");
  check("setDeviceMuted(1) sends field 4", alarm.setDeviceMuted(1));
  check("cache updated", alarm.getDeviceMuted() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.22's own worked example: "AT+MTALARM=9,5,0 -> OK (self-
 * test complete; fires SelfTestComplete)". */
static void test_completeselftest_sends_field5_value0(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,5,0", "OK\r\n");
  check("completeSelfTest() sends exactly field 5 value 0", alarm.completeSelfTest());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_completeselftest_always_reaches_the_wire(void) {
  /* No cached TestInProgress value exists, so back-to-back calls must both
   * reach the wire -- there is nothing for a no-op check to compare
   * against. */
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,5,0", "OK\r\n");
  check("first completeSelfTest() reaches the wire", alarm.completeSelfTest());
  s.expect("AT+MTALARM=1,5,0", "OK\r\n");
  check("second completeSelfTest() reaches the wire too", alarm.completeSelfTest());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_sethardwarefaultalert_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,6,1", "OK\r\n");
  check("setHardwareFaultAlert(true) sends field 6 value 1", alarm.setHardwareFaultAlert(true));
  check("cache updated", alarm.getHardwareFaultAlert());
  s.expect("AT+MTALARM=1,6,0", "OK\r\n");
  check("setHardwareFaultAlert(false) sends field 6 value 0", alarm.setHardwareFaultAlert(false));
  check("cache updated", !alarm.getHardwareFaultAlert());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setendofservicealert_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,7,1", "OK\r\n");
  check("setEndOfServiceAlert(1) sends field 7", alarm.setEndOfServiceAlert(1));
  check("cache updated", alarm.getEndOfServiceAlert() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinterconnectsmokealarm_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,8,2", "OK\r\n");
  check("setInterconnectSmokeAlarm(2) sends field 8", alarm.setInterconnectSmokeAlarm(2));
  check("cache updated", alarm.getInterconnectSmokeAlarm() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinterconnectcoalarm_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,9,2", "OK\r\n");
  check("setInterconnectCOAlarm(2) sends field 9", alarm.setInterconnectCOAlarm(2));
  check("cache updated", alarm.getInterconnectCOAlarm() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setcontaminationstate_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,10,3", "OK\r\n");
  check("setContaminationState(3) sends field 10", alarm.setContaminationState(3));
  check("cache updated", alarm.getContaminationState() == 3);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsmokesensitivitylevel_exact_wire_pin_and_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,11,2", "OK\r\n");
  check("setSmokeSensitivityLevel(2) sends field 11", alarm.setSmokeSensitivityLevel(2));
  check("cache updated", alarm.getSmokeSensitivityLevel() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setter_noop_on_unchanged_value(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm); /* cache starts 0 */
  check("setSmokeState(0), already the cache, is a no-op", alarm.setSmokeState(0));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setter_failed_write_leaves_cache(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTALARM=1,1,3", "+MTERR:1\r\nERROR\r\n"); /* 3 is not a valid AlarmStateEnum value */
  check("a rejected write returns false", !alarm.setSmokeState(3));
  check("cache untouched (still 0)", alarm.getSmokeState() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoint 0 (not yet reconciled): every custom write path must fail
 * without ever reaching the wire, the same "endpoint 0 is unaddressable"
 * reasoning every sibling class's custom write path uses. */
static void test_setter_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", alarm.begin());
  check("setSmokeState() before reconcile fails", !alarm.setSmokeState(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== +MTCMD: SelfTestRequest, always notify-only (seq 0) ===== */

/*
 * AT_MT_SPEC.md S3.17/S3.22, pinned exactly: "+MTCMD:0,<ep>,92,0" --
 * SmokeCoAlarmServer::HandleRemoteSelfTestRequest already answered the
 * controller before this ever arrives, so there is no verdict to give.
 * The callback still runs (dispatch without reply is not "no dispatch"),
 * but no AT+MTCMDRESP is ever sent: sending one for seq 0 would itself
 * earn +MTERR:1 (S3.17), so nothing is scripted for it here and
 * scriptDrained() proves none was sent.
 */
static void test_selftest_notifyonly_fires_callback_no_reply(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  int calls = 0;
  alarm.onSelfTest([&]() { calls++; });
  s.injectURC("+MTCMD:0,1,92,0");
  Hearth.poll();
  check("the callback ran exactly once", calls == 1);
  check("no AT+MTCMDRESP was ever sent for seq 0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_selftest_no_callback_registered_is_silent(void) {
  /* No onSelfTest() registered at all: the dispatch must not crash, and
   * still no reply is sent (seq 0 never gets one regardless). */
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.injectURC("+MTCMD:0,1,92,0");
  Hearth.poll();
  check("no reply sent with nothing registered either", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_selftest_wrong_cluster_does_not_fire(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  int calls = 0;
  alarm.onSelfTest([&]() { calls++; });
  s.injectURC("+MTCMD:0,1,257,0"); /* DoorLock's cluster, not SmokeCoAlarm's */
  Hearth.poll();
  check("the wrong cluster does not fire the callback", calls == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== ExpressedState: a genuine AT+MTATTR read, never cached ===== */

static void test_getexpressedstate_reads_the_wire(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTATTR=1,92,0", "+MTATTR:1,92,0,4\r\nOK\r\n"); /* 4 == Testing */
  check("getExpressedState() reads the exact wire pin", alarm.getExpressedState() == 4);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_getexpressedstate_two_reads_both_hit_the_wire(void) {
  /* Never cached: back-to-back calls must both issue a fresh read. */
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTATTR=1,92,0", "+MTATTR:1,92,0,0\r\nOK\r\n");
  check("first read returns Normal", alarm.getExpressedState() == 0);
  s.expect("AT+MTATTR=1,92,0", "+MTATTR:1,92,0,1\r\nOK\r\n");
  check("second read returns SmokeAlarm, not a stale cache", alarm.getExpressedState() == 1);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_getexpressedstate_failed_read_returns_normal(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  bringUp(s, alarm);
  s.expect("AT+MTATTR=1,92,0", "+MTERR:3\r\nERROR\r\n"); /* no SmokeCoAlarm cluster */
  check("a failed read returns 0 (Normal)", alarm.getExpressedState() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_getexpressedstate_before_reconcile_fails_without_traffic(void) {
  MockStream s;
  MatterSmokeCOAlarm alarm;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", alarm.begin());
  check("getExpressedState() before reconcile returns 0", alarm.getExpressedState() == 0);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterSmokeCOAlarm tests =====\n");
  test_begin_declares_0x0076_variant0();
  test_rebegin_after_reconcile_refused();
  test_setsmokestate_exact_wire_pin_and_cache();
  test_setcostate_exact_wire_pin_and_cache();
  test_setbatteryalert_exact_wire_pin_and_cache();
  test_setdevicemuted_exact_wire_pin_and_cache();
  test_completeselftest_sends_field5_value0();
  test_completeselftest_always_reaches_the_wire();
  test_sethardwarefaultalert_exact_wire_pin_and_cache();
  test_setendofservicealert_exact_wire_pin_and_cache();
  test_setinterconnectsmokealarm_exact_wire_pin_and_cache();
  test_setinterconnectcoalarm_exact_wire_pin_and_cache();
  test_setcontaminationstate_exact_wire_pin_and_cache();
  test_setsmokesensitivitylevel_exact_wire_pin_and_cache();
  test_setter_noop_on_unchanged_value();
  test_setter_failed_write_leaves_cache();
  test_setter_before_reconcile_fails_without_traffic();
  test_selftest_notifyonly_fires_callback_no_reply();
  test_selftest_no_callback_registered_is_silent();
  test_selftest_wrong_cluster_does_not_fire();
  test_getexpressedstate_reads_the_wire();
  test_getexpressedstate_two_reads_both_hit_the_wire();
  test_getexpressedstate_failed_read_returns_normal();
  test_getexpressedstate_before_reconcile_fails_without_traffic();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

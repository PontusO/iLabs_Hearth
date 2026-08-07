/*
 * test_watervalve.cpp - Task C7: MatterWaterValve, the first C7 class to
 * exercise the widened +MTCMD dispatch (hasPayload/payload, though the
 * valve itself never uses the payload field) and the first class whose
 * onOpen()/onClose() verdict cannot fail the command on the wire; see
 * MatterWaterValve.h's header comment for why.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterWaterValve.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* begin() issues no AT traffic (no initial state to reconcile; see the
 * header comment), so bringing a valve up is a plain single-endpoint
 * adopt. */
static void bringUp(MockStream &s, MatterWaterValve &valve) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  valve.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0042\r\nOK\r\n");
  Matter.begin();
}

/* ===== Step 1: +MTCMD dispatch, Open/Close verdict both ways ===== */

static void test_forwarded_open_allow_sends_verdict_1(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  valve.onOpen([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,129,0"); /* Open on ep 1 */
  Hearth.poll();
  check("an allowing onOpen() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * MatterWaterValve.h's header comment, pinned: unlike the door lock, this
 * verdict cannot fail the command on the wire at all --
 * ValveConfigurationAndControl's own server calls the delegate's
 * HandleOpenValve/HandleCloseValve synchronously and discards what they
 * return (AT_MT_SPEC.md S3.19, TEMPORARY_RETURN_IGNORED). This library's
 * dispatcher still sends the AT+MTCMDRESP argument the callback returned
 * (it has no way to special-case this endpoint type), and this test proves
 * that the wire pin is unchanged either way; the "no wire failure" fact
 * itself is not something a host-side unit test can observe (it is a
 * firmware/SDK behaviour on the OTHER end of the link), so it is asserted
 * here only as a comment, matching the brief's "no-wire-failure doc
 * pinned in a comment" requirement -- the real evidence lives in
 * AT_MT_SPEC.md S3.19's own citation of valve-configuration-and-control-
 * cluster.cpp.
 */
static void test_forwarded_close_deny_sends_verdict_0(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  valve.onClose([]() { return false; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,129,1"); /* Close on ep 1 */
  Hearth.poll();
  check("a denying onClose() still answers exactly AT+MTCMDRESP=7,0 on the wire", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  /* NO-WIRE-FAILURE DOC: per AT_MT_SPEC.md S3.19, the controller sees
   * Status::Success regardless of this deny -- the SDK discards the
   * delegate's return value unconditionally (TEMPORARY_RETURN_IGNORED).
   * The deny only ever reaches valve.onClose()'s own callback, which is
   * the host-side actuation gate this test cannot observe from here. */
}

static void test_forwarded_command_no_callback_denies(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  /* no onOpen()/onClose() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,129,0");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_unrecognised_id_denies(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  valve.onOpen([]() { return true; });
  valve.onClose([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,129,9"); /* command id 9: not Open/Close */
  Hearth.poll();
  check("an unrecognised command id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  valve.onOpen([]() { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* DoorLock's cluster, not the valve's */
  Hearth.poll();
  check("the wrong cluster id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== Step 2: class behaviour ===== */

static void test_begin_declares_0x0042_variant0(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  check("declared as the water_valve device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0042);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", valve.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached state is Closed", valve.getValveState() == MatterWaterValve::kStateClosed);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  check("a second begin() after Matter.begin() is refused", !valve.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

static void test_setvalvestate_one_arg_exact_wire_pin_and_cache_update(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve); /* cache starts Closed */
  s.expect("AT+MTVALVE=1,1", "OK\r\n"); /* Open, no level */
  check("setValveState(kStateOpen) sends the exact wire pin", valve.setValveState(MatterWaterValve::kStateOpen));
  check("cache updated to Open", valve.getValveState() == MatterWaterValve::kStateOpen);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setvalvestate_two_arg_exact_wire_pin_with_level(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  s.expect("AT+MTVALVE=1,1,75", "OK\r\n");
  check("setValveState(kStateOpen, 75) sends state and level", valve.setValveState(MatterWaterValve::kStateOpen, 75));
  check("cache updated to Open", valve.getValveState() == MatterWaterValve::kStateOpen);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setvalvestate_noop_on_unchanged_one_arg_state(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve); /* cache starts Closed */
  check("setValveState(kStateClosed), already the cache, is a no-op", valve.setValveState(MatterWaterValve::kStateClosed));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* The two-arg overload always reaches the wire, even at an unchanged
 * state: a level report is meaningful even then (see the header comment,
 * there is no cached level to compare against). */
static void test_setvalvestate_two_arg_always_writes_even_at_same_state(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve); /* cache starts Closed */
  s.expect("AT+MTVALVE=1,0,50", "OK\r\n");
  check("setValveState(kStateClosed, 50) still writes", valve.setValveState(MatterWaterValve::kStateClosed, 50));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setvalvestate_failed_write_leaves_cache(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve);
  s.expect("AT+MTVALVE=1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !valve.setValveState(MatterWaterValve::kStateOpen));
  check("cache untouched (still Closed)", valve.getValveState() == MatterWaterValve::kStateClosed);
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoint 0 (not yet reconciled): setValveState() must fail without ever
 * reaching the wire, the same "endpoint 0 is unaddressable" reasoning
 * every sibling class's custom write path uses, including the error code. */
static void test_setvalvestate_before_reconcile_fails_without_traffic(void) {
  MockStream s; MatterWaterValve valve;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", valve.begin());
  check("setValveState() before reconcile fails", !valve.setValveState(MatterWaterValve::kStateOpen));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* Controller/firmware-driven change: CurrentState (cluster 129, attr 4)
 * over +MTATTR updates the cache the same way every other class's
 * URC-fed attribute does, via the generic attributeChangeCB dispatch. */
static void test_controller_attr_urc_updates_cache(void) {
  MockStream s; MatterWaterValve valve;
  bringUp(s, valve); /* cache starts Closed */
  s.injectURC("+MTATTR:1,129,4,1"); /* CurrentState -> Open */
  Hearth.poll();
  check("a controller-driven CurrentState change updates the cache", valve.getValveState() == MatterWaterValve::kStateOpen);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_attr_type_is_uint8(void) {
  MatterWaterValve valve;
  check("CurrentState is uint8", valve.hearthAttrTypeFor(129, 4) == ESP_MATTER_VAL_TYPE_UINT8);
  check("an unrelated cluster falls through to the base default", valve.hearthAttrTypeFor(6, 0) == ESP_MATTER_VAL_TYPE_INTEGER);
}

int main(void) {
  printf("\n===== MatterWaterValve tests =====\n");
  test_forwarded_open_allow_sends_verdict_1();
  test_forwarded_close_deny_sends_verdict_0();
  test_forwarded_command_no_callback_denies();
  test_forwarded_command_unrecognised_id_denies();
  test_forwarded_command_wrong_cluster_denies();
  test_begin_declares_0x0042_variant0();
  test_rebegin_after_reconcile_refused();
  test_setvalvestate_one_arg_exact_wire_pin_and_cache_update();
  test_setvalvestate_two_arg_exact_wire_pin_with_level();
  test_setvalvestate_noop_on_unchanged_one_arg_state();
  test_setvalvestate_two_arg_always_writes_even_at_same_state();
  test_setvalvestate_failed_write_leaves_cache();
  test_setvalvestate_before_reconcile_fails_without_traffic();
  test_controller_attr_urc_updates_cache();
  test_attr_type_is_uint8();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

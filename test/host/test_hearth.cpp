#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void test_version(void) {
  MockStream s;
  s.expect("AT+MTVER?", "+MTVER:0.4.0\r\nOK\r\n");
  Hearth.begin(s);
  check("firmwareVersion parses +MTVER", Hearth.firmwareVersion() == String("0.4.0"));
}

static void test_last_error_cleared_on_success(void) {
  MockStream s;
  s.expect("AT+MTBOGUS", "+MTERR:8\r\nERROR\r\n");
  s.expect("AT", "OK\r\n");
  Hearth.begin(s);
  Hearth.hearthCommand("AT+MTBOGUS");
  check("error code retained", Hearth.lastError() == 8);
  Hearth.hearthCommand("AT");
  check("a later success clears it", Hearth.lastError() == 0);
}

static void test_reboot_urc_raises_event(void) {
  MockStream s;
  Hearth.begin(s);
  hearthEvent_t got = HEARTH_LINK_DOWN;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) { got = e; seen++; });
  s.injectURC("+MTREADY");
  Hearth.poll();
  check("an unexpected +MTREADY is a reboot", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED);
}

static void test_link_up_probe(void) {
  MockStream s;
  s.expect("AT", "OK\r\n");
  Hearth.begin(s);
  check("linkUp probes with a bare AT", Hearth.linkUp());
  check("script drained", s.scriptDrained());
}

/* An armed, expected reboot: the +MTREADY that completes it must not be
 * reported as a spontaneous HEARTH_COPROCESSOR_REBOOTED, and
 * hearthExpectedRebootSeen() must record that it arrived. */
static void test_expected_reboot_consumed_silently(void) {
  MockStream s;
  Hearth.begin(s);
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t) { seen++; });
  Hearth.hearthArmExpectedReboot();
  s.injectURC("+MTREADY");
  Hearth.poll();
  check("no reboot event for the expected +MTREADY", seen == 0);
  check("hearthExpectedRebootSeen reports it", Hearth.hearthExpectedRebootSeen());
}

/* Regression test for the swallowed-reboot bug found in review: an arm that
 * is explicitly disarmed (the caller's own timeout path, e.g. after
 * AT+MTEPAPPLY never got its +MTREADY) must not still be live for the next,
 * unrelated spontaneous +MTREADY. Fails if hearthDisarmExpectedReboot() is
 * removed or stops working. */
static void test_expected_reboot_disarmed_then_spontaneous_reboot_raises(void) {
  MockStream s;
  Hearth.begin(s);
  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) { got = e; seen++; });
  Hearth.hearthArmExpectedReboot();
  Hearth.hearthDisarmExpectedReboot();
  s.injectURC("+MTREADY");
  Hearth.poll();
  check(
    "a spontaneous +MTREADY after explicit disarm is a reboot", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED
  );
}

/* Same regression, via the self-expiring deadline instead of an explicit
 * disarm: an arm nobody ever disarmed or satisfied must still not survive
 * past its timeout, or the following spontaneous reboot would be swallowed. */
static void test_expected_reboot_expires_then_spontaneous_reboot_raises(void) {
  MockStream s;
  Hearth.begin(s);
  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) { got = e; seen++; });
  Hearth.hearthArmExpectedReboot(100);  // short timeout so the test does not wait on the real default
  g_millis += 200;                      // let the arm expire
  Hearth.poll();                        // no URC pending yet; this just lets the expiry check run
  check("no event on bare expiry", seen == 0);
  s.injectURC("+MTREADY");
  Hearth.poll();
  check("a spontaneous +MTREADY after expiry is a reboot", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED);
}

/*
 * Regression test for the review-round-2 finding: the reply arrived well
 * within the deadline, sitting unread in the stream, and only the *host*
 * was late calling poll(). That must not be misreported as a spontaneous
 * reboot: the buffered +MTREADY has to be dispatched (and consume the arm)
 * before the expiry check gets a chance to clear it out from under the
 * reply that already arrived.
 */
static void test_expected_reboot_reply_already_buffered_before_late_poll(void) {
  MockStream s;
  Hearth.begin(s);
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t) { seen++; });
  Hearth.hearthArmExpectedReboot(100);
  s.injectURC("+MTREADY");  // the co-processor replied promptly...
  g_millis += 200;          // ...but the host doesn't get around to poll() until after the deadline
  Hearth.poll();
  check("no false reboot when only the host was late to poll", seen == 0);
  check("hearthExpectedRebootSeen still reports the reply arrived", Hearth.hearthExpectedRebootSeen());
}

static void test_net_three_fields_still_parses(void) {
  /* Old firmware: no mismatch field. Must behave exactly as before. */
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTNET?", "+MTNET:WIFI,1,1\r\nOK\r\n");
  check("3-field isWiFiConnected", Matter.isWiFiConnected());
  ms.expect("AT+MTNET?", "+MTNET:WIFI,1,1\r\nOK\r\n");
  check("3-field no mismatch reported", !Hearth.transportMismatch());
  check("script drained", ms.scriptDrained());
}

static void test_net_four_fields_mismatch(void) {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTNET?", "+MTNET:THREAD,1,0,1\r\nOK\r\n");
  check("4-field mismatch seen", Hearth.transportMismatch());
  ms.expect("AT+MTNET?", "+MTNET:THREAD,1,0,1\r\nOK\r\n");
  check("4-field connected still parses", !Matter.isThreadConnected());
  ms.expect("AT+MTNET?", "+MTNET:THREAD,1,1,0\r\nOK\r\n");
  check("4-field connected positive", Matter.isThreadConnected());
  ms.expect("AT+MTNET?", "+MTNET:THREAD,1,1,0\r\nOK\r\n");
  check("4-field mismatch clear", !Hearth.transportMismatch());
  check("script drained", ms.scriptDrained());
}

static void test_evt27_raises_link_event(void) {
  MockStream ms;
  Hearth.begin(ms);
  static int gotHearthEvt;
  static int gotMatterEvt;
  gotHearthEvt = -1;
  gotMatterEvt = 0;
  Hearth.onLinkEvent([](hearthEvent_t e) { gotHearthEvt = (int)e; });
  Matter.onEvent([](matterEvent_t, const chip::DeviceLayer::ChipDeviceEvent *) { gotMatterEvt++; });
  ms.injectURC("+MTEVT:27");
  Hearth.poll();
  check("evt27 raised HEARTH_TRANSPORT_MISMATCH",
        gotHearthEvt == (int)HEARTH_TRANSPORT_MISMATCH);
  check("evt27 did not hit the parity callback", gotMatterEvt == 0);
  Matter.onEvent(nullptr);
  Hearth.onLinkEvent(nullptr);
}

/*
 * Task 4 (0.11.0): bit 28 gained real meaning (MT_EVT_THREAD_ROLE_CHANGED,
 * AT_MT_SPEC.md S3.27), so it is no longer simply reserved -- but it still
 * never reaches onLinkEvent() or Matter.onEvent(): it routes only to
 * Hearth.onThreadRoleChange(), test_thread.cpp's own suite. This test
 * narrows to the one case that still IS silently dropped by both of those:
 * a payload-less "+MTEVT:28" line, which carries no role token for
 * hearthDispatchThreadRoleEvt() to decode and is treated the same as any
 * other malformed URC in this file.
 */
static void test_evt28_malformed_no_payload_still_dropped(void) {
  MockStream ms;
  Hearth.begin(ms);
  static int gotAny;
  gotAny = 0;
  Hearth.onLinkEvent([](hearthEvent_t) { gotAny++; });
  Matter.onEvent([](matterEvent_t, const chip::DeviceLayer::ChipDeviceEvent *) { gotAny++; });
  ms.injectURC("+MTEVT:28");
  Hearth.poll();
  check("a payload-less evt28 still reaches neither onLinkEvent nor Matter.onEvent", gotAny == 0);
  Matter.onEvent(nullptr);
  Hearth.onLinkEvent(nullptr);
}

static void test_evt27_reaches_link_even_when_matter_unregistered(void) {
  MockStream ms;
  Hearth.begin(ms);
  static int gotHearthEvt;
  gotHearthEvt = -1;
  Hearth.onLinkEvent([](hearthEvent_t e) { gotHearthEvt = (int)e; });
  /* Leave Matter.onEvent unregistered (nullptr) to ensure bit 27 is
   * special-cased BEFORE the !ArduinoMatter::_matterEventCB check. */
  ms.injectURC("+MTEVT:27");
  Hearth.poll();
  check("evt27 raised HEARTH_TRANSPORT_MISMATCH with no Matter.onEvent",
        gotHearthEvt == (int)HEARTH_TRANSPORT_MISMATCH);
  Hearth.onLinkEvent(nullptr);
}

/* Pins what decommission() claims to do in its doc comment: it removes the
 * device from its fabric by sending AT+MTRESET, the firmware's Matter
 * reset, and depends on that command's erasure of fabrics and credentials
 * rather than on any side effect of a reboot. */
static void test_decommission_sends_matter_reset() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTRESET", "OK\r\n");
  Matter.decommission();
  check("decommission sent AT+MTRESET", ms.scriptDrained());
  check("nothing unexpected", ms.unexpected().empty());
}

/* AT+MTSTATE? (S3.3): the three states parse, and the fabric count is
 * carried through. Mirrors the isDeviceCommissioned/isWiFiConnected style
 * of pinning a query against a scripted +MT line. */
static void test_state_parses_all_three(void) {
  MockStream ms;
  Hearth.begin(ms);
  MatterDeviceState st = MATTER_STATE_UNINITIALIZED;
  unsigned int fabrics = 99;

  ms.expect("AT+MTSTATE?", "+MTSTATE:0,0\r\nOK\r\n");
  check("uninitialized query ok", Matter.deviceState(&st, &fabrics));
  check("state 0 uninitialized", st == MATTER_STATE_UNINITIALIZED);
  check("0 fabrics", fabrics == 0);

  ms.expect("AT+MTSTATE?", "+MTSTATE:1,0\r\nOK\r\n");
  check("commissioning query ok", Matter.deviceState(&st, &fabrics));
  check("state 1 commissioning", st == MATTER_STATE_COMMISSIONING);

  ms.expect("AT+MTSTATE?", "+MTSTATE:2,3\r\nOK\r\n");
  check("operational query ok", Matter.deviceState(&st, &fabrics));
  check("state 2 operational", st == MATTER_STATE_OPERATIONAL);
  check("3 fabrics carried through", fabrics == 3);
  check("script drained", ms.scriptDrained());
}

/* A null fabrics pointer is allowed: the state alone is the common case. */
static void test_state_null_fabrics_ok(void) {
  MockStream ms;
  Hearth.begin(ms);
  MatterDeviceState st = MATTER_STATE_UNINITIALIZED;
  ms.expect("AT+MTSTATE?", "+MTSTATE:2,1\r\nOK\r\n");
  check("null fabrics query ok", Matter.deviceState(&st, nullptr));
  check("state read with null fabrics", st == MATTER_STATE_OPERATIONAL);
  check("script drained", ms.scriptDrained());
}

/* Fail-closed: an OK with no +MTSTATE line must return false, not silently
 * report state 0, the same guard hearthQueryFabricCount() documents. */
static void test_state_missing_line_fails(void) {
  MockStream ms;
  Hearth.begin(ms);
  MatterDeviceState st = MATTER_STATE_OPERATIONAL;
  ms.expect("AT+MTSTATE?", "OK\r\n");
  check("missing +MTSTATE line returns false", !Matter.deviceState(&st, nullptr));
  check("script drained", ms.scriptDrained());
}

/* AT+MTCOMMISSION (S3.5): timeout 0 sends the bare exec form; a value in
 * range sends the set form verbatim; out-of-range values are clamped to the
 * 180..900 window CHIP enforces before hitting the wire. */
static void test_commission_window_forms(void) {
  MockStream ms;
  Hearth.begin(ms);

  ms.expect("AT+MTCOMMISSION", "OK\r\n");
  check("default timeout sends bare exec form", Matter.openCommissioningWindow(0));

  ms.expect("AT+MTCOMMISSION=600", "OK\r\n");
  check("in-range timeout sends set form", Matter.openCommissioningWindow(600));

  ms.expect("AT+MTCOMMISSION=180", "OK\r\n");
  check("below-floor clamped up to 180", Matter.openCommissioningWindow(30));

  ms.expect("AT+MTCOMMISSION=900", "OK\r\n");
  check("above-ceiling clamped down to 900", Matter.openCommissioningWindow(1200));

  check("script drained", ms.scriptDrained());
  check("nothing unexpected", ms.unexpected().empty());
}

/* An ERROR reply is a failure, not a silent success. */
static void test_commission_window_error_fails(void) {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTCOMMISSION=600", "+MTERR:1\r\nERROR\r\n");
  check("ERROR reply returns false", !Matter.openCommissioningWindow(600));
  check("script drained", ms.scriptDrained());
}

int main(void) {
  printf("\n===== Hearth link tests =====\n");
  test_version();
  test_last_error_cleared_on_success();
  test_reboot_urc_raises_event();
  test_link_up_probe();
  test_expected_reboot_consumed_silently();
  test_expected_reboot_disarmed_then_spontaneous_reboot_raises();
  test_expected_reboot_expires_then_spontaneous_reboot_raises();
  test_expected_reboot_reply_already_buffered_before_late_poll();
  test_net_three_fields_still_parses();
  test_net_four_fields_mismatch();
  test_evt27_raises_link_event();
  test_evt28_malformed_no_payload_still_dropped();
  test_evt27_reaches_link_even_when_matter_unregistered();
  test_decommission_sends_matter_reset();
  test_state_parses_all_three();
  test_state_null_fabrics_ok();
  test_state_missing_line_fails();
  test_commission_window_forms();
  test_commission_window_error_fails();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

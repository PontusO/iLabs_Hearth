/*
 * test_thread.cpp - Task 4 (Thread role API, 0.11.0): Hearth.threadInfo(),
 * Hearth.threadRole(), Hearth.onThreadRoleChange() and hearthThreadRoleName(),
 * against AT_MT_SPEC.md S3.27 (AT+MTTHREAD?) and event bit 28, read from the
 * firmware repo, not from the plan's own summary.
 *
 * Two read paths, deliberately different costs, both pinned here:
 * threadInfo() is a full round trip that fills every field including the
 * has* flags for the wire's nullable ones; threadRole() is a cached read
 * with NO wire traffic at all, seeded to HEARTH_THREAD_UNSPECIFIED and
 * refreshed only by an incoming +MTEVT:28.
 *
 * HearthThreadEvtProbe proves bit 28 does not leak into any other dispatch
 * path: its attributeChangeCB/hearthOnForwardedCommandFields overrides
 * (the +MTATTR/+MTCMD targets) must never fire for a +MTEVT:28 line, and
 * neither must Hearth.onLinkEvent() or Matter.onEvent() (the +MTEVT:27 /
 * matterEvent_t paths), the same probe shape test_cmdfields.cpp's
 * FieldsProbe uses for the same kind of proof.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

class HearthThreadEvtProbe : public MatterEndPoint {
public:
  int attrCalls = 0;
  int cmdCalls = 0;

  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    attrCalls++;
    return true;
  }
  bool hearthOnForwardedCommandFields(uint32_t, uint32_t, const HearthCmdFields &) override {
    cmdCalls++;
    return true;
  }
};

/* hearthThreadRoleName(): the display helper, all seven named tokens, the
 * review-round HEARTH_THREAD_UNKNOWN sentinel, and a truly out-of-range
 * cast (neither a named role nor the sentinel itself), which also falls
 * back to "UNKNOWN" rather than crashing or misreporting. */
static void test_role_name_strings(void) {
  check("UNSPECIFIED", strcmp(hearthThreadRoleName(HEARTH_THREAD_UNSPECIFIED), "UNSPECIFIED") == 0);
  check("UNASSIGNED", strcmp(hearthThreadRoleName(HEARTH_THREAD_UNASSIGNED), "UNASSIGNED") == 0);
  check("SLEEPY_END_DEVICE", strcmp(hearthThreadRoleName(HEARTH_THREAD_SLEEPY_END_DEVICE), "SLEEPY_END_DEVICE") == 0);
  check("END_DEVICE", strcmp(hearthThreadRoleName(HEARTH_THREAD_END_DEVICE), "END_DEVICE") == 0);
  check("REED", strcmp(hearthThreadRoleName(HEARTH_THREAD_REED), "REED") == 0);
  check("ROUTER", strcmp(hearthThreadRoleName(HEARTH_THREAD_ROUTER), "ROUTER") == 0);
  check("LEADER", strcmp(hearthThreadRoleName(HEARTH_THREAD_LEADER), "LEADER") == 0);
  check("UNKNOWN sentinel", strcmp(hearthThreadRoleName(HEARTH_THREAD_UNKNOWN), "UNKNOWN") == 0);
  check("a truly out-of-range value also falls back to UNKNOWN's name", strcmp(hearthThreadRoleName((HearthThreadRole)99), "UNKNOWN") == 0);
}

/* The exact wire pin for the query itself: no set form, no extra traffic. */
static void test_threadinfo_sends_bare_query(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"MyThreadNet\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("script drained (AT+MTTHREAD? only, no set form)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Spec's own worked example: an attached router. Every field, every has*
 * flag, and the cache sync (threadRole() reflects it with no further wire
 * traffic afterwards). */
static void test_threadinfo_attached_line(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"MyThreadNet\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("role decoded ROUTER", info.role == HEARTH_THREAD_ROUTER);
  check("attached true", info.attached == true);
  check("hasChannel true, channel 15", info.hasChannel && info.channel == 15);
  check("hasPanId true, panId 0xFA25", info.hasPanId && info.panId == 0xFA25);
  check(
    "hasExtPanId true, extPanId 0x000DB01A5C3F2E10",
    info.hasExtPanId && info.extPanId == 0x000DB01A5C3F2E10ULL
  );
  check("hasPartitionId true, partitionId 0x00003A21", info.hasPartitionId && info.partitionId == 0x00003A21UL);
  check("name decoded", strcmp(info.name, "MyThreadNet") == 0);
  check("threadRole cache synced by threadInfo, no further wire traffic", Hearth.threadRole() == HEARTH_THREAD_ROUTER);
}

/* Spec's own worked example: a detached device with no dataset. Every
 * nullable field's has* flag must come back false -- NOT a silently
 * zeroed value indistinguishable from a real one. */
static void test_threadinfo_detached_line(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:UNASSIGNED,0,,,,,\"\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("role decoded UNASSIGNED", info.role == HEARTH_THREAD_UNASSIGNED);
  check("attached false", info.attached == false);
  check("hasChannel false (null, not zero)", !info.hasChannel);
  check("hasPanId false (null, not zero)", !info.hasPanId);
  check("hasExtPanId false (null, not zero)", !info.hasExtPanId);
  check("hasPartitionId false (null, not zero)", !info.hasPartitionId);
  check("name empty string", strcmp(info.name, "") == 0);
}

/* A name containing a comma: the name is the LAST field and quoted, so a
 * parser that (incorrectly) split on every comma would corrupt this. */
static void test_threadinfo_name_with_comma(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"My,Net\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("name with an embedded comma survives intact", strcmp(info.name, "My,Net") == 0);
}

/* A name containing an escaped quote: S3.27's quoting rule, '"' -> '\"'.
 * The sketch never meets the escaping; it gets a plain unescaped C string. */
static void test_threadinfo_name_with_escaped_quote(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect(
    "AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"Bob\\\"s Net\"\r\nOK\r\n"
  );
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("escaped quote unescaped to a plain \"", strcmp(info.name, "Bob\"s Net") == 0);
}

/* A name containing a literal backslash: S3.27's OTHER quoting rule,
 * '\' escaped as '\\'. Not exercised by the escaped-quote test above (same
 * unescape branch, but review-round finding: the spec states both escapes
 * as normative, so both need their own wire pin, not just the more
 * memorable one). */
static void test_threadinfo_name_with_literal_backslash(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect(
    "AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"C:\\\\Thread\"\r\nOK\r\n"
  );
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("escaped backslash unescaped to a plain \\", strcmp(info.name, "C:\\Thread") == 0);
}

/* A name at Thread's exact 16-byte limit: the 17-byte buffer (16 + NUL)
 * must hold it whole, not truncate one short. */
static void test_threadinfo_name_at_16_byte_limit(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect(
    "AT+MTTHREAD?", "+MTTHREAD:ROUTER,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"ABCDEFGHIJKLMNOP\"\r\nOK\r\n"
  );
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("16-byte name held whole", strcmp(info.name, "ABCDEFGHIJKLMNOP") == 0);
  check("name field is exactly 16 chars", strlen(info.name) == 16);
}

/* An unknown role number (S3.27: a future SDK addition renders as the raw
 * decimal instead of a token). Review-round finding: collapsing this onto
 * HEARTH_THREAD_UNSPECIFIED would misreport a device that is up, attached
 * and running a role this library predates as "the Thread interface is
 * down" -- exactly the lie the wire's own decimal degrade
 * (design spec S2.1: "degrades to a number rather than a lie") exists to
 * avoid. It must not crash and must not mis-map onto ANY named real role
 * either: it degrades to the dedicated HEARTH_THREAD_UNKNOWN sentinel
 * (255, chosen so it can never collide with a future RoutingRoleEnum
 * value). Every other field on the same line must still parse correctly,
 * proving the unfamiliar role token does not derail the rest of the
 * parse. */
static void test_threadinfo_unknown_role_number(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:99,1,15,0xFA25,0x000DB01A5C3F2E10,0x00003A21,\"Foo\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo still succeeds (does not crash)", Hearth.threadInfo(info));
  check("unrecognized role degrades to the UNKNOWN sentinel, not UNSPECIFIED and not a mis-mapped real role", info.role == HEARTH_THREAD_UNKNOWN);
  check("attached still parsed correctly", info.attached == true);
  check("channel still parsed correctly", info.hasChannel && info.channel == 15);
  check("name still parsed correctly", strcmp(info.name, "Foo") == 0);
}

/* The other half of the same review-round finding: a literal "UNSPECIFIED"
 * wire token -- the device's own honest "the Thread interface is down" --
 * must still decode to HEARTH_THREAD_UNSPECIFIED, provably distinct from
 * the unrecognized-token case above, which decodes to
 * HEARTH_THREAD_UNKNOWN. If these two ever collapsed onto the same value
 * again, this test and the one above could not both pass. */
static void test_threadinfo_literal_unspecified_token_stays_unspecified(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTTHREAD:UNSPECIFIED,0,,,,,\"\"\r\nOK\r\n");
  HearthThreadInfo info;
  check("threadInfo succeeds", Hearth.threadInfo(info));
  check("a literal UNSPECIFIED token decodes to HEARTH_THREAD_UNSPECIFIED, not UNKNOWN", info.role == HEARTH_THREAD_UNSPECIFIED);
  check("distinct from the unrecognized-token case", HEARTH_THREAD_UNSPECIFIED != HEARTH_THREAD_UNKNOWN);
}

/* +MTERR:8 (design point 4): on a WiFi image (or the combined image booted
 * in WiFi mode) threadInfo() fails, lastError() is 8, and threadRole()
 * reports HEARTH_THREAD_UNSPECIFIED. */
static void test_threadinfo_wifi_image_unsupported(void) {
  MockStream s;
  Hearth.begin(s);
  s.expect("AT+MTTHREAD?", "+MTERR:8\r\nERROR\r\n");
  HearthThreadInfo info;
  check("threadInfo returns false on a WiFi image", !Hearth.threadInfo(info));
  check("lastError is 8 (HEARTH_ERR_NOT_SUPPORTED)", Hearth.lastError() == HEARTH_ERR_NOT_SUPPORTED);
  check("threadRole reports unspecified", Hearth.threadRole() == HEARTH_THREAD_UNSPECIFIED);
  check("script drained", s.scriptDrained());
}

/* threadRole(): seeded on first use (a fresh begin() gives
 * HEARTH_THREAD_UNSPECIFIED with zero wire traffic, not garbage and not a
 * query), and it never touches the wire on its own -- the whole point of
 * the second, cheaper read path. */
static void test_threadrole_seeded_no_wire(void) {
  MockStream s;
  Hearth.begin(s);
  check("seeded UNSPECIFIED before any query or event", Hearth.threadRole() == HEARTH_THREAD_UNSPECIFIED);
  check("no wire traffic from threadRole() itself", s.scriptDrained() && s.unexpected().empty());
}

/* threadRole() is refreshed by every +MTEVT:28, with no wire traffic of its
 * own for the read. */
static void test_threadrole_refreshed_by_evt28(void) {
  MockStream s;
  Hearth.begin(s);
  check("seeded UNSPECIFIED", Hearth.threadRole() == HEARTH_THREAD_UNSPECIFIED);
  s.injectURC("+MTEVT:28,ROUTER");
  Hearth.poll();
  check("cache refreshed by +MTEVT:28", Hearth.threadRole() == HEARTH_THREAD_ROUTER);
  check("threadRole() read itself put nothing on the wire", s.unexpected().empty());
}

/* onThreadRoleChange(): the plain-function-pointer callback fires once per
 * +MTEVT:28 with the decoded token, and the cache is updated alongside it. */
static void test_onthreadrolechange_fires(void) {
  MockStream s;
  Hearth.begin(s);
  static HearthThreadRole got;
  static int calls;
  got = HEARTH_THREAD_UNSPECIFIED;
  calls = 0;
  Hearth.onThreadRoleChange([](HearthThreadRole r) {
    got = r;
    calls++;
  });
  s.injectURC("+MTEVT:28,LEADER");
  Hearth.poll();
  check("callback fired exactly once", calls == 1);
  check("callback received the decoded role", got == HEARTH_THREAD_LEADER);
  check("cache also updated", Hearth.threadRole() == HEARTH_THREAD_LEADER);
  Hearth.onThreadRoleChange(nullptr);
}

/* Design point 2: the callback runs inside URC dispatch, so a wire write
 * from within it (even indirectly, via threadInfo() or any other
 * Hearth.hearthCommand()) is refused HEARTH_CMD_REENTRANT, exactly the same
 * rule every other URC-dispatched callback in this library carries. */
static void test_callback_wire_write_refused_reentrant(void) {
  MockStream s;
  Hearth.begin(s);
  static int rc;
  rc = 0;
  Hearth.onThreadRoleChange([](HearthThreadRole) {
    rc = Hearth.hearthCommand("AT");
  });
  s.injectURC("+MTEVT:28,ROUTER");
  Hearth.poll();
  check("a wire write from inside the callback is refused", rc == HEARTH_CMD_REENTRANT);
  check("nothing reached the wire", s.unexpected().empty());
  Hearth.onThreadRoleChange(nullptr);
}

/* +MTEVT:28 with no payload (malformed: the generic numeric <detail> this
 * bit does NOT carry) is dropped exactly like any other malformed URC in
 * this codebase -- no callback, no cache change, no crash. */
static void test_evt28_malformed_no_payload_dropped(void) {
  MockStream s;
  Hearth.begin(s);
  static int calls;
  calls = 0;
  Hearth.onThreadRoleChange([](HearthThreadRole) { calls++; });
  HearthThreadRole before = Hearth.threadRole();
  s.injectURC("+MTEVT:28");
  Hearth.poll();
  check("no callback for a payload-less bit 28", calls == 0);
  check("cache unchanged", Hearth.threadRole() == before);
  Hearth.onThreadRoleChange(nullptr);
}

/* The probe: bit 28 must not leak into +MTATTR/+MTCMD dispatch (a declared
 * endpoint's callbacks), nor into the generic link-event or matterEvent_t
 * paths (+MTEVT:27's own route, and the numeric <detail> table every other
 * bit uses) -- only the thread-role callback may fire. */
static void test_evt28_does_not_touch_other_dispatch_paths(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  HearthThreadEvtProbe ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0100);
  ep.setEndPointId(1);
  Hearth.begin(s);

  static int gotHearthEvt;
  static int gotMatterEvt;
  gotHearthEvt = 0;
  gotMatterEvt = 0;
  Hearth.onLinkEvent([](hearthEvent_t) { gotHearthEvt++; });
  Matter.onEvent([](matterEvent_t, const chip::DeviceLayer::ChipDeviceEvent *) { gotMatterEvt++; });

  static HearthThreadRole got;
  got = HEARTH_THREAD_UNSPECIFIED;
  Hearth.onThreadRoleChange([](HearthThreadRole r) { got = r; });

  s.injectURC("+MTEVT:28,ROUTER");
  Hearth.poll();

  check("the thread-role callback fired with the decoded role", got == HEARTH_THREAD_ROUTER);
  check("attributeChangeCB never called", ep.attrCalls == 0);
  check("hearthOnForwardedCommandFields never called", ep.cmdCalls == 0);
  check("onLinkEvent never called", gotHearthEvt == 0);
  check("Matter.onEvent never called", gotMatterEvt == 0);
  check("no wire traffic", s.unexpected().empty());

  Hearth.onLinkEvent(nullptr);
  Matter.onEvent(nullptr);
  Hearth.onThreadRoleChange(nullptr);
  MatterEndPoint::hearthClearDeclarations();
}

int main(void) {
  printf("\n===== Hearth Thread role tests =====\n");
  test_role_name_strings();
  test_threadinfo_sends_bare_query();
  test_threadinfo_attached_line();
  test_threadinfo_detached_line();
  test_threadinfo_name_with_comma();
  test_threadinfo_name_with_escaped_quote();
  test_threadinfo_name_with_literal_backslash();
  test_threadinfo_name_at_16_byte_limit();
  test_threadinfo_unknown_role_number();
  test_threadinfo_literal_unspecified_token_stays_unspecified();
  test_threadinfo_wifi_image_unsupported();
  test_threadrole_seeded_no_wire();
  test_threadrole_refreshed_by_evt28();
  test_onthreadrolechange_fires();
  test_callback_wire_write_refused_reentrant();
  test_evt28_malformed_no_payload_dropped();
  test_evt28_does_not_touch_other_dispatch_paths();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

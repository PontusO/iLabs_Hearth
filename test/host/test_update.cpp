/* test/host/test_update.cpp: Hearth.update, the link route, begin() and the
 * URC demultiplexing (plan Task 4, first half: cases 1, 2, 3, 4, 6a, 6c, 6e,
 * 9 of the plan's list). The second half (the download loop, the states'
 * actions, the verdict, consent and the baud switch) appends to this file in
 * its own dispatch.
 *
 * The co-processor is scripted the way test_hearthlink.cpp does it: a
 * MockStream queues each command the library must send and the bytes to hand
 * back, and injectURC() queues an unsolicited line. HearthFsMem is attached
 * through hearthAttach(). The good.ota fixture is the "image the provider
 * serves" for the second half; this half stops before the download starts,
 * so no bundle is parsed yet. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "HearthUpdate.h"
#include "HearthFsMem.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static int g_statusCalls = 0;
static HearthUpdateStateEnum g_lastState = HEARTH_UPDATE_UNAVAILABLE;
static void onStatus(const HearthUpdateStatus &st) {
  g_statusCalls++;
  g_lastState = st.state;
}

/* A fake filesystem whose begin() refuses (the "no FS" board layout). */
struct FakeFsNoBegin : HearthFs {
  bool begin() override { return false; }
  HearthFile *open(const char *, const char *) override { return 0; }
  bool exists(const char *) override { return false; }
  bool remove(const char *) override { return false; }
  bool rename(const char *, const char *) override { return false; }
  bool mkdir(const char *) override { return false; }
  uint32_t freeBytes() override { return 0; }
  uint32_t totalBytes() override { return 0; }
};

/* Case 1: begin() on a fake fs whose begin() returns false: returns false,
 * HEARTH_UPDATE_ERR_NO_FS, and no AT command was sent. */
static void test_begin_no_fs(void) {
  MockStream s;
  HearthClass hearth;
  FakeFsNoBegin noFs;
  hearth.begin(s);
  hearth.update.hearthAttach(noFs);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin fails on a no-FS layout", !ok);
  check("error is HEARTH_UPDATE_ERR_NO_FS",
        hearth.update.status().error == HEARTH_UPDATE_ERR_NO_FS);
  check("no AT command was sent", s.unexpected().empty() && s.scriptDrained());
}

/* Case 2: begin(0x10400, "1.4.0") against the full scripted handshake. */
static void test_begin_ok(void) {
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin returns true", ok);
  check("state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("effectiveVersion is the baseline",
        hearth.update.status().effectiveVersion == 0x10400);
  check("the manifest is untouched (none written yet)", !fs.exists("/hearth/manifest"));
  check("every scripted command was sent", s.scriptDrained());
  check("nothing unexpected on the wire", s.unexpected().empty());
}

/* Case 3: AT+MTOTA=1 answers +MTERR:8: the image has no requestor. begin()
 * still returns true, the state is UNAVAILABLE, available() is false and no
 * further command was sent. */
static void test_begin_unavailable(void) {
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "+MTERR:8\r\nERROR\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin returns true on +MTERR:8 (not an error)", ok);
  check("state is UNAVAILABLE", hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("available() is false", !hearth.update.available());
  check("no further command was sent", s.scriptDrained() && s.unexpected().empty());
}

/* Case 4: the manifest wins. A manifest with productVersion 0x10500 present
 * before begin(0x10400, ...) makes the effective version 0x10500, so the
 * AT+MTSWVER that goes out carries 66816 and the manifest's own string. */
static void test_manifest_wins(void) {
  HearthFsMem fs;
  HearthUpdateStage stage;
  check("stage begin for seeding", stage.begin(fs));
  HearthManifest m;
  m.productVersion = 0x10500;
  strcpy(m.productVersionString, "1.5.0");
  m.hostVersion[0] = 0;
  check("seed manifest written", stage.saveManifest(m));

  MockStream s;
  s.expect("AT+MTSWVER=66816,\"1.5.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin returns true", ok);
  check("AT+MTSWVER carried the manifest's version (66816, 0x10500)", s.scriptDrained());
  check("effectiveVersion is the manifest's", hearth.update.status().effectiveVersion == 0x10500);
  check("nothing unexpected on the wire", s.unexpected().empty());
}

/* Case 6a: the query answer is not a URC (the revision's defect). The
 * +MTOTA:0,IDLE,0,wifi line of AT+MTOTA? must reach the command's own onLine
 * (the variant is cached as wifi), while a genuine +MTOTA:IDLE URC in the
 * same window goes to hearthOnOtaLine. With the digit test missing from
 * isAsyncURC(), the query's answer is misrouted as a URC and the command
 * times out (g_yieldAdvanceMs keeps that a failure instead of a hang); with
 * it missing from hearthOnURCLine(), the URC is never parsed. */
static void test_query_answer_not_a_urc(void) {
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  /* The URC lands ahead of the query's own answer, inside its window. */
  s.expect("AT+MTOTA?", "+MTOTA:IDLE\r\n+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_statusCalls = 0;
  g_lastState = HEARTH_UPDATE_UNAVAILABLE;
  hearth.update.onStatus(onStatus);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin returns true (the answer was claimed by the command)", ok);
  check("the URC reached hearthOnOtaLine (state IDLE after it)",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("no command was swallowed by the URC route",
        s.scriptDrained() && s.unexpected().empty());
  check("the URC fired onStatus (it was parsed, not dropped)", g_statusCalls >= 1);
}

/* Case 6c: checkNow() sends AT+MTOTA=2; an +MTERR:12 answer (disabled, busy
 * or no provider) returns false and changes nothing. */
static void test_check_now_busy(void) {
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  s.expect("AT+MTOTA=2", "+MTERR:12\r\nERROR\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  check("begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  HearthUpdateStatus before = hearth.update.status();
  bool rc = hearth.update.checkNow();
  check("checkNow returns false on +MTERR:12", !rc);
  check("the state changed nothing", hearth.update.status().state == before.state);
  check("the version and error changed nothing",
        hearth.update.status().effectiveVersion == before.effectiveVersion
        && hearth.update.status().error == before.error);
  check("no further command was sent", s.scriptDrained() && s.unexpected().empty());
}

/* Case 6e: ERR_NO_SPACE. A fake fs whose totalBytes() is 4 MiB on a mock
 * answering ESP32-C6 Hearth: begin() returns false with
 * HEARTH_UPDATE_ERR_NO_SPACE, and AT+MTOTA=1 was never sent. The same fs on
 * an MGM240P Hearth (3.5 MiB need) begins normally. */
static void test_no_space(void) {
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  fs.setFreeLimit(4 * 1024 * 1024);
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  bool ok = hearth.update.begin(0x10400, "1.4.0");
  check("begin fails with HEARTH_UPDATE_ERR_NO_SPACE on the C6", !ok
        && hearth.update.status().error == HEARTH_UPDATE_ERR_NO_SPACE);
  check("AT+MTOTA=1 was never sent (the DE625 check stopped it after CGMM)",
        s.unexpected().empty() && s.scriptDrained());

  /* The same fs size, a co-processor whose need is 3.5 MiB: it fits. A
   * fresh MockStream and HearthClass: the first begin left the stream's
   * script queue with two unconsumed expectations (the DE625 check stopped
   * it after AT+CGMM), and a second HearthClass on the same stream would
   * desync the queue. */
  MockStream s2;
  s2.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s2.expect("AT+CGMM", "MGM240P Hearth\r\nOK\r\n");
  s2.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s2.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,combined\r\nOK\r\n");
  s2.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth2;
  HearthFsMem fs2;
  fs2.setFreeLimit(4 * 1024 * 1024);
  hearth2.begin(s2);
  hearth2.update.hearthAttach(fs2);
  ok = hearth2.update.begin(0x10400, "1.4.0");
  check("the same 4 MiB fs begins normally on an MGM240P Hearth", ok
        && hearth2.update.status().state == HEARTH_UPDATE_IDLE);
  check("nothing unexpected on the wire", s2.unexpected().empty() && s2.scriptDrained());
}

/* Case 9: +MTOTA:BLOCK while the update is disabled (never began) is
 * ignored: no state change, no status callback, nothing on the wire. */
static void test_block_while_disabled_ignored(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_statusCalls = 0;
  g_lastState = HEARTH_UPDATE_UNAVAILABLE;
  hearth.update.onStatus(onStatus);
  s.injectURC("+MTOTA:BLOCK,0,1024");
  hearth.poll();
  check("the state is still DISABLED", hearth.update.status().state == HEARTH_UPDATE_DISABLED);
  check("no status callback fired", g_statusCalls == 0);
  check("nothing went on the wire", s.unexpected().empty());
}

/* The disabled default: before begin() the state is DISABLED, available() is
 * false and there is no error. */
static void test_default_disabled(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  check("the default state is DISABLED", hearth.update.status().state == HEARTH_UPDATE_DISABLED);
  check("available() is false while disabled", !hearth.update.available());
  check("there is no error while disabled", hearth.update.status().error == HEARTH_UPDATE_OK);
}

int main(void) {
  printf("\n===== HearthUpdate (task 4a) tests =====\n");
  test_begin_no_fs();
  test_begin_ok();
  test_begin_unavailable();
  test_manifest_wins();
  test_query_answer_not_a_urc();
  test_check_now_busy();
  test_no_space();
  test_block_while_disabled_ignored();
  test_default_disabled();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

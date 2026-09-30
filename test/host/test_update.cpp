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
#include "UpdateHarness.h"

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

/*
 * The download loop (plan Task 4b1: case 5's download half, case 6's
 * retry/timeout/mterr rules, and the re-entry guard). The co-processor is
 * scripted the way the cases above do it; the block lines are generated
 * from the good.ota fixture the way the co-processor would send them:
 * ceil(len/96) +MTOTABLK:<seq>,<off>,<hex> lines per block, the hex
 * upper-case, the last line of a short block shorter.
 *
 * The fake clock: every call that may wait for a reply the mock does not
 * script (the timeout case, or a script this loop does not match) must run
 * with g_yieldAdvanceMs = 50, back to 0 after, or the wait loop spins on a
 * clock nothing advances. The existing precedent is
 * test_composition_parent.cpp, which does the same around its Matter.begin()
 * calls.
 */
/* The baud hook and its record, forward-declared for the first-half tests;
 * defined with the second-half hooks below. */
static int g_baudCalls = 0;
static uint32_t g_lastBaud = 0;
static void baudHook(uint32_t b);

/* Case 5, the download half: the whole fixture, block by block, ends in the
 * staged file byte for byte and _downloadComplete set, with no
 * AT+MTOTASTAGED yet (4b2 sends it). */
static void test_download_loop(void) {
  std::string fx;
  check("the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  const char *stagedPath = "/hearth/staged.ota";
  size_t pos = 0;
  uint32_t seq = 0;
  /* The co-processor's state lines: AVAILABLE with the offered version,
   * then DOWNLOADING at 0 percent before the first block. The drain on
   * AVAILABLE also sends the download-baud switch (4b2 case 10). */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  hearth.poll();
  check("the state is DOWNLOADING before the first block",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);
  while (pos < fx.size()) {
    size_t blen = fx.size() - pos;
    if (blen > 1024) blen = 1024;
    /* The poll that dispatches the BLOCK URC is the same poll that sends
     * the GET, so the GET's response must be scripted before the URC is
     * injected. The co-processor answers the previous ACK before
     * announcing the next block; block 0 follows the DOWNLOADING state
     * line the test feeds before this loop. */
    s.expect("AT+MTOTAGET=" + std::to_string((unsigned)seq),
             blkAnswer(seq, (uint32_t)blen, fx, pos));
    s.expect("AT+MTOTAACK=" + std::to_string((unsigned)seq), "OK\r\n");
    s.injectURC("+MTOTA:BLOCK," + std::to_string((unsigned)seq) + ","
                + std::to_string((unsigned)blen));
    hearth.poll();
    pos += blen;
    seq++;
  }
  check("every block pulled and acknowledged", s.scriptDrained() && s.unexpected().empty());
  check("the state is DOWNLOADING while the last block is pulled",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);
  check("no staged file before DOWNLOADED (the write ends on it)",
        !fs.exists(stagedPath));
  check("no download-complete flag yet", !hearth.update.hearthDownloadComplete());

  /* The drain on DOWNLOADED (4b2) restores the link baud, verifies the
   * bundle and gives the verdict. With no consent hook installed the
   * verdict is accepted: AT+MTOTASTAGED=1, state WAIT_APPLY. */
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTASTAGED=1", "OK\r\n");
  s.injectURC("+MTOTA:DOWNLOADED");
  hearth.poll();
  check("the staged file exists after the drain of DOWNLOADED", fs.exists(stagedPath));
  check("_downloadComplete is set", hearth.update.hearthDownloadComplete());
  check("the staged file is the fixture byte for byte",
        fs.files.count(stagedPath) == 1
        && fs.files[stagedPath].size() == fx.size()
        && memcmp(fs.files[stagedPath].data(), fx.data(), fx.size()) == 0);
  check("the verdict was AT+MTOTASTAGED=1 (the bundle verified)",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is WAIT_APPLY after the verdict",
        hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);
  check("the link baud was restored on DOWNLOADED",
        s.scriptDrained());
}

/* Case 6, first half: a garbled chunk line (one hex digit replaced) makes
 * the update re-send AT+MTOTAGET=0 once, and the clean answer on the retry
 * is used: the block is appended and acknowledged, the transfer goes on. */
static void test_pull_garbled_retries_once(void) {
  std::string fx;
  check("the good.ota fixture loads (garbled retry)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (garbled retry)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  std::string good = blkAnswer(0, 1024, fx, 0);
  std::string bad = good;
  /* Corrupt the offset field of the second line: the offset 96 becomes 97,
   * which is out of sequence (the first line ended at 96). The parse
   * validates the offset exactly, so this triggers a parse failure. */
  size_t idx = bad.find("MTOTABLK:0,96,");
  check("the second block line is where the helper put it",
        idx != std::string::npos);
  if (idx != std::string::npos) {
    bad[idx + strlen("MTOTABLK:0,")] = '7'; /* 96 -> 97 */
  }
  /* The first pull gets the garbled answer, the retry gets the good one,
   * then the ACK: the transfer continues. The BLOCK URC is injected after
   * the script so the poll that dispatches it finds the GET response
   * ready. */
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTOTAGET=0", good);
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the garbled line was re-pulled once and the clean answer used",
        s.scriptDrained() && s.unexpected().empty());
  check("the block was appended and acknowledged (no abort)",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING
        && hearth.update.status().error == HEARTH_UPDATE_OK);
}

/* Case 6, the abort half: the same garbled line twice in a row. The second
 * parse failure aborts the transfer with AT+MTOTA=0 then AT+MTOTA=1 (final
 * review I1: the =0 turns the requestor off, so the abort re-arms it for
 * the next offer), state FAILED and HEARTH_UPDATE_ERR_LINK, and the
 * partial staged file is removed. */
static void test_pull_garbled_twice_aborts(void) {
  std::string fx;
  check("the good.ota fixture loads (abort)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (abort)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  const char *stagedPath = "/hearth/staged.ota";
  std::string bad = blkAnswer(0, 1024, fx, 0);
  /* Same out-of-sequence offset as the retry test: both pulls fail to
   * parse. */
  size_t idx = bad.find("MTOTABLK:0,96,");
  if (idx != std::string::npos) {
    bad[idx + strlen("MTOTABLK:0,")] = '7'; /* 96 -> 97 */
  }
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTA=0", "OK\r\n");
  /* I1: the abort turns the requestor back on for the next offer. */
  s.expect("AT+MTOTA=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the pull was sent twice (one re-pull on the first failure) and the abort went out",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED after the second parse failure",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_LINK",
        hearth.update.status().error == HEARTH_UPDATE_ERR_LINK);
  check("the partial staged file is removed", !fs.exists(stagedPath));
  check("the download-complete flag is clear", !hearth.update.hearthDownloadComplete());
}

/* Case 6, the timeout half: no OK within the pull's 1500 ms. The update is
 * NOT re-pulled (the 5 s acknowledgement deadline cannot absorb a second
 * full pull at 115200): it aborts at once with AT+MTOTA=0, state FAILED,
 * HEARTH_UPDATE_ERR_LINK, and the partial staged file is removed. Case 10's
 * switch back on FAILED: the download ran at the download baud, so the abort
 * restores the link baud (AT+MTBAUD=115200) before the AT+MTOTA=0, or the
 * co-processor's next reboot at the default rate would no longer be
 * understood. Final review I1: the abort then sends AT+MTOTA=1, the
 * requestor back on for the next offer. */
static void test_pull_timeout_aborts_at_once(void) {
  std::string fx;
  check("the good.ota fixture loads (timeout)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (timeout)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_baudCalls = 0;
  g_lastBaud = 0;
  hearth.update.hearthSetBaudChanger(baudHook);
  const char *stagedPath = "/hearth/staged.ota";
  /* The switch to the download baud on AVAILABLE, then the pull answered
   * with the first block line and then silence: the co-processor never
   * sends the OK, so the pull's 1500 ms runs out. The abort restores the
   * link baud before it sends AT+MTOTA=0. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=0", blkLine(0, 0, 96, fx, 0, false));
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTA=0", "OK\r\n");
  /* I1: the abort turns the requestor back on for the next offer. */
  s.expect("AT+MTOTA=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the pull timed out and was NOT re-pulled (the abort went out next)",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on a pull timeout",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_LINK on a pull timeout",
        hearth.update.status().error == HEARTH_UPDATE_ERR_LINK);
  check("the partial staged file is removed on a pull timeout",
        !fs.exists(stagedPath));
  check("the download-complete flag is clear on a pull timeout",
        !hearth.update.hearthDownloadComplete());
  check("the link baud was restored on the abort (AT+MTBAUD=115200 scripted, hook last 115200)",
        g_lastBaud == 115200);
}

/* Case 6, the mterr half: a pull answered with some +MTOTABLK lines and
 * then +MTERR:12 (the co-processor dropped the transfer mid-pull). The
 * partial block is discarded, nothing is acknowledged, and the update
 * waits for the next +MTOTA line (the ERROR,<detail> and IDLE that
 * follow). */
static void test_pull_mterr_discards_partial(void) {
  std::string fx;
  check("the good.ota fixture loads (mterr)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (mterr)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  const char *stagedPath = "/hearth/staged.ota";
  /* Three block lines, then the co-processor's +MTERR:12, then the
   * terminal ERROR the command waits for (the IDLE is a URC the case
   * names, injected afterwards). */
  std::string resp = blkLine(0, 0, 96, fx, 0, false);
  resp += blkLine(0, 96, 96, fx, 96, false);
  resp += blkLine(0, 192, 96, fx, 192, false);
  resp += "+MTERR:12\r\nERROR\r\n";
  s.expect("AT+MTOTAGET=0", resp);
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  /* The co-processor's ERROR URC (the detail line the case names) lands
   * after the command's read loop has closed: a second poll dispatches it. */
  s.injectURC("+MTOTA:ERROR,abort");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the mterr pull was sent once and not re-pulled",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on the co-processor's +MTERR:12",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_COPROC",
        hearth.update.status().error == HEARTH_UPDATE_ERR_COPROC);
  check("the co-processor's detail was parsed",
        strcmp(hearth.update.status().detail, "abort") == 0);
  check("nothing was acknowledged (no ACK went out)",
        s.unexpected().empty());
  check("the partial staged file is removed on the mterr",
        !fs.exists(stagedPath));

  /* The IDLE that follows brings the update back to a clean state. */
  s.injectURC("+MTOTA:IDLE");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the state is IDLE after the IDLE line",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("nothing else went on the wire", s.unexpected().empty());
}

/* The re-entry guard: a drain entered while one is running sends nothing.
 * The pull sends its own hearthCommand()s, each of which ends in a nested
 * hearthDrain(); the nested drain finds _draining set and returns at once,
 * so the script sees exactly one AT+MTOTAGET=0. */
static void test_drain_reentry_sends_nothing(void) {
  std::string fx;
  check("the good.ota fixture loads (reentry)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (reentry)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  /* The script has exactly one AT+MTOTAGET=0: the nested drain (the one
   * the outer pull's own hearthCommand() runs at the end of its call)
   * finds _draining set and returns without sending a second one. */
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 1024, fx, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("exactly one AT+MTOTAGET=0 went out (the nested drain sent nothing)",
        s.scriptDrained() && s.unexpected().empty());
  check("the block was pulled and acknowledged once",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);
}

/*
 * The second half (plan Task 4b2): the verification and the verdict, the
 * co-processor states' actions, the consent and the baud switch. These run
 * the full download loop against the fixtures the way the first half does,
 * then feed +MTOTA:DOWNLOADED and assert what the drain does next: the
 * bundle is verified (HearthBundle::open over the staged file, the target and
 * variant against the cached AT+CGMM and AT+MTOTA?, verifyPart for each
 * selected part), the verdict goes out as AT+MTOTASTAGED, the consent hook
 * runs, and the baud switches.
 */

/* The consent and baud hooks, recorded by the tests. */
static int g_consentCalls = 0;
static bool g_consentResult = true;
static bool consentHook() { g_consentCalls++; return g_consentResult; }

static void baudHook(uint32_t b) { g_baudCalls++; g_lastBaud = b; }

/* Run the full download of `fx` against the scripted co-processor and end on
 * +MTOTA:DOWNLOADED (the default), so the staged file is the fixture and the
 * state is VERIFYING with _downloadComplete set, and the verdict command
 * `verdict` (AT+MTOTASTAGED=1 or =0,<reason>) is scripted and sent on the
 * same drain. The drain on AVAILABLE sends AT+MTBAUD=<download baud> and the
 * drain on DOWNLOADED sends AT+MTBAUD=<default> before the verdict, so those
 * are scripted in order here. With beginWrite false the download stops after
 * the last block (no DOWNLOADED): the staged write is still open and the
 * state still DOWNLOADING, which is the state a transfer in the middle of
 * its consent window is in. */

/* Case 5, the rest: after the download the good.ota bundle verifies, the
 * apply decision selects the fw part (1.3.0 differs from the cached 1.2.0)
 * and the host part (1.4.0 differs from the manifest none), the consent hook
 * is called, AT+MTOTASTAGED=1 is sent and the state is WAIT_APPLY. */
static void test_verdict_accepted(void) {
  std::string fx;
  check("the good.ota fixture loads (accepted)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (accepted)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_consentCalls = 0;
  g_consentResult = true;
  hearth.update.onApplyRequest(consentHook);
  g_baudCalls = 0;
  g_lastBaud = 0;
  hearth.update.hearthSetBaudChanger(baudHook);

  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");

  check("the bundle verified and the verdict was AT+MTOTASTAGED=1",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is WAIT_APPLY",
        hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);
  check("the consent hook was called", g_consentCalls == 1);
  check("no error on the accepted verdict",
        hearth.update.status().error == HEARTH_UPDATE_OK);
}

/* Case 7, the signature refusal: a bundle signed with another key refuses
 * with AT+MTOTASTAGED=0,1 (HEARTH_BUNDLE_ERR_SIGNATURE), the staged bundle is
 * removed, the state is FAILED with HEARTH_UPDATE_ERR_BUNDLE and .reason 1. */
static void test_verdict_bad_signature(void) {
  std::string fx;
  check("the other-key.ota fixture loads", loadFixture("fixtures/other-key.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (bad sig)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,1");

  check("the verdict was AT+MTOTASTAGED=0,1",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on a bad signature",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_BUNDLE",
        hearth.update.status().error == HEARTH_UPDATE_ERR_BUNDLE);
  check("the reason is 1 (signature)",
        hearth.update.status().reason == HEARTH_BUNDLE_ERR_SIGNATURE);
  check("the staged bundle was removed",
        !fs.exists("/hearth/staged.ota"));
}

/* Case 7, the target refusal: a bundle whose Hearth part targets nRF54L15
 * Hearth on the C6 refuses with AT+MTOTASTAGED=0,3 (ERR_TARGET). */
static void test_verdict_bad_target(void) {
  std::string fx;
  check("the nrf-only.ota fixture loads", loadFixture("fixtures/nrf-only.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (bad target)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,3");

  check("the verdict was AT+MTOTASTAGED=0,3",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on a bad target",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_BUNDLE",
        hearth.update.status().error == HEARTH_UPDATE_ERR_BUNDLE);
  check("the reason is 3 (target)",
        hearth.update.status().reason == HEARTH_BUNDLE_ERR_TARGET);
  check("the staged bundle was removed",
        !fs.exists("/hearth/staged.ota"));
}

/* Case 7, the downgrade refusal and the allowDowngrade accept: a bundle with
 * a product version below the effective one refuses with =0,4 (ERR_VERSION);
 * with allowDowngrade the same bundle is accepted. */
static void test_verdict_downgrade(void) {
  std::string fx;
  check("the downgrade.ota fixture loads", loadFixture("fixtures/downgrade.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (downgrade)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  g_consentResult = true;
  hearth.update.onApplyRequest(consentHook);

  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,4");

  check("the downgrade was refused with AT+MTOTASTAGED=0,4",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on the downgrade",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the reason is 4 (version)",
        hearth.update.status().reason == HEARTH_BUNDLE_ERR_VERSION);

  /* The same bundle with allowDowngrade is accepted. */
  MockStream s2;
  scriptBegin(s2);
  HearthClass h2;
  HearthFsMem f2;
  h2.begin(s2);
  h2.update.hearthAttach(f2);
  HearthUpdateConfig cfg;
  cfg.allowDowngrade = true;
  g_yieldAdvanceMs = 50;
  check("begin returns true (allowDowngrade)",
        h2.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  g_consentResult = true;
  h2.update.onApplyRequest(consentHook);
  runDownload(s2, h2, fx, "AT+MTOTASTAGED=1");
  check("allowDowngrade accepts the same bundle with AT+MTOTASTAGED=1",
        s2.scriptDrained() && s2.unexpected().empty()
        && h2.update.status().state == HEARTH_UPDATE_WAIT_APPLY);
}

/* A HearthByteSource over the fixture's bytes, so the test can open the
 * container with the library's own parser and find the parts. */
struct TmpSource : HearthByteSource {
  const std::string *d;
  explicit TmpSource(const std::string &s) : d(&s) {}
  bool read(uint32_t off, uint8_t *buf, size_t n) override {
    if (off + n > d->size()) return false;
    memcpy(buf, d->data() + off, n);
    return true;
  }
  uint32_t size() const override { return (uint32_t)d->size(); }
};

/* Case 7, the tampered-part refusal: a good.ota byte flipped in the fw part
 * refuses with AT+MTOTASTAGED=0,2 (ERR_DIGEST), not =0,1: the container's
 * signature covers the header and the parts table only, so a tampered part
 * still opens (the digest is what catches it, one part at a time). The
 * tamper's offset comes from the parsed container (HearthBundle::open on the
 * fixture: info.containerOffset + parts[0].offset), not a hardcoded file
 * offset, so it cannot land on the signature. */
static void test_verdict_tampered(void) {
  std::string fx;
  check("the good.ota fixture loads (tampered)", loadFixture("fixtures/good.ota", fx));
  TmpSource tsrc(fx);
  HearthBundleInfo info;
  check("the good.ota container opens (tampered)",
        HearthBundle::open(tsrc, HEARTH_DEV_PUBKEY, info) == HEARTH_BUNDLE_OK);
  /* Flip one byte inside the fw part's data, past the part's start and well
   * inside its length, so it is part data and not the header, the table or
   * the signature. */
  size_t tamperAt = (size_t)info.containerOffset + (size_t)info.parts[0].offset + 10;
  check("the tamper offset is inside the fw part",
        tamperAt < fx.size()
        && tamperAt > (size_t)info.containerOffset + (size_t)info.parts[0].offset
        && tamperAt < (size_t)info.containerOffset + (size_t)info.parts[0].offset
                            + (size_t)info.parts[0].length);
  fx[tamperAt] = (char)(fx[tamperAt] ^ 0xFF);
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (tampered)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,2");

  check("the tampered part was refused with AT+MTOTASTAGED=0,2",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED on the tampered part",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the reason is 2 (digest)",
        hearth.update.status().reason == HEARTH_BUNDLE_ERR_DIGEST);
  check("the staged bundle was removed",
        !fs.exists("/hearth/staged.ota"));
}

/* Case 6d: a variant of unknown from AT+MTOTA? selects no Hearth part at all
 * (a bundle carrying only a Hearth part answers =0,3, the target does not
 * match the unknown variant), while a host part still applies. */
static void test_variant_unknown(void) {
  std::string fx;
  check("the nrf-only.ota fixture loads (unknown variant)", loadFixture("fixtures/nrf-only.ota", fx));
  MockStream s;
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  /* The variant is unknown. */
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,unknown\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (unknown variant)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,3");
  check("an unknown variant refuses a Hearth-only bundle with =0,3",
        s.scriptDrained() && s.unexpected().empty()
        && hearth.update.status().reason == HEARTH_BUNDLE_ERR_TARGET);
}

/* Case 6b: the co-processor states' actions. */
static void test_coproc_states(void) {
  /* ERROR,<detail> then IDLE: FAILED with ERR_COPROC and the detail, then
   * IDLE, and the error stays readable until the next transfer starts. */
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (states)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_statusCalls = 0;
  hearth.update.onStatus(onStatus);
  s.injectURC("+MTOTA:ERROR,session");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("ERROR,session is FAILED with ERR_COPROC",
        hearth.update.status().state == HEARTH_UPDATE_FAILED
        && hearth.update.status().error == HEARTH_UPDATE_ERR_COPROC);
  check("the detail is session",
        strcmp(hearth.update.status().detail, "session") == 0);

  s.injectURC("+MTOTA:IDLE");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the state is IDLE after the IDLE line",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the error stays readable until the next transfer starts",
        hearth.update.status().error == HEARTH_UPDATE_ERR_COPROC
        && strcmp(hearth.update.status().detail, "session") == 0);

  /* DEFERRED,<s>: IDLE with deferredSeconds. */
  s.injectURC("+MTOTA:DEFERRED,120");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("DEFERRED,120 is IDLE with deferredSeconds 120",
        hearth.update.status().state == HEARTH_UPDATE_IDLE
        && hearth.update.status().deferredSeconds == 120);

  /* DISCONTINUED: IDLE. */
  s.injectURC("+MTOTA:DISCONTINUED");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("DISCONTINUED is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* QUERYING: leaves the state IDLE and fires onStatus. */
  int before = g_statusCalls;
  s.injectURC("+MTOTA:QUERYING");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("QUERYING leaves the state IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("QUERYING fired onStatus", g_statusCalls == before + 1);
}

/* Case 8: consent refused, then accepted. */
static void test_consent_refused_then_accepted(void) {
  std::string fx;
  check("the good.ota fixture loads (consent)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (consent)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_consentCalls = 0;
  g_consentResult = false;  /* refuse first */
  hearth.update.onApplyRequest(consentHook);
  /* No verdict is sent on the first drain (the hook refuses), so the
   * verdict is empty here; the accepted drain's AT+MTOTASTAGED=1 is
   * scripted after runDownload, for the next poll. */
  runDownload(s, hearth, fx, "");
  s.expect("AT+MTOTASTAGED=1", "OK\r\n");  /* for the second, accepted, drain */
  check("the consent hook was refused: no AT+MTOTASTAGED yet",
        hearth.update.status().state == HEARTH_UPDATE_VERIFYING
        && g_consentCalls == 1);
  check("the staged bundle stays while consent is pending",
        fs.exists("/hearth/staged.ota"));

  g_consentResult = true;  /* accept on the next drain */
  g_yieldAdvanceMs = 50;
  hearth.update.hearthDrain();
  g_yieldAdvanceMs = 0;
  check("the next drain after the hook returns true sends AT+MTOTASTAGED=1",
        s.scriptDrained() && s.unexpected().empty()
        && hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);
}

/* Case 8, the abandoned half: a refusal past the consent window (the test
 * sets it to 0 and advances the clock) makes the update send AT+MTOTA=0 then
 * AT+MTOTA=1, which ends the attempt and frees the co-processor's block
 * buffer at once instead of after its six-hour watchdog (spec 11.1), and the
 * state is IDLE. */
static void test_consent_abandoned_past_window(void) {
  std::string fx;
  check("the good.ota fixture loads (abandon)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.consentWindowMs = 0;  /* any refusal is immediately past the window */
  g_yieldAdvanceMs = 50;
  check("begin returns true (abandon)", hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  g_consentCalls = 0;
  g_consentResult = false;  /* always refuse */
  hearth.update.onApplyRequest(consentHook);
  /* The first drain refuses (no verdict sent); the abandonment's
   * AT+MTOTA=0/AT+MTOTA=1 go out on the next drain. */
  runDownload(s, hearth, fx, "");
  s.expect("AT+MTOTA=0", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  /* The first drain refuses; the next drain finds the refusal past the
   * (zero) window and abandons. */
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the abandonment sent AT+MTOTA=0 then AT+MTOTA=1",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is IDLE after the abandonment",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the staged bundle was removed on the abandonment",
        !fs.exists("/hearth/staged.ota"));
}

/* Case 8, the zero-clock half: a refusal recorded when millis() is 0 must
 * lapse like any other. With 0 as the "no refusal" sentinel such a refusal
 * would never lapse, so the lapsed check now runs on the _consentRefused
 * flag and the timestamp carries the time only. */
static void test_consent_refused_at_zero_clock(void) {
  g_millis = 0;  /* the refusal is recorded at millis() == 0 */
  std::string fx;
  check("the good.ota fixture loads (refusal at zero)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.consentWindowMs = 0;  /* a refusal is outlived by the very next drain */
  g_yieldAdvanceMs = 50;
  check("begin returns true (refusal at zero)",
        hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  g_consentCalls = 0;
  g_consentResult = false;  /* always refuse */
  hearth.update.onApplyRequest(consentHook);
  /* The download ends on DOWNLOADED (the verdict drain refuses, the
   * AT+MTOTA:DOWNLOADED state is VERIFYING) and the refusal is recorded at
   * g_millis == 0. */
  runDownload(s, hearth, fx, "");
  check("the refusal was recorded at the zero clock (no abandonment yet)",
        hearth.update.status().state == HEARTH_UPDATE_VERIFYING
        && g_consentCalls == 1);

  /* The next drain finds the zero-clock refusal outlived by the (zero)
   * window and abandons: AT+MTOTA=0 then AT+MTOTA=1, state IDLE. */
  s.expect("AT+MTOTA=0", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.update.hearthDrain();
  g_yieldAdvanceMs = 0;
  check("the zero-clock refusal lapsed on the next drain",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is IDLE after the abandonment",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the staged bundle was removed on the abandonment",
        !fs.exists("/hearth/staged.ota"));
}

/* The nothing-to-do half of the apply decision: a bundle whose parts all
 * match what is already applied (the fw part at the running Hearth version,
 * the host part at the manifest's host version) selects nothing. The plan's
 * Task 4 rule answers AT+MTOTASTAGED=0,4 (its chosen answer for "nothing to
 * do") and updates the manifest's product version to the bundle's so the
 * provider stops offering it, with the state IDLE and no error (it is not a
 * failure). The fixture is the good.ota bundle as-is; the test sets the
 * running Hearth version and the manifest's host version to the bundle's
 * part versions so nothing differs. */
static void test_verdict_nothing_to_do(void) {
  std::string fx;
  check("the good.ota fixture loads (nothing to do)", loadFixture("fixtures/good.ota", fx));
  TmpSource tsrc(fx);
  HearthBundleInfo info;
  check("the good.ota container opens (nothing to do)",
        HearthBundle::open(tsrc, HEARTH_DEV_PUBKEY, info) == HEARTH_BUNDLE_OK);
  const char *fwVer = 0, *hostVer = 0;
  for (int i = 0; i < (int)info.partCount; i++) {
    if (info.parts[i].type == 2) fwVer = info.parts[i].version;
    else if (info.parts[i].type == 1) hostVer = info.parts[i].version;
  }
  check("the fixture has one fw and one host part",
        fwVer && hostVer);

  /* A manifest whose host version is the host part's: nothing differs. */
  HearthFsMem fs;
  HearthUpdateStage stage;
  check("stage begin for seeding (nothing to do)", stage.begin(fs));
  HearthManifest m;
  m.productVersion = 0x10400;
  strcpy(m.productVersionString, "1.4.0");
  strcpy(m.hostVersion, hostVer);
  check("seed manifest written (nothing to do)", stage.saveManifest(m));

  MockStream s;
  /* The running Hearth version (AT+MTVER?) is the fw part's version, so the
   * fw part is not selected; the manifest's host version is the host part's
   * version, so the host part is not selected either: nothing differs. The
   * effective version is max(baseline, manifest) = 0x10400, so the
   * declaration is unchanged. */
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", std::string("+MTVER:") + fwVer + "\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (nothing to do)",
        hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,4");

  check("the nothing-to-do bundle was answered with AT+MTOTASTAGED=0,4",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is IDLE (not a failure)",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the error is HEARTH_UPDATE_OK (not a failure)",
        hearth.update.status().error == HEARTH_UPDATE_OK);
  check("the staged bundle was removed",
        !fs.exists("/hearth/staged.ota"));
  HearthManifest after;
  check("the manifest loaded back",
        hearth.update.stage().loadManifest(after));
  check("the manifest's product version is the bundle's",
        after.productVersion == info.productVersion);
  check("the manifest's version string is the bundle's",
        strcmp(after.productVersionString, info.productVersionString) == 0);
  check("the manifest's host version is kept",
        strcmp(after.hostVersion, hostVer) == 0);
}

/* Case 10: the baud switch. With a test hook installed, AT+MTBAUD=<download
 * baud> is sent on AVAILABLE, the hook is called, and AT+MTBAUD=<default>
 * plus the hook are called again on DOWNLOADED and on FAILED. With no hook
 * installed (the host build, HEARTH_SERIAL_PORT undefined) the switch is the
 * AT+MTBAUD only. */
static void test_baud_switch(void) {
  std::string fx;
  check("the good.ota fixture loads (baud)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (baud)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_consentResult = true;
  hearth.update.onApplyRequest(consentHook);
  g_baudCalls = 0;
  g_lastBaud = 0;
  hearth.update.hearthSetBaudChanger(baudHook);

  /* The switch to the download baud on AVAILABLE. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("AT+MTBAUD=921600 was sent on AVAILABLE",
        s.unexpected().empty());
  check("the baud hook was called with the download baud",
        g_baudCalls == 1 && g_lastBaud == 921600);

  /* The rest of the download, then DOWNLOADED restores the default. */
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 1024, fx, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=1", blkAnswer(1, 1024, fx, 1024));
  s.expect("AT+MTOTAACK=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,1,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=2", blkAnswer(2, (uint32_t)(fx.size() - 2048), fx, 2048));
  s.expect("AT+MTOTAACK=2", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,2," + std::to_string((unsigned)(fx.size() - 2048)));
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  /* The switch back on DOWNLOADED. */
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTASTAGED=1", "OK\r\n");
  s.injectURC("+MTOTA:DOWNLOADED");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("AT+MTBAUD=115200 was sent on DOWNLOADED",
        s.scriptDrained() && s.unexpected().empty());
  check("the baud hook was called again with the default baud",
        g_baudCalls == 2 && g_lastBaud == 115200);

  /* The switch back on FAILED (a bad-signature refusal). */
  std::string fx2;
  check("the other-key.ota fixture loads (baud failed)",
        loadFixture("fixtures/other-key.ota", fx2));
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66562");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  size_t pos = 0;
  uint32_t seq = 0;
  while (pos < fx2.size()) {
    size_t blen = fx2.size() - pos;
    if (blen > 1024) blen = 1024;
    s.expect("AT+MTOTAGET=" + std::to_string((unsigned)seq),
             blkAnswer(seq, (uint32_t)blen, fx2, pos));
    s.expect("AT+MTOTAACK=" + std::to_string((unsigned)seq), "OK\r\n");
    s.injectURC("+MTOTA:BLOCK," + std::to_string((unsigned)seq) + ","
                + std::to_string((unsigned)blen));
    g_yieldAdvanceMs = 50;
    hearth.poll();
    g_yieldAdvanceMs = 0;
    pos += blen;
    seq++;
  }
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTASTAGED=0,1", "OK\r\n");
  s.injectURC("+MTOTA:DOWNLOADED");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the baud hook was called again on the FAILED verdict",
        g_baudCalls == 4 && g_lastBaud == 115200);
  check("the state is FAILED after the refusal",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
}

/* The folded-from-4b1-review trap (c): one hex digit of a +MTOTABLK line is
 * corrupted (not the offset), driving the parse-failure branch: one re-pull,
 * then the second corrupt answer aborts (final review I1: the abort sends
 * AT+MTOTA=0 then AT+MTOTA=1, the requestor back on for the next offer). */
static void test_pull_hexdigit_corrupt_aborts(void) {
  std::string fx;
  check("the good.ota fixture loads (hexdigit)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("begin returns true (hexdigit)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  std::string good = blkAnswer(0, 1024, fx, 0);
  std::string bad = good;
  /* Corrupt one hex digit (not the offset) to a non-hex character, so the
   * sscanf("%2x") parse of that byte fails and the pull marks the answer
   * failed. A flipped-but-still-hex digit would parse fine, so the
   * corruption must leave the hex set. */
  size_t idx = bad.find("MTOTABLK:0,0,");
  check("the first block line is where the helper put it",
        idx != std::string::npos);
  if (idx != std::string::npos) {
    size_t hexStart = idx + strlen("MTOTABLK:0,0,");
    bad[hexStart] = 'Z';  /* not a hex digit */
  }
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTA=0", "OK\r\n");
  /* I1: the abort turns the requestor back on for the next offer. */
  s.expect("AT+MTOTA=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the hex-digit corruption was re-pulled once and then aborted",
        s.scriptDrained() && s.unexpected().empty());
  check("the state is FAILED after the second corrupt answer",
        hearth.update.status().state == HEARTH_UPDATE_FAILED
        && hearth.update.status().error == HEARTH_UPDATE_ERR_LINK);
  check("the partial staged file is removed",
        !fs.exists("/hearth/staged.ota"));
}

int main(void) {
  printf("\n===== HearthUpdate (task 4a + 4b1 + 4b2) tests =====\n");
  test_begin_no_fs();
  test_begin_ok();
  test_begin_unavailable();
  test_manifest_wins();
  test_query_answer_not_a_urc();
  test_check_now_busy();
  test_no_space();
  test_block_while_disabled_ignored();
  test_default_disabled();
  test_download_loop();
  test_pull_garbled_retries_once();
  test_pull_garbled_twice_aborts();
  test_pull_timeout_aborts_at_once();
  test_pull_mterr_discards_partial();
  test_drain_reentry_sends_nothing();
  test_verdict_accepted();
  test_verdict_bad_signature();
  test_verdict_bad_target();
  test_verdict_downgrade();
  test_verdict_tampered();
  test_variant_unknown();
  test_coproc_states();
  test_consent_refused_then_accepted();
  test_consent_abandoned_past_window();
  test_consent_refused_at_zero_clock();
  test_verdict_nothing_to_do();
  test_baud_switch();
  test_pull_hexdigit_corrupt_aborts();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

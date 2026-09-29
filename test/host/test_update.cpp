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
static bool loadFixture(const char *path, std::string &out) {
  /* Try the path as-is first (CWD is test/host under make run), then with
   * the test/host prefix (CWD is the repo root under the direct binary
   * invocation). */
  FILE *f = fopen(path, "rb");
  if (!f) {
    std::string prefixed = std::string("test/host/") + path;
    f = fopen(prefixed.c_str(), "rb");
  }
  if (!f) {
    return false;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  fclose(f);
  return true;
}

static void scriptBegin(MockStream &s) {
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
}

/* One +MTOTABLK: line: the seq, the block-local offset and the hex for the
 * bytes at that offset, upper-case. The line carries at most 96 bytes; the
 * last line of a short block carries the rest. The terminal OK the
 * co-processor sends after the last line is appended by the caller when ok
 * is true. */
static std::string blkLine(uint32_t seq, uint32_t off, uint32_t len,
                           const std::string &data, uint32_t dataOff, bool ok) {
  static const char *hexd = "0123456789ABCDEF";
  std::string s = "+MTOTABLK:";
  char tmp[24];
  snprintf(tmp, sizeof(tmp), "%u,%u,", (unsigned)seq, (unsigned)off);
  s += tmp;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t b = (uint8_t)data[dataOff + i];
    s += hexd[b >> 4];
    s += hexd[b & 0xF];
  }
  s += "\r\n";
  if (ok) {
    s += "OK\r\n";
  }
  return s;
}

/* The full answer for one AT+MTOTAGET: ceil(len/96) lines, the last
 * shorter for a short block, the OK on the last one. */
static std::string blkAnswer(uint32_t seq, uint32_t len,
                             const std::string &data, uint32_t dataOff) {
  std::string resp;
  for (uint32_t off = 0; off < len; off += 96) {
    uint32_t n = len - off;
    if (n > 96) n = 96;
    bool last = (off + n >= len);
    resp += blkLine(seq, off, n, data, dataOff + off, last);
  }
  return resp;
}

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
   * then DOWNLOADING at 0 percent before the first block. */
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

  s.injectURC("+MTOTA:DOWNLOADED");
  hearth.poll();
  check("the staged file exists after the drain of DOWNLOADED", fs.exists(stagedPath));
  check("_downloadComplete is set", hearth.update.hearthDownloadComplete());
  check("the staged file is the fixture byte for byte",
        fs.files.count(stagedPath) == 1
        && fs.files[stagedPath].size() == fx.size()
        && memcmp(fs.files[stagedPath].data(), fx.data(), fx.size()) == 0);
  check("the state is VERIFYING after DOWNLOADED",
        hearth.update.status().state == HEARTH_UPDATE_VERIFYING);
  check("no AT+MTOTASTAGED yet (4b2 sends it)", s.unexpected().empty());
  check("nothing else went on the wire", s.scriptDrained());
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
 * parse failure aborts the transfer with AT+MTOTA=0, state FAILED and
 * HEARTH_UPDATE_ERR_LINK, and the partial staged file is removed. */
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
  s.expect("AT+MTOTA=0", "OK\r\n");
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
 * HEARTH_UPDATE_ERR_LINK, and the partial staged file is removed. */
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

  const char *stagedPath = "/hearth/staged.ota";
  /* The pull is answered with the first block line and then silence: the
   * co-processor never sends the OK, so the pull's 1500 ms runs out. */
  s.expect("AT+MTOTAGET=0", blkLine(0, 0, 96, fx, 0, false));
  s.expect("AT+MTOTA=0", "OK\r\n");
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

int main(void) {
  printf("\n===== HearthUpdate (task 4a + 4b1) tests =====\n");
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
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

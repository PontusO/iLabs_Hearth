/* test/host/test_update_apply.cpp: plan Task 6a, the apply of the Hearth
 * (co-processor) part. The update has downloaded, verified and judged the
 * bundle and sits in HEARTH_UPDATE_WAIT_APPLY; the co-processor sends
 * +MTOTA:APPLY and the drain answers it: the declaration, the flash
 * attempts, the ready wait, the version check, the retry and the rollback
 * to the retained image.
 *
 * The co-processor is scripted the way test_update.cpp does it (a
 * MockStream, the whole download through runDownload), and the flasher is
 * a FlasherFake installed with hearthSetFlasher(): it records every call
 * (off, len, sha256, pins, the bytes read back) and runs onFlash(i) inside
 * call i, where a test injects +MTREADY (the co-processor rebooting into
 * the new image) and checks nextExpected() to prove which commands had
 * gone out before the flash began.
 *
 * The flasher's arguments are compared against fixtures/apply_parts.txt
 * (the controller's oracle, from hearth_bundle.py): each fixture part's
 * staged offset, length and sha256. Nothing here recomputes a digest.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "HearthUpdate.h"
#include "HearthFlasher.h"
#include "HearthBundle.h"
#include "HearthFsMem.h"
#include "FlasherFake.h"
#include "UpdateHarness.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* A line of apply_parts.txt: part|<fixture>|<index>|<type>|<target>|
 * <variant>|<version>|<staged offset>|<length>|<sha256 hex>. */
struct PartLine {
  uint32_t off;
  uint32_t len;
  uint8_t sha[32];
};
static bool loadPartLine(const char *fixture, int idx, PartLine &out) {
  std::string data;
  if (!loadFixture("fixtures/apply_parts.txt", data)) {
    return false;
  }
  size_t pos = 0;
  while (pos < data.size()) {
    size_t eol = data.find('\n', pos);
    std::string line = data.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
    pos = (eol == std::string::npos) ? data.size() : eol + 1;
    if (line.size() < 2 || line[0] != 'p' || line[1] != 'a') {
      continue;
    }
    char *fields[10];
    int nf = 0;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line.c_str());
    for (char *f = strtok(buf, "|"); f && nf < 10; f = strtok(0, "|")) {
      fields[nf++] = f;
    }
    if (nf < 10 || strcmp(fields[1], fixture) != 0 || atoi(fields[2]) != idx) {
      continue;
    }
    out.off = (uint32_t)strtoul(fields[7], 0, 10);
    out.len = (uint32_t)strtoul(fields[8], 0, 10);
    const char *hex = fields[9];
    for (int i = 0; i < 32; i++) {
      unsigned v;
      if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
        return false;
      }
      out.sha[i] = (uint8_t)v;
    }
    return true;
  }
  return false;
}

/* The common setup (the brief's "Common setup for the ESP32-C6 cases"): a
 * manifest saved before begin so good.ota's host part 1.4.0 is NOT
 * selected (6a stays fw-only), the begin script for the given model, and
 * the whole download to WAIT_APPLY. The flasher is installed and the
 * apply's own commands are left for the caller to script. */
static bool setupToApply(MockStream &s, HearthClass &hearth, HearthFsMem &fs,
                         FlasherFake &fake, std::string &fx,
                         const char *fixture, const char *swver, const char *model,
                         const char *variant, uint32_t baseVersion, const char *baseStr,
                         uint32_t cfgSize, bool withManifest, const char *mtver = "1.2.0") {
  (void)baseVersion;
  char checkName[80];
  snprintf(checkName, sizeof(checkName), "%s fixture loads", fixture);
  check(checkName, loadFixture((std::string("fixtures/") + fixture + ".ota").c_str(), fx));
  HearthUpdateStage stg;
  if (withManifest) {
    check("seed manifest written", stg.begin(fs));
    HearthManifest m;
    m.productVersion = 0x10300;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("seed manifest saved", stg.saveManifest(m));
  }
  scriptBeginFor(s, swver, model, mtver, variant);
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("update begin returns true", hearth.update.begin(cfgSize, baseStr, cfg));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");
  return hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY;
}

/* Case 1 and 2 together: the order and the arguments of a successful
 * apply (good.ota, one flash), and the bookkeeping it leaves behind. */
static void test_order_and_arguments(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (order)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));

  PartLine pl;
  check("the part|good|0 line loads", loadPartLine("good", 0, pl));

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      check("the declaration had gone out before the flash", s.nextExpected() == "AT+MTVER?");
      HearthUpdateState st;
      check("the stage's state loads (inside the flash)", fs.exists("/hearth/ota.state"));
      check("the state loads with phase FW, attempts 1, targetVersion 66560",
            hearth.update.stage().loadState(st)
            && st.phase == HEARTH_PHASE_FW
            && st.attempts == 1
            && st.targetVersion == 66560);
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly one flash call", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("the off matches apply_parts.txt part|good|0", c.off == pl.off);
    check("the len matches apply_parts.txt part|good|0", c.len == pl.len);
    check("the sha256 matches apply_parts.txt part|good|0", memcmp(c.sha256, pl.sha, 32) == 0);
    std::vector<uint8_t> want(fx.begin() + pl.off, fx.begin() + pl.off + pl.len);
    check("the image read back equals the fixture's bytes at [off, off+len)",
          c.image == want);
    check("the pins are {15, true, 14, true}",
          c.pins.reset == 15 && c.pins.resetActiveLow && c.pins.strap == 14
          && c.pins.strapActiveLow);
  }
  check("the script is drained", s.scriptDrained());
  check("nothing unexpected on the wire", s.unexpected().empty());

  /* Case 2: the bookkeeping. */
  char rt[33], rv[33];
  uint32_t rl = 0;
  check("retainedFwInfo gives the new part", hearth.update.stage().retainedFwInfo(rt, rv, rl)
        && strcmp(rt, "ESP32-C6 Hearth") == 0 && strcmp(rv, "1.3.0") == 0 && rl == 1000);
  HearthManifest m;
  check("the manifest is 66560 / 1.4.0 / host 1.4.0",
        hearth.update.stage().loadManifest(m)
        && m.productVersion == 66560
        && strcmp(m.productVersionString, "1.4.0") == 0
        && strcmp(m.hostVersion, "1.4.0") == 0);
  check("the staged bundle is gone", !hearth.update.stage().stagedExists());
  check("the state file is gone", !hearth.update.stage().haveState());
  check("the status is IDLE with no error",
        hearth.update.status().state == HEARTH_UPDATE_IDLE
        && hearth.update.status().error == HEARTH_UPDATE_OK);
  check("effectiveVersion is the bundle's (66560)",
        hearth.update.status().effectiveVersion == 66560);
  check("hearthVersion is the new running version",
        strcmp(hearth.update.status().hearthVersion, "1.3.0") == 0);
}

/* Case 3: the nRF-only bundle on an nRF54L15, fw-only (no manifest). */
static void test_nrf_fw_only(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (nrf)", setupToApply(s, hearth, fs, fake, fx, "nrf-only",
        "AT+MTSWVER=66048,\"1.2.0\"", "nRF54L15 Hearth", "thread", 0x10200, "1.2.0", 0x10200, false));

  PartLine pl;
  check("the part|nrf-only|0 line loads", loadPartLine("nrf-only", 0, pl));

  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly one flash call (nrf)", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("the off matches apply_parts.txt part|nrf-only|0", c.off == pl.off);
    check("the len matches apply_parts.txt part|nrf-only|0", c.len == pl.len);
    check("the sha256 matches apply_parts.txt part|nrf-only|0", memcmp(c.sha256, pl.sha, 32) == 0);
    std::vector<uint8_t> want(fx.begin() + pl.off, fx.begin() + pl.off + pl.len);
    check("the image read back equals the fixture's bytes (nrf)", c.image == want);
  }
  check("the script is drained (nrf)", s.scriptDrained());
  check("nothing unexpected on the wire (nrf)", s.unexpected().empty());

  HearthManifest m;
  check("the manifest is 66304 / 1.3.0 / host \"\"",
        hearth.update.stage().loadManifest(m)
        && m.productVersion == 66304
        && strcmp(m.productVersionString, "1.3.0") == 0
        && m.hostVersion[0] == 0);
  check("the state is IDLE (nrf)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
}

/* Case 4: three failed verifies, no retained image. */
static void test_three_failed_no_retained(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (3fail)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    (void)i;
    s.injectURC("+MTREADY");
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("exactly three flash calls", fake.calls().size() == 3);
  check("the script is drained (3fail)", s.scriptDrained());
  check("nothing unexpected on the wire (3fail)", s.unexpected().empty());
  check("the state is FAILED", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  check("the staged bundle is kept (manual retry)", hearth.update.stage().stagedExists());
  check("the state file is gone (no resume)", !hearth.update.stage().haveState());
}

/* Case 5: three failed verifies, a retained image that fits the model. */
static void test_retained_rollback(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (rollback)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));

  /* A retained image of 500 known bytes, through the fs, before the apply. */
  std::vector<uint8_t> retBytes(500);
  for (int i = 0; i < 500; i++) {
    retBytes[i] = (uint8_t)(i * 7 + 3);
  }
  HearthUpdateStage stg;
  check("stage begin for the retained seed", stg.begin(fs));
  {
    HearthFile *f = fs.open("/hearth/fw-scratch.bin", "w");
    check("the scratch file opens", f != 0);
    if (f) {
      check("the 500 bytes are written", f->write(retBytes.data(), retBytes.size()) == retBytes.size());
      delete f;
    }
    f = fs.open("/hearth/fw-scratch.bin", "r");
    check("the scratch file reopens", f != 0);
    if (f) {
      check("the retained image is written",
            stg.retainFwPart(*f, 0, 500, "ESP32-C6 Hearth", "1.2.0"));
      delete f;
    }
  }

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 3) {
      check("the old version was declared BEFORE the rollback flash",
            s.nextExpected() == "AT+MTVER?");
    }
    s.injectURC("+MTREADY");
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly four flash calls (3 attempts + the rollback)", calls.size() == 4);
  if (calls.size() == 4) {
    check("the rollback's image equals the 500 retained bytes",
          calls[3].image == retBytes);
  }
  check("the script is drained (rollback)", s.scriptDrained());
  check("nothing unexpected on the wire (rollback)", s.unexpected().empty());
  check("the state is FAILED (rollback)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_FLASH (rollback)",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  check("hearthVersion is the rolled-back 1.2.0",
        strcmp(hearth.update.status().hearthVersion, "1.2.0") == 0);
}

/* Case 6: a flasher error on the first attempt, then a success. */
static void test_flasher_error_then_success(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (flerr)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));

  fake.results.push_back(HEARTH_FLASH_ERR_WRITE);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 1) {
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("exactly two flash calls (error then success)", fake.calls().size() == 2);
  check("the script is drained (flerr)", s.scriptDrained());
  check("nothing unexpected on the wire (flerr)", s.unexpected().empty());
  check("the state is IDLE (flerr)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
}

/* Case 7: a good flash, but no +MTREADY: the wait runs out, the attempt
 * fails, the second one succeeds. */
static void test_no_ready_then_success(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (no ready)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 1) {
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("exactly two flash calls (no ready then success)", fake.calls().size() == 2);
  check("the script is drained (no ready)", s.scriptDrained());
  check("nothing unexpected on the wire (no ready)", s.unexpected().empty());
  check("the state is IDLE (no ready)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
}

/*
 * Task 6b: the host part, the first-boot confirm and the first-boot
 * failure. The hooks are test functions over file-static records: the fake
 * XIP bytes (300 known bytes), and a record of every stageImage (path,
 * off, len), reboot and coprocReset call, in order. coprocReset injects
 * +MTREADY into the current MockStream and returns true unless a flag says
 * false.
 */
struct StageRec { std::string path; uint32_t off, len; };
struct HostRec {
  std::vector<uint8_t> xip;
  std::vector<StageRec> stage;
  int reboots = 0;
  int coprocResets = 0;
  bool coprocResetFalse = false;
  MockStream *stream = 0;
};
static HostRec g_host;

static bool hImageRange(const uint8_t **start, uint32_t *len) {
  *start = g_host.xip.data();
  *len = (uint32_t)g_host.xip.size();
  return true;
}
static bool hStageImage(const char *path, uint32_t off, uint32_t len) {
  StageRec r; r.path = path; r.off = off; r.len = len;
  g_host.stage.push_back(r);
  return true;
}
static void hReboot() { g_host.reboots++; }
static bool hCoprocReset(const HearthCoprocPins &pins) {
  (void)pins;
  g_host.coprocResets++;
  if (g_host.stream) g_host.stream->injectURC("+MTREADY");
  return !g_host.coprocResetFalse;
}

/* Reset the host record and install the test hooks on the given update. */
static void hostSetup(MockStream &s, HearthClass &hearth) {
  g_host = HostRec();
  g_host.xip.resize(300);
  for (int i = 0; i < 300; i++) g_host.xip[i] = (uint8_t)(i * 3 + 1);
  g_host.stream = &s;
  HearthHostHooks h;
  h.imageRange = hImageRange;
  h.stageImage = hStageImage;
  h.reboot = hReboot;
  h.coprocReset = hCoprocReset;
  hearth.update.hearthSetHostHooks(h);
}

/* Case 1 (6b): both parts selected. The Hearth part applies as in 6a, then
 * the host part: host-prev.bin holds the XIP bytes, one stageImage with the
 * staged path and the part|good|1 offset and length, one reboot after it,
 * the state loads phase HOST, no manifest, the staged bundle still there. */
static void test_both_parts(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (both)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  hostSetup(s, hearth);

  PartLine pl0;
  PartLine pl1;
  check("the part|good|0 line loads (both)", loadPartLine("good", 0, pl0));
  check("the part|good|1 line loads (both)", loadPartLine("good", 1, pl1));

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  /* The Hearth part: one flash with the part|good|0 range. */
  check("exactly one flash call (both)", fake.calls().size() == 1);
  if (fake.calls().size() == 1) {
    const FlasherFake::Call &c = fake.calls()[0];
    check("the fw off matches part|good|0 (both)", c.off == pl0.off);
    check("the fw len matches part|good|0 (both)", c.len == pl0.len);
  }
  /* The host part: host-prev.bin holds the XIP, one stageImage, one reboot. */
  check("host-prev.bin holds the 300 XIP bytes (both)",
        fs.exists("/hearth/host-prev.bin"));
  check("one stageImage call (both)", g_host.stage.size() == 1);
  if (g_host.stage.size() == 1) {
    const StageRec &r = g_host.stage[0];
    check("the stageImage path is the staged path (both)", r.path == hearth.update.stage().stagedPath());
    check("the stageImage off matches part|good|1 (both)", r.off == pl1.off);
    check("the stageImage len matches part|good|1 (both)", r.len == pl1.len);
  }
  check("one reboot, after the stageImage (both)", g_host.reboots == 1);
  HearthUpdateState st;
  check("the state loads phase HOST, targetVersion 66560, hostPart 1 (both)",
        hearth.update.stage().loadState(st)
        && st.phase == HEARTH_PHASE_HOST
        && st.targetVersion == 66560
        && st.hostPart == 1);
  check("no manifest file (both)", !hearth.update.stage().haveManifest());
  check("the staged bundle is still there (both)", hearth.update.stage().stagedExists());
  check("the script is drained (both)", s.scriptDrained());
  check("nothing unexpected on the wire (both)", s.unexpected().empty());
}

/* Case 2 (6b): host only. The Hearth part is not selected (running version
 * equals the part's), so the fallback is gone and the apply goes straight
 * to the host part: the declaration, the stageImage, one reboot, zero
 * flasher calls. */
static void test_host_only(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  /* Running Hearth 1.3.0, the same as the part's version: the Hearth part
   * is not selected. */
  check("setup reaches WAIT_APPLY (host only)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "wifi", 0x10400, "1.4.0", 0x10400, false, "1.3.0"));
  hostSetup(s, hearth);

  PartLine pl1;
  check("the part|good|1 line loads (host only)", loadPartLine("good", 1, pl1));

  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("zero flasher calls (the fallback is gone)", fake.calls().size() == 0);
  check("one stageImage call (host only)", g_host.stage.size() == 1);
  if (g_host.stage.size() == 1) {
    const StageRec &r = g_host.stage[0];
    check("the stageImage path is the staged path (host only)", r.path == hearth.update.stage().stagedPath());
    check("the stageImage off matches part|good|1 (host only)", r.off == pl1.off);
    check("the stageImage len matches part|good|1 (host only)", r.len == pl1.len);
  }
  check("one reboot (host only)", g_host.reboots == 1);
  check("host-prev.bin holds the XIP (host only)", fs.exists("/hearth/host-prev.bin"));
  HearthUpdateState st;
  check("the state loads phase HOST (host only)",
        hearth.update.stage().loadState(st) && st.phase == HEARTH_PHASE_HOST);
  check("the script is drained (host only)", s.scriptDrained());
  check("nothing unexpected on the wire (host only)", s.unexpected().empty());
}

/* Case 3 (6b): the first-boot confirm. Continue from case 1's fs: a new
 * HearthClass and MockStream over the same fs, as the new sketch after the
 * reboot. The declaration answers, the manifest is written, the state and
 * the staged bundle are gone, the co-processor is reset and the requestor
 * is turned back on. */
static void test_first_boot_confirm(void) {
  std::string fx;
  MockStream s1;
  HearthClass hearth1;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (confirm)", setupToApply(s1, hearth1, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  hostSetup(s1, hearth1);
  PartLine pl0, pl1;
  loadPartLine("good", 0, pl0);
  loadPartLine("good", 1, pl1);
  s1.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s1.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s1.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) { if (i == 0) s1.injectURC("+MTREADY"); };
  s1.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth1.poll();
  g_yieldAdvanceMs = 0;

  /* The new sketch after the reboot: a new HearthClass and MockStream over
   * the same fs. */
  MockStream s2;
  HearthClass hearth2;
  hostSetup(s2, hearth2);
  scriptBeginFor(s2, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.3.0", "wifi");
  s2.expect("AT+MTOTA=1", "OK\r\n");
  hearth2.begin(s2);
  hearth2.update.hearthAttach(fs);
  hearth2.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("the confirm's begin returns true", hearth2.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  HearthManifest m;
  check("the manifest is 66560 / 1.4.0 / host 1.4.0 (confirm)",
        hearth2.update.stage().loadManifest(m)
        && m.productVersion == 66560
        && strcmp(m.productVersionString, "1.4.0") == 0
        && strcmp(m.hostVersion, "1.4.0") == 0);
  check("the state is gone (confirm)", !hearth2.update.stage().haveState());
  check("the staged bundle is gone (confirm)", !hearth2.update.stage().stagedExists());
  check("one coprocReset, after the link commands (confirm)", g_host.coprocResets == 1);
  check("the status is IDLE (confirm)", hearth2.update.status().state == HEARTH_UPDATE_IDLE);
  check("the effectiveVersion is 66560 (confirm)", hearth2.update.status().effectiveVersion == 66560);
  check("the script is drained (confirm)", s2.scriptDrained());
  check("nothing unexpected on the wire (confirm)", s2.unexpected().empty());
}

/* Case 4 (6b): the first-boot confirm without a reset line: coprocReset
 * returns false. The confirm still completes (manifest, state, staged) but
 * there is no second AT+MTOTA=1. */
static void test_first_boot_confirm_no_reset(void) {
  std::string fx;
  MockStream s1;
  HearthClass hearth1;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (no reset)", setupToApply(s1, hearth1, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  hostSetup(s1, hearth1);
  PartLine pl0, pl1;
  loadPartLine("good", 0, pl0);
  loadPartLine("good", 1, pl1);
  s1.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s1.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s1.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) { if (i == 0) s1.injectURC("+MTREADY"); };
  s1.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth1.poll();
  g_yieldAdvanceMs = 0;

  MockStream s2;
  HearthClass hearth2;
  hostSetup(s2, hearth2);
  g_host.coprocResetFalse = true;
  scriptBeginFor(s2, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.3.0", "wifi");
  hearth2.begin(s2);
  hearth2.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("the confirm's begin returns true (no reset)", hearth2.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  HearthManifest m;
  check("the manifest is written (no reset)",
        hearth2.update.stage().loadManifest(m) && m.productVersion == 66560);
  check("the state is gone (no reset)", !hearth2.update.stage().haveState());
  check("the staged bundle is gone (no reset)", !hearth2.update.stage().stagedExists());
  check("no second AT+MTOTA=1 (no reset)", s2.scriptDrained());
  check("nothing unexpected on the wire (no reset)", s2.unexpected().empty());
}

/* Case 5 (6b): the first-boot failure. The new sketch cannot reach Hearth
 * (a silent stream): the declaration times out three times, host-prev.bin
 * is re-staged, the state becomes HOST_CONFIRM, and the sketch reboots. */
static void test_first_boot_failure(void) {
  std::string fx;
  MockStream s1;
  HearthClass hearth1;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (failure)", setupToApply(s1, hearth1, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  hostSetup(s1, hearth1);
  PartLine pl0, pl1;
  loadPartLine("good", 0, pl0);
  loadPartLine("good", 1, pl1);
  s1.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s1.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s1.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) { if (i == 0) s1.injectURC("+MTREADY"); };
  s1.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth1.poll();
  g_yieldAdvanceMs = 0;

  /* The new sketch after the reboot: a silent stream (no answers at all). */
  MockStream s2;
  HearthClass hearth2;
  hostSetup(s2, hearth2);
  hearth2.begin(s2);
  hearth2.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("the failure's begin returns false", !hearth2.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  /* The declaration went out exactly three times. */
  check("the declaration went out exactly 3 times (failure)",
        s2.unexpected().size() == 3
        && s2.unexpected()[0] == "AT+MTSWVER=66560,\"1.4.0\""
        && s2.unexpected()[1] == "AT+MTSWVER=66560,\"1.4.0\""
        && s2.unexpected()[2] == "AT+MTSWVER=66560,\"1.4.0\"");
  /* host-prev.bin is re-staged whole. */
  check("one stageImage with hostPrevPath (failure)", g_host.stage.size() == 1);
  if (g_host.stage.size() == 1) {
    const StageRec &r = g_host.stage[0];
    check("the stageImage path is hostPrevPath (failure)", r.path == hearth2.update.stage().hostPrevPath());
    check("the stageImage off is 0 (failure)", r.off == 0);
    check("the stageImage len is 300 (failure)", r.len == 300);
  }
  HearthUpdateState st;
  check("the state loads phase HOST_CONFIRM (failure)",
        hearth2.update.stage().loadState(st) && st.phase == HEARTH_PHASE_HOST_CONFIRM);
  check("one reboot (failure)", g_host.reboots == 1);
  check("the status is FAILED (failure)", hearth2.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_HOST (failure)", hearth2.update.status().error == HEARTH_UPDATE_ERR_HOST);
}

/* Case 6 (6b): HOST_CONFIRM with the link down. The previous sketch is
 * running again after a failed first boot, and the link is down too: the
 * declaration times out, the state is cleared, and begin() returns false
 * with no stageImage and no reboot (the loop must stop). */
static void test_host_confirm_link_down(void) {
  /* Build the fs from case 5's failure: a state with phase HOST_CONFIRM. */
  HearthFsMem fs;
  HearthUpdateStage stg;
  check("stage begin (confirm down)", stg.begin(fs));
  HearthUpdateState st;
  memset(&st, 0, sizeof(st));
  st.phase = HEARTH_PHASE_HOST_CONFIRM;
  st.targetVersion = 66560;
  snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s", "1.4.0");
  check("the HOST_CONFIRM state is saved (confirm down)", stg.saveState(st));

  MockStream s;
  HearthClass hearth;
  hostSetup(s, hearth);
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("the confirm-down begin returns false", !hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  check("the state is gone (confirm down)", !hearth.update.stage().haveState());
  check("zero stageImage (confirm down)", g_host.stage.size() == 0);
  check("zero reboot (confirm down)", g_host.reboots == 0);
  check("the status is FAILED (confirm down)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_LINK (confirm down)", hearth.update.status().error == HEARTH_UPDATE_ERR_LINK);
}

/* Case 7 (6b): HOST_CONFIRM with the link up. The previous sketch is
 * running again and the link answers: begin() completes, the state is
 * cleared, the status is FAILED with ERR_HOST (the host update did not
 * take), the manifest is untouched and the staged bundle kept. */
static void test_host_confirm_link_up(void) {
  HearthFsMem fs;
  HearthUpdateStage stg;
  check("stage begin (confirm up)", stg.begin(fs));
  HearthUpdateState st;
  memset(&st, 0, sizeof(st));
  st.phase = HEARTH_PHASE_HOST_CONFIRM;
  st.targetVersion = 66560;
  snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s", "1.4.0");
  check("the HOST_CONFIRM state is saved (confirm up)", stg.saveState(st));
  /* A manifest and a staged bundle, untouched by the confirm. */
  HearthManifest m;
  m.productVersion = 66304;
  snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
  snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
  check("the manifest is saved (confirm up)", stg.saveManifest(m));
  std::string fx;
  check("the fixture loads (confirm up)", loadFixture("fixtures/good.ota", fx));
  {
    HearthFile *f = fs.open("/hearth/staged.ota", "w");
    check("the staged file opens (confirm up)", f != 0);
    if (f) {
      f->write((const uint8_t *)fx.data(), fx.size());
      delete f;
    }
  }

  MockStream s;
  HearthClass hearth;
  hostSetup(s, hearth);
  /* The effective version is 66560 "1.4.0" (the manifest's last applied). */
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.3.0", "wifi");
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("the confirm-up begin returns true", hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;

  check("the state is gone (confirm up)", !hearth.update.stage().haveState());
  check("the status is FAILED (confirm up)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_HOST (confirm up)", hearth.update.status().error == HEARTH_UPDATE_ERR_HOST);
  check("no manifest file was changed (confirm up)",
        hearth.update.stage().loadManifest(m) && m.productVersion == 66304);
  check("the staged bundle is still there (confirm up)", hearth.update.stage().stagedExists());
  check("the script is drained (confirm up)", s.scriptDrained());
}

/* Case 8 (6b): no hooks (the host defaults, none installed). Case 1's flow
 * ends FAILED with ERR_HOST after the Hearth part succeeded: no state, the
 * staged bundle kept, zero reboots. */
static void test_no_hooks(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (no hooks)", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  /* Install no hooks: the host defaults (all null). */
  g_host = HostRec();

  PartLine pl0, pl1;
  loadPartLine("good", 0, pl0);
  loadPartLine("good", 1, pl1);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) { if (i == 0) s.injectURC("+MTREADY"); };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("exactly one flash call (no hooks)", fake.calls().size() == 1);
  check("no state (no hooks)", !hearth.update.stage().haveState());
  check("the staged bundle is kept (no hooks)", hearth.update.stage().stagedExists());
  check("zero reboots (no hooks)", g_host.reboots == 0);
  check("the status is FAILED (no hooks)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_HOST (no hooks)", hearth.update.status().error == HEARTH_UPDATE_ERR_HOST);
}

int main(void) {
  printf("\n===== HearthUpdate apply (task 6a) tests =====\n");
  test_order_and_arguments();
  test_nrf_fw_only();
  test_three_failed_no_retained();
  test_retained_rollback();
  test_flasher_error_then_success();
  test_no_ready_then_success();
  printf("\n===== HearthUpdate apply (task 6b) tests =====\n");
  test_both_parts();
  test_host_only();
  test_first_boot_confirm();
  test_first_boot_confirm_no_reset();
  test_first_boot_failure();
  test_host_confirm_link_down();
  test_host_confirm_link_up();
  test_no_hooks();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

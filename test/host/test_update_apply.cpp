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
                         uint32_t cfgSize, bool withManifest, const char *mtver = "1.2.0",
                         bool withCfg = true) {
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
  g_yieldAdvanceMs = 50;
  bool begun;
  if (withCfg) {
    HearthUpdateConfig cfg;
    cfg.resetPin = 15;
    cfg.resetActiveLow = true;
    cfg.strapPin = 14;
    cfg.strapActiveLow = true;
    begun = hearth.update.begin(cfgSize, baseStr, cfg);
  } else {
    begun = hearth.update.begin(cfgSize, baseStr);
  }
  g_yieldAdvanceMs = 0;
  check("update begin returns true", begun);
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
  /* The three verifies after the attempts answer 9.9.9 (neither the
   * pre-apply 1.2.0 nor the bundle's 1.3.0): every attempt booted a
   * version that is not what was running before, so the rollback is the
   * right thing and F2b's I7 skip (its verdict answers the pre-apply
   * 1.2.0 here) does not fire. */
  s.expect("AT+MTVER?", "+MTVER:9.9.9\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:9.9.9\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:9.9.9\r\nOK\r\n");
  /* I7: the pre-apply check answers the same third version. */
  s.expect("AT+MTVER?", "+MTVER:9.9.9\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  /* After the rollback flash the co-processor runs the retained 1.2.0. */
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
    s.injectURC("+MTREADY");  /* I6: the flasher's own exit reset (B668) reboots it */
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
  /* M6: the host part's failure (null hooks) re-declares the version in
   * force before it reports FAILED. */
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
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

/*
 * Task 6c: the resume of a Hearth-part flash after a power loss. The host
 * lost power in the middle of the flash: the state file says phase FW with
 * the attempts spent so far, the staged bundle is still on the
 * filesystem, and the co-processor may be in its recovery bootloader with
 * a half-written application (it may not answer at all). The starting
 * state is built directly on a fresh HearthFsMem before begin(): the state
 * file, a manifest when the case has one, and the staged bundle written as
 * /hearth/staged.ota from fixtures/good.ota (6b's case 7 shows exactly
 * this). begin() then runs, and its phase-FW branch (before the
 * declaration and the link commands) resumes the flash.
 */

/* Write the staged bundle from the fixture's bytes. */
static bool seedStaged(HearthFsMem &fs, const std::string &fx) {
  HearthFile *f = fs.open("/hearth/staged.ota", "w");
  if (!f) {
    return false;
  }
  size_t n = f->write((const uint8_t *)fx.data(), fx.size());
  delete f;
  return n == fx.size();
}

/* Save the phase-FW state record on the given fs. */
static bool seedFwState(HearthFsMem &fs, uint8_t attempts, uint32_t target,
                        const char *targetStr, uint8_t fwPart, uint8_t hostPart) {
  HearthUpdateStage stg;
  if (!stg.begin(fs)) {
    return false;
  }
  HearthUpdateState st;
  memset(&st, 0, sizeof(st));
  st.phase = HEARTH_PHASE_FW;
  st.attempts = attempts;
  st.targetVersion = target;
  snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s", targetStr);
  st.fwPart = fwPart;
  st.hostPart = hostPart;
  bool r = stg.saveState(st);
  return r;
}

/* Case 1 (6c): the resume, the link up, fw-only. State {FW, attempts 1,
 * target 66560 "1.4.0", fwPart 0, hostPart 0xFF}, the manifest {66304,
 * "1.3.0", host "1.4.0"} and the staged bundle. The resume declares the
 * state's target, the interrupted attempt counts (attempt 2 is the
 * resume's first flash), and after the success tail the normal sequence
 * declares the NEW effective version. */
static void test_resume_link_up(void) {
  std::string fx;
  check("the fixture loads (resume up)", loadFixture("fixtures/good.ota", fx));
  HearthFsMem fs;
  check("the staged bundle is seeded (resume up)", seedStaged(fs, fx));
  {
    HearthUpdateStage stg;
    stg.begin(fs);
    HearthManifest m;
    m.productVersion = 66304;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("the manifest is saved (resume up)", stg.saveManifest(m));
  }
  check("the FW state is seeded (resume up)",
        seedFwState(fs, 1, 66560, "1.4.0", 0, 0xFF));
  {
    HearthUpdateState dbg;
    if (HearthUpdateStage().begin(fs) && HearthUpdateStage().loadState(dbg)) {
    } else {
    }
  }

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  PartLine pl;
  check("the part|good|0 line loads (resume up)", loadPartLine("good", 0, pl));
  {
    HearthUpdateState dbg;
    if (hearth.update.stage().loadState(dbg)) {
    }
  }
  /* The resume's declaration, the flash's own commands, and begin()'s own
   * sequence (which declares the NEW effective version, 66560 "1.4.0",
   * from the manifest the success tail wrote). */
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.3.0", "wifi");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      check("the resume declaration had gone out before the flash (resume up)",
            s.nextExpected() == "AT+MTVER?");
      HearthUpdateState st2;
      check("the state loads with phase FW, attempts 2, inside the flash (resume up)",
            hearth.update.stage().loadState(st2)
            && st2.phase == HEARTH_PHASE_FW
            && st2.attempts == 2);
      s.injectURC("+MTREADY");
    }
  };
  g_yieldAdvanceMs = 50;
  check("the resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly one flash call (resume up)", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("the off matches apply_parts.txt part|good|0 (resume up)", c.off == pl.off);
    check("the len matches apply_parts.txt part|good|0 (resume up)", c.len == pl.len);
    check("the sha256 matches apply_parts.txt part|good|0 (resume up)", memcmp(c.sha256, pl.sha, 32) == 0);
    std::vector<uint8_t> want(fx.begin() + pl.off, fx.begin() + pl.off + pl.len);
    check("the image read back equals the fixture's bytes (resume up)", c.image == want);
  }
  HearthManifest m2;
  check("the manifest is 66560 / 1.4.0 / host 1.4.0 (resume up)",
        hearth.update.stage().loadManifest(m2)
        && m2.productVersion == 66560
        && strcmp(m2.productVersionString, "1.4.0") == 0
        && strcmp(m2.hostVersion, "1.4.0") == 0);
  char rt[33], rv[33];
  uint32_t rl = 0;
  check("the retained image is the new part (resume up)",
        hearth.update.stage().retainedFwInfo(rt, rv, rl)
        && strcmp(rt, "ESP32-C6 Hearth") == 0 && strcmp(rv, "1.3.0") == 0 && rl == 1000);
  check("the state is gone (resume up)", !hearth.update.stage().haveState());
  check("the staged bundle is gone (resume up)", !hearth.update.stage().stagedExists());
  check("the status is IDLE (resume up)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the error is OK (resume up)", hearth.update.status().error == HEARTH_UPDATE_OK);
  check("the effectiveVersion is 66560 (resume up)", hearth.update.status().effectiveVersion == 66560);
  check("hearthVersion is the new running version (resume up)",
        strcmp(hearth.update.status().hearthVersion, "1.3.0") == 0);
  check("the script is drained (resume up)", s.scriptDrained());
  check("nothing unexpected on the wire (resume up)", s.unexpected().empty());
}

/* Case 2 (6c): the resume, the co-processor silent until the flash. As
 * case 1, but the script does not answer the resume's declaration (the
 * co-processor is in its recovery bootloader): the declaration goes to
 * unexpected(), no reply comes (the command timeout runs out), and the
 * flash runs anyway. */
static void test_resume_silent_until_flash(void) {
  std::string fx;
  check("the fixture loads (resume silent)", loadFixture("fixtures/good.ota", fx));
  HearthFsMem fs;
  check("the staged bundle is seeded (resume silent)", seedStaged(fs, fx));
  {
    HearthUpdateStage stg;
    stg.begin(fs);
    HearthManifest m;
    m.productVersion = 66304;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("the manifest is saved (resume silent)", stg.saveManifest(m));
  }
  check("the FW state is seeded (resume silent)",
        seedFwState(fs, 1, 66560, "1.4.0", 0, 0xFF));

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  PartLine pl;
  check("the part|good|0 line loads (resume silent)", loadPartLine("good", 0, pl));
  /* The script starts at AT+MTVER?: the resume's declaration is not in
   * it, and an unmatched command gets no reply and does not consume the
   * script. */
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.3.0", "wifi");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      s.injectURC("+MTREADY");
    }
  };
  g_yieldAdvanceMs = 50;
  check("the silent resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly one flash call (resume silent)", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("the off matches apply_parts.txt part|good|0 (resume silent)", c.off == pl.off);
    check("the len matches apply_parts.txt part|good|0 (resume silent)", c.len == pl.len);
    check("the sha256 matches apply_parts.txt part|good|0 (resume silent)", memcmp(c.sha256, pl.sha, 32) == 0);
  }
  check("unexpected() holds exactly one entry (resume silent)", s.unexpected().size() == 1);
  if (s.unexpected().size() == 1) {
    check("the one unexpected entry is the resume declaration (resume silent)",
          s.unexpected()[0] == "AT+MTSWVER=66560,\"1.4.0\"");
  }
  check("the state is gone (resume silent)", !hearth.update.stage().haveState());
  check("the staged bundle is gone (resume silent)", !hearth.update.stage().stagedExists());
  check("the status is IDLE (resume silent)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the effectiveVersion is 66560 (resume silent)",
        hearth.update.status().effectiveVersion == 66560);
  check("the script is drained (resume silent)", s.scriptDrained());
}

/* Case 3 (6c): the resume after three attempts. The state says three
 * attempts were already spent: no declaration, no flash at all, straight
 * to the failure tail (the old version re-declared, the requestor on, the
 * state cleared, the staged bundle kept), and begin() ends with the FAILED
 * status and the link up. */
static void test_resume_three_attempts(void) {
  std::string fx;
  check("the fixture loads (resume 3)", loadFixture("fixtures/good.ota", fx));
  HearthFsMem fs;
  check("the staged bundle is seeded (resume 3)", seedStaged(fs, fx));
  {
    HearthUpdateStage stg;
    stg.begin(fs);
    HearthManifest m;
    m.productVersion = 66304;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("the manifest is saved (resume 3)", stg.saveManifest(m));
  }
  check("the FW state is seeded with three attempts (resume 3)",
        seedFwState(fs, 3, 66560, "1.4.0", 0, 0xFF));

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  /* The failure tail (the old version re-declared, no rollback image
   * here) and begin()'s own sequence (the effective version, still 66304
   * "1.3.0", with the running version 1.2.0). Final review I6: the
   * failure tail arms the expected reboot and waits for the +MTREADY the
   * last attempt's flasher exit (B668) owes it before the declaration, so
   * the tail's script starts with a +MTREADY injection, not a command. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("the 3-attempts resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  check("zero flash calls (resume 3)", fake.calls().size() == 0);
  check("the state is gone (resume 3)", !hearth.update.stage().haveState());
  check("the staged bundle is kept (resume 3)", hearth.update.stage().stagedExists());
  check("the status is FAILED (resume 3)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_FLASH (resume 3)",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  check("the script is drained (resume 3)", s.scriptDrained());
  check("nothing unexpected on the wire (resume 3)", s.unexpected().empty());
}

/* Case 4 (6c): the resume with the remaining attempts failing. State
 * {FW, attempts 1, ...} and a retained image of 500 known bytes for
 * "ESP32-C6 Hearth" "1.2.0": the resume flashes attempts 2 and 3 (both
 * answer 1.2.0, not the part's version), then the failure tail re-declares
 * the old version, flashes the retained image, and gives up with FAILED
 * and the staged bundle kept. */
static void test_resume_remaining_fail_rollback(void) {
  std::string fx;
  check("the fixture loads (resume roll)", loadFixture("fixtures/good.ota", fx));
  HearthFsMem fs;
  check("the staged bundle is seeded (resume roll)", seedStaged(fs, fx));
  {
    HearthUpdateStage stg;
    stg.begin(fs);
    HearthManifest m;
    m.productVersion = 66304;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("the manifest is saved (resume roll)", stg.saveManifest(m));
  }
  check("the FW state is seeded (resume roll)", seedFwState(fs, 1, 66560, "1.4.0", 0, 0xFF));
  /* A retained image of 500 known bytes, through the fs (6a's case 5
   * shows how). */
  std::vector<uint8_t> retBytes(500);
  for (int i = 0; i < 500; i++) {
    retBytes[i] = (uint8_t)(i * 7 + 3);
  }
  HearthUpdateStage stg2;
  check("stage begin for the retained seed (resume roll)", stg2.begin(fs));
  {
    HearthFile *f = fs.open("/hearth/fw-scratch.bin", "w");
    check("the scratch file opens (resume roll)", f != 0);
    if (f) {
      check("the 500 bytes are written (resume roll)",
            f->write(retBytes.data(), retBytes.size()) == retBytes.size());
      delete f;
    }
    f = fs.open("/hearth/fw-scratch.bin", "r");
    check("the scratch file reopens (resume roll)", f != 0);
    if (f) {
      check("the retained image is written (resume roll)",
            stg2.retainFwPart(*f, 0, 500, "ESP32-C6 Hearth", "1.2.0"));
      delete f;
    }
  }

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  /* The resume's declaration, the two remaining attempts (both answer
   * 1.2.0), the failure tail (I7's pre-apply check, the old version
   * re-declared, the rollback flash, its AT+MTVER?) and begin()'s own
   * sequence. */
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  fake.onFlash = [&](int i) {
    (void)i;
    s.injectURC("+MTREADY");
  };
  g_yieldAdvanceMs = 50;
  check("the roll resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("exactly three flash calls (resume roll)", calls.size() == 3);
  if (calls.size() == 3) {
    check("the third flash's image equals the 500 retained bytes (resume roll)",
          calls[2].image == retBytes);
  }
  check("the staged bundle is kept (resume roll)", hearth.update.stage().stagedExists());
  check("the state is gone (resume roll)", !hearth.update.stage().haveState());
  check("the status is FAILED (resume roll)", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is ERR_FLASH (resume roll)",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  check("the script is drained (resume roll)", s.scriptDrained());
  check("nothing unexpected on the wire (resume roll)", s.unexpected().empty());
}

/* Case 5 (6c): the resume with a host part. No manifest; the state says
 * the host part was selected (hostPart 1). The resume flashes (one
 * attempt, success), the success tail stages the host part through the
 * hook and reboots; on the host test the reboot hook returns, so begin()
 * returns at once with the state the host apply left (phase HOST). */
static void test_resume_with_host_part(void) {
  std::string fx;
  check("the fixture loads (resume host)", loadFixture("fixtures/good.ota", fx));
  HearthFsMem fs;
  check("the staged bundle is seeded (resume host)", seedStaged(fs, fx));
  check("the FW state is seeded with hostPart 1 (resume host)",
        seedFwState(fs, 1, 66560, "1.4.0", 0, 1));

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  hostSetup(s, hearth);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  PartLine pl1;
  check("the part|good|1 line loads (resume host)", loadPartLine("good", 1, pl1));
  /* The resume's declaration, the flash, and the requestor on from the
   * success tail; nothing after (the reboot hook returns, begin() does). */
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      s.injectURC("+MTREADY");
    }
  };
  g_yieldAdvanceMs = 50;
  check("the host resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  check("exactly one flash call (resume host)", fake.calls().size() == 1);
  check("one stageImage call (resume host)", g_host.stage.size() == 1);
  if (g_host.stage.size() == 1) {
    const StageRec &r = g_host.stage[0];
    check("the stageImage off matches part|good|1 (resume host)", r.off == pl1.off);
    check("the stageImage len matches part|good|1 (resume host)", r.len == pl1.len);
  }
  check("one reboot (resume host)", g_host.reboots == 1);
  HearthUpdateState st2;
  check("the state loads phase HOST (resume host)",
        hearth.update.stage().loadState(st2) && st2.phase == HEARTH_PHASE_HOST);
  check("the script is drained (resume host)", s.scriptDrained());
  check("nothing unexpected on the wire (resume host)", s.unexpected().empty());
}

/* Case 6 (6c): the FW state with no staged bundle. There is nothing to
 * resume: the state is cleared and begin() runs its normal sequence as if
 * there had been no state file. */
static void test_resume_no_staged(void) {
  HearthFsMem fs;
  {
    HearthUpdateStage stg;
    stg.begin(fs);
    HearthManifest m;
    m.productVersion = 66304;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("the manifest is saved (resume none)", stg.saveManifest(m));
  }
  check("the FW state is seeded (resume none)", seedFwState(fs, 1, 66560, "1.4.0", 0, 0xFF));

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;

  /* begin()'s own sequence (the effective version 66304 "1.3.0" from the
   * manifest, the running version 1.2.0). No resume command at all. */
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("the no-staged resume's begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;

  check("zero flash calls (resume none)", fake.calls().size() == 0);
  check("the state is gone (resume none)", !hearth.update.stage().haveState());
  check("the status is IDLE (resume none)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the script is drained (resume none)", s.scriptDrained());
  check("nothing unexpected on the wire (resume none)", s.unexpected().empty());
}

/*
 * Task 7a2 (bug B662): the refusal of a gzip host part. The RP2350 OTA
 * bootloader inflates only a file that begins with the gzip magic at byte
 * 0, but the host part is staged in place inside the bundle, whose byte 0
 * is the Matter OTA header. A selected gzip host part would be flashed
 * still compressed and the host would not boot, so the library refuses it
 * (verdict 0, reason 6) before any part digest is verified. The fixture
 * host-gz.ota is good.ota's two parts, byte for byte and signed, with the
 * host part's flags bit 0 set.
 */

/* Case a: the bundle's host part is selected (no manifest) and refuses. */
static void test_host_gz_refused(void) {
  std::string fx;
  check("host-gz fixture loads (gz refused)",
        loadFixture("fixtures/host-gz.ota", fx));
  HearthFsMem fs;
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("begin returns true (gz refused)", hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=0,6");

  check("the status is FAILED (gz refused)",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("the error is HEARTH_UPDATE_ERR_BUNDLE (gz refused)",
        hearth.update.status().error == HEARTH_UPDATE_ERR_BUNDLE);
  check("the reason is 6 (gz refused)", hearth.update.status().reason == 6);
  check("no staged bundle (gz refused)", !hearth.update.stage().stagedExists());
  check("the script is drained (gz refused)", s.scriptDrained());
  check("nothing unexpected on the wire (gz refused)", s.unexpected().empty());
}

/* Case b: the same bundle with the manifest's host version 1.4.0, the host
 * part not selected: the Hearth part alone is staged and the apply proceeds
 * (the verdict is 1, the state WAIT_APPLY, as in good.ota's fw-only apply). */
static void test_host_gz_not_selected_applies(void) {
  std::string fx;
  check("host-gz fixture loads (gz not selected)",
        loadFixture("fixtures/host-gz.ota", fx));
  HearthFsMem fs;
  HearthUpdateStage stg;
  check("stage begin for the manifest seed (gz not selected)", stg.begin(fs));
  HearthManifest m;
  m.productVersion = 66304;
  snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
  snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
  check("the manifest is saved (gz not selected)", stg.saveManifest(m));

  MockStream s;
  HearthClass hearth;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("begin returns true (gz not selected)", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");

  check("the verdict is 1 and the state is WAIT_APPLY (gz not selected)",
        hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);
  check("the staged bundle is kept (gz not selected)", hearth.update.stage().stagedExists());
  check("the script is drained (gz not selected)", s.scriptDrained());
  check("nothing unexpected on the wire (gz not selected)", s.unexpected().empty());
}

/*
 * Task 7a3: the two rules the plan states that were never implemented.
 * Case 1: the begin() of a firmware 1.2.0 or earlier, which has no
 * AT+MTSWVER: the declaration gets the ordinary unknown command answer
 * (+MTERR:8 then ERROR) and begin() reports UNAVAILABLE, returns true and
 * sends nothing further. Cases 2 to 4: the B632 settle wait, at least 2 s
 * after a commissioning complete (+MTEVT:3) before any reset the update
 * drives, and no wait without one (or an old one).
 */

/* Case 1: the firmware without FOTA. The declaration is the only command
 * out: +MTERR:8 then ERROR, no AT+CGMM or anything after. */
static void test_begin_firmware_without_fota(void) {
  HearthFsMem fs;
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "+MTERR:8\r\nERROR\r\n");
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("begin returns true (no FOTA)", hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  check("the status is UNAVAILABLE (no FOTA)",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("the error is OK (no FOTA)",
        hearth.update.status().error == HEARTH_UPDATE_OK);
  check("the script is drained (no FOTA)", s.scriptDrained());
  check("nothing else went out (no FOTA)", s.unexpected().empty());
}

/* The B632 settle wait, cases 2 to 4. setupToApply's signature takes the
 * fixture as its second argument (a local alias keeps the call below
 * readable). */
static bool setupToApply7a3(MockStream &s, HearthClass &hearth, HearthFsMem &fs,
                            FlasherFake &fake, const char *fixture, const char *swver) {
  std::string fx;
  return setupToApply(s, hearth, fs, fake, fx, fixture, swver,
                      "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true, "1.2.0");
}

/* Case 2: the commissioning lands while the apply waits, and the first
 * flash is held for the settle: the recorded time minus t0 is at least
 * 2000. */
static void test_settle_wait_after_commissioning(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (settle)", setupToApply7a3(s, hearth, fs, fake, "good",
        "AT+MTSWVER=66304,\"1.3.0\""));
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  uint32_t flashAt = 0;
  fake.onFlash = [&](int i) {
    if (i == 0) {
      flashAt = millis();
      s.injectURC("+MTREADY");
    }
  };
  uint32_t t0 = millis();
  s.injectURC("+MTEVT:3");
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the flash is held at least 2 s after the commissioning (settle)",
        flashAt >= t0 && flashAt - t0 >= 2000);
  check("one flash call (settle)", fake.calls().size() == 1);
  check("the status is IDLE (settle)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the script is drained (settle)", s.scriptDrained());
  check("nothing unexpected on the wire (settle)", s.unexpected().empty());
}

/* Case 3: no commissioning at all: the same apply, the flash is reached
 * without the wait, the recorded time minus t0 is below 2000. */
static void test_no_settle_without_commissioning(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (no settle)", setupToApply7a3(s, hearth, fs, fake, "good",
        "AT+MTSWVER=66304,\"1.3.0\""));
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  uint32_t flashAt = 0;
  fake.onFlash = [&](int i) {
    if (i == 0) {
      flashAt = millis();
      s.injectURC("+MTREADY");
    }
  };
  uint32_t t0 = millis();
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the flash is reached in under 2 s without a commissioning (no settle)",
        flashAt >= t0 && flashAt - t0 < 2000);
  check("one flash call (no settle)", fake.calls().size() == 1);
  check("the status is IDLE (no settle)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the script is drained (no settle)", s.scriptDrained());
  check("nothing unexpected on the wire (no settle)", s.unexpected().empty());
}

/* Case 4: an old commissioning does not wait: the +MTEVT:3 is injected
 * and polled, 2500 ms run, then the apply starts: the flash is reached
 * within 2000 ms of the apply poll's start. */
static void test_old_commissioning_does_not_wait(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("setup reaches WAIT_APPLY (old commissioning)", setupToApply7a3(s, hearth, fs, fake, "good",
        "AT+MTSWVER=66304,\"1.3.0\""));
  g_yieldAdvanceMs = 50;
  s.injectURC("+MTEVT:3");
  hearth.poll();
  g_yieldAdvanceMs = 0;
  delay(2500);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  uint32_t flashAt = 0;
  fake.onFlash = [&](int i) {
    if (i == 0) {
      flashAt = millis();
      s.injectURC("+MTREADY");
    }
  };
  uint32_t t0 = millis();
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the old commissioning does not hold the flash (old commissioning)",
        flashAt >= t0 && flashAt - t0 < 2000);
  check("one flash call (old commissioning)", fake.calls().size() == 1);
  check("the status is IDLE (old commissioning)", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("the script is drained (old commissioning)", s.scriptDrained());
  check("nothing unexpected on the wire (old commissioning)", s.unexpected().empty());
}

/*
 * Task 7c-fix1 (bug B666): the download baud switch must re-clock the
 * host's own UART. The co-processor answers AT+MTBAUD at the current rate
 * and only then switches (firmware core/mt/mt_at.c, cmd_mtbaud), so the
 * library re-clocks the link's own port (hearthRebaudLink) only on an OK
 * answer, and a refused switch is not retried. The device path itself is
 * proven by the RP2350 compile and the bench run; here a hook installed
 * through hearthSetBaudChanger() stands in for the re-clock, so the
 * cases below cover the gate: a refusal is never followed and never
 * retried, an acceptance is followed exactly once.
 */

static int g_b7fixCalls = 0;
static uint32_t g_b7fixBaud = 0;
static void b7fixBaudHook(uint32_t b) {
  g_b7fixCalls++;
  g_b7fixBaud = b;
}

/* Case a: the download starts (the way runDownload() starts it), but the
 * switch is refused. The hook is never called, a second poll sends no
 * second AT+MTBAUD (the script has none, unexpected() stays empty), and
 * the following +MTOTA:BLOCK is still pulled: the transfer goes on at the
 * old rate. */
static void test_b666_refused_not_followed(void) {
  std::string fx;
  check("fixture loads (b666 refused)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("begin returns true (b666 refused)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_b7fixCalls = 0;
  g_b7fixBaud = 0;
  hearth.update.hearthSetBaudChanger(b7fixBaudHook);

  /* The download's start: AVAILABLE, the refused switch, DOWNLOADING. */
  s.expect("AT+MTBAUD=921600", "+MTERR:1\r\nERROR\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the hook was never called (b666 refused)", g_b7fixCalls == 0);

  /* A second poll: no second AT+MTBAUD (the script has none, so
   * unexpected() must stay empty). */
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("no second AT+MTBAUD (b666 refused)", s.unexpected().empty());

  /* The following block is still pulled: the download goes on at the old
   * rate. */
  size_t blen = fx.size();
  if (blen > 1024) blen = 1024;
  s.expect("AT+MTOTAGET=0", blkAnswer(0, (uint32_t)blen, fx, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0," + std::to_string((unsigned)blen));
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the following block was still pulled (b666 refused)", s.scriptDrained());
  check("nothing unexpected on the wire (b666 refused)", s.unexpected().empty());
}

/* Case b: the same start with the switch answered OK: the hook is called
 * exactly once, with 921600. */
static void test_b666_accepted_followed_once(void) {
  std::string fx;
  check("fixture loads (b666 accepted)", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("begin returns true (b666 accepted)", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_b7fixCalls = 0;
  g_b7fixBaud = 0;
  hearth.update.hearthSetBaudChanger(b7fixBaudHook);

  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("the hook was called exactly once (b666 accepted)", g_b7fixCalls == 1);
  check("the hook got 921600 (b666 accepted)", g_b7fixBaud == 921600);
}

/*
 * Task 7c-fix2 (B667): the begin() script for an nRF54L15 that is not
 * commissioned yet: AT+MTSWVER answers OK, AT+MTOTA=1 answers +MTERR:8.
 * begin() must settle UNAVAILABLE and arm the retry; the nRF's fs need is
 * 3,670,016 B, so the fs is capped just above it (4 MiB, as test_update.cpp
 * does for the DE625 cases).
 */
static void scriptBeginB667(MockStream &s) {
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "nRF54L15 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,thread\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "+MTERR:8\r\nERROR\r\n");
}

static int g_b667Calls = 0;
static HearthUpdateStateEnum g_b667State = HEARTH_UPDATE_UNAVAILABLE;
static HearthUpdateErr g_b667Error = HEARTH_UPDATE_ERR_NO_FS;
static void b667Status(const HearthUpdateStatus &st) {
  g_b667Calls++;
  g_b667State = st.state;
  g_b667Error = st.error;
}

/* B667, case 1: begin() settles UNAVAILABLE on the requestor's 8 and
 * holds; a commissioning (the nRF wires its requestor on +MTEVT:3)
 * releases the retry on the next poll: AT+MTOTA=1 goes out, OK, the state
 * is IDLE and the status callback fired. */
static void test_b667_commissioning_releases_retry(void) {
  HearthFsMem fs;
  fs.setFreeLimit(4 * 1024 * 1024);
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_b667Calls = 0;
  g_b667State = HEARTH_UPDATE_UNAVAILABLE;
  g_b667Error = HEARTH_UPDATE_ERR_NO_FS;
  hearth.update.onStatus(b667Status);
  scriptBeginB667(s);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("b667: begin returns true", hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  check("b667: the state is UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("b667: the script is drained", s.scriptDrained());
  check("b667: nothing else went out on begin", s.unexpected().empty());
  check("b667: the status callback fired on begin", g_b667Calls == 1
        && g_b667State == HEARTH_UPDATE_UNAVAILABLE);

  /* A poll 10 s later: no commissioning and no 30 s deadline, so
   * nothing goes out. */
  delay(10000);
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667: the 10 s poll sends nothing", s.unexpected().empty());
  check("b667: still UNAVAILABLE after the quiet poll",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("b667: no extra status callback on the quiet poll", g_b667Calls == 1);

  /* The commissioning: the nRF wires its requestor, the next poll
   * retries and it goes. */
  s.injectURC("+MTEVT:3");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667: AT+MTOTA=1 went out on the commissioning poll", s.scriptDrained());
  check("b667: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("b667: the error is OK",
        hearth.update.status().error == HEARTH_UPDATE_OK);
  check("b667: the status callback fired on the retry", g_b667Calls == 2
        && g_b667State == HEARTH_UPDATE_IDLE);
  check("b667: nothing unexpected on the wire", s.unexpected().empty());
}

/* B667, case 2: the periodic path on a fresh setup. No commissioning:
 * the retry goes out on the 30 s deadline, holds UNAVAILABLE while it
 * keeps meeting 8, and goes IDLE when it finally gets OK. */
static void test_b667_periodic_retry(void) {
  HearthFsMem fs;
  fs.setFreeLimit(4 * 1024 * 1024);
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginB667(s);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("b667 periodic: begin returns true",
        hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  check("b667 periodic: the state is UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);

  /* 30 s on the clock, no commissioning: the deadline retry goes out
   * once and meets 8 again. */
  delay(30000);
  s.expect("AT+MTOTA=1", "+MTERR:8\r\nERROR\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  /* The poll put nothing but the retry on the wire: with no commissioning
   * and the 30 s deadline just met, the retry was the only command. The
   * MockStream counts a matched expectation as consumed, not unexpected,
   * so the proof is the drained script plus the quiet polls around it. */
  check("b667 periodic: the deadline retry went out once, no commissioning",
        s.scriptDrained());
  check("b667 periodic: still UNAVAILABLE on the 8",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("b667 periodic: nothing unexpected on the wire (8)", s.unexpected().empty());

  /* Another 30 s, and this time the requestor answers OK. */
  delay(30000);
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667 periodic: the next deadline retry went out", s.scriptDrained());
  check("b667 periodic: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("b667 periodic: the error is OK",
        hearth.update.status().error == HEARTH_UPDATE_OK);
  check("b667 periodic: nothing unexpected on the wire (OK)",
        s.unexpected().empty());
}

/* B667, case 3: the firmware without FOTA stays settled. AT+MTSWVER
 * itself answers +MTERR:8 (firmware 1.2.0 and earlier, the Task 7a3
 * branch of begin()), so the retry is never armed: a commissioning and
 * two 30 s deadlines later, nothing more has gone out and the state is
 * still UNAVAILABLE. */
static void test_b667_no_fota_not_retried(void) {
  HearthFsMem fs;
  fs.setFreeLimit(4 * 1024 * 1024);
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "+MTERR:8\r\nERROR\r\n");
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.resetActiveLow = true;
  cfg.strapPin = 14;
  cfg.strapActiveLow = true;
  g_yieldAdvanceMs = 50;
  check("b667 no fota: begin returns true",
        hearth.update.begin(0x10400, "1.4.0", cfg));
  g_yieldAdvanceMs = 0;
  check("b667 no fota: the state is UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  /* The 7a3 branch stops at the declaration: AT+MTOTA=1 was never sent
   * (a scripted expectation that no command consumes would hide a stray
   * one, so the wire proof here is the unexpected list alone). */
  check("b667 no fota: nothing else went out on begin", s.unexpected().empty());

  s.injectURC("+MTEVT:3");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667 no fota: the commissioning poll sends nothing",
        s.unexpected().empty());
  delay(60000);
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667 no fota: the first deadline poll sends nothing",
        s.unexpected().empty());
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("b667 no fota: the second deadline poll sends nothing",
        s.unexpected().empty());
  check("b667 no fota: still UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);
  check("b667 no fota: the error is OK",
        hearth.update.status().error == HEARTH_UPDATE_OK);
}

/*
 * Task 7c-fix4 (F669, B670, B671): the co-processor's pins for boards
 * without the variant macros, and FOTA that survives a co-processor
 * reboot. The host build has no pins and no port, so the pin paths of
 * hearthResetCoprocessor() are not exercised here (the RP2350 compile and
 * the controller's bench prove them); these tests cover what the host can
 * see: the pins accessor, begin() taking the pins from the owner, and the
 * re-probe after a co-processor reboot (B671).
 */
static void test_coproc_pins_accessor(void) {
  HearthClass hearth;
  HearthCoprocPins p = hearth.hearthCoprocPins();
  check("7cfix4: default pins are -1s",
        p.reset == -1 && p.strap == -1
        && p.resetActiveLow && p.strapActiveLow);
  hearth.coprocessorPins(2, 3);
  p = hearth.hearthCoprocPins();
  check("7cfix4: stored pins are {2, true, 3, true}",
        p.reset == 2 && p.resetActiveLow && p.strap == 3 && p.strapActiveLow);
  HearthClass h2;
  h2.coprocessorPins(5, 6, false, true);
  p = h2.hearthCoprocPins();
  check("7cfix4: explicit polarity is kept",
        p.reset == 5 && !p.resetActiveLow && p.strap == 6 && p.strapActiveLow);
}

/* 7cfix4: begin() with a default config (pins -1) takes the flasher's
 * pins from the owner's hearthCoprocPins(). Drive an apply through
 * FlasherFake and check the pins it was handed. The setup is done
 * manually (not through setupToApply, which hardcodes explicit pins)
 * so the default config's pins -1 reach begin() and are resolved from
 * the owner. */
static void test_begin_takes_owner_pins(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  hearth.coprocessorPins(2, 3);
  check("7cfix4 pins: fixture loads",
        loadFixture("fixtures/good.ota", fx));
  HearthUpdateStage stg;
  check("7cfix4 pins: seed manifest written", stg.begin(fs));
  {
    HearthManifest m;
    m.productVersion = 0x10300;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("7cfix4 pins: seed manifest saved", stg.saveManifest(m));
  }
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  g_yieldAdvanceMs = 50;
  check("7cfix4 pins: update begin returns true",
        hearth.update.begin(0x10300, "1.3.0"));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");
  check("7cfix4 pins: setup reaches WAIT_APPLY",
        hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);

  PartLine pl;
  check("7cfix4 pins: the part|good|0 line loads", loadPartLine("good", 0, pl));

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

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("7cfix4 pins: exactly one flash call", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("7cfix4 pins: the flasher was handed {2, true, 3, true}",
          c.pins.reset == 2 && c.pins.resetActiveLow
          && c.pins.strap == 3 && c.pins.strapActiveLow);
  }
  check("7cfix4 pins: the script is drained", s.scriptDrained());
  check("7cfix4 pins: nothing unexpected on the wire", s.unexpected().empty());
}

/* 7cfix4 (B671): the re-probe after an unexpected reboot. begin() to
 * IDLE, then +MTREADY, then the re-probe sends the declaration and the
 * requestor switch on the next poll. */
static void test_b671_reprobe_after_reboot(void) {
  HearthFsMem fs;
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("7cfix4 b671: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* The co-processor reboots: the re-probe goes out on the next poll. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671: the re-probe sent both commands", s.scriptDrained());
  check("7cfix4 b671: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("7cfix4 b671: nothing unexpected on the wire", s.unexpected().empty());
}

/* 7cfix4 (B671): the re-probe recovers the boot race. begin() meets
 * +MTERR:8 on AT+MTSWVER (UNAVAILABLE, settled for good), then a reboot
 * re-probes and the declaration answers OK this time. */
static void test_b671_reprobe_recovers_boot_race(void) {
  HearthFsMem fs;
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "+MTERR:8\r\nERROR\r\n");
  g_yieldAdvanceMs = 50;
  check("7cfix4 b671 race: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 race: the state is UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);

  /* The co-processor reboots and this time the declaration answers OK. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 race: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("7cfix4 b671 race: the script is drained", s.scriptDrained());
  check("7cfix4 b671 race: nothing unexpected on the wire", s.unexpected().empty());
}

/* 7cfix4 (B671): the re-probe arms the B667 retry. begin() to IDLE,
 * then a reboot, the re-probe's requestor switch meets 8 (UNAVAILABLE
 * with the retry armed), and a commissioning releases it. */
static void test_b671_reprobe_arms_b667_retry(void) {
  HearthFsMem fs;
  fs.setFreeLimit(4 * 1024 * 1024);
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "nRF54L15 Hearth", "1.2.0", "thread");
  g_yieldAdvanceMs = 50;
  check("7cfix4 b671 retry: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 retry: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* The co-processor reboots; the re-probe's requestor switch meets 8. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "+MTERR:8\r\nERROR\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 retry: the state is UNAVAILABLE",
        hearth.update.status().state == HEARTH_UPDATE_UNAVAILABLE);

  /* A commissioning releases the B667 retry. */
  s.injectURC("+MTEVT:3");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 retry: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("7cfix4 b671 retry: the script is drained", s.scriptDrained());
  check("7cfix4 b671 retry: nothing unexpected on the wire", s.unexpected().empty());
}

/* 7cfix4 (B671): no re-probe mid-transfer. A +MTREADY during a download
 * does not send AT+MTSWVER (the state is DOWNLOADING, not IDLE or
 * UNAVAILABLE). */
static void test_b671_no_reprobe_mid_transfer(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  scriptBeginFor(s, "AT+MTSWVER=66560,\"1.4.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  g_yieldAdvanceMs = 50;
  check("7cfix4 b671 mid: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 mid: the state is IDLE",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* Start a download that stops mid-transfer (beginWrite false). */
  runDownload(s, hearth, fx, "", false);
  check("7cfix4 b671 mid: the state is DOWNLOADING",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);

  /* A reboot mid-transfer: no re-probe (the state is not IDLE or
   * UNAVAILABLE). */
  s.injectURC("+MTREADY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 mid: nothing went out on the poll", s.unexpected().empty());
}

/* 7cfix4 (B671): no re-probe before begin(). A HearthClass whose update
 * was never begun: a +MTREADY and a poll send nothing. */
static void test_b671_no_reprobe_before_begin(void) {
  MockStream s;
  HearthClass hearth;
  hearth.begin(s);
  /* update was never begun: the state is DISABLED. */
  check("7cfix4 b671 nobegin: the state is DISABLED",
        hearth.update.status().state == HEARTH_UPDATE_DISABLED);
  s.injectURC("+MTREADY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("7cfix4 b671 nobegin: nothing went out", s.unexpected().empty());
  check("7cfix4 b671 nobegin: the script is drained", s.scriptDrained());
}

/*
 * Final review robustness round (task F2a): I1, I2, I4, M1, M5, M7, M8.
 * Each test below is named after the finding it covers.
 */

/* I1: a pull that aborts (the garbled answer twice, the same setup as
 * test_update.cpp's test_pull_garbled_twice_aborts) sends AT+MTOTA=0 and
 * then AT+MTOTA=1: the =0 turns the requestor off, and nothing else
 * turns it back on, so the abort re-arms it for the next offer. */
static void test_f2a_i1_abort_rearms_requestor(void) {
  std::string fx;
  check("f2a i1: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2a i1: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  std::string bad = blkAnswer(0, 1024, fx, 0);
  /* The same out-of-sequence offset corruption as the abort test in
   * test_update.cpp: both pulls fail to parse. */
  size_t idx = bad.find("MTOTABLK:0,96,");
  check("f2a i1: the second block line is where the helper put it",
        idx != std::string::npos);
  if (idx != std::string::npos) {
    bad[idx + strlen("MTOTABLK:0,")] = '7';  /* 96 -> 97 */
  }
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTOTAGET=0", bad);
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.expect("AT+MTOTA=0", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i1: the abort sent AT+MTOTA=0 then AT+MTOTA=1",
        s.scriptDrained() && s.unexpected().empty());
  check("f2a i1: the state is FAILED after the abort",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2a i1: the error is HEARTH_UPDATE_ERR_LINK",
        hearth.update.status().error == HEARTH_UPDATE_ERR_LINK);
  check("f2a i1: the partial staged file is removed",
        !fs.exists("/hearth/staged.ota") && !fs.exists("/hearth/staged.ota.tmp"));
}

/* I2a: a transfer the co-processor ends with +MTOTA:ERROR between pulls
 * leaves the staged write open. On the next poll the drain ends and
 * discards it (the partial .tmp is gone), and the next transfer's BLOCK 0
 * begins a FRESH file whose content equals only the new transfer's bytes.
 * The transfer here is a byte string the co-processor serves, not a signed
 * bundle, so nothing is verified on DOWNLOADED: the download stops after
 * the last block and the file content is read straight from the fs. */
static void test_f2a_i2a_error_discards_partial(void) {
  std::string fx;
  check("f2a i2a: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  /* A 3000-byte payload: three 1024-byte blocks of the first transfer,
   * then 999 + 500 for the second, so the two transfers have different
   * content at the same file offset. */
  std::string p1, p2;
  p1.resize(3000);
  for (size_t i = 0; i < p1.size(); i++) p1[i] = (char)(i * 3 + 1);
  p2.resize(1499);
  for (size_t i = 0; i < p2.size(); i++) p2[i] = (char)(i * 7 + 5);

  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2a i2a: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  /* The first transfer, two full blocks, then it stops (no DOWNLOADED):
   * the staged write is still open after the third block. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  for (uint32_t seq = 0; seq < 3; seq++) {
    s.expect("AT+MTOTAGET=" + std::to_string((unsigned)seq),
             blkAnswer(seq, 1024, p1, 1024 * (size_t)seq));
    s.expect("AT+MTOTAACK=" + std::to_string((unsigned)seq), "OK\r\n");
    s.injectURC("+MTOTA:BLOCK," + std::to_string((unsigned)seq) + ",1024");
    g_yieldAdvanceMs = 50;
    hearth.poll();
    g_yieldAdvanceMs = 0;
  }
  check("f2a i2a: the partial .tmp is open mid-download",
        fs.exists("/hearth/staged.ota.tmp"));
  check("f2a i2a: the state is DOWNLOADING mid-download",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);

  /* The co-processor ends the transfer between pulls. The ERROR clears
   * the download-baud want, so the drain also switches the link back to
   * the default rate before it discards the partial write. */
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.injectURC("+MTOTA:ERROR,session");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i2a: the state is FAILED after the ERROR line",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2a i2a: the detail is session",
        strcmp(hearth.update.status().detail, "session") == 0);
  check("f2a i2a: the partial .tmp is gone after the poll",
        !fs.exists("/hearth/staged.ota.tmp"));
  check("f2a i2a: the staged file was not renamed",
        !fs.exists("/hearth/staged.ota"));

  /* The next offer, 1499 bytes: its BLOCK 0 begins a fresh file holding
   * only the new transfer's bytes. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66562");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 999, p2, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,999");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=1", blkAnswer(1, 500, p2, 999));
  s.expect("AT+MTOTAACK=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,1,500");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i2a: the script is drained", s.scriptDrained());
  check("f2a i2a: nothing unexpected on the wire", s.unexpected().empty());
  check("f2a i2a: the staged file exists and holds exactly the new bytes",
        fs.files.count("/hearth/staged.ota.tmp") == 1
        && 0 == memcmp(fs.files["/hearth/staged.ota.tmp"].data(), p2.data(), p2.size()) && fs.files["/hearth/staged.ota.tmp"].size() == p2.size());
  check("f2a i2a: the state is DOWNLOADING in the second transfer",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);
}

/* I2b: a new transfer's BLOCK 0 while a write is open restarts it. The
 * old transfer's partial .tmp is discarded, the new transfer's first block
 * is pulled into a fresh file, and afterwards the staged file holds only
 * the bytes from the restart on. The transfer is a byte string (as I2a),
 * so nothing is verified on DOWNLOADED. */
static void test_f2a_i2b_block_zero_restarts_write(void) {
  std::string fx;
  check("f2a i2b: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  std::string p1, p2;
  p1.resize(1500);
  for (size_t i = 0; i < p1.size(); i++) p1[i] = (char)(i * 3 + 1);
  /* The new transfer's payload, 1900 bytes: block 0 (900) and block 1
   * (1000), both within the 1024-byte block max. */
  p2.resize(1900);
  for (size_t i = 0; i < p2.size(); i++) p2[i] = (char)(i * 7 + 5);

  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2a i2b: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  /* The first transfer: AVAILABLE, DOWNLOADING, then block 0 (1024 bytes)
   * pulled, the write open, the transfer stops. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 1024, p1, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i2b: the partial .tmp is open mid-download",
        fs.exists("/hearth/staged.ota.tmp"));

  /* The new transfer: AVAILABLE (no baud switch, the link is at the
   * download baud already), and its BLOCK 0 (900 bytes of p2). The drain
   * discards the old .tmp and pulls the new block into a fresh file. */
  s.injectURC("+MTOTA:AVAILABLE,66562");
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 900, p2, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,900");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  uint32_t tailLen = (uint32_t)(p2.size() - 900);
  s.expect("AT+MTOTAGET=1", blkAnswer(1, tailLen, p2, 900));
  s.expect("AT+MTOTAACK=1", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,1," + std::to_string((unsigned)tailLen));
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i2b: the script is drained", s.scriptDrained());
  check("f2a i2b: nothing unexpected on the wire", s.unexpected().empty());
  check("f2a i2b: the staged file holds exactly the new transfer's bytes",
        fs.files.count("/hearth/staged.ota.tmp") == 1
        && fs.files["/hearth/staged.ota.tmp"].size() == p2.size()
        && 0 == memcmp(fs.files["/hearth/staged.ota.tmp"].data(), p2.data(), p2.size()));
  check("f2a i2b: the state is DOWNLOADING in the second transfer",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING);
}

/* I4: a drain while the link's busy gate is held returns at once, before
 * it touches the wire or the stage. The gate is held the way the tests do
 * for re-entrancy: HearthLink refuses a re-entrant command with
 * HEARTH_CMD_REENTRANT and a re-entrant poll is a no-op (test_hearthlink),
 * and the host build's MockStream busyHeld flag is part of that gate, so
 * a test holds it across a HearthLink exchange. With a block pending, the
 * nested drain pulls nothing (the flag survives), and the next poll pulls
 * it. */
static void test_f2a_i4_busy_link_skips_drain(void) {
  std::string fx;
  check("f2a i4: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2a i4: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  /* Start a download and pull block 0 (the staged write is open now). */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  s.expect("AT+MTOTAGET=0", blkAnswer(0, 1024, fx, 0));
  s.expect("AT+MTOTAACK=0", "OK\r\n");
  s.injectURC("+MTOTA:BLOCK,0,1024");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i4: the first block was pulled", s.scriptDrained());

  /* The block is pending, the link is busy (the gate held across the
   * exchange). A nested drain returns at once: the gate is busy, so it
   * does not pull the block, and the flag survives. */
  size_t blen = fx.size() - 1024;
  if (blen > 1024) blen = 1024;
  /* The block is announced; the gate is held for the whole poll, so the
   * URC is dispatched (setting the pending flag) but the poll's own tail
   * drain finds the link busy and returns before it pulls the block. */
  s.injectURC("+MTOTA:BLOCK,1," + std::to_string((unsigned)blen));
  g_linkBusyHeld = true;
  g_yieldAdvanceMs = 50;
  hearth.poll();  /* dispatches the URC; its tail drain bails on busy() */
  g_linkBusyHeld = false;
  g_yieldAdvanceMs = 0;
  /* The URC set the pending flag, but the drain bailed on busy(), so no
   * AT+MTOTAGET went out: the script is still drained (nothing consumed)
   * and nothing unexpected. */
  check("f2a i4: the block is pending but not pulled (the drain bailed)",
        hearth.update.status().state == HEARTH_UPDATE_DOWNLOADING
        && s.scriptDrained() && s.unexpected().empty());

  /* The exchange ends (the latch released, the gate down). The next poll
  * pulls the pending block. */
  s.expect("AT+MTOTAGET=1", blkAnswer(1, (uint32_t)blen, fx, 1024));
  s.expect("AT+MTOTAACK=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a i4: the next poll pulled the pending block", s.scriptDrained());
  check("f2a i4: nothing unexpected on the wire", s.unexpected().empty());
}

/* M5: a +MTOTA:APPLY in any state but HEARTH_UPDATE_WAIT_APPLY is
 * ignored: no flasher call, no command sent, the state unchanged. The
 * state here is IDLE (no verdict, no staged bundle), the only state an
 * APPLY with no verdict in this boot can arrive in (the co-processor
 * survived a host reboot). */
static void test_f2a_m5_apply_ignored_outside_wait_apply(void) {
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  g_yieldAdvanceMs = 50;
  check("f2a m5: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("f2a m5: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);

  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a m5: no flasher call", fake.calls().size() == 0);
  check("f2a m5: nothing went on the wire", s.unexpected().empty());
  check("f2a m5: the script is drained", s.scriptDrained());
  check("f2a m5: the state is unchanged (still IDLE)",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("f2a m5: no error", hearth.update.status().error == HEARTH_UPDATE_OK);
}

/* M7: a stagedBeginWrite() while a staged.ota exists removes the staged
 * file first, so two bundles never coexist on the filesystem. The stage
 * is used directly (the fs is attached to a HearthClass so the stage's
 * paths resolve), and the .tmp is the only file present while the write
 * is open. */
static void test_f2a_m7_begin_write_removes_staged(void) {
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  check("f2a m7: the stage begins", hearth.update.stage().begin(fs));

  /* A staged.ota already there (the bundle kept for a manual retry). */
  std::string old(2048, '\0');
  for (size_t i = 0; i < old.size(); i++) old[i] = (char)(i + 1);
  {
    HearthFile *f = fs.open("/hearth/staged.ota", "w");
    check("f2a m7: the staged file opens", f != 0);
    if (f) {
      check("f2a m7: the 2048 bytes are written",
            f->write((const uint8_t *)old.data(), old.size()) == old.size());
      delete f;
    }
  }
  check("f2a m7: the staged file exists before the write",
        fs.exists("/hearth/staged.ota"));

  check("f2a m7: the staged write begins", hearth.update.stage().stagedBeginWrite());
  check("f2a m7: the staged file is removed by the beginWrite",
        !fs.exists("/hearth/staged.ota"));
  check("f2a m7: the .tmp is the only file present while the write is open",
        fs.exists("/hearth/staged.ota.tmp"));

  /* Append a few bytes and end the write: the rename puts the new bytes
   * over the old path, the old content gone. */
  uint8_t few[3] = {1, 2, 3};
  check("f2a m7: the append lands", hearth.update.stage().stagedAppend(few, 3));
  hearth.update.stage().stagedEndWrite();
  check("f2a m7: the staged file is the new 3 bytes after the endWrite",
        fs.files.count("/hearth/staged.ota") == 1
        && fs.files["/hearth/staged.ota"].size() == 3
        && memcmp(fs.files["/hearth/staged.ota"].data(), few, 3) == 0);
  check("f2a m7: the .tmp is gone after the endWrite",
        !fs.exists("/hearth/staged.ota.tmp"));
}

/* M8: a +MTOTA:BLOCK without a comma, or without the length after the
 * seq, is ignored: no NULL dereference, no read past the terminator, no
 * state change, nothing on the wire. */
static void test_f2a_m8_block_malformed_ignored(void) {
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2a m8: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;
  check("f2a m8: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* No comma at all: the old code dereferenced NULL + 1. */
  s.injectURC("+MTOTA:BLOCK");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a m8: BLOCK with no comma is ignored (state unchanged)",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("f2a m8: nothing went on the wire (no comma)", s.unexpected().empty());
  check("f2a m8: no staged file (no comma)",
        !fs.exists("/hearth/staged.ota.tmp") && !fs.exists("/hearth/staged.ota"));

  /* No length after the seq: the old code read past the terminator. */
  s.injectURC("+MTOTA:BLOCK,5");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2a m8: BLOCK with no length is ignored (state unchanged)",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("f2a m8: nothing went on the wire (no length)", s.unexpected().empty());
  check("f2a m8: no staged file (no length)",
        !fs.exists("/hearth/staged.ota.tmp") && !fs.exists("/hearth/staged.ota"));
}

/* M1: a failed apply (6a's three-failures case, the flasher's verify
 * answers the old version all three times) leaves no open staged handle:
 * the stagedRemove() after the failure succeeds and the file is gone.
 * HearthFsMem does not track open handles, so the proof is the file's
 * absence plus the stagedRemove() succeeding. */
static void test_f2a_m1_failed_apply_no_open_handle(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2a m1: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
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

  check("f2a m1: exactly three flash calls", fake.calls().size() == 3);
  check("f2a m1: the script is drained", s.scriptDrained());
  check("f2a m1: nothing unexpected on the wire", s.unexpected().empty());
  check("f2a m1: the state is FAILED",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2a m1: the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  /* The failure tail keeps the staged bundle (for a manual retry), so the
   * file is there, but the handle hearthApply() held is closed and
   * deleted: a stagedRemove() after the failure runs without the file
   * being locked by a still-open handle, and the file is gone. */
  check("f2a m1: the staged bundle is kept (manual retry)",
        hearth.update.stage().stagedExists());
  hearth.update.stage().stagedRemove();
  check("f2a m1: the stagedRemove() after the failure succeeded",
        !hearth.update.stage().stagedExists());
  check("f2a m1: the file is gone after the stagedRemove()",
        !hearth.update.stage().stagedExists());
}

/*
 * Final review robustness round 2 (task F2b): I3, I5, I6, I7, M4, M6,
 * M10, the bench MG24 flash-ready timeout and the stale error. Each test
 * below is named after the finding it covers.
 */

/* I3: the switch BACK to the link baud is refused (the co-processor
 * rebooted at the download baud, its +MTREADY unreadable, the pull timed
 * out). The hook is called with 115200 anyway (the co-processor comes up
 * at its default) and the next poll sends no further AT+MTBAUD (the rates
 * agree now, the deaf-link loop is broken). */
static void test_f2b_i3_refused_switch_back(void) {
  std::string fx;
  check("f2b i3: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2b i3: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_b7fixCalls = 0;
  g_b7fixBaud = 0;
  hearth.update.hearthSetBaudChanger(b7fixBaudHook);

  /* The download's start, the switch up answered OK. */
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i3: the switch up was followed", g_b7fixCalls == 1 && g_b7fixBaud == 921600);

  /* The attempt goes back to IDLE without an ERROR line (a provider
   * abort): the drain switches back, and the co-processor, rebooted,
   * answers nothing. */
  s.expect("AT+MTBAUD=115200", "");
  s.injectURC("+MTOTA:IDLE");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i3: the hook was called with 115200 anyway",
        g_b7fixCalls == 2 && g_b7fixBaud == 115200);

  /* The next poll: no further AT+MTBAUD (the rates agree, the script has
   * none, so unexpected() stays empty). */
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i3: no further AT+MTBAUD", s.unexpected().empty());
}

/* I3b: AVAILABLE then IDLE (no ERROR) also switches back to the link
 * baud: the wanted-download flag is cleared on the IDLE state line, not
 * only on DOWNLOADED and ERROR, so a later reboot does not hit the deaf
 * link at the download rate. */
static void test_f2b_i3b_idle_switches_back(void) {
  std::string fx;
  check("f2b i3b: the good.ota fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  scriptBegin(s);
  HearthClass hearth;
  HearthFsMem fs;
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  g_yieldAdvanceMs = 50;
  check("f2b i3b: begin returns true", hearth.update.begin(0x10400, "1.4.0"));
  g_yieldAdvanceMs = 0;

  g_b7fixCalls = 0;
  g_b7fixBaud = 0;
  hearth.update.hearthSetBaudChanger(b7fixBaudHook);

  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i3b: the switch up was followed", g_b7fixCalls == 1 && g_b7fixBaud == 921600);

  /* No DOWNLOADING blocks, no ERROR: the attempt goes straight back to
   * IDLE, and the drain switches the link back. */
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  s.injectURC("+MTOTA:IDLE");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i3b: the switch back went out on IDLE",
        g_b7fixCalls == 2 && g_b7fixBaud == 115200);
  check("f2b i3b: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("f2b i3b: nothing unexpected on the wire", s.unexpected().empty());
}

/* I5: after a fw-only success (6a's case 1), a co-processor reboot
 * re-probes with the NEW effective version and its NEW string, not the
 * pre-update pair the success tail used to leave behind. */
static void test_f2b_i5_reprobe_new_version(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b i5: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
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
  check("f2b i5: the state is IDLE after the success",
        hearth.update.status().state == HEARTH_UPDATE_IDLE);

  /* The co-processor reboots: the re-probe declares the NEW version with
   * the NEW string. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i5: the re-probe declared the NEW version with the NEW string",
        s.scriptDrained());
  check("f2b i5: nothing unexpected on the wire", s.unexpected().empty());
  check("f2b i5: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);
}

/* I6a: in FAILED (6a's three-failures case), a co-processor reboot
 * re-probes and the successful re-probe moves the state to IDLE with
 * error OK: the co-processor is up and FOTA is live again. */
static void test_f2b_i6a_failed_reprobe_to_idle(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b i6a: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
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
  check("f2b i6a: the state is FAILED after the three failures",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);

  /* The co-processor reboots: the re-probe runs in FAILED now, and its
   * success moves the state to IDLE with error OK. */
  s.injectURC("+MTREADY");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f2b i6a: the re-probe sent both commands", s.scriptDrained());
  check("f2b i6a: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);
  check("f2b i6a: the error is OK", hearth.update.status().error == HEARTH_UPDATE_OK);
  check("f2b i6a: nothing unexpected on the wire", s.unexpected().empty());
}

/* I6b: the last attempt fails with a flasher error (the flasher's B668
 * exit reset the co-processor): the failure tail's declaration goes out
 * only after the +MTREADY was consumed (the script order proves it: the
 * wait's +MTREADY injection precedes the declaration, and onFlash(2)
 * checks nextExpected()). */
static void test_f2b_i6b_failure_tail_waits_for_exit_reset(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b i6b: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
  fake.results = {HEARTH_FLASH_ERR_WRITE, HEARTH_FLASH_ERR_WRITE, HEARTH_FLASH_ERR_WRITE};
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 2) {
      check("f2b i6b: the failure tail's declaration had not gone out before the exit reset",
            s.nextExpected() == "AT+MTSWVER=66304,\"1.3.0\"");
      /* The flasher's own exit reset (B668) reboots the co-processor:
       * its +MTREADY is what the failure tail's wait consumes. */
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("f2b i6b: exactly three flash calls", fake.calls().size() == 3);
  check("f2b i6b: the script is drained", s.scriptDrained());
  check("f2b i6b: nothing unexpected on the wire", s.unexpected().empty());
  check("f2b i6b: the state is FAILED",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2b i6b: the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
}

/* I7: three failed verifies, a retained image present, and the
 * co-processor still answering the pre-apply version to the extra
 * AT+MTVER?: no fourth flash call, the old version re-declared, FAILED
 * and HEARTH_UPDATE_ERR_FLASH. */
static void test_f2b_i7_pre_apply_version_skips_rollback(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b i7: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
  /* A retained image of 500 known bytes, through the fs, before the apply
   * (the same seed as 6a's case 5). */
  std::vector<uint8_t> retBytes(500);
  for (int i = 0; i < 500; i++) {
    retBytes[i] = (uint8_t)(i * 7 + 3);
  }
  HearthUpdateStage stg;
  check("f2b i7: stage begin for the retained seed", stg.begin(fs));
  {
    HearthFile *f = fs.open("/hearth/fw-scratch.bin", "w");
    check("f2b i7: the scratch file opens", f != 0);
    if (f) {
      check("f2b i7: the 500 bytes are written", f->write(retBytes.data(), retBytes.size()) == retBytes.size());
      delete f;
    }
    f = fs.open("/hearth/fw-scratch.bin", "r");
    check("f2b i7: the scratch file reopens", f != 0);
    if (f) {
      check("f2b i7: the retained image is written",
            stg.retainFwPart(*f, 0, 500, "ESP32-C6 Hearth", "1.1.0"));
      delete f;
    }
  }
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  /* I7: the pre-apply check answers the pre-apply version (1.2.0), so
   * the rollback flash is skipped and only the old version is
   * re-declared. */
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

  check("f2b i7: exactly three flash calls (no rollback)", fake.calls().size() == 3);
  check("f2b i7: the script is drained", s.scriptDrained());
  check("f2b i7: nothing unexpected on the wire", s.unexpected().empty());
  check("f2b i7: the state is FAILED", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2b i7: the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
  check("f2b i7: hearthVersion is the pre-apply 1.2.0",
        strcmp(hearth.update.status().hearthVersion, "1.2.0") == 0);
}

/* I7b: the same with AT+MTVER? answering a version different from the
 * pre-apply one: the rollback flash happens (6a's case 5's
 * expectations). */
static void test_f2b_i7b_different_version_rolls_back(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b i7b: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
  std::vector<uint8_t> retBytes(500);
  for (int i = 0; i < 500; i++) {
    retBytes[i] = (uint8_t)(i * 7 + 3);
  }
  HearthUpdateStage stg;
  check("f2b i7b: stage begin for the retained seed", stg.begin(fs));
  {
    HearthFile *f = fs.open("/hearth/fw-scratch.bin", "w");
    check("f2b i7b: the scratch file opens", f != 0);
    if (f) {
      check("f2b i7b: the 500 bytes are written", f->write(retBytes.data(), retBytes.size()) == retBytes.size());
      delete f;
    }
    f = fs.open("/hearth/fw-scratch.bin", "r");
    check("f2b i7b: the scratch file reopens", f != 0);
    if (f) {
      check("f2b i7b: the retained image is written",
            stg.retainFwPart(*f, 0, 500, "ESP32-C6 Hearth", "1.1.0"));
      delete f;
    }
  }
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  /* I7b: the pre-apply check answers a different version (1.1.0, the
   * retained image's): the rollback flash happens. */
  s.expect("AT+MTVER?", "+MTVER:1.1.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.1.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    (void)i;
    s.injectURC("+MTREADY");
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("f2b i7b: exactly four flash calls (3 attempts + the rollback)", calls.size() == 4);
  if (calls.size() == 4) {
    check("f2b i7b: the rollback's image equals the 500 retained bytes",
          calls[3].image == retBytes);
  }
  check("f2b i7b: the script is drained", s.scriptDrained());
  check("f2b i7b: nothing unexpected on the wire", s.unexpected().empty());
  check("f2b i7b: the state is FAILED", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2b i7b: the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
}

/* M4: during 6a's three-failures case the sketch's link-event callback
 * never sees HEARTH_COPROCESSOR_REBOOTED: the flasher's own exit resets
 * (B668) are armed before each attempt, so their +MTREADYs are consumed
 * as expected reboots. The file-static vector keeps the lambda alive
 * past this function (a heap lambda would otherwise outlive its frame). */
static std::vector<hearthEvent_t> g_f2bM4Events;

static void test_f2b_m4_no_rebooted_event(void) {
  g_f2bM4Events.clear();
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  hearth.onLinkEvent([](hearthEvent_t e) { g_f2bM4Events.push_back(e); });
  check("f2b m4: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
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

  bool sawRebooted = false;
  for (size_t i = 0; i < g_f2bM4Events.size(); i++) {
    if (g_f2bM4Events[i] == HEARTH_COPROCESSOR_REBOOTED) {
      sawRebooted = true;
    }
  }
  check("f2b m4: exactly three flash calls", fake.calls().size() == 3);
  check("f2b m4: no HEARTH_COPROCESSOR_REBOOTED reached the sketch", !sawRebooted);
  check("f2b m4: the state is FAILED",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
}

/* M6: both parts, the Hearth part succeeds, the host part fails (the
 * hooks are null): the old version is re-declared after the failure, so
 * the controller is not told the product updated. */
static void test_f2b_m6_host_failure_redeclares(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b m6: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, false));
  /* No hooks: the host defaults (all null), so the host part fails. */
  g_host = HostRec();
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.3.0\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  /* M6: the host part's failure re-declares the version in force. */
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 0) {
      s.injectURC("+MTREADY");
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("f2b m6: exactly one flash call", fake.calls().size() == 1);
  check("f2b m6: the script is drained", s.scriptDrained());
  check("f2b m6: nothing unexpected on the wire", s.unexpected().empty());
  check("f2b m6: the state is FAILED",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f2b m6: the error is HEARTH_UPDATE_ERR_HOST",
        hearth.update.status().error == HEARTH_UPDATE_ERR_HOST);
}

/* M10: a config with resetPin 7 and strapPin -1, the owner's pins (2, 3):
 * the flasher gets reset 7 (the config's) and strap 3 (the owner's). */
static void test_f2b_m10_per_pin_merge(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  hearth.coprocessorPins(2, 3);
  check("f2b m10: the good.ota fixture loads",
        loadFixture("fixtures/good.ota", fx));
  HearthUpdateStage stg;
  check("f2b m10: seed manifest written", stg.begin(fs));
  {
    HearthManifest m;
    m.productVersion = 0x10300;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("f2b m10: seed manifest saved", stg.saveManifest(m));
  }
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  hearth.begin(s);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 7;
  cfg.resetActiveLow = true;
  cfg.strapPin = -1;  /* the owner's strap (3) must fill this in */
  g_yieldAdvanceMs = 50;
  check("f2b m10: update begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");
  check("f2b m10: setup reaches WAIT_APPLY",
        hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);

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

  const std::vector<FlasherFake::Call> &calls = fake.calls();
  check("f2b m10: exactly one flash call", calls.size() == 1);
  if (calls.size() == 1) {
    const FlasherFake::Call &c = calls[0];
    check("f2b m10: the flasher was handed reset 7 and strap 3",
          c.pins.reset == 7 && c.pins.resetActiveLow
          && c.pins.strap == 3 && c.pins.strapActiveLow);
  }
  check("f2b m10: the state is IDLE", hearth.update.status().state == HEARTH_UPDATE_IDLE);
}

/* The post-flash wait gives up only after at least 30000 ms of fake
 * time: a good flash, no +MTREADY, the wait runs out at
 * HEARTH_FLASH_READY_TIMEOUT_MS (the bench MG24's first-boot is longer
 * than the link's 10 s). */
static void test_f2b_flash_ready_timeout(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f2b timeout: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  uint32_t t0 = 0, t1 = 0;
  fake.onFlash = [&](int i) {
    if (i == 0) {
      t0 = millis();
    } else if (i == 1) {
      t1 = millis();
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  check("f2b timeout: exactly three flash calls", fake.calls().size() == 3);
  check("f2b timeout: the attempt gave up only after at least 30000 ms of fake time",
        t1 >= t0 && t1 - t0 >= 30000);
  check("f2b timeout: the state is FAILED",
        hearth.update.status().state == HEARTH_UPDATE_FAILED);
}

/* ------------------------------------------------------------------
 * Final re-review fix round (task F4). The controller wrote these
 * tests as the oracle; the implementation must make them pass
 * unchanged.
 * ------------------------------------------------------------------ */

/* A co-processor that is booting answers nothing: a Stream over the
 * MockStream that, from bootAt() until the given time, swallows every
 * byte written (recording each whole line in lost()) and then releases
 * "+MTREADY" once. Before bootAt() and after the release it passes
 * everything through unchanged. The host clock moves only through
 * g_yieldAdvanceMs, so the release lands inside whatever wait the code
 * under test is running. */
class BootWindowStream : public Stream {
public:
  explicit BootWindowStream(MockStream &m) : _m(m) {}
  void bootAt(uint32_t readyAtMs) { _booting = true; _readyAt = readyAtMs; }
  const std::vector<std::string> &lost() const { return _lost; }
  size_t write(uint8_t c) override {
    pump();
    if (_booting) {
      if (c == '\n' || c == '\r') {
        if (!_line.empty()) _lost.push_back(_line);
        _line.clear();
      } else {
        _line += (char)c;
      }
      return 1;
    }
    return _m.write(c);
  }
  int available() override { pump(); return _m.available(); }
  int read() override { pump(); return _m.read(); }
  int peek() override { pump(); return _m.peek(); }
private:
  void pump() {
    if (_booting && (int32_t)(millis() - _readyAt) >= 0) {
      _booting = false;
      _m.injectURC("+MTREADY");
    }
  }
  MockStream &_m;
  bool _booting = false;
  uint32_t _readyAt = 0;
  std::string _line;
  std::vector<std::string> _lost;
};

/* F4 Important 1: a provider that answers ApplyUpdateRequest with
 * AwaitNextAction makes the requestor report +MTOTA:DEFERRED after the
 * verdict (spec 5.5); the APPLY that follows the delay must still apply
 * the staged bundle. The M5 gate keys on "a verdict was sent for the
 * staged bundle in this boot", not on the previous state. */
static void test_f4_deferred_then_apply(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f4 deferred: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
        "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "wifi", 0x10300, "1.3.0", 0x10300, true));
  s.injectURC("+MTOTA:DEFERRED,5");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  check("f4 deferred: no flash on DEFERRED", fake.calls().size() == 0);
  check("f4 deferred: nothing went on the wire for DEFERRED", s.unexpected().empty());

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
  check("f4 deferred: the APPLY after DEFERRED flashed once", fake.calls().size() == 1);
  check("f4 deferred: the script is drained", s.scriptDrained());
  check("f4 deferred: nothing unexpected on the wire", s.unexpected().empty());
  check("f4 deferred: the status is IDLE with no error",
        hearth.update.status().state == HEARTH_UPDATE_IDLE
        && hearth.update.status().error == HEARTH_UPDATE_OK);
  check("f4 deferred: the staged bundle is gone", !hearth.update.stage().stagedExists());
}

/* F4 Important 2 (I7 with the I6 wait first): the last attempt fails in
 * the flasher after its entry reset, whose exit reset reboots the
 * co-processor. The failure tail must wait for that boot's +MTREADY
 * BEFORE the pre-apply AT+MTVER?: asked during the boot it gets no
 * answer, and the retained image would be flashed over a healthy
 * application. Nothing may be written while the co-processor boots,
 * no fourth flash call, the old version re-declared, and the boot's
 * +MTREADY does not reach the sketch as HEARTH_COPROCESSOR_REBOOTED
 * (the M4 arm covers the tail's wait too). */
static std::vector<hearthEvent_t> g_f4Events;

static void test_f4_i7_waits_for_boot_before_version_check(void) {
  g_f4Events.clear();
  std::string fx;
  check("f4 i7: good fixture loads", loadFixture("fixtures/good.ota", fx));
  MockStream s;
  BootWindowStream bw(s);
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  {
    HearthUpdateStage stg;
    check("f4 i7: seed manifest written", stg.begin(fs));
    HearthManifest m;
    m.productVersion = 0x10300;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", "1.3.0");
    snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", "1.4.0");
    check("f4 i7: seed manifest saved", stg.saveManifest(m));
    std::vector<uint8_t> retBytes(500);
    for (int i = 0; i < 500; i++) {
      retBytes[i] = (uint8_t)(i * 7 + 3);
    }
    HearthFile *f = fs.open("/hearth/fw-scratch.bin", "w");
    check("f4 i7: the scratch file opens", f != 0);
    if (f) {
      f->write(retBytes.data(), retBytes.size());
      delete f;
    }
    f = fs.open("/hearth/fw-scratch.bin", "r");
    check("f4 i7: the scratch file reopens", f != 0);
    if (f) {
      check("f4 i7: the retained image is written",
            stg.retainFwPart(*f, 0, 500, "ESP32-C6 Hearth", "1.1.0"));
      delete f;
    }
  }
  scriptBeginFor(s, "AT+MTSWVER=66304,\"1.3.0\"", "ESP32-C6 Hearth", "1.2.0", "wifi");
  hearth.onLinkEvent([](hearthEvent_t e) { g_f4Events.push_back(e); });
  hearth.begin(bw);
  hearth.update.hearthAttach(fs);
  hearth.update.hearthSetFlasher(&fake);
  HearthUpdateConfig cfg;
  cfg.resetPin = 15;
  cfg.strapPin = 14;
  g_yieldAdvanceMs = 50;
  check("f4 i7: update begin returns true", hearth.update.begin(0x10300, "1.3.0", cfg));
  g_yieldAdvanceMs = 0;
  runDownload(s, hearth, fx, "AT+MTOTASTAGED=1");
  check("f4 i7: setup reaches WAIT_APPLY", hearth.update.status().state == HEARTH_UPDATE_WAIT_APPLY);

  fake.results = {HEARTH_FLASH_ERR_WRITE, HEARTH_FLASH_ERR_WRITE, HEARTH_FLASH_ERR_WRITE};
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  /* After the boot: the pre-apply check answers the pre-apply version,
   * so the rollback is skipped and the old version re-declared. */
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTSWVER=66304,\"1.3.0\"", "OK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
  fake.onFlash = [&](int i) {
    if (i == 2) {
      bw.bootAt(millis() + 3000);   /* the exit reset: 3 s of boot, then +MTREADY */
    }
  };
  s.injectURC("+MTOTA:APPLY");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;

  bool sawRebooted = false;
  for (size_t i = 0; i < g_f4Events.size(); i++) {
    if (g_f4Events[i] == HEARTH_COPROCESSOR_REBOOTED) {
      sawRebooted = true;
    }
  }
  check("f4 i7: nothing was written while the co-processor booted", bw.lost().empty());
  check("f4 i7: exactly three flash calls (no rollback over a healthy image)",
        fake.calls().size() == 3);
  check("f4 i7: the script is drained", s.scriptDrained());
  check("f4 i7: nothing unexpected on the wire", s.unexpected().empty());
  check("f4 i7: no HEARTH_COPROCESSOR_REBOOTED reached the sketch", !sawRebooted);
  check("f4 i7: the state is FAILED", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  check("f4 i7: the error is HEARTH_UPDATE_ERR_FLASH",
        hearth.update.status().error == HEARTH_UPDATE_ERR_FLASH);
}

/* F4 Minor (_lastAttemptFlasherError): three verify mismatches (the
 * flasher succeeded every time, the co-processor booted each image and
 * is up) leave nothing further to reboot, so the failure tail must not
 * spend HEARTH_FLASH_READY_TIMEOUT_MS waiting for a +MTREADY that
 * cannot come (bench MG24 2026-09-30: 30 s of dropped URCs). The whole
 * apply, three attempts included, stays well under that. */
static void test_f4_verify_mismatch_tail_does_not_wait(void) {
  std::string fx;
  MockStream s;
  HearthClass hearth;
  HearthFsMem fs;
  FlasherFake fake;
  check("f4 nowait: setup reaches WAIT_APPLY", setupToApply(s, hearth, fs, fake, fx, "good",
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
  uint32_t t0 = millis();
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  uint32_t took = millis() - t0;
  check("f4 nowait: exactly three flash calls", fake.calls().size() == 3);
  check("f4 nowait: the script is drained", s.scriptDrained());
  check("f4 nowait: nothing unexpected on the wire", s.unexpected().empty());
  check("f4 nowait: the state is FAILED", hearth.update.status().state == HEARTH_UPDATE_FAILED);
  char name[96];
  snprintf(name, sizeof(name), "f4 nowait: the apply took under %u ms (took %lu)",
           (unsigned)(HEARTH_FLASH_READY_TIMEOUT_MS / 2), (unsigned long)took);
  check(name, took < HEARTH_FLASH_READY_TIMEOUT_MS / 2);
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
  printf("\n===== HearthUpdate apply (task 6c) tests =====\n");
  test_resume_link_up();
  test_resume_silent_until_flash();
  test_resume_three_attempts();
  test_resume_remaining_fail_rollback();
  test_resume_with_host_part();
  test_resume_no_staged();
  printf("\n===== HearthUpdate apply (task 7a2) tests =====\n");
  test_host_gz_refused();
  test_host_gz_not_selected_applies();
  printf("\n===== HearthUpdate apply (task 7a3) tests =====\n");
  test_begin_firmware_without_fota();
  test_settle_wait_after_commissioning();
  test_no_settle_without_commissioning();
  test_old_commissioning_does_not_wait();
  printf("\n===== HearthUpdate apply (task 7c-fix1, B666) tests =====\n");
  test_b666_refused_not_followed();
  test_b666_accepted_followed_once();
  printf("\n===== HearthUpdate apply (task 7c-fix2, B667) tests =====\n");
  test_b667_commissioning_releases_retry();
  test_b667_periodic_retry();
  test_b667_no_fota_not_retried();
  printf("\n===== HearthUpdate apply (task 7c-fix4, F669/B670/B671) tests =====\n");
  test_coproc_pins_accessor();
  test_begin_takes_owner_pins();
  test_b671_reprobe_after_reboot();
  test_b671_reprobe_recovers_boot_race();
  test_b671_reprobe_arms_b667_retry();
  test_b671_no_reprobe_mid_transfer();
  test_b671_no_reprobe_before_begin();
  printf("\n===== HearthUpdate apply (final review F2a: I1, I2, I4, M1, M5, M7, M8) tests =====\n");
  test_f2a_i1_abort_rearms_requestor();
  test_f2a_i2a_error_discards_partial();
  test_f2a_i2b_block_zero_restarts_write();
  test_f2a_i4_busy_link_skips_drain();
  test_f2a_m5_apply_ignored_outside_wait_apply();
  test_f2a_m7_begin_write_removes_staged();
  test_f2a_m8_block_malformed_ignored();
  test_f2a_m1_failed_apply_no_open_handle();
  printf("\n===== HearthUpdate apply (final review F2b: I3, I5, I6, I7, M4, M6, M10, bench MG24) tests =====\n");
  test_f2b_i3_refused_switch_back();
  test_f2b_i3b_idle_switches_back();
  test_f2b_i5_reprobe_new_version();
  test_f2b_i6a_failed_reprobe_to_idle();
  test_f2b_i6b_failure_tail_waits_for_exit_reset();
  test_f2b_i7_pre_apply_version_skips_rollback();
  test_f2b_i7b_different_version_rolls_back();
  test_f2b_m4_no_rebooted_event();
  test_f2b_m6_host_failure_redeclares();
  test_f2b_m10_per_pin_merge();
  test_f2b_flash_ready_timeout();
  test_f4_deferred_then_apply();
  test_f4_i7_waits_for_boot_before_version_check();
  test_f4_verify_mismatch_tail_does_not_wait();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

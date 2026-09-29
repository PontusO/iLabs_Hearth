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
                         uint32_t cfgSize, bool withManifest) {
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
  scriptBeginFor(s, swver, model, "1.2.0", variant);
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

int main(void) {
  printf("\n===== HearthUpdate apply (task 6a) tests =====\n");
  test_order_and_arguments();
  test_nrf_fw_only();
  test_three_failed_no_retained();
  test_retained_rollback();
  test_flasher_error_then_success();
  test_no_ready_then_success();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

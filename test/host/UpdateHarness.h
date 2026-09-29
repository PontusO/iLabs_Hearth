#pragma once
/* test/host/UpdateHarness.h: the Hearth.update test scaffolding shared by
 * test_update.cpp (the download, verdict and consent cases) and
 * test_update_apply.cpp (the apply, rollback, resume and first-boot
 * cases): the fixture loader, the begin() script, the +MTOTABLK answer
 * builder and the whole-download driver. Moved here unchanged from
 * test_update.cpp by the controller; scriptBegin() keeps its ESP32-C6
 * script and scriptBeginFor() takes the model, the running Hearth
 * version and the variant for the other ports. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"

static inline bool loadFixture(const char *path, std::string &out) {
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

static inline void scriptBegin(MockStream &s) {
  s.expect("AT+MTSWVER=66560,\"1.4.0\"", "OK\r\n");
  s.expect("AT+CGMM", "ESP32-C6 Hearth\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:1.2.0\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0,wifi\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
}

/* begin()'s five commands for a given co-processor: the declaration
 * (swver is the whole AT+MTSWVER command), the model, the running Hearth
 * version and the variant, then the requestor on. */
static inline void scriptBeginFor(MockStream &s, const std::string &swver, const std::string &model,
                           const std::string &mtver, const std::string &variant) {
  s.expect(swver, "OK\r\n");
  s.expect("AT+CGMM", model + "\r\nOK\r\n");
  s.expect("AT+MTVER?", "+MTVER:" + mtver + "\r\nOK\r\n");
  s.expect("AT+MTOTA?", "+MTOTA:0,IDLE,0," + variant + "\r\nOK\r\n");
  s.expect("AT+MTOTA=1", "OK\r\n");
}

/* One +MTOTABLK: line: the seq, the block-local offset and the hex for the
 * bytes at that offset, upper-case. The line carries at most 96 bytes; the
 * last line of a short block carries the rest. The terminal OK the
 * co-processor sends after the last line is appended by the caller when ok
 * is true. */
static inline std::string blkLine(uint32_t seq, uint32_t off, uint32_t len,
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
static inline std::string blkAnswer(uint32_t seq, uint32_t len,
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

/* The whole download of fx: AVAILABLE and DOWNLOADING, every block pulled
 * and acknowledged, then DOWNLOADED and, when verdict is not empty, that
 * verdict command. beginWrite false stops mid-transfer. */
static inline void runDownload(MockStream &s, HearthClass &hearth, const std::string &fx,
                        const std::string &verdict, bool beginWrite = true) {
  s.expect("AT+MTBAUD=921600", "OK\r\n");
  s.injectURC("+MTOTA:AVAILABLE,66561");
  s.injectURC("+MTOTA:DOWNLOADING,0");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
  size_t pos = 0;
  uint32_t seq = 0;
  while (pos < fx.size()) {
    size_t blen = fx.size() - pos;
    if (blen > 1024) blen = 1024;
    s.expect("AT+MTOTAGET=" + std::to_string((unsigned)seq),
             blkAnswer(seq, (uint32_t)blen, fx, pos));
    s.expect("AT+MTOTAACK=" + std::to_string((unsigned)seq), "OK\r\n");
    s.injectURC("+MTOTA:BLOCK," + std::to_string((unsigned)seq) + ","
                + std::to_string((unsigned)blen));
    g_yieldAdvanceMs = 50;
    hearth.poll();
    g_yieldAdvanceMs = 0;
    pos += blen;
    seq++;
  }
  if (!beginWrite) {
    return;  /* the download stops here, mid-transfer */
  }
  s.expect("AT+MTBAUD=115200", "OK\r\n");
  if (!verdict.empty()) {
    s.expect(verdict, "OK\r\n");
  }
  s.injectURC("+MTOTA:DOWNLOADED");
  g_yieldAdvanceMs = 50;
  hearth.poll();
  g_yieldAdvanceMs = 0;
}


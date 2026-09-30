/* test/host/test_update_stage.cpp: HearthUpdateStage against HearthFsMem,
 * the in-memory HearthFs stand-in. The stage is the device-side home of the
 * downloaded bundle, the manifest, the apply state and the retained images
 * (HearthUpdateStage.h carries the power-loss story for each of them).
 * HearthFileSource is a HearthByteSource over a HearthFile, so the read-back
 * cases exercise the same path the bundle parser takes on the device. */
#include <stdio.h>
#include <string.h>
#include "HearthFsMem.h"
#include "HearthUpdateStage.h"

static int g_pass, g_fail;
static void check(const char *n, bool c) { printf("  [%s] %s\n", c ? "PASS" : "FAIL", n); (c ? g_pass : g_fail)++; }

static void loadInto(HearthFsMem &fs, const char *path, std::vector<uint8_t> &out) {
  out.clear();
  if (!fs.exists(path)) return;
  HearthFile *f = fs.open(path, "r");
  if (!f) { check("internal: file opens for reading", false); return; }
  uint8_t b;
  while (f->read(&b, 1) > 0) out.push_back(b);
  f->close();
  delete f;
}

int main() {
  /* begin creates the directory. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin on a fresh fs", st.begin(fs));
    check("begin created the directory", fs.exists("/hearth"));
    check("stagedPath is the PicoOTA path", strcmp(st.stagedPath(), "/hearth/staged.ota") == 0);
  }

  /* manifest: round trip, corrupt magic, no file. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    HearthManifest m;
    check("no manifest yet", (!st.haveManifest() && !st.loadManifest(m)));
    m.productVersion = 0x10400;
    strcpy(m.productVersionString, "1.4.0");
    strcpy(m.hostVersion, "1.3.0");
    check("saveManifest", st.saveManifest(m));
    check("manifest file written to /hearth/manifest", fs.exists("/hearth/manifest"));
    HearthManifest m2;
    check("manifest round trip", st.loadManifest(m2)
          && m2.productVersion == 0x10400
          && strcmp(m2.productVersionString, "1.4.0") == 0
          && strcmp(m2.hostVersion, "1.3.0") == 0);
    std::vector<uint8_t> blob;
    loadInto(fs, "/hearth/manifest", blob);
    check("manifest magic is HMF1", blob.size() >= 4 && memcmp(blob.data(), "HMF1", 4) == 0);
    if (blob.size() >= 4) blob[1] = (uint8_t)(blob[1] ^ 1);    /* corrupt the magic */
    HearthFsMem broken = fs;
    broken.files["/hearth/manifest"] = blob;
    HearthUpdateStage stBroken;
    check("corrupt manifest (wrong magic) loads as none",
          stBroken.begin(broken) && !stBroken.loadManifest(m2));
  }

  /* manifest: a truncated or overlong file is corrupt, not a load. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    HearthManifest m;
    memset(&m, 0, sizeof(m));
    m.productVersion = 0x10700;
    strcpy(m.productVersionString, "1.7.0");
    strcpy(m.hostVersion, "");
    check("saveManifest", st.saveManifest(m));
    std::vector<uint8_t> blob;
    loadInto(fs, "/hearth/manifest", blob);
    /* truncated to half its size: the load reads up to EOF and would
     * otherwise succeed with the struct partly uninitialised */
    blob.resize(blob.size() / 2);
    HearthFsMem half = fs;
    half.files["/hearth/manifest"] = blob;
    HearthUpdateStage stHalf;
    check("manifest truncated to half its size loads as false",
          stHalf.begin(half) && !stHalf.loadManifest(m));
    /* one byte too long: the extra byte is not part of the struct */
    std::vector<uint8_t> longblob;
    loadInto(fs, "/hearth/manifest", longblob);
    longblob.push_back(0);
    HearthFsMem longFs = fs;
    longFs.files["/hearth/manifest"] = longblob;
    HearthUpdateStage stLong;
    check("manifest one byte too long loads as false",
          stLong.begin(longFs) && !stLong.loadManifest(m));
  }

  /* state: round trip, clear, unknown phase treated as none. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    HearthUpdateState s;
    check("no state yet", (!st.haveState() && !st.loadState(s)));
    memset(&s, 0, sizeof(s));
    s.phase = HEARTH_PHASE_HOST;
    s.attempts = 2;
    s.targetVersion = 0x10500;
    strcpy(s.targetVersionString, "1.5.0");
    s.fwPart = 0;
    s.hostPart = 1;
    check("saveState", st.saveState(s));
    check("state file written to /hearth/ota.state", fs.exists("/hearth/ota.state"));
    HearthUpdateState s2;
    check("state round trip", st.loadState(s2)
          && s2.phase == HEARTH_PHASE_HOST
          && s2.attempts == 2
          && s2.targetVersion == 0x10500
          && strcmp(s2.targetVersionString, "1.5.0") == 0
          && s2.fwPart == 0
          && s2.hostPart == 1);
    st.clearState();
    check("clearState removes the file", !fs.exists("/hearth/ota.state"));
    check("no state after clear", (!st.haveState() && !st.loadState(s)));

    /* a state file whose phase byte is not a known phase: treated as none.
     * Build the struct in memory, corrupt the phase byte, then serialize
     * the exact sizeof(struct) bytes after the magic. */
    HearthUpdateState sCorrupt;
    memset(&sCorrupt, 0, sizeof(sCorrupt));
    sCorrupt.phase = 7;                       /* an unknown phase value */
    sCorrupt.attempts = 1;
    sCorrupt.targetVersion = 0x10600;
    strcpy(sCorrupt.targetVersionString, "1.6.0");
    sCorrupt.fwPart = 0xFF;
    sCorrupt.hostPart = 0xFF;
    std::vector<uint8_t> b;
    b.push_back('H'); b.push_back('S'); b.push_back('T'); b.push_back('1');
    const uint8_t *raw = (const uint8_t *)&sCorrupt;
    for (size_t i = 0; i < sizeof(sCorrupt); i++) b.push_back(raw[i]);
    HearthFsMem fs2 = fs;
    fs2.files["/hearth/ota.state"] = b;
    HearthUpdateState s3;
    HearthUpdateStage st2;
    check("unknown phase is treated as none, not a failure",
          st2.begin(fs2) && st2.loadState(s3) && s3.phase == HEARTH_PHASE_NONE);
  }

  /* state: a truncated or overlong file is corrupt, not a load. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    HearthUpdateState s;
    memset(&s, 0, sizeof(s));
    s.phase = HEARTH_PHASE_HOST_CONFIRM;
    s.attempts = 1;
    s.targetVersion = 0x10800;
    strcpy(s.targetVersionString, "1.8.0");
    s.fwPart = 0;
    s.hostPart = 0xFF;
    check("saveState", st.saveState(s));
    std::vector<uint8_t> blob;
    loadInto(fs, "/hearth/ota.state", blob);
    /* truncated to half its size: the load reads up to EOF and would
     * otherwise succeed with the struct partly uninitialised */
    blob.resize(blob.size() / 2);
    HearthFsMem half = fs;
    half.files["/hearth/ota.state"] = blob;
    HearthUpdateState s2;
    HearthUpdateStage stHalf;
    check("state truncated to half its size loads as false",
          stHalf.begin(half) && !stHalf.loadState(s2));
    /* one byte too long: the extra byte is not part of the struct */
    std::vector<uint8_t> longblob;
    loadInto(fs, "/hearth/ota.state", longblob);
    longblob.push_back(0);
    HearthFsMem longFs = fs;
    longFs.files["/hearth/ota.state"] = longblob;
    HearthUpdateState s3;
    HearthUpdateStage stLong;
    check("state one byte too long loads as false",
          stLong.begin(longFs) && !stLong.loadState(s3));
  }

  /* staged: three appends, read back byte for byte through HearthFileSource. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    const uint8_t a1[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    const uint8_t a2[30] = { 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
                             25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 };
    const uint8_t a3[5] = { 255, 254, 253, 252, 251 };
    check("stagedBeginWrite", st.stagedBeginWrite());
    check("first append", st.stagedAppend(a1, sizeof(a1)));
    check("second append", st.stagedAppend(a2, sizeof(a2)));
    check("third append", st.stagedAppend(a3, sizeof(a3)));
    st.stagedEndWrite();
    check("staged file exists after end", fs.exists("/hearth/staged.ota"));
    check("stagedSize is the sum of the appends", st.stagedSize() == 45);
    HearthFile *f = st.stagedOpenRead();
    check("stagedOpenRead", f != nullptr);
    if (f) {
      HearthFileSource src(*f);
      check("source size matches", src.size() == 45);
      uint8_t expect[45];
      memcpy(expect, a1, 10); memcpy(expect + 10, a2, 30); memcpy(expect + 40, a3, 5);
      uint8_t got[16];
      bool ok = true;
      for (uint32_t off = 0; off < 45 && ok; off += 16) {
        size_t n = 45 - off < 16 ? 45 - off : 16;
        if (!src.read(off, got, n)) ok = false;
        else if (memcmp(got, expect + off, n) != 0) ok = false;
      }
      check("read back byte for byte through HearthFileSource", ok);
      uint8_t one;
      check("source read past the end fails", !src.read(45, &one, 1));
      delete f;
    }
  }

  /* free space: the append that would not fit fails, end removes the partial. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    fs.setFreeLimit(60);
    check("begin under a free-space limit", st.begin(fs));
    check("stagedBeginWrite", st.stagedBeginWrite());
    check("append 50 of 60", st.stagedAppend((const uint8_t *)"01234567890123456789012345678901234567890123456789", 50));
    check("append 20 more would not fit: false", !st.stagedAppend((const uint8_t *)"abcdefghijklmnopqrst", 20));
    st.stagedEndWrite();
    check("the partial staged file is removed", !fs.exists("/hearth/staged.ota") && !st.stagedExists());
    check("free space is back", fs.freeBytes() == 60);
  }

  /* retained fw part: copy a range, info round trip, meta as text lines. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    std::vector<uint8_t> data(3000);
    for (uint32_t i = 0; i < data.size(); i++) data[i] = (uint8_t)(i % 251);
    {
      std::vector<uint8_t> &slot = fs.files["/hearth/staged.ota"];
      slot.assign(data.begin(), data.end());
    }
    HearthFile *rf = fs.open("/hearth/staged.ota", "r");
    check("staged opens for the retain", rf != nullptr);
    check("retainFwPart copies the range",
          rf && st.retainFwPart(*rf, 100, 1234, "ESP32-C6 Hearth", "1.3.0"));
    char tgt[33], ver[33]; uint32_t len = 0;
    check("retainedFwInfo returns target, version and length",
          st.retainedFwInfo(tgt, ver, len) && strcmp(tgt, "ESP32-C6 Hearth") == 0
          && strcmp(ver, "1.3.0") == 0 && len == 1234);
    HearthFile *im = st.retainedFwOpen();
    check("retainedFwOpen", im != nullptr);
    if (im) {
      check("retained size is the part length", im->size() == 1234);
      uint8_t got[16];
      bool ok = im->seek(1000) && im->read(got, 16) == 16
            && memcmp(got, &data[1100], 16) == 0;
      check("retained content is the requested range", ok);
      delete im;
    }
    std::vector<uint8_t> meta;
    loadInto(fs, "/hearth/fw-retained.meta", meta);
    std::string ms((const char *)meta.data(), meta.size());
    check("meta is target, version, length as text lines",
          ms == "ESP32-C6 Hearth\n1.3.0\n1234");
  }

  /* rotation (DE625): the old retained image goes BEFORE the new one is
   * written, so the two never coexist. The fake's free space is set so
   * holding both would fail, and the call succeeds. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    std::vector<uint8_t> oldimg(1000), newimg(1200);
    for (uint32_t i = 0; i < oldimg.size(); i++) oldimg[i] = (uint8_t)(i & 0xFF);
    for (uint32_t i = 0; i < newimg.size(); i++) newimg[i] = (uint8_t)(0xA0 ^ (i & 0x7F));
    std::vector<uint8_t> &s1 = fs.files["/hearth/staged.ota"];
    s1 = oldimg;
    HearthFile *r1 = fs.open("/hearth/staged.ota", "r");
    check("first retain", r1 && st.retainFwPart(*r1, 0, 1000, "ESP32-C6 Hearth", "1.2.9"));
    check("retained image is there", fs.exists("/hearth/fw-retained.bin")
          && fs.files["/hearth/fw-retained.bin"].size() == 1000);
    /* the limit is set so that holding both retained images at once would
     * not fit, but the rotation, which deletes the old image before writing
     * the new one, succeeds. At the point of rotation the map holds staged
     * (1200), the old retained bin (1000) and its meta (26); the rotation
     * removes the old bin and meta first (freeing 1026), then writes the
     * new 1200-byte bin and its meta. With a 3500-byte limit the peak
     * usage during the write is staged + new bin + new meta, well under
     * 3500, while staging both retained bins at once would exceed it. */
    fs.setFreeLimit(3500);
    std::vector<uint8_t> &s2 = fs.files["/hearth/staged.ota"];
    s2 = newimg;
    HearthFile *r2 = fs.open("/hearth/staged.ota", "r");
    check("rotation succeeds although holding both would not",
          r2 && st.retainFwPart(*r2, 0, 1200, "ESP32-C6 Hearth", "1.3.0"));
    check("the retained image is the new one",
          fs.files["/hearth/fw-retained.bin"].size() == 1200
          && memcmp(fs.files["/hearth/fw-retained.bin"].data(), newimg.data(), 1200) == 0);
    char tgt[33], ver[33]; uint32_t len = 0;
    check("info follows the rotation", st.retainedFwInfo(tgt, ver, len)
          && strcmp(ver, "1.3.0") == 0 && len == 1200);
  }

  /* host-prev: saveHostPrev round trip. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    std::vector<uint8_t> img(777);
    for (uint32_t i = 0; i < img.size(); i++) img[i] = (uint8_t)(i * 7 % 256);
    check("saveHostPrev", st.saveHostPrev(img.data(), img.size()));
    check("host-prev file is present", fs.exists("/hearth/host-prev.bin"));
    HearthFile *f = st.hostPrevOpen();
    check("hostPrevOpen", f != nullptr);
    if (f) {
      check("host-prev size round trips", f->size() == 777);
      bool ok = f->seek(0);
      uint8_t got[16];
      uint32_t pos = 0;
      while (ok && pos < f->size()) {
        size_t n = f->size() - pos < 16 ? f->size() - pos : 16;
        if (f->read(got, n) != (int)n || memcmp(got, &img[pos], n) != 0) ok = false;
        pos += (uint32_t)n;
      }
      check("host-prev content round trips", ok);
      delete f;
    }
  }

  /* every file lives under /hearth/: drive one of each kind, then inspect. */
  {
    HearthFsMem fs; HearthUpdateStage st;
    check("begin", st.begin(fs));
    HearthManifest m;
    m.productVersion = 1; strcpy(m.productVersionString, "1.0.0"); strcpy(m.hostVersion, "");
    st.saveManifest(m);
    HearthUpdateState s;
    memset(&s, 0, sizeof(s));
    s.phase = HEARTH_PHASE_FW; s.fwPart = 0xFF; s.hostPart = 0xFF;
    st.saveState(s);
    st.stagedBeginWrite();
    st.stagedAppend((const uint8_t *)"staged", 6);
    st.stagedEndWrite();
    std::vector<uint8_t> img(32);
    for (uint32_t i = 0; i < img.size(); i++) img[i] = (uint8_t)i;
    std::vector<uint8_t> &slot = fs.files["/hearth/staged.ota"];
    /* staged is 6 bytes now; grow it so the retain range fits */
    slot.resize(32);
    for (uint32_t i = 6; i < 32; i++) slot[i] = (uint8_t)i;
    HearthFile *rf = fs.open("/hearth/staged.ota", "r");
    check("retain in the path scan", rf && st.retainFwPart(*rf, 0, 16, "ESP32-C6 Hearth", "1.0.0"));
    st.saveHostPrev(img.data(), img.size());
    bool all = true;
    for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = fs.files.begin();
         it != fs.files.end(); ++it) {
      if (strncmp(it->first.c_str(), "/hearth/", 8) != 0) { all = false; printf("  stray file: %s\n", it->first.c_str()); }
    }
    check("every file lives under /hearth/", all);
  }

  printf("test_update_stage: %d passed, %d failed\n", g_pass, g_fail);
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

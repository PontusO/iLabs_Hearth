/*
 * HearthUpdateStage.cpp - implementation. See HearthUpdateStage.h for the
 * file layout and the power-loss story of each of them; the .tmp-and-
 * rename rule and its one exception (fw-retained.bin, DE625) are the load-
 * bearing parts.
 */
#include "HearthUpdateStage.h"
#include <string.h>
#include <stdio.h>

namespace {
const char kManifestMagic[4] = { 'H', 'M', 'F', '1' };
const char kStateMagic[4]    = { 'H', 'S', 'T', '1' };
const size_t kRetainChunk = 512;

bool knownPhase(uint8_t p) {
  return p >= HEARTH_PHASE_NONE && p <= HEARTH_PHASE_HOST_CONFIRM;
}
}

bool HearthUpdateStage::begin(HearthFs &fs, const char *dir) {
  _fs = &fs;
  _w = 0;
  snprintf(_dir, sizeof(_dir), "%s", dir);
  return _fs->mkdir(_dir);
}

void HearthUpdateStage::path(char *out, size_t n, const char *name) const {
  snprintf(out, n, "%s/%s", _dir, name);
}

/* The .tmp-and-rename write: a power loss leaves either the old file or
 * the new one, never a partial one. */
bool HearthUpdateStage::writeFile(const char *path, const uint8_t *data, size_t len) {
  char tmp[128];
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  _fs->remove(tmp);
  HearthFile *f = _fs->open(tmp, "w");
  if (!f) return false;
  size_t written = f->write(data, len);
  f->close();
  delete f;
  if (written != len) {
    _fs->remove(tmp);
    return false;
  }
  return _fs->rename(tmp, path);
}

bool HearthUpdateStage::haveManifest() {
  char p[64];
  path(p, sizeof(p), "manifest");
  return _fs && _fs->exists(p);
}

bool HearthUpdateStage::loadManifest(HearthManifest &m) {
  if (!_fs) return false;
  char p[64];
  path(p, sizeof(p), "manifest");
  if (!_fs->exists(p)) return false;
  HearthFile *f = _fs->open(p, "r");
  if (!f) return false;
  /* exactly magic plus the struct, no more and no less: a truncated file
   * would load with the struct partly uninitialised, and a longer one is
   * not a manifest this code wrote */
  uint8_t hdr[4];
  bool ok = f->size() == 4 + sizeof(HearthManifest)
      && f->seek(4) && f->read((uint8_t *)&m, sizeof(m)) == (int)sizeof(m)
      && f->seek(0) && f->read(hdr, 4) == 4 && memcmp(hdr, kManifestMagic, 4) == 0;
  delete f;
  return ok;
}

bool HearthUpdateStage::saveManifest(const HearthManifest &m) {
  if (!_fs) return false;
  uint8_t buf[4 + sizeof(HearthManifest)];
  memcpy(buf, kManifestMagic, 4);
  memcpy(buf + 4, &m, sizeof(m));
  char p[64];
  path(p, sizeof(p), "manifest");
  return writeFile(p, buf, sizeof(buf));
}

bool HearthUpdateStage::haveState() {
  char p[64];
  path(p, sizeof(p), "ota.state");
  return _fs && _fs->exists(p);
}

bool HearthUpdateStage::loadState(HearthUpdateState &s) {
  if (!_fs) return false;
  char p[64];
  path(p, sizeof(p), "ota.state");
  if (!_fs->exists(p)) return false;
  HearthFile *f = _fs->open(p, "r");
  if (!f) return false;
  /* exactly magic plus the struct, no more and no less: a truncated file
   * would load with the struct partly uninitialised, and a longer one is
   * not a state file this code wrote */
  uint8_t hdr[4];
  bool ok = f->size() == 4 + sizeof(HearthUpdateState)
      && f->seek(4) && f->read((uint8_t *)&s, sizeof(s)) == (int)sizeof(s)
      && f->seek(0) && f->read(hdr, 4) == 4 && memcmp(hdr, kStateMagic, 4) == 0;
  /* a state file we cannot make sense of is not a failure: the resume path
   * simply starts from none */
  if (ok && !knownPhase(s.phase)) s.phase = HEARTH_PHASE_NONE;
  delete f;
  return ok;
}

bool HearthUpdateStage::saveState(const HearthUpdateState &s) {
  if (!_fs) return false;
  uint8_t buf[4 + sizeof(HearthUpdateState)];
  memcpy(buf, kStateMagic, 4);
  memcpy(buf + 4, &s, sizeof(s));
  char p[64];
  path(p, sizeof(p), "ota.state");
  return writeFile(p, buf, sizeof(buf));
}

void HearthUpdateStage::clearState() {
  if (!_fs) return;
  char p[64];
  path(p, sizeof(p), "ota.state");
  _fs->remove(p);
}

bool HearthUpdateStage::stagedBeginWrite() {
  if (!_fs) return false;
  char tmp[68];
  char staged[64];
  path(staged, sizeof(staged), "staged.ota");
  snprintf(tmp, sizeof(tmp), "%s.tmp", staged);
  _fs->remove(tmp);
  /* Final review M7: the staged bundle kept for a manual retry used to
   * sit here for the whole next download, so the old staged.ota and the
   * new .tmp coexisted (on a C6 that is about 2.06 + 2.06 + 1.88
   * (retained) + host-prev MB against the 6.5 MiB need, enough to fill
   * the partition and fail the append). Remove it first: two bundles
   * never coexist on the filesystem, and the next download's rename at
   * the end still overwrites it exactly as before. */
  _fs->remove(staged);
  _w = _fs->open(tmp, "w");
  _wFailed = false;
  return _w != 0;
}

bool HearthUpdateStage::stagedAppend(const uint8_t *p, size_t n) {
  if (!_w || n == 0) return false;
  bool ok = _w->write(p, n) == n;
  if (!ok) _wFailed = true;
  return ok;
}

void HearthUpdateStage::stagedEndWrite() {
  if (!_fs) return;
  char staged[64];
  char tmp[68];
  path(staged, sizeof(staged), "staged.ota");
  snprintf(tmp, sizeof(tmp), "%s.tmp", staged);
  bool ok = _w != 0 && !_wFailed;
  if (_w) {
    _w->close();
    delete _w;
    _w = 0;
  }
  if (ok) {
    _fs->rename(tmp, staged);
  } else {
    _fs->remove(tmp);
  }
}

bool HearthUpdateStage::stagedExists() {
  char p[64];
  path(p, sizeof(p), "staged.ota");
  return _fs && _fs->exists(p);
}

uint32_t HearthUpdateStage::stagedSize() {
  char p[64];
  path(p, sizeof(p), "staged.ota");
  if (!_fs || !_fs->exists(p)) return 0;
  HearthFile *f = _fs->open(p, "r");
  if (!f) return 0;
  uint32_t s = f->size();
  delete f;
  return s;
}

void HearthUpdateStage::stagedRemove() {
  char p[64];
  path(p, sizeof(p), "staged.ota");
  if (_fs) _fs->remove(p);
}

HearthFile *HearthUpdateStage::stagedOpenRead() {
  char p[64];
  path(p, sizeof(p), "staged.ota");
  return _fs ? _fs->open(p, "r") : 0;
}

bool HearthUpdateStage::retainFwPart(HearthFile &staged, uint32_t off, uint32_t len,
                                     const char *target, const char *version) {
  if (!_fs) return false;
  char bin[64], meta[64];
  path(bin, sizeof(bin), "fw-retained.bin");
  path(meta, sizeof(meta), "fw-retained.meta");
  /* DE625: the old retained image and its meta go FIRST, and only then is
   * the new one written. Holding both at once would not fit the smallest
   * supported filesystem, and this runs only after the new image is
   * confirmed running, so a power loss in the window loses rollback, never
   * service. */
  _fs->remove(bin);
  _fs->remove(meta);

  HearthFile *out = _fs->open(bin, "w");
  if (!out) return false;
  bool ok = staged.seek(off);
  uint8_t chunk[kRetainChunk];
  uint32_t remaining = len;
  while (ok && remaining > 0) {
    size_t n = remaining < kRetainChunk ? (size_t)remaining : kRetainChunk;
    int r = staged.read(chunk, n);
    if (r < 0 || (size_t)r != n) { ok = false; break; }
    if (out->write(chunk, n) != n) { ok = false; break; }
    remaining -= (uint32_t)n;
  }
  out->close();
  delete out;
  if (!ok) {
    _fs->remove(bin);
    return false;
  }

  char mbuf[33 + 1 + 33 + 1 + 16];
  int mlen = snprintf(mbuf, sizeof(mbuf), "%s\n%s\n%u", target, version, (unsigned)len);
  if (mlen < 0 || (size_t)mlen >= sizeof(mbuf)) return false;
  return writeFile(meta, (const uint8_t *)mbuf, (size_t)mlen);
}

bool HearthUpdateStage::retainedFwInfo(char *target, char *version, uint32_t &len) {
  if (!_fs) return false;
  char p[64];
  path(p, sizeof(p), "fw-retained.meta");
  if (!_fs->exists(p)) return false;
  HearthFile *f = _fs->open(p, "r");
  if (!f) return false;
  /* the meta is three short text lines: target, version, length */
  uint8_t all[128];
  size_t total = 0;
  int r;
  while (total < sizeof(all) && (r = f->read(all + total, 1)) == 1) total++;
  delete f;
  if (total == 0) return false;
  const uint8_t *p2 = all;
  const uint8_t *end = all + total;
  const uint8_t *lines[3];
  size_t lineLens[3];
  int lineNo = 0;
  while (p2 < end && lineNo < 3) {
    const uint8_t *nl = (const uint8_t *)memchr(p2, '\n', (size_t)(end - p2));
    size_t llen = nl ? (size_t)(nl - p2) : (size_t)(end - p2);
    lines[lineNo] = p2;
    lineLens[lineNo] = llen;
    lineNo++;
    p2 = nl ? nl + 1 : end;
  }
  if (lineNo < 3) return false;
  size_t tlen = lineLens[0] < 32 ? lineLens[0] : 32;
  memcpy(target, lines[0], tlen);
  target[tlen] = 0;
  size_t vlen = lineLens[1] < 32 ? lineLens[1] : 32;
  memcpy(version, lines[1], vlen);
  version[vlen] = 0;
  unsigned l = 0;
  bool gotDigits = false;
  for (size_t i = 0; i < lineLens[2]; i++) {
    if (lines[2][i] < '0' || lines[2][i] > '9') return false;
    l = l * 10 + (lines[2][i] - '0');
    gotDigits = true;
  }
  if (!gotDigits) return false;
  len = (uint32_t)l;
  return true;
}

HearthFile *HearthUpdateStage::retainedFwOpen() {
  char p[64];
  path(p, sizeof(p), "fw-retained.bin");
  return _fs ? _fs->open(p, "r") : 0;
}

bool HearthUpdateStage::saveHostPrev(const uint8_t *xip, uint32_t len) {
  if (!_fs) return false;
  char p[64];
  path(p, sizeof(p), "host-prev.bin");
  return writeFile(p, xip, (size_t)len);
}

HearthFile *HearthUpdateStage::hostPrevOpen() {
  char p[64];
  path(p, sizeof(p), "host-prev.bin");
  return _fs ? _fs->open(p, "r") : 0;
}

const char *HearthUpdateStage::hostPrevPath() const {
  static char p[64];
  snprintf(p, sizeof(p), "%s/host-prev.bin", _dir[0] ? _dir : "/hearth");
  return p;
}

const char *HearthUpdateStage::stagedPath() const {
  static char p[64];
  snprintf(p, sizeof(p), "%s/staged.ota", _dir[0] ? _dir : "/hearth");
  return p;
}

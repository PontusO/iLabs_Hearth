#pragma once
/*
 * HearthFsLittle - the device HearthFs, over arduino-pico's LittleFS.
 *
 * Header-only, and the reason is a link cost (final review I8): LittleFS
 * and PicoOTA are archives (dot_a_linkage=true), and the linker pulls an
 * archive member in for any reference from a loose object, before
 * --gc-sections. LittleFS.cpp.o holds a global `FS LittleFS` with a
 * constructor, so one reference from a library .cpp kept all of lfs (about
 * 21 KB) in every sketch that links Hearth, even one that never begins
 * the update. With every reference to LittleFS in this inline header, the
 * archive is pulled in only by a sketch that calls Hearth.update.begin().
 * Guarded on ARDUINO, so the host suite never includes it, and its
 * ArduinoShim.h carries no LittleFS. The whole point of the interface is
 * that HearthUpdateStage runs on this on the device and on HearthFsMem in
 * the host tests without changing a line.
 *
 * begin() returns LittleFS.begin() straight through. That is false on a
 * board flashed with a layout that has no filesystem partition at all, and
 * the update reports HEARTH_UPDATE_ERR_NO_FS for it (Task 4). mkdir()
 * ignores the "already exists" answer, the way LittleFS's own mkdir()
 * returns false for it.
 */
#ifdef ARDUINO
#include "HearthFs.h"
#include <LittleFS.h>

class HearthFileLittle : public HearthFile {
public:
  explicit HearthFileLittle(fs::File f) : _f(f) {}
  virtual ~HearthFileLittle() { close(); }
  int read(uint8_t *b, size_t n) override {
    int r = _f.read(b, n);
    return r < 0 ? 0 : r;
  }
  size_t write(const uint8_t *b, size_t n) override { return _f.write(b, n); }
  bool seek(uint32_t pos) override { return _f.seek(pos); }
  uint32_t size() override { return (uint32_t)_f.size(); }
  uint32_t position() override { return (uint32_t)_f.position(); }
  void close() override {
    if (_f) _f.close();
  }
private:
  fs::File _f;
};

class HearthFsLittle : public HearthFs {
public:
  virtual ~HearthFsLittle() {}
  bool begin() override { return LittleFS.begin(); }
  HearthFile *open(const char *path, const char *mode) override {
    fs::File f = LittleFS.open(path, mode);
    if (!f) return nullptr;
    return new HearthFileLittle(f);
  }
  bool exists(const char *path) override { return LittleFS.exists(path); }
  bool remove(const char *path) override { return LittleFS.remove(path); }
  bool rename(const char *from, const char *to) override { return LittleFS.rename(from, to); }
  bool mkdir(const char *path) override {
    /* LittleFS.mkdir() reports false when the directory is already there,
     * which is not a failure for the caller. */
    return LittleFS.exists(path) || LittleFS.mkdir(path);
  }
  uint32_t freeBytes() override {
    FSInfo info;
    if (!LittleFS.info(info)) return 0;
    return (uint32_t)(info.totalBytes - info.usedBytes);
  }
  uint32_t totalBytes() override {
    FSInfo info;
    if (!LittleFS.info(info)) return 0;
    return (uint32_t)info.totalBytes;
  }
};

inline HearthFs &hearthLittleFs() {
  static HearthFsLittle fs;
  return fs;
}

#endif /* ARDUINO */

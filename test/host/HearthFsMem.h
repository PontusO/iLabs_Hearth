/*
 * HearthFsMem.h - the in-memory HearthFs the update stage tests run
 * against (test_update_stage.cpp).
 *
 * A std::map of path to bytes, so iteration is by path. The free-space
 * limit is settable (setFreeLimit()): totalBytes() is the limit, and a
 * write fails when the file would grow past the free space, which is what
 * makes a staged append fail in exactly the place a real LittleFS would
 * run out. The map's bytes are the single source of truth for the free
 * space, so the stage's .tmp-and-rename writes and the retain rotation's
 * delete-before-write both show up through this bookkeeping, which is the
 * whole point of the limit: the tests can prove which order the writes
 * happen in.
 *
 * Directories are the paths the stage mkdir()s: they are remembered (so
 * exists("/hearth") is true after begin()) but they hold no bytes.
 *
 * HearthFileMem::write() is declared here but defined after HearthFsMem,
 * because it calls back into the owning fs for the free-space check and
 * the fs class must be complete at the point of the call.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>
#include "HearthFs.h"

class HearthFsMem;

class HearthFileMem : public HearthFile {
public:
  HearthFileMem(std::vector<uint8_t> &buf, HearthFsMem *fs, bool isWrite)
    : _buf(buf), _fs(fs), _isWrite(isWrite), _pos(0), _closed(false) {}
  virtual ~HearthFileMem() { close(); }
  int read(uint8_t *b, size_t n) override;
  size_t write(const uint8_t *b, size_t n) override;
  bool seek(uint32_t pos) override { _pos = pos; return true; }
  uint32_t size() override { return (uint32_t)_buf.size(); }
  uint32_t position() override { return (uint32_t)_pos; }
  void close() override { _closed = true; }
private:
  std::vector<uint8_t> &_buf;
  HearthFsMem *_fs;
  bool _isWrite;
  size_t _pos;
  bool _closed;
};

class HearthFsMem : public HearthFs {
public:
  std::map<std::string, std::vector<uint8_t> > files;
  std::map<std::string, bool> dirs;
  uint32_t totalBytes_;

  HearthFsMem() : totalBytes_(256 * 1024 * 1024) {}

  /* Cap the free space. totalBytes() follows it; the current usage is
   * kept, so freeBytes() becomes limit - usage. */
  void setFreeLimit(uint32_t limit) {
    totalBytes_ = limit;
  }

  virtual ~HearthFsMem() {}
  bool begin() override { return true; }
  HearthFile *open(const char *path, const char *mode) override {
    if (!mode || !mode[0]) return nullptr;
    if (mode[0] == 'r') {
      std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.find(path);
      if (it == files.end()) return nullptr;
      return new HearthFileMem(
          const_cast<std::vector<uint8_t>&>(it->second), this, false);
    }
    std::vector<uint8_t> &buf = files[path];
    if (mode[0] == 'w') buf.clear();
    return new HearthFileMem(buf, this, true);
  }
  bool exists(const char *path) override {
    return files.find(path) != files.end() || dirs.find(path) != dirs.end();
  }
  bool remove(const char *path) override {
    return files.erase(path) != 0 || dirs.erase(path) != 0;
  }
  bool rename(const char *from, const char *to) override {
    std::map<std::string, std::vector<uint8_t> >::iterator it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  bool mkdir(const char *path) override {
    dirs[path] = true;
    return true;
  }
  uint32_t freeBytes() override { return fsFreeBytes(); }
  uint32_t totalBytes() override { return totalBytes_; }
  /* non-virtual: the free-space computation, also called by
   * HearthFileMem::write() for its per-write check */
  uint32_t fsFreeBytes() {
    uint32_t used = 0;
    for (std::map<std::string, std::vector<uint8_t> >::const_iterator it = files.begin();
         it != files.end(); ++it) used += (uint32_t)it->second.size();
    return totalBytes_ >= used ? totalBytes_ - used : 0;
  }
};

/* defined here, after HearthFsMem is complete */
inline int HearthFileMem::read(uint8_t *b, size_t n) {
  size_t avail = _buf.size() > _pos ? _buf.size() - _pos : 0;
  if (n > avail) n = avail;
  if (n == 0) return 0;
  memcpy(b, _buf.data() + _pos, n);
  _pos += n;
  return (int)n;
}
inline size_t HearthFileMem::write(const uint8_t *b, size_t n) {
  if (!_isWrite) return 0;
  if (_fs && (uint32_t)(n + _buf.size()) > _fs->fsFreeBytes()) return 0;   /* would not fit */
  _buf.resize(_pos + n);
  memcpy(_buf.data() + _pos, b, n);
  _pos += n;
  return n;
}

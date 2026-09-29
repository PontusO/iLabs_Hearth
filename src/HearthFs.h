/* HearthFs: the thin filesystem surface the update stage stands on.
 *
 * The device implementation wraps arduino-pico's LittleFS
 * (HearthFsLittle.cpp, target only); the host tests drive the stage
 * through HearthFsMem (test/host/HearthFsMem.h). Paths are LittleFS
 * absolute paths; the stage keeps everything under its own directory. */
#pragma once
#include <stddef.h>
#include <stdint.h>

class HearthFile {
public:
  virtual ~HearthFile() {}
  virtual int read(uint8_t *b, size_t n) = 0;       /* bytes read, 0 at end, < 0 on error */
  virtual size_t write(const uint8_t *b, size_t n) = 0;
  virtual bool seek(uint32_t pos) = 0;              /* absolute, from the start */
  virtual uint32_t size() = 0;
  virtual uint32_t position() = 0;
  virtual void close() = 0;
};

class HearthFs {
public:
  virtual ~HearthFs() {}
  virtual bool begin() = 0;                         /* false on a no-FS layout */
  virtual HearthFile *open(const char *path, const char *mode) = 0;   /* "r", "w", "a"; caller deletes */
  virtual bool exists(const char *path) = 0;
  virtual bool remove(const char *path) = 0;
  virtual bool rename(const char *from, const char *to) = 0;
  virtual bool mkdir(const char *path) = 0;
  virtual uint32_t freeBytes() = 0;
  virtual uint32_t totalBytes() = 0;                /* the whole partition, not the free space */
};

/* The target's HearthFs, over LittleFS. HearthFsLittle.cpp only. */
HearthFs &hearthLittleFs();

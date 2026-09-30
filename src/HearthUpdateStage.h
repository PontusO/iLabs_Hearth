/* HearthUpdateStage: the device-side home of the update's files, under a
 * single directory on the HearthFs (LittleFS on the device, HearthFsMem in
 * the host tests).
 *
 * The files and what each of them is for:
 *   manifest          the applied product version (HMF1 + struct)
 *   ota.state         the apply state (HST1 + struct), for resume
 *   staged.ota        the bundle the co-processor is serving, as it lands
 *   fw-retained.bin   the last confirmed-running Hearth image, for rollback
 *   fw-retained.meta  its target, version and length, as text lines
 *   host-prev.bin     the running host image, saved before the host applies
 *
 * Every write goes to a .tmp file and is renamed over the real one, so a
 * power loss at any point leaves the previous file intact: the manifest
 * and the state either have the old value or the new one, never half of
 * either. The staged file is the exception in the other direction: it is
 * the file a power loss is EXPECTED to leave partial, so it is written
 * plain and removed again by stagedEndWrite() when the download did not
 * finish.
 *
 * fw-retained.bin breaks the .tmp rule on purpose (DE625): its rotation
 * deletes the old retained image and its meta first, and only then writes
 * the new one. Holding both at once would not fit the smallest supported
 * filesystem, and the rotation runs only after the new image is confirmed
 * running, so a power loss in the window loses the rollback option, never
 * the service.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "HearthFs.h"
#include "HearthBundle.h"   /* HearthByteSource */

struct HearthManifest {
  uint32_t productVersion;
  char productVersionString[33];
  char hostVersion[33];     /* the last host image applied, "" for none */
};

enum HearthApplyPhase {
  HEARTH_PHASE_NONE = 0,
  HEARTH_PHASE_FW = 1,
  HEARTH_PHASE_HOST = 2,
  HEARTH_PHASE_HOST_CONFIRM = 3
};

struct HearthUpdateState {
  uint8_t phase;            /* a HearthApplyPhase, 0xFF treated as NONE */
  uint8_t attempts;
  uint32_t targetVersion;
  char targetVersionString[33];
  uint8_t fwPart;           /* part indexes into the bundle, 0xFF none */
  uint8_t hostPart;
};

class HearthUpdateStage {
public:
  HearthUpdateStage() : _fs(0), _w(0) { _dir[0] = 0; }

  /* Create the directory (mkdir ignores "already there"). */
  bool begin(HearthFs &fs, const char *dir = "/hearth");

  /* The manifest. loadManifest returns false when there is no manifest
   * file, when it is corrupt (bad magic, wrong size) or when out is null;
   * on success out holds the file and true is returned. */
  bool haveManifest();
  bool loadManifest(HearthManifest &m);
  bool saveManifest(const HearthManifest &m);

  /* The apply state, same contract as the manifest. A state file whose
   * phase byte is not a known phase loads with phase NONE, not a failure:
   * a state we do not recognise is a state there is no point resuming. */
  bool haveState();
  bool loadState(HearthUpdateState &s);
  bool saveState(const HearthUpdateState &s);
  void clearState();

  /* The staged bundle. Begin opens the .tmp, appends grow it (false when
   * the filesystem would not fit the bytes), end closes it and renames it
   * over the real file, or removes it when an append failed, so a partial
   * download never looks like a finished one. */
  bool stagedBeginWrite();
  bool stagedAppend(const uint8_t *p, size_t n);
  void stagedEndWrite();
  bool stagedExists();
  uint32_t stagedSize();
  void stagedRemove();
  HearthFile *stagedOpenRead();   /* caller deletes; null when absent */

  /* Copy [off, off+len) of the staged file to the retained image,
   * streaming in 512-byte reads, then write its meta. See the class
   * comment for the rotation order and why. Returns false on any read or
   * write failure (the old retained image is already gone by then). */
  bool retainFwPart(HearthFile &staged, uint32_t off, uint32_t len,
                    const char *target, const char *version);
  bool retainedFwInfo(char *target, char *version, uint32_t &len);
  HearthFile *retainedFwOpen();   /* caller deletes; null when absent */

  /* The running host image, saved before the host applies a new one. */
  bool saveHostPrev(const uint8_t *xip, uint32_t len);
  HearthFile *hostPrevOpen();     /* caller deletes; null when absent */
  /* The saved image's path, as PicoOTA's addFile() wants it: the
   * first-boot failure re-stages the whole file from offset 0. */
  const char *hostPrevPath() const;

  /* The staged file's path, as PicoOTA's addFile() wants it. */
  const char *stagedPath() const;

private:
  void path(char *out, size_t n, const char *name) const;
  bool writeFile(const char *path, const uint8_t *data, size_t len);

  HearthFs *_fs;
  char _dir[24];
  HearthFile *_w;
  bool _wFailed;   /* an append failed, so endWrite must remove, not rename */
};

/* A HearthByteSource over a HearthFile: the same path the bundle parser
 * takes on the device (stagedOpenRead() into this, into HearthBundle). */
class HearthFileSource : public HearthByteSource {
public:
  explicit HearthFileSource(HearthFile &f) : _f(f) {}
  virtual ~HearthFileSource() {}
  /* Returns true only when all n bytes were read: a short read, such as
   * one near the end of file on a filesystem that may hand back fewer
   * bytes than asked, is a failure, not a success with less. Callers
   * therefore read size-bounded ranges within size(), never a request
   * that could run past the end. */
  bool read(uint32_t offset, uint8_t *buf, size_t n) override {
    if (offset + n > _f.size() || !_f.seek(offset)) return false;
    int r = _f.read(buf, n);
    return r >= 0 && (size_t)r == n;
  }
  uint32_t size() const override { return _f.size(); }
private:
  HearthFile &_f;
};

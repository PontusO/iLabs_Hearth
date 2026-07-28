/*
 * Preferences.cpp - record codec for the Preferences compatibility shim.
 *
 * Layout of the region (HearthPrefsStore.h owns the bytes):
 *
 *   0   'H' 'P' 'R' 'F'      magic
 *   4   version (1)
 *   5   records, back to back, terminated by a zero namespace length:
 *
 *         u8  nslen          0 marks the end of the records
 *         u8  keylen
 *         u8  type           PreferenceType
 *         u8  datalen_lo
 *         u8  datalen_hi
 *         ns[nslen]          not NUL-terminated
 *         key[keylen]
 *         data[datalen]
 *
 * A linear scan, deliberately. The store is 4 KB and holds a handful of
 * settings; an index would cost more bytes than it saves lookups, and every
 * operation here happens at a human's pace (a light being switched, a boot).
 *
 * Namespace and key are stored per record rather than grouped under a
 * namespace header. Grouping would save a few bytes per record and would
 * make removing a namespace a single splice, but it also makes every write
 * to an existing namespace a mid-store insertion. Flat records mean the only
 * structural operations are "overwrite in place" and "delete then append".
 */

#include "Preferences.h"
#include "HearthPrefsStore.h"

#include <string.h>
#include <stdlib.h>
#include <math.h>

namespace {

const uint8_t kMagic[4] = {'H', 'P', 'R', 'F'};
const uint8_t kVersion = 1;
const size_t kHeaderLen = 5;   /* magic + version */
const size_t kRecordHdr = 5;   /* nslen, keylen, type, datalen lo/hi */

size_t rd(size_t off) {
  return hearthPrefsStoreRead(off);
}

bool nameOk(const char *s) {
  if (!s || *s == '\0') {
    return false;
  }
  return strlen(s) <= HEARTH_PREFS_MAX_NAME;
}

/*
 * Validate the header, or lay down a fresh one. An unformatted region is the
 * normal state of a board that has never run this code, and is
 * indistinguishable from one holding somebody else's EEPROM data, so the
 * only safe reading of a bad magic is "not ours yet".
 */
void ensureFormatted() {
  hearthPrefsStoreBegin();
  bool ok = true;
  for (size_t i = 0; i < sizeof(kMagic); i++) {
    if (rd(i) != kMagic[i]) {
      ok = false;
      break;
    }
  }
  if (ok && rd(4) == kVersion) {
    return;
  }
  for (size_t i = 0; i < sizeof(kMagic); i++) {
    hearthPrefsStoreWrite(i, kMagic[i]);
  }
  hearthPrefsStoreWrite(4, kVersion);
  hearthPrefsStoreWrite(kHeaderLen, 0);  /* empty record list */
  hearthPrefsStoreCommit();
}

struct Record {
  size_t off;      /* start of the record header */
  size_t nslen;
  size_t keylen;
  uint8_t type;
  size_t datalen;
  size_t dataOff;  /* start of the payload */
  size_t total;    /* bytes from off to the end of the payload */
};

bool readRecord(size_t off, Record &r) {
  if (off + kRecordHdr > hearthPrefsStoreSize()) {
    return false;
  }
  r.off = off;
  r.nslen = rd(off);
  if (r.nslen == 0) {
    return false;  /* terminator */
  }
  r.keylen = rd(off + 1);
  r.type = (uint8_t)rd(off + 2);
  r.datalen = rd(off + 3) | (rd(off + 4) << 8);
  r.dataOff = off + kRecordHdr + r.nslen + r.keylen;
  r.total = kRecordHdr + r.nslen + r.keylen + r.datalen;
  /* A length field that runs past the region means the store is damaged.
   * Refusing to walk further turns that into "the rest of the store is
   * missing" rather than a read off the end of the buffer. */
  if (off + r.total > hearthPrefsStoreSize()) {
    return false;
  }
  return true;
}

bool matches(const Record &r, const char *ns, const char *key) {
  if (r.nslen != strlen(ns) || r.keylen != strlen(key)) {
    return false;
  }
  size_t p = r.off + kRecordHdr;
  for (size_t i = 0; i < r.nslen; i++) {
    if (rd(p + i) != (uint8_t)ns[i]) {
      return false;
    }
  }
  p += r.nslen;
  for (size_t i = 0; i < r.keylen; i++) {
    if (rd(p + i) != (uint8_t)key[i]) {
      return false;
    }
  }
  return true;
}

/* Offset of the terminator, i.e. the end of the used bytes. */
size_t endOffset() {
  size_t off = kHeaderLen;
  Record r;
  while (readRecord(off, r)) {
    off += r.total;
  }
  return off;
}

bool find(const char *ns, const char *key, Record &out) {
  size_t off = kHeaderLen;
  Record r;
  while (readRecord(off, r)) {
    if (matches(r, ns, key)) {
      out = r;
      return true;
    }
    off += r.total;
  }
  return false;
}

/* Close the gap a removed record leaves, then re-terminate. */
void spliceOut(const Record &r) {
  size_t end = endOffset();
  size_t src = r.off + r.total;
  size_t dst = r.off;
  while (src < end) {
    hearthPrefsStoreWrite(dst++, (uint8_t)rd(src++));
  }
  hearthPrefsStoreWrite(dst, 0);
}

}  // namespace

Preferences::Preferences() : _started(false), _readOnly(false) {
  _name[0] = '\0';
}

Preferences::~Preferences() {
  end();
}

bool Preferences::begin(const char *name, bool readOnly, const char *partition_label) {
  (void)partition_label;  /* NVS has partitions; one EEPROM sector does not. */
  if (!nameOk(name)) {
    return false;
  }
  ensureFormatted();
  strncpy(_name, name, HEARTH_PREFS_MAX_NAME);
  _name[HEARTH_PREFS_MAX_NAME] = '\0';
  _readOnly = readOnly;
  _started = true;
  return true;
}

void Preferences::end() {
  _started = false;
  _name[0] = '\0';
}

bool Preferences::clear() {
  if (!_started || _readOnly) {
    return false;
  }
  /* This namespace only, as upstream's nvs_erase_all does: the handle is
   * scoped to a namespace and clearing somebody else's would be a surprise
   * no caller can guard against. */
  bool any;
  do {
    any = false;
    size_t off = kHeaderLen;
    Record r;
    while (readRecord(off, r)) {
      if (r.nslen == strlen(_name)) {
        bool same = true;
        for (size_t i = 0; i < r.nslen; i++) {
          if (rd(r.off + kRecordHdr + i) != (uint8_t)_name[i]) {
            same = false;
            break;
          }
        }
        if (same) {
          spliceOut(r);
          any = true;
          break;  /* offsets moved; restart the walk */
        }
      }
      off += r.total;
    }
  } while (any);
  return hearthPrefsStoreCommit();
}

bool Preferences::remove(const char *key) {
  if (!_started || _readOnly || !nameOk(key)) {
    return false;
  }
  Record r;
  if (!find(_name, key, r)) {
    return false;
  }
  spliceOut(r);
  return hearthPrefsStoreCommit();
}

size_t Preferences::hearthPut(const char *key, PreferenceType type, const void *data, size_t len) {
  if (!_started || _readOnly || !nameOk(key) || (!data && len > 0)) {
    return 0;
  }
  if (len > 0xFFFF) {
    return 0;
  }

  Record r;
  bool exists = find(_name, key, r);
  if (exists && r.datalen == len && r.type == (uint8_t)type) {
    /* Same shape: overwrite the payload where it lies. Keeps the common case
     * (a setting changing value) from moving every record after it. */
    for (size_t i = 0; i < len; i++) {
      hearthPrefsStoreWrite(r.dataOff + i, ((const uint8_t *)data)[i]);
    }
    return hearthPrefsStoreCommit() ? len : 0;
  }
  if (exists) {
    spliceOut(r);
  }

  size_t nslen = strlen(_name);
  size_t keylen = strlen(key);
  size_t need = kRecordHdr + nslen + keylen + len;
  size_t end = endOffset();
  /* +1 for the terminator that has to follow the new record. */
  if (end + need + 1 > hearthPrefsStoreSize()) {
    /* Out of room. The removal above already happened, so a caller that
     * overwrote an existing key with something too big has lost the old
     * value. Upstream fails the same way for a different reason (NVS full),
     * and pretending otherwise would mean buffering the old record for a
     * case that means the store is misconfigured. */
    hearthPrefsStoreCommit();
    return 0;
  }

  size_t p = end;
  hearthPrefsStoreWrite(p++, (uint8_t)nslen);
  hearthPrefsStoreWrite(p++, (uint8_t)keylen);
  hearthPrefsStoreWrite(p++, (uint8_t)type);
  hearthPrefsStoreWrite(p++, (uint8_t)(len & 0xFF));
  hearthPrefsStoreWrite(p++, (uint8_t)((len >> 8) & 0xFF));
  for (size_t i = 0; i < nslen; i++) {
    hearthPrefsStoreWrite(p++, (uint8_t)_name[i]);
  }
  for (size_t i = 0; i < keylen; i++) {
    hearthPrefsStoreWrite(p++, (uint8_t)key[i]);
  }
  for (size_t i = 0; i < len; i++) {
    hearthPrefsStoreWrite(p++, ((const uint8_t *)data)[i]);
  }
  hearthPrefsStoreWrite(p, 0);
  return hearthPrefsStoreCommit() ? len : 0;
}

size_t Preferences::hearthGet(const char *key, void *out, size_t maxLen, PreferenceType *typeOut) const {
  if (!_started || !nameOk(key)) {
    return 0;
  }
  Record r;
  if (!find(_name, key, r)) {
    return 0;
  }
  if (typeOut) {
    *typeOut = (PreferenceType)r.type;
  }
  if (!out) {
    return r.datalen;  /* length query */
  }
  if (r.datalen > maxLen) {
    return 0;
  }
  for (size_t i = 0; i < r.datalen; i++) {
    ((uint8_t *)out)[i] = (uint8_t)rd(r.dataOff + i);
  }
  return r.datalen;
}

/* ---- typed put ---------------------------------------------------------- */

size_t Preferences::putChar(const char *key, int8_t value) {
  return hearthPut(key, PT_I8, &value, sizeof(value));
}
size_t Preferences::putUChar(const char *key, uint8_t value) {
  return hearthPut(key, PT_U8, &value, sizeof(value));
}
size_t Preferences::putShort(const char *key, int16_t value) {
  return hearthPut(key, PT_I16, &value, sizeof(value));
}
size_t Preferences::putUShort(const char *key, uint16_t value) {
  return hearthPut(key, PT_U16, &value, sizeof(value));
}
size_t Preferences::putInt(const char *key, int32_t value) {
  return hearthPut(key, PT_I32, &value, sizeof(value));
}
size_t Preferences::putUInt(const char *key, uint32_t value) {
  return hearthPut(key, PT_U32, &value, sizeof(value));
}
size_t Preferences::putLong(const char *key, int32_t value) {
  return hearthPut(key, PT_I32, &value, sizeof(value));
}
size_t Preferences::putULong(const char *key, uint32_t value) {
  return hearthPut(key, PT_U32, &value, sizeof(value));
}
size_t Preferences::putLong64(const char *key, int64_t value) {
  return hearthPut(key, PT_I64, &value, sizeof(value));
}
size_t Preferences::putULong64(const char *key, uint64_t value) {
  return hearthPut(key, PT_U64, &value, sizeof(value));
}
size_t Preferences::putFloat(const char *key, float value) {
  return hearthPut(key, PT_BLOB, &value, sizeof(value));
}
size_t Preferences::putDouble(const char *key, double value) {
  return hearthPut(key, PT_BLOB, &value, sizeof(value));
}
/* Upstream stores a bool as a one-byte U8, so the two are interchangeable on
 * read. Keep it that way: a sketch that wrote with putBool and reads with
 * getUChar works on ESP32 and must work here. */
size_t Preferences::putBool(const char *key, bool value) {
  uint8_t v = value ? 1 : 0;
  return hearthPut(key, PT_U8, &v, sizeof(v));
}
size_t Preferences::putString(const char *key, const char *value) {
  if (!value) {
    return 0;
  }
  return hearthPut(key, PT_STR, value, strlen(value) + 1);  /* NUL included */
}
size_t Preferences::putString(const char *key, String value) {
  return putString(key, value.c_str());
}
size_t Preferences::putBytes(const char *key, const void *value, size_t len) {
  return hearthPut(key, PT_BLOB, value, len);
}

/* ---- typed get ---------------------------------------------------------- */

bool Preferences::isKey(const char *key) {
  Record r;
  return _started && nameOk(key) && find(_name, key, r);
}

PreferenceType Preferences::getType(const char *key) {
  PreferenceType t = PT_INVALID;
  Record r;
  if (!_started || !nameOk(key) || !find(_name, key, r)) {
    return PT_INVALID;
  }
  t = (PreferenceType)r.type;
  return t;
}

/*
 * Every scalar getter is the same three steps: read the payload, insist it is
 * exactly the width of the type asked for, otherwise hand back the caller's
 * default. The width check is what stops a getInt() on a key written with
 * putChar() from returning three bytes of whatever follows it.
 */
#define HEARTH_GET_SCALAR(TYPE)                       \
  TYPE v;                                             \
  if (hearthGet(key, &v, sizeof(v), NULL) != sizeof(v)) { \
    return defaultValue;                              \
  }                                                   \
  return v;

int8_t Preferences::getChar(const char *key, int8_t defaultValue) {
  HEARTH_GET_SCALAR(int8_t)
}
uint8_t Preferences::getUChar(const char *key, uint8_t defaultValue) {
  HEARTH_GET_SCALAR(uint8_t)
}
int16_t Preferences::getShort(const char *key, int16_t defaultValue) {
  HEARTH_GET_SCALAR(int16_t)
}
uint16_t Preferences::getUShort(const char *key, uint16_t defaultValue) {
  HEARTH_GET_SCALAR(uint16_t)
}
int32_t Preferences::getInt(const char *key, int32_t defaultValue) {
  HEARTH_GET_SCALAR(int32_t)
}
uint32_t Preferences::getUInt(const char *key, uint32_t defaultValue) {
  HEARTH_GET_SCALAR(uint32_t)
}
int32_t Preferences::getLong(const char *key, int32_t defaultValue) {
  HEARTH_GET_SCALAR(int32_t)
}
uint32_t Preferences::getULong(const char *key, uint32_t defaultValue) {
  HEARTH_GET_SCALAR(uint32_t)
}
int64_t Preferences::getLong64(const char *key, int64_t defaultValue) {
  HEARTH_GET_SCALAR(int64_t)
}
uint64_t Preferences::getULong64(const char *key, uint64_t defaultValue) {
  HEARTH_GET_SCALAR(uint64_t)
}
float Preferences::getFloat(const char *key, float defaultValue) {
  HEARTH_GET_SCALAR(float)
}
double Preferences::getDouble(const char *key, double defaultValue) {
  HEARTH_GET_SCALAR(double)
}

bool Preferences::getBool(const char *key, bool defaultValue) {
  uint8_t v;
  if (hearthGet(key, &v, sizeof(v), NULL) != sizeof(v)) {
    return defaultValue;
  }
  return v != 0;
}

size_t Preferences::getString(const char *key, char *value, size_t maxLen) {
  return hearthGet(key, value, maxLen, NULL);
}

String Preferences::getString(const char *key, String defaultValue) {
  size_t len = getStringLength(key);
  if (len == 0) {
    return defaultValue;
  }
  char *tmp = (char *)malloc(len + 1);
  if (!tmp) {
    return defaultValue;
  }
  size_t got = hearthGet(key, tmp, len, NULL);
  tmp[len] = '\0';  /* the stored NUL is inside `len`; this is belt and braces */
  String out = (got == len) ? String(tmp) : defaultValue;
  free(tmp);
  return out;
}

size_t Preferences::getStringLength(const char *key) {
  PreferenceType t = PT_INVALID;
  size_t len = hearthGet(key, NULL, 0, &t);
  if (t != PT_STR || len == 0) {
    return 0;
  }
  return len - 1;  /* upstream reports the length without the NUL */
}

size_t Preferences::getBytesLength(const char *key) {
  PreferenceType t = PT_INVALID;
  size_t len = hearthGet(key, NULL, 0, &t);
  return (t == PT_BLOB) ? len : 0;
}

size_t Preferences::getBytes(const char *key, void *buf, size_t maxLen) {
  return hearthGet(key, buf, maxLen, NULL);
}

/*
 * Upstream answers in NVS entries, a unit that has no meaning here. Bytes
 * left in the region is the honest analogue and the number a caller checking
 * "will my next put fit" actually wants.
 */
size_t Preferences::freeEntries() {
  ensureFormatted();
  size_t end = endOffset();
  size_t size = hearthPrefsStoreSize();
  return (end + 1 < size) ? (size - end - 1) : 0;
}

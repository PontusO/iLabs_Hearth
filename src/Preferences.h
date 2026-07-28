/*
 * Preferences.h - the arduino-esp32 Preferences API, on arduino-pico.
 *
 * arduino-pico ships no Preferences and nothing equivalent, so every
 * arduino-esp32 sketch that remembers state across a reboot fails to compile
 * on a Challenger. Two of the three upstream Matter examples this library
 * carries do exactly that. The examples are kept byte-identical to upstream
 * deliberately (that identity is the whole claim of the port), so the gap has
 * to be filled underneath them rather than edited out of them.
 *
 * Per the naming rule the file is a compatibility shim like HearthCompat.h:
 * the class keeps its upstream name because it is an interop symbol. This
 * header deliberately claims the global `Preferences.h` include name, which
 * is what makes an unmodified `#include <Preferences.h>` resolve. A sketch
 * that also installs some other Preferences library will get the usual
 * "Multiple libraries were found" notice and one of the two will win.
 *
 * Storage: arduino-pico's EEPROM, not a filesystem.
 *
 *   EEPROM is a 4 KB flash sector the core always reserves, present whatever
 *   the board's flash menu says. LittleFS would be the closer analogue of
 *   NVS (wear levelling, room to grow, a natural namespace mapping), but the
 *   default flash option for every Challenger is "no FS", so a LittleFS-backed
 *   store would fail to mount on a default install and every get() would
 *   silently return its default. Silently wrong beats nothing only in the
 *   short term.
 *
 * Fidelity, and where it stops:
 *
 *   - Semantics match upstream where the API is observable: put() writes
 *     through immediately (NVS does too), get() returns the caller's default
 *     for a missing key, and namespace and key names are capped at 15
 *     characters as NVS caps them, so a name that would fail there fails here.
 *   - readOnly mode is honoured: put/remove/clear on a read-only handle fail.
 *   - There is no partition_label. NVS has partitions; a single EEPROM sector
 *     does not. The parameter is accepted and ignored, so upstream call sites
 *     still compile.
 *   - The whole store is HEARTH_PREFS_SIZE bytes for ALL namespaces together,
 *     against NVS's tens of kilobytes. A put() that does not fit fails and
 *     returns 0 rather than evicting anything.
 *   - No wear levelling. Every put() rewrites the sector. At the rate these
 *     examples write (a light being switched) that is decades of flash life;
 *     a sketch that writes in a loop will destroy the sector, exactly as it
 *     would with EEPROM directly.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>  /* NAN, the default for getFloat()/getDouble() upstream */

/*
 * Region of the EEPROM sector this store owns. A sketch that uses EEPROM
 * directly as well must keep out of it, or move it with a build flag.
 * Default is the whole sector, because a sketch that reached for Preferences
 * was not planning to hand-roll EEPROM records too.
 */
#ifndef HEARTH_PREFS_OFFSET
#define HEARTH_PREFS_OFFSET 0
#endif
#ifndef HEARTH_PREFS_SIZE
#define HEARTH_PREFS_SIZE 4096
#endif

/* NVS's own limits, reproduced so a name that fails on ESP32 fails here. */
#define HEARTH_PREFS_MAX_NAME 15

typedef enum {
  PT_I8,
  PT_U8,
  PT_I16,
  PT_U16,
  PT_I32,
  PT_U32,
  PT_I64,
  PT_U64,
  PT_STR,
  PT_BLOB,
  PT_INVALID
} PreferenceType;

class Preferences {
protected:
  char _name[HEARTH_PREFS_MAX_NAME + 1];
  bool _started;
  bool _readOnly;

public:
  Preferences();
  ~Preferences();

  bool begin(const char *name, bool readOnly = false, const char *partition_label = NULL);
  void end();

  bool clear();
  bool remove(const char *key);

  size_t putChar(const char *key, int8_t value);
  size_t putUChar(const char *key, uint8_t value);
  size_t putShort(const char *key, int16_t value);
  size_t putUShort(const char *key, uint16_t value);
  size_t putInt(const char *key, int32_t value);
  size_t putUInt(const char *key, uint32_t value);
  size_t putLong(const char *key, int32_t value);
  size_t putULong(const char *key, uint32_t value);
  size_t putLong64(const char *key, int64_t value);
  size_t putULong64(const char *key, uint64_t value);
  size_t putFloat(const char *key, float value);
  size_t putDouble(const char *key, double value);
  size_t putBool(const char *key, bool value);
  size_t putString(const char *key, const char *value);
  size_t putString(const char *key, String value);
  size_t putBytes(const char *key, const void *value, size_t len);

  bool isKey(const char *key);
  PreferenceType getType(const char *key);
  int8_t getChar(const char *key, int8_t defaultValue = 0);
  uint8_t getUChar(const char *key, uint8_t defaultValue = 0);
  int16_t getShort(const char *key, int16_t defaultValue = 0);
  uint16_t getUShort(const char *key, uint16_t defaultValue = 0);
  int32_t getInt(const char *key, int32_t defaultValue = 0);
  uint32_t getUInt(const char *key, uint32_t defaultValue = 0);
  int32_t getLong(const char *key, int32_t defaultValue = 0);
  uint32_t getULong(const char *key, uint32_t defaultValue = 0);
  int64_t getLong64(const char *key, int64_t defaultValue = 0);
  uint64_t getULong64(const char *key, uint64_t defaultValue = 0);
  float getFloat(const char *key, float defaultValue = NAN);
  double getDouble(const char *key, double defaultValue = NAN);
  bool getBool(const char *key, bool defaultValue = false);
  size_t getString(const char *key, char *value, size_t maxLen);
  String getString(const char *key, String defaultValue = String());
  size_t getStringLength(const char *key);
  size_t getBytesLength(const char *key);
  size_t getBytes(const char *key, void *buf, size_t maxLen);
  size_t freeEntries();

private:
  size_t hearthPut(const char *key, PreferenceType type, const void *data, size_t len);
  size_t hearthGet(const char *key, void *out, size_t maxLen, PreferenceType *typeOut) const;
};

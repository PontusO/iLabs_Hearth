/*
 * PreferencesEEPROM.cpp - the arduino-pico EEPROM backing for Preferences.
 *
 * Target-only: the host test build supplies its own byte array instead. See
 * HearthPrefsStore.h for why the split exists.
 *
 * arduino-pico's EEPROM is a RAM copy of the last 4 KB flash sector, which
 * the core reserves whatever the board's flash menu says about filesystems.
 * That is the entire reason this store is EEPROM and not LittleFS: every
 * Challenger's default flash option is "no FS".
 */

#include "HearthPrefsStore.h"
#include "Preferences.h"

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)

#include <EEPROM.h>

static bool s_begun = false;

void hearthPrefsStoreBegin(void) {
  if (s_begun) {
    return;
  }
  /* The whole sector, not just this store's region: EEPROM.begin() sizes the
   * RAM copy and the flush, and asking for less than the region's end would
   * put our last byte outside it. */
  EEPROM.begin(HEARTH_PREFS_OFFSET + HEARTH_PREFS_SIZE);
  s_begun = true;
}

uint8_t hearthPrefsStoreRead(size_t off) {
  if (off >= HEARTH_PREFS_SIZE) {
    return 0;
  }
  return EEPROM.read((int)(HEARTH_PREFS_OFFSET + off));
}

void hearthPrefsStoreWrite(size_t off, uint8_t v) {
  if (off >= HEARTH_PREFS_SIZE) {
    return;
  }
  EEPROM.write((int)(HEARTH_PREFS_OFFSET + off), v);
}

bool hearthPrefsStoreCommit(void) {
  return EEPROM.commit();
}

size_t hearthPrefsStoreSize(void) {
  return HEARTH_PREFS_SIZE;
}

#endif  /* RP2040 / RP2350 */

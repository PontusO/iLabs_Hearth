/*
 * HearthPrefsStore.h - the byte store Preferences records live in.
 *
 * Split out from Preferences.cpp so the record codec is testable on the host
 * without an EEPROM: the codec is the part with the interesting failure modes
 * (record walking, in-place overwrite, compaction, the region running out),
 * and it has no business knowing what flash is.
 *
 * Target builds get PreferencesEEPROM.cpp. The host test build substitutes a
 * plain byte array and is the only reason this interface is not just three
 * calls into EEPROM.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Make the backing store readable and writable. Idempotent; every entry
 * point into Preferences calls it, because a sketch is free to construct a
 * Preferences object as a global, before any begin() has run. */
void hearthPrefsStoreBegin(void);

/* Byte access, relative to the start of this store's region. Out-of-range
 * reads answer 0 and out-of-range writes are dropped, so a corrupt length
 * field in the region cannot walk the codec off the end. */
uint8_t hearthPrefsStoreRead(size_t off);
void hearthPrefsStoreWrite(size_t off, uint8_t v);

/* Push the working copy to flash. Called after every mutation, matching
 * upstream, where a put() reaches NVS before it returns. */
bool hearthPrefsStoreCommit(void);

/* Usable size of the region, in bytes. */
size_t hearthPrefsStoreSize(void);

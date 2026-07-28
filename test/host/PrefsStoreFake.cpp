/*
 * PrefsStoreFake.cpp - the Preferences byte store, as a plain array.
 *
 * Stands in for PreferencesEEPROM.cpp on the host so the record codec can be
 * tested without flash. hearthPrefsFakeReset() is the extra: it gives a test
 * a store that has never been formatted, which is the state of a board out of
 * the factory and the one case the codec has to detect rather than trust.
 *
 * hearthPrefsFakeCommits() counts commits, so a test can pin that a put()
 * reaches storage before it returns, which is the property that makes this
 * shim behave like NVS rather than like a write-back cache.
 */

#include "HearthPrefsStore.h"
#include "PrefsStoreFake.h"
#include <string.h>

static uint8_t s_store[HEARTH_PREFS_FAKE_SIZE];
static bool s_begun = false;
static int s_commits = 0;
static bool s_commit_fails = false;

void hearthPrefsFakeReset(void) {
  memset(s_store, 0xFF, sizeof(s_store));  /* erased flash, not zeroed */
  s_begun = false;
  s_commits = 0;
  s_commit_fails = false;
}

int hearthPrefsFakeCommits(void) {
  return s_commits;
}

void hearthPrefsFakeFailCommits(bool fail) {
  s_commit_fails = fail;
}

uint8_t *hearthPrefsFakeBytes(void) {
  return s_store;
}

void hearthPrefsStoreBegin(void) {
  s_begun = true;
}

uint8_t hearthPrefsStoreRead(size_t off) {
  if (off >= sizeof(s_store)) {
    return 0;
  }
  return s_store[off];
}

void hearthPrefsStoreWrite(size_t off, uint8_t v) {
  if (off >= sizeof(s_store)) {
    return;
  }
  s_store[off] = v;
}

bool hearthPrefsStoreCommit(void) {
  s_commits++;
  return !s_commit_fails;
}

size_t hearthPrefsStoreSize(void) {
  return sizeof(s_store);
}

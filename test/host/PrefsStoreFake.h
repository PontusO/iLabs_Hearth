#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * Deliberately small, far below the 4 KB the real EEPROM store gets: a
 * region-full test that had to write four kilobytes of records would be slow
 * to run and impossible to read. 256 bytes exercises the same boundary.
 */
#define HEARTH_PREFS_FAKE_SIZE 256

void hearthPrefsFakeReset(void);
int hearthPrefsFakeCommits(void);
void hearthPrefsFakeFailCommits(bool fail);
uint8_t *hearthPrefsFakeBytes(void);

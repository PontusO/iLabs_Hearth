/*
 * HearthFlasher: the co-processor's image flashers (plan Task 5). One
 * interface, one implementation per co-processor family:
 *
 *   - HearthFlasherSmp     MCUboot's serial recovery (nRF54L15/LM20A),
 *                          the SMP/newtmgr protocol over the same UART
 *                          (Task 5a, this family)
 *   - HearthFlasherXmodem  the MG24 Gecko bootloader (Task 5b)
 *   - HearthFlasherEsp     the ESP32-C6 ROM download loader (Task 5c)
 *
 * flash() owns the whole recovery window: it enters the co-processor's
 * recovery mode on the same UART (strap asserted, reset pulsed, strap
 * released at the end), writes the image and verifies it, then resets the
 * device into the new application. It is blocking; a full image takes
 * minutes, so the caller (the update apply path) runs it on its own and
 * waits for +MTREADY after it returns.
 *
 * The pins come from HearthUpdateConfig (resetPin/strapPin plus their
 * active-low flags). A pin of -1 means the board variant defines no such
 * line and the sketch supplies none: a flasher that cannot drive the
 * lines refuses with HEARTH_FLASH_ERR_ENTER rather than guessing.
 *
 * The stream type comes from the platform: on target the core's Stream
 * (arduino-pico's is arduino::Stream, made visible by the core's own
 * using directive), on the host test the shim's (test/host/Arduino.h
 * redirects <Arduino.h> to the shim, the same pattern HearthLink.h
 * uses). A forward declaration at global scope would declare a different
 * class than the core's, so the type is taken from <Arduino.h>.
 */
#pragma once
#include <Arduino.h>
#include <stdint.h>

class HearthByteSource;   /* HearthBundle.h */

/*
 * The co-processor's control pins for recovery entry and exit. reset and
 * strap are Arduino pin numbers, -1 for "not wired here"; the
 * activeLow flags carry the polarity (every family this library knows
 * is active low with an active-low reset, but the flags stay so a
 * high-active part later does not need a new interface).
 */
struct HearthCoprocPins {
  int reset;
  bool resetActiveLow;
  int strap;
  bool strapActiveLow;
};

class HearthFlasher {
public:
  virtual ~HearthFlasher() {}

  /*
   * Program the application image read from src[off, off+len) and verify
   * it. sha256 is the digest of exactly those bytes (the bundle part's
   * digest, which covers the same range). Returns 0, or a negative
   * HearthFlasherErr. Blocking, by design: the co-processor's recovery
   * window is ours for the duration, so no other link traffic may be in
   * flight when this runs.
   */
  virtual int flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
                    const uint8_t sha256[32]) = 0;

  /*
   * Which implementation serves this AT+CGMM model string, or nullptr
   * when no flasher knows the part (the update then reports the flash
   * error to the co-processor and the state file keeps the bundle for a
   * manual retry). The result is a pointer to a library-owned,
   * function-local static instance: never delete it, and treat the
   * object as mutable state the caller must not race with (the whole
   * library is single-threaded, so that holds).
   */
  static HearthFlasher *forModel(const char *model);
};

enum HearthFlasherErr {
  HEARTH_FLASH_OK = 0,
  HEARTH_FLASH_ERR_ENTER = -1,     /* the recovery strap/reset could not be driven, or no reply came */
  HEARTH_FLASH_ERR_PROTOCOL = -2,  /* a reply the protocol does not allow (bad rc, bad frame, stalled) */
  HEARTH_FLASH_ERR_WRITE = -3,     /* the image would not land in the co-processor's flash */
  HEARTH_FLASH_ERR_VERIFY = -4,    /* the written image fails verification */
  HEARTH_FLASH_ERR_SOURCE = -5     /* the image bytes could not be read (or are not this image) */
};

/*
 * Drive the co-processor's recovery strap: recovery true asserts it,
 * false releases it, honouring the pin's polarity. Returns false when the
 * pin is -1 (nothing to drive), true otherwise. On the host this is a
 * no-op that reports the pin's availability; under ARDUINO it sets the
 * pin mode and the level.
 */
bool hearthCoprocStrap(const HearthCoprocPins &p, bool recovery);

/*
 * Pulse the co-processor's reset line: asserted for pulseMs, released
 * afterwards, honouring the polarity. Returns false when the pin is -1,
 * true otherwise. Same host/Arduino split as hearthCoprocStrap().
 */
bool hearthCoprocReset(const HearthCoprocPins &p, uint32_t pulseMs = 50);

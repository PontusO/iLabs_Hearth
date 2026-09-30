/*
 * HearthFlasherEsp: the ESP32-C6 co-processor's ROM download loader, over
 * the vendored esp-serial-flasher (src/vendor/esp_loader, see its
 * VENDORED.md). The wire is the Espressif UART ROM protocol: SLIP-framed
 * commands at 115200, a fast 921600 rate the ROM takes on command, and
 * flash writes the ROM verifies by MD5.
 *
 * Two halves live in this file:
 *
 *   1. The port layer, the loader_port_* functions the vendored library
 *      calls (esp_loader_io.h). They reach the bound Stream and the
 *      co-processor's reset and strap pins through file-static state the
 *      brief's bind() sets up. The reference for their semantics is
 *      upstream's own Pi Pico port (.superpowers/.../ref-pi_pico_port.c):
 *      read returns ESP_LOADER_ERROR_TIMEOUT when `size` bytes have not
 *      all arrived within `timeout` ms, and the timer pair is on millis().
 *
 *   2. flash(), the sequence on top of the library. It owns the whole
 *      recovery window, exactly as the other two families: the strap is
 *      asserted on entry and released on every exit, so a failed flash
 *      never leaves IO9 asserted, and a failure after the entry reset
 *      also resets the target (B668), so it restarts into its
 *      application. The caller re-clocks the AT link after it returns.
 *
 * The library is compiled through esp_loader_build.c under
 * esp_loader_hearth_config.h (UART, MD5_ENABLED, 3 write retries). C++
 * includes it config-first, then esp_loader.h and esp_loader_io.h, in
 * that order; the headers carry their own extern "C" guards.
 */
#include "HearthFlasherEsp.h"
#include "HearthFlasherEspInternal.h"
#include "HearthBundle.h"   /* HearthByteSource */

#include "vendor/esp_loader/esp_loader_hearth_config.h"
#include "vendor/esp_loader/esp_loader.h"
#include "vendor/esp_loader/esp_loader_io.h"

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#ifdef HEARTH_SERIAL_PORT
#include "Hearth.h"         /* HEARTH_SERIAL_PORT, the AT link's port object */
#endif
#else
#include "ArduinoShim.h"    /* millis()/yield()/Stream for the host suite */
#endif

/* ---- the C6's ROM-download constants (platform/esp32c6/fw/flash.py) ---- */
#define HEARTH_ESP_ROM_BAUD 115200   /* the ROM's fixed entry rate */
#define HEARTH_ESP_FAST_BAUD 921600  /* the ROM takes this on command once connected */
#define HEARTH_ESP_APP_OFFSET 0x20000  /* the application slot: only it is written, never the bootloader, partition table or nvs */
#define HEARTH_ESP_BLOCK 1024        /* the write block size, flash.py's CHUNK */
#define HEARTH_ESP_RESET_MS 100      /* the reset pulse width, flash.py's RESET_HOLD */
#define HEARTH_ESP_BOOT_HOLD_MS 50   /* the strap hold after the reset, before release */

/* ---- file-static port state (the bound stream, the pins, the timer) ---- */

static Stream *s_uart;
static HearthCoprocPins s_pins;
static uint32_t s_timer_deadline;   /* absolute millis() the running timer runs to */

/*
 * Bind the loader port to a stream and the co-processor's pins. The whole
 * connection setup is this: the vendored library has no init call of its
 * own on the UART, it reaches the hardware only through the port layer,
 * so the bound stream and pins are the entire link.
 */
void hearthEspPortBind(Stream *uart, const HearthCoprocPins &pins) {
  s_uart = uart;
  s_pins = pins;
}

/*
 * True only where the AT link's port is known and can be re-clocked.
 * M9: and only when the stream the port layer is bound to IS that port.
 * When the link runs on a sketch-owned Stream (Hearth.begin(Stream&)),
 * s_uart points at the sketch's stream, not at HEARTH_SERIAL_PORT, so
 * re-clocking would change a port the library does not own and the rate
 * is left alone (as on the host, where the macro is not defined).
 * flash() binds the port (hearthEspPortBind) before it consults this, so
 * s_uart is the stream the flasher was handed by the time it is read.
 */
bool hearthEspCanReclock() {
#if defined(ARDUINO) && defined(HEARTH_SERIAL_PORT)
  return s_uart == (Stream *)&HEARTH_SERIAL_PORT;
#else
  return false;
#endif
}

/* ---- the port layer: the functions esp-serial-flasher calls ---- */

extern "C" {

/* Every byte to the bound stream, byte by byte (the shim's Stream has no
 * write(buf, n)). A write never times out on this link: it returns
 * SUCCESS once all the bytes are handed to the stream. */
esp_loader_error_t loader_port_write(const uint8_t *data, uint16_t size, uint32_t timeout) {
  (void)timeout;
  if (s_uart) {
    for (uint16_t i = 0; i < size; i++) {
      s_uart->write(data[i]);
    }
  }
  return ESP_LOADER_SUCCESS;
}

/* Fill data with size bytes, or ESP_LOADER_ERROR_TIMEOUT when they have
 * not all arrived within timeout ms (the Pi Pico port's semantics). The
 * deadline is absolute on millis(); a short read yields and retries until
 * the deadline passes. M2: the comparison is by subtraction, so the
 * 49.7-day millis() wrap does not make a fresh wait time out at once. */
esp_loader_error_t loader_port_read(uint8_t *data, uint16_t size, uint32_t timeout) {
  uint32_t deadline = millis() + timeout;
  uint16_t got = 0;
  while (got < size) {
    int avail = s_uart ? s_uart->available() : 0;
    while (avail > 0 && got < size) {
      int c = s_uart->read();
      if (c < 0) break;
      data[got++] = (uint8_t)c;
      avail--;
    }
    if (got >= size) break;
    if ((int32_t)(millis() - deadline) >= 0) return ESP_LOADER_ERROR_TIMEOUT;
    yield();
  }
  return ESP_LOADER_SUCCESS;
}

void loader_port_delay_ms(uint32_t ms) {
  delay(ms);
}

void loader_port_start_timer(uint32_t ms) {
  s_timer_deadline = millis() + ms;
}

/* Remaining time since start_timer, 0 once past, never wrapping (M2: the
 * subtraction is signed, so the millis() wrap does not read as a huge
 * remaining time or an instant expiry). */
uint32_t loader_port_remaining_time(void) {
  uint32_t now = millis();
  int32_t rem = (int32_t)(s_timer_deadline - now);
  if (rem <= 0) return 0;
  return (uint32_t)rem;
}

/* Assert the strap, pulse the reset, hold, release the strap. IO9 is
 * sampled at reset only, so it is released after the hold. */
void loader_port_enter_bootloader(void) {
  hearthCoprocStrap(s_pins, true);
  hearthCoprocReset(s_pins, HEARTH_ESP_RESET_MS);
  delay(HEARTH_ESP_BOOT_HOLD_MS);
  hearthCoprocStrap(s_pins, false);
}

/* Reset into the running application: strap released, one reset pulse. */
void loader_port_reset_target(void) {
  hearthCoprocStrap(s_pins, false);
  hearthCoprocReset(s_pins, HEARTH_ESP_RESET_MS);
}

/* The library calls this only with SERIAL_FLASHER_DEBUG_TRACE on, which is
 * off in Hearth's configuration, so it prints nothing. */
void loader_port_debug_print(const char *str) {
  (void)str;
}

/* Re-clock the port to the ROM's fast rate. Only the AT link's port can
 * follow the ROM; anywhere else the function is unsupported, and the
 * caller (flash()) then never asks the ROM to change rate. M9: the
 * re-clock also requires the bound stream to BE that port, so a
 * sketch-owned Stream (Hearth.begin(Stream&)) is never re-clocked. */
esp_loader_error_t loader_port_change_transmission_rate(uint32_t transmission_rate) {
#if defined(ARDUINO) && defined(HEARTH_SERIAL_PORT)
  if (s_uart != (Stream *)&HEARTH_SERIAL_PORT) {
    return ESP_LOADER_ERROR_UNSUPPORTED_FUNC;
  }
  HEARTH_SERIAL_PORT.begin(transmission_rate);
  return ESP_LOADER_SUCCESS;
#else
  (void)transmission_rate;
  return ESP_LOADER_ERROR_UNSUPPORTED_FUNC;
#endif
}

}   /* extern "C" */

/* ---- flash(): the sequence on top of the library ---- */

HearthFlasherEsp::HearthFlasherEsp() {}

/*
 * B668: a failure AFTER the entry reset (the ROM connection asserted the
 * strap across a reset pulse) leaves the co-processor in its ROM
 * download mode. Release the strap and pulse the reset, so it restarts
 * into its application (or, if the application slot was partly written,
 * into whatever its bootloader then does; the apply's retry and rollback
 * handle that). The pin check and the port re-clock that refused before
 * the entry run before any entry reset, so they keep their plain
 * returns.
 */
static int espFail(const HearthCoprocPins &pins, int err) {
  hearthCoprocStrap(pins, false);
  hearthCoprocReset(pins, 50);
  return err;
}

int HearthFlasherEsp::flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
                            const uint8_t sha256[32]) {
  (void)sha256;   /* the bundle verified it; the ROM's MD5 verifies the written flash */

  /* A pin of -1 is refused before anything is written, exactly as the
   * XMODEM client does at the top of its flash(). M3: the availability
   * is checked by the pin numbers, not by driving the lines (the old
   * check pulsed the reset for a real 1 ms on the device just to test
   * availability). The ESP flasher's existing semantics stay: refuse
   * when either pin is -1. */
  if (pins.reset < 0 || pins.strap < 0) {
    return HEARTH_FLASH_ERR_ENTER;
  }

  hearthEspPortBind(&uart, pins);
  bool raised = false;

  /* Step 3: where the port can follow, set it to the ROM's entry rate
   * first (the AT link may have run faster). */
  if (hearthEspCanReclock()) {
    if (loader_port_change_transmission_rate(HEARTH_ESP_ROM_BAUD) != ESP_LOADER_SUCCESS) {
      return HEARTH_FLASH_ERR_ENTER;   /* the port would not take the entry rate */
    }
  }

  /* Step 4: the ROM connection. enter_bootloader inside it asserts the
   * strap, pulses the reset, holds, and releases it again. */
  esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
  esp_loader_error_t cerr = esp_loader_connect(&args);
  if (cerr != ESP_LOADER_SUCCESS) {
    return espFail(pins, cerr == ESP_LOADER_ERROR_TIMEOUT ? HEARTH_FLASH_ERR_ENTER : HEARTH_FLASH_ERR_PROTOCOL);
  }
  if (esp_loader_get_target() != ESP32C6_CHIP) {
    return espFail(pins, HEARTH_FLASH_ERR_PROTOCOL);
  }

  /* Step 5: only where the port can follow, ask the ROM for the fast
   * rate and re-clock the port to match. On any failure the port stays at
   * 115200 and the ROM (having not changed) is left as is. */
  if (hearthEspCanReclock()) {
    if (esp_loader_change_transmission_rate(HEARTH_ESP_FAST_BAUD) == ESP_LOADER_SUCCESS) {
      if (loader_port_change_transmission_rate(HEARTH_ESP_FAST_BAUD) == ESP_LOADER_SUCCESS) {
        delay(50);
        raised = true;
      }
    }
  }

  /* Step 6: start the flash write into the application slot. On failure,
   * once only, the fallback: the port back to 115200, a fresh connect, no
   * rate change, flash_start again. */
  esp_loader_error_t ferr = esp_loader_flash_start(HEARTH_ESP_APP_OFFSET, len, HEARTH_ESP_BLOCK);
  if (ferr != ESP_LOADER_SUCCESS && raised) {
    loader_port_change_transmission_rate(HEARTH_ESP_ROM_BAUD);
    esp_loader_connect_args_t args2 = ESP_LOADER_CONNECT_DEFAULT();
    esp_loader_error_t rc2 = esp_loader_connect(&args2);
    if (rc2 == ESP_LOADER_SUCCESS && esp_loader_get_target() == ESP32C6_CHIP) {
      raised = false;
      ferr = esp_loader_flash_start(HEARTH_ESP_APP_OFFSET, len, HEARTH_ESP_BLOCK);
    }
  }
  if (ferr != ESP_LOADER_SUCCESS) {
    return espFail(pins, HEARTH_FLASH_ERR_WRITE);
  }

  /* Step 7: the image in 1024-byte chunks from one file-static buffer. */
  static uint8_t block[HEARTH_ESP_BLOCK];
  for (uint32_t pos = 0; pos < len; pos += HEARTH_ESP_BLOCK) {
    uint32_t n = len - pos;
    if (n > HEARTH_ESP_BLOCK) n = HEARTH_ESP_BLOCK;
    if (!src.read(off + pos, block, n)) {
      return espFail(pins, HEARTH_FLASH_ERR_SOURCE);
    }
    if (esp_loader_flash_write(block, n) != ESP_LOADER_SUCCESS) {
      return espFail(pins, HEARTH_FLASH_ERR_WRITE);
    }
  }

  /* Step 8: the ROM's MD5 check of the written region. */
  esp_loader_error_t verr = esp_loader_flash_verify();
  if (verr != ESP_LOADER_SUCCESS) {
    return espFail(pins, HEARTH_FLASH_ERR_VERIFY);
  }

  /* Step 9: reset the co-processor into the new application. Every path
   * here has released the strap (or this exit does), so a failed or
   * successful flash never leaves IO9 asserted. */
  esp_loader_reset_target();
  return HEARTH_FLASH_OK;
}

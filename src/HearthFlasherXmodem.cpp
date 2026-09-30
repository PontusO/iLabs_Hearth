/*
 * HearthFlasherXmodem: the MG24 co-processor's Gecko Bootloader
 * (v3.02.01, the uart-xmodem build) client, ported from the firmware
 * repo's platform/silabs/fw/xmodem.py (the authoritative sender: crc16,
 * block building, handshake, per-block retries, EOT) and flash.py's
 * entry, upload and exit sequence. The MG24 README ("Uploading an
 * application: fw/flash.py") documents the same sequence against the
 * bench module.
 *
 * The wire, in the order this file meets it:
 *   1. the host UART at 115200, fixed: the bootloader never changes rate
 *      during a download, so the port is set back to 115200 before entry
 *      even when the AT link ran faster than that.
 *   2. entry: strap asserted low for 100 ms, reset pulsed 100 ms, read
 *      until the menu's "BL >" prompt (within 3 s), then the strap
 *      released (it is sampled at boot only).
 *   3. "1": the bootloader echoes the 17-byte "\r\nbegin upload\r\n\0"
 *      preamble, THEN the XMODEM 'C'. The preamble is drained first, and
 *      the 'C' must be consumed before any block goes out.
 *   4. XMODEM-CRC, 128-byte SOH blocks only (the bootloader's parser
 *      never takes the 1K STX form): SOH, seq (from 1, wrapping at 256),
 *      255-seq, 128 bytes padded 0x1A, CRC16-XMODEM big-endian. An ACK
 *      advances, a NAK resends (10 retries); only a NAK causes a
 *      retransmit, a missing reply within 10 s is fatal (xmodem.py's
 *      behaviour), a CAN aborts.
 *   5. EOT expecting ACK, then the menu's "Serial upload complete"
 *      within 5 s.
 *   6. 200 ms, then "2" from the menu runs the application; no reset
 *      pulse is needed, the caller waits for +MTREADY on the AT link.
 *
 * A 1 MB .gbl image is about 14 ms a block, about two minutes.
 */
#include "HearthFlasherXmodem.h"
#include "HearthFlasherXmodemInternal.h"
#include "HearthBundle.h"   /* HearthByteSource */

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#include "Hearth.h"         /* HEARTH_SERIAL_PORT, the AT link's port object */
#else
#include "ArduinoShim.h"    /* millis()/yield()/Stream for the host suite */
#endif

/* ---- XMODEM control bytes (xmodem.py) ---- */
#define HEARTH_XMODEM_SOH     0x01   /* 128-byte block header */
#define HEARTH_XMODEM_EOT     0x04   /* end of transmission */
#define HEARTH_XMODEM_ACK     0x06
#define HEARTH_XMODEM_NAK     0x15
#define HEARTH_XMODEM_CAN     0x18
#define HEARTH_XMODEM_CRC_C   0x43   /* 'C': the receiver asks for the CRC variant */

/* Entry and upload timings (flash.py, the brief): strap settle 100 ms,
 * reset pulse 100 ms, the menu within 3 s, the preamble and 'C' within
 * 5 s, a missing reply fatal after three 3-second waits, 10 NAK retries,
 * "Serial upload complete" within 5 s, and 200 ms before the menu's "2". */
#define HEARTH_XMODEM_SETTLE_MS        100UL
#define HEARTH_XMODEM_PULSE_MS         100UL
#define HEARTH_XMODEM_MENU_TIMEOUT_MS  3000UL
#define HEARTH_XMODEM_HANDSHAKE_MS     5000UL
#define HEARTH_XMODEM_REPLY_WAIT_MS    3000UL   /* per xmodem.py's _wait_for */
#define HEARTH_XMODEM_REPLY_TRIES      3        /* xmodem.py's _wait_for(tries=3) */
#define HEARTH_XMODEM_RETRIES          10       /* xmodem.py's send(retries=10) */
#define HEARTH_XMODEM_DONE_TIMEOUT_MS  5000UL
#define HEARTH_XMODEM_RUN_SETTLE_MS    200UL

/* ---- crc16-xmodem (poly 0x1021, init 0, no reflection, no xor-out) ---- */

uint16_t xmodemCrc16(const uint8_t *data, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* ---- block building (xmodem.py's build_block(seq, payload, 128)) ---- */

void xmodemBuildBlock(uint8_t out[3 + HEARTH_XMODEM_BLOCK + 2], uint8_t seq, const uint8_t *payload, size_t n) {
  if (n > HEARTH_XMODEM_BLOCK) n = HEARTH_XMODEM_BLOCK;
  out[0] = HEARTH_XMODEM_SOH;
  out[1] = seq;
  out[2] = (uint8_t)(0xFF - seq);   /* 255 - seq: the complement, one byte */
  if (payload) {
    memcpy(out + 3, payload, n);
  }
  if (n < HEARTH_XMODEM_BLOCK) {
    memset(out + 3 + n, 0x1A, (size_t)HEARTH_XMODEM_BLOCK - n);   /* the last block's padding */
  }
  uint16_t crc = xmodemCrc16(out + 3, HEARTH_XMODEM_BLOCK);
  out[3 + HEARTH_XMODEM_BLOCK] = (uint8_t)(crc >> 8);   /* big-endian */
  out[3 + HEARTH_XMODEM_BLOCK + 1] = (uint8_t)crc;
}

/* ---- stream helpers: deadlines run on millis()/yield(), as on the host ----
 * M2: every deadline below is compared by subtraction ((int32_t)(millis()
 * - deadline) >= 0), so the 49.7-day millis() wrap does not make a fresh
 * wait time out at once. ---- */

/*
 * Read the next byte of the stream, advancing simulated time (or, on
 * target, letting the port's ISR fill its buffer) until one arrives or
 * timeoutMs pass (then -1). The stream may already hold bytes, which are
 * delivered at once; only a stream that stays empty burns the clock. The
 * return mirrors Stream::read(): -1 for "no byte in time", and the byte
 * value (0 to 255, INCLUDING 0x00) otherwise. The XMODEM stream carries
 * NUL bytes (the menu and the upload echo both end in one), so a
 * distinct sentinel is required: 0 would be ambiguous with a NUL byte.
 * This is the byte-at-a-time form of xmodem.py's read(1, timeout).
 */
static int xmodemReadByte(Stream &uart, uint32_t timeoutMs) {
  uint32_t deadline = millis() + timeoutMs;
  for (;;) {
    int c = uart.read();
    if (c >= 0) return c;
    if ((int32_t)(millis() - deadline) >= 0) return -1;
    yield();
  }
}

/*
 * Wait for one of the wanted reply bytes, the C++ form of xmodem.py's
 * _wait_for(wanted, timeout, tries): up to tries waits of timeoutMs each,
 * and each wait consumes the stream until a wanted byte arrives or that
 * wait's own time is up. The stream is consumed, not sampled one byte
 * per wait: a wanted byte that arrives together with the bytes around it
 * (an echoed 'C' before one, the menu text around a prompt) is found by
 * reading through those bytes, and a byte the protocol did not ask for
 * is skipped, as in the reference. Returns 0 when nothing wanted arrived
 * within the whole window: the missing-reply case that xmodem.py raises,
 * the one that is fatal here (only a NAK retransmits).
 */
static int xmodemWaitFor(Stream &uart, const uint8_t *wanted, size_t nWanted, uint32_t timeoutMs, int tries) {
  for (int i = 0; i < tries; i++) {
    for (;;) {
      int c = xmodemReadByte(uart, timeoutMs);
      if (c < 0) break;   /* this wait's time is up; try again (or give up) */
      for (size_t w = 0; w < nWanted; w++) {
        if ((uint8_t)c == wanted[w]) return c;
      }
      /* not wanted (a NUL, the menu text, ...): drop it and keep reading */
    }
  }
  return 0;   /* nothing wanted within the whole window: the fatal case */
}

/*
 * Read until a marker has landed on the stream (flash.py's read_until
 * with one marker), or false when the deadline passes first. The marker
 * is searched in the tail of what has been read, and that tail is
 * checked before every byte is taken, so a marker that was already
 * buffered when the wait started is found. The tail is a ring of the
 * last ML bytes. The deadline is absolute (the whole wait), because the
 * stream may hold the whole marker at once.
 */
static bool xmodemReadUntil(Stream &uart, const char *marker, uint32_t timeoutMs) {
  const size_t ML = strlen(marker);
  char tail[64];
  size_t tlen = 0;
  uint32_t deadline = millis() + timeoutMs;
  for (;;) {
    if (tlen == ML && memcmp(tail, marker, ML) == 0) return true;
    if (uart.available() > 0) {
      int c = uart.read();
      if (c >= 0) {
        if (tlen < ML) {
          tail[tlen++] = (char)c;
        } else {
          memmove(tail, tail + 1, ML - 1);
          tail[ML - 1] = (char)c;
        }
        continue;   /* a byte landed, so the deadline is not re-armed */
      }
    }
    if ((int32_t)(millis() - deadline) >= 0) return false;
    yield();
  }
}

HearthFlasherXmodem::HearthFlasherXmodem() {}

/*
 * B668: a failure AFTER the entry reset (the strap was held across it)
 * leaves the co-processor in its bootloader (the Gecko menu). Release
 * the strap and pulse the reset, so it restarts into its application
 * (or, if the application slot was partly written, into whatever its
 * bootloader then does; the apply's retry and rollback handle that). The
 * pin check runs before any entry reset, so it keeps its plain return.
 */
static int xmodemFail(const HearthCoprocPins &pins, int err) {
  hearthCoprocStrap(pins, false);
  hearthCoprocReset(pins, 50);
  return err;
}

int HearthFlasherXmodem::flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
                               const uint8_t sha256[32]) {
  /* A pin of -1 is refused before anything is written, exactly as the
   * other two families do at the top of their flash(). M3: the
   * availability is checked by the pin numbers, not by driving the
   * lines (the old check pulsed the reset for a real 1 ms on the
   * device just to test availability). */
  if (pins.reset < 0 || pins.strap < 0) {
    return HEARTH_FLASH_ERR_ENTER;
  }
  (void)sha256;   /* verified by the bundle; the bootloader takes the blocks as they are */

#ifdef ARDUINO
#ifdef HEARTH_SERIAL_PORT
  /* The AT link's port is the object the caller's begin() re-clocks
   * after this returns; the same pattern Hearth.cpp uses at bring-up.
   * M9: re-clock only the port the flasher was actually handed. When
   * the link runs on a sketch-owned Stream (Hearth.begin(Stream&)),
   * re-clocking HEARTH_SERIAL_PORT would change a port the library does
   * not own, so the rate is left alone (as on the host, where the
   * macro is not defined). */
  if (&uart == (Stream *)&HEARTH_SERIAL_PORT) {
    HEARTH_SERIAL_PORT.begin(HEARTH_XMODEM_BAUD);
  }
#endif
#endif

  /* Entry: strap asserted low for 100 ms, reset pulsed 100 ms, the menu
   * within 3 s, the strap released afterwards (it is sampled at boot
   * only). One attempt: the brief's sequence is a single entry, not
   * flash.py's three-attempt strap loop (which retries a strap the menu
   * did not answer). */
  hearthCoprocStrap(pins, true);
  delay(HEARTH_XMODEM_SETTLE_MS);
  hearthCoprocReset(pins, HEARTH_XMODEM_PULSE_MS);
  if (!xmodemReadUntil(uart, "BL >", HEARTH_XMODEM_MENU_TIMEOUT_MS)) {
    return xmodemFail(pins, HEARTH_FLASH_ERR_ENTER);   /* the strap is still held: release it and reset */
  }
  hearthCoprocStrap(pins, false);

  /* "1" to the menu: the bootloader echoes the 17-byte preamble, then
   * the 'C'. The preamble is drained first (flash.py's read_until), and
   * the 'C' is consumed before any block goes out, so the first block
   * lands on a receiver that has asked for the CRC variant. */
  uart.write('1');
  if (!xmodemReadUntil(uart, "\r\nbegin upload\r\n", HEARTH_XMODEM_HANDSHAKE_MS)) {
    return xmodemFail(pins, HEARTH_FLASH_ERR_ENTER);   /* no preamble: the menu did not take the upload */
  }
  {
    static const uint8_t C[] = {HEARTH_XMODEM_CRC_C, HEARTH_XMODEM_NAK};
    if (xmodemWaitFor(uart, C, 2, HEARTH_XMODEM_HANDSHAKE_MS, 1) == 0) {
      return xmodemFail(pins, HEARTH_FLASH_ERR_ENTER);
    }
  }

  /* The upload, xmodem.py's send() with block_size=128 and retries=10:
   * each block written, then the reply; ACK advances, NAK resends up to
   * 10 times, CAN aborts, and a missing reply within the 3 x 3-second
   * waits is fatal (only a NAK retransmits, by design). */
  int err = HEARTH_FLASH_OK;
  uint8_t seq = 1;
  uint8_t frame[3 + HEARTH_XMODEM_BLOCK + 2];
  for (uint32_t o = 0; o < len; o += HEARTH_XMODEM_BLOCK) {
    uint32_t n = len - o;
    if (n > HEARTH_XMODEM_BLOCK) n = HEARTH_XMODEM_BLOCK;
    uint8_t data[HEARTH_XMODEM_BLOCK];
    if (!src.read(off + o, data, n)) {
      return xmodemFail(pins, HEARTH_FLASH_ERR_SOURCE);
    }
    xmodemBuildBlock(frame, seq, data, n);
    int attempt;
    for (attempt = 0; attempt <= HEARTH_XMODEM_RETRIES; attempt++) {
      for (size_t i = 0; i < sizeof(frame); i++) uart.write(frame[i]);
      static const uint8_t REPLIES[] = {HEARTH_XMODEM_ACK, HEARTH_XMODEM_NAK, HEARTH_XMODEM_CAN};
      int r = xmodemWaitFor(uart, REPLIES, 3, HEARTH_XMODEM_REPLY_WAIT_MS, HEARTH_XMODEM_REPLY_TRIES);
      if (r == HEARTH_XMODEM_ACK) break;
      if (r == HEARTH_XMODEM_CAN) {
        return xmodemFail(pins, HEARTH_FLASH_ERR_PROTOCOL);   /* the receiver cancelled this block */
      }
      if (r == 0) {
        err = HEARTH_FLASH_ERR_PROTOCOL;    /* a missing reply is fatal: no retransmit */
        break;
      }
      /* NAK: retransmit, unless this was already the last retry */
    }
    if (err != HEARTH_FLASH_OK) break;
    seq = (uint8_t)((seq + 1) & 0xFF);
  }
  if (err != HEARTH_FLASH_OK) return xmodemFail(pins, err);

  /* EOT, expecting ACK (xmodem.py's _wait_for(ACK, tries=retries)). */
  uart.write(HEARTH_XMODEM_EOT);
  {
    static const uint8_t ACK[] = {HEARTH_XMODEM_ACK};
    if (xmodemWaitFor(uart, ACK, 1, HEARTH_XMODEM_REPLY_WAIT_MS, HEARTH_XMODEM_REPLY_TRIES) != HEARTH_XMODEM_ACK) {
      return xmodemFail(pins, HEARTH_FLASH_ERR_PROTOCOL);
    }
  }
  /* The menu's own report of a good transfer, within 5 s (flash.py). */
  if (!xmodemReadUntil(uart, "Serial upload complete", HEARTH_XMODEM_DONE_TIMEOUT_MS)) {
    return xmodemFail(pins, HEARTH_FLASH_ERR_PROTOCOL);
  }

  /* "2" from the menu runs the application; no reset pulse is needed
   * (flash.py sleeps 200 ms first, for the menu to be back). The caller
   * waits for +MTREADY on the AT link after this returns. */
  delay(HEARTH_XMODEM_RUN_SETTLE_MS);
  uart.write('2');
  return HEARTH_FLASH_OK;
}

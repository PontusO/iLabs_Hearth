/*
 * HearthFlasherSmpInternal: the SMP client's protocol primitives,
 * declared for the host test (the layered oracles in
 * fixtures/smp_layers.txt are checked against them one layer at a time).
 * The definitions live in HearthFlasherSmp.cpp; this header declares
 * them only and never duplicates them.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <Arduino.h>   /* Stream; the same host/target split HearthFlasher.h uses */

/* crc16-xmodem (poly 0x1021, init 0, no reflection, no xor-out). */
uint16_t smpCrc16(const uint8_t *data, size_t n);

/*
 * Encode one frame (8-byte header + CBOR payload) into the base64 line
 * runs and write them to the stream: u16 BE totlen || frame ||
 * crc16(frame) as u16 BE, base64 as a whole, split into runs of at most
 * 124 characters, the first run prefixed 0x06 0x09, the continuations
 * 0x04 0x14, each ending with a newline. smp.py's serial_encode() byte
 * for byte.
 */
void smpWriteFrameLines(Stream &uart, const uint8_t *frame, size_t frameLen);

/*
 * The line-based receive side of the framing (smp.py's SerialDecoder /
 * boot_serial_in_dec): feed raw bytes and a complete, CRC-valid packet
 * yields its decoded frame (header + payload). feed() must produce the
 * same frame whatever the chunking, one byte at a time, a few at a time
 * or the whole packet at once: on the device the UART hands the bytes
 * over in batches of 1 or 2 as they arrive (B673). The class is defined
 * here and used by both the client (HearthFlasherSmp.cpp) and the host
 * test, so there is a single definition, not a duplicate.
 */
#define HEARTH_SMP_FRAME_MTU 124    /* base64 chars per line */
#define HEARTH_SMP_PKT_START_1 0x06
#define HEARTH_SMP_PKT_START_2 0x09
#define HEARTH_SMP_PKT_CONT_1  0x04
#define HEARTH_SMP_PKT_CONT_2  0x14
#define HEARTH_SMP_PACKET_MAX  (1024 + 4)   /* a packet is totlen(2) + frame + crc(2); the frame is at most CONFIG_BOOT_SERIAL_MAX_RECEIVE_SIZE 1024, so a packet is at most 1028 */

class SmpSerialDecoder {
public:
  SmpSerialDecoder();

  size_t feed(const uint8_t *data, size_t n, uint8_t *out, size_t outCap);
  void reset();

private:
  bool feedLine(uint8_t *out, size_t outCap);
  /* One in-flight line: the 2-byte marker plus at most
   * HEARTH_SMP_FRAME_MTU base64 characters, so 2 + MTU (+1 slack) bytes.
   * The line is rebuilt from the pending bytes on every feed, so it must
   * be reset before the bytes are examined (the old two-pass code
   * re-appended the pending line to the one already in hand and
   * corrupted it, B673). */
  uint8_t line[2 + HEARTH_SMP_FRAME_MTU + 1];
  size_t lineLen;
  uint8_t acc[HEARTH_SMP_PACKET_MAX];
  size_t accLen;
  uint8_t pend[HEARTH_SMP_PACKET_MAX];
  size_t pendLen;
  size_t outLen;
};

/*
 * Decode one SMP response payload into the two fields the protocol
 * carries: "rc" (always) and "off" (only when the key is present and
 * non-negative). The payload is a map, definite or indefinite. Returns
 * false when the payload is not a map carrying an "rc" entry.
 */
bool smpDecodeResponse(const uint8_t *payload, size_t n, int32_t &rc, bool &hasOff, uint32_t &off);

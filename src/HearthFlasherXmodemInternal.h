/*
 * HearthFlasherXmodemInternal: the XMODEM client's protocol primitives,
 * declared for the host test (the layered oracles in
 * fixtures/xmodem_layers.txt are checked against them one layer at a
 * time). The definitions live in HearthFlasherXmodem.cpp; this header
 * declares them only and never duplicates them.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* The block size this bootloader takes: XMODEM_DATA_SIZE 128 in the SDK's
 * btl_xmodem.h (the firmware repo's README, "Uploading an application").
 * The parser never accepts the 1K STX block. */
#define HEARTH_XMODEM_BLOCK 128

/* The fixed rate of the bootloader's USART0; the bootloader never changes
 * rate during a download, so a download that ran faster must be set back
 * to this before the next one (flash.py's BAUD). */
#define HEARTH_XMODEM_BAUD 115200UL

/*
 * crc16-xmodem (poly 0x1021, init 0, no reflection, no xor-out), xmodem.py's
 * crc16_xmodem. The block's CRC field carries it big-endian, which the
 * standard's name "CRC16-XMODEM" already implies: the value is computed
 * over the data bytes only (never the header) and transmitted most
 * significant byte first.
 */
uint16_t xmodemCrc16(const uint8_t *data, size_t n);

/*
 * Build one 133-byte SOH block: SOH, the sequence byte (starting at 1,
 * wrapping at 256 to 0), its complement (255 - seq), the payload padded
 * with 0x1A to 128 bytes, and the block's CRC16-XMODEM big-endian.
 * xmodem.py's build_block(seq, payload, 128) byte for byte. The payload
 * may be shorter than 128 bytes (the last block); it must not be longer.
 */
void xmodemBuildBlock(uint8_t out[3 + HEARTH_XMODEM_BLOCK + 2], uint8_t seq, const uint8_t *payload, size_t n);

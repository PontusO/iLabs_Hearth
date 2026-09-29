/*
 * HearthFlasherXmodem: the MG24 co-processor's Gecko Bootloader v3.02.01
 * (uart-xmodem build). The wire protocol is XMODEM-CRC with 128-byte SOH
 * blocks only (the bootloader's btl_xmodem.h parses XMODEM_DATA_SIZE 128
 * and never 1K STX), ported from the firmware repo's
 * platform/silabs/fw/xmodem.py (the authoritative client) and flash.py's
 * entry, upload and exit sequence.
 */
#pragma once
#include "HearthFlasher.h"

class HearthFlasherXmodem : public HearthFlasher {
public:
  HearthFlasherXmodem();
  int flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
            const uint8_t sha256[32]) override;
};

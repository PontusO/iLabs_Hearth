/*
 * HearthFlasherEsp: the ESP32-C6 co-processor's ROM download loader, over
 * the vendored esp-serial-flasher (src/vendor/esp_loader, see its
 * VENDORED.md). The wire is the Espressif UART ROM protocol: SLIP-framed
 * commands at 115200, a fast 921600 rate the ROM takes on command, and
 * flash writes verified by the ROM's own MD5.
 *
 * The flasher only ever writes the application slot (HEARTH_ESP_APP_OFFSET,
 * the firmware repo's platform/esp32c6/fw/flash.py APP_OFFSET). The
 * bootloader, partition table and key-value (nvs) partitions are left
 * alone: the key-value partition is never touched by any flasher.
 */
#pragma once
#include "HearthFlasher.h"

class HearthFlasherEsp : public HearthFlasher {
public:
  HearthFlasherEsp();
  int flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
            const uint8_t sha256[32]) override;
};

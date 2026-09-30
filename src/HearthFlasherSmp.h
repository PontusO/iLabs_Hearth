/*
 * HearthFlasherSmp: MCUboot's serial recovery for the nRF co-processors
 * (nRF54L15, nRF54LM20A). The wire protocol is SMP/newtmgr over the same
 * UART the AT link uses, ported from the firmware repo's
 * platform/nrf54l15/fw/smp.py (the authoritative client, every constant
 * cited to boot_serial.c) and its flash.py entry/exit/upload sequence.
 */
#pragma once
#include "HearthFlasher.h"

class HearthFlasherSmp : public HearthFlasher {
public:
  HearthFlasherSmp();
  int flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
            const uint8_t sha256[32]) override;
};

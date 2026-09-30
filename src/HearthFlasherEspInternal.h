/*
 * HearthFlasherEspInternal: the two hooks the ESP loader's host test
 * drives that are not the public flash() sequence. The definitions live in
 * HearthFlasherEsp.cpp; this header declares them only and never
 * duplicates them.
 */
#pragma once
#include <Arduino.h>   /* Stream; the same host/target split HearthFlasher.h uses */
#include "HearthFlasher.h"   /* HearthCoprocPins */

/*
 * Bind the loader port to a UART stream and the co-processor's pins. The
 * port layer (the loader_port_* functions in HearthFlasherEsp.cpp) holds
 * the bound stream and pins as file-static state; the vendored library
 * reaches them only through those functions, so binding is the whole
 * connection setup. flash() calls this before it hands the port to
 * esp_loader_connect(); the host test calls it directly to drive the
 * port layer's primitives (write, read, the timer) one at a time.
 */
void hearthEspPortBind(Stream *uart, const HearthCoprocPins &pins);

/*
 * Whether the port can be re-clocked to the ROM's fast rate once the
 * connection is established. True only where the AT link's port is
 * known (ARDUINO and HEARTH_SERIAL_PORT defined): the loader then asks
 * the ROM to change to HEARTH_ESP_FAST_BAUD and re-clocks the port to
 * match. On the host (and on a target without that macro) it is false,
 * and the ROM is never asked to change rate: a port that cannot follow
 * the ROM would just stop receiving.
 */
bool hearthEspCanReclock();

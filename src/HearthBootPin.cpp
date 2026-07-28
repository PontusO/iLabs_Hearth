/*
 * HearthBootPin.cpp - make BOOT_PIN mean the BOOTSEL button.
 *
 * arduino-esp32 defines BOOT_PIN as the GPIO the boot strapping button sits
 * on, and the upstream Matter examples read it with pinMode()/digitalRead()
 * to offer a decommission-by-long-press. On RP2040 and RP2350 that button is
 * BOOTSEL, which is not a GPIO at all: it is sampled by momentarily driving
 * the QSPI chip select to Hi-Z and reading it back. arduino-pico exposes it
 * as a `BOOTSEL` object with an operator bool(), which an unmodified sketch
 * has no way to reach.
 *
 * So BOOT_PIN (HearthCompat.h) is a reserved pin number outside any variant's
 * GPIO range, and this file teaches pinMode() and digitalRead() about it.
 *
 * No macro tricks are involved: arduino-pico declares both functions as weak
 * aliases (`__attribute__((weak, alias("__pinMode")))`, wiring_digital.cpp),
 * which is exactly the hook this needs. A strong definition here wins at link
 * time, and every real pin is forwarded to the core's own implementation
 * unchanged, so nothing else in the sketch can tell the difference.
 *
 * Polarity matches the example's assumption of a button to ground with a
 * pull-up: pressed reads LOW.
 */

#include <Arduino.h>

#if defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_RP2350)

#include "HearthCompat.h"

/*
 * Reading BOOTSEL is not a cheap GPIO read. get_bootsel_button() in the
 * core's Bootsel.cpp disables interrupts, idles the other core, drives QSPI
 * CS to Hi-Z, busy-waits, and puts it all back. The upstream examples poll
 * the button twice per loop() with no delay, which on ESP32 is two register
 * reads and here would be thousands of interrupt-off critical sections a
 * second, stalling any setup1()/loop1() the sketch runs on core 1.
 *
 * So the reading is cached briefly. 5 ms is far below the 250 ms debounce
 * every one of those examples applies, so no press or release is missed, and
 * it turns an unbounded poll rate into at most 200 reads a second. Set
 * HEARTH_BOOTSEL_CACHE_MS to 0 to read through on every call.
 */
#ifndef HEARTH_BOOTSEL_CACHE_MS
#define HEARTH_BOOTSEL_CACHE_MS 5
#endif

static bool hearthReadBootsel() {
#if HEARTH_BOOTSEL_CACHE_MS > 0
  static uint32_t last_ms = 0;
  static bool last_state = false;
  static bool primed = false;
  uint32_t now = millis();
  if (primed && (now - last_ms) < HEARTH_BOOTSEL_CACHE_MS) {
    return last_state;
  }
  last_ms = now;
  last_state = (bool)BOOTSEL;
  primed = true;
  return last_state;
#else
  return (bool)BOOTSEL;
#endif
}

extern "C" void __pinMode(pin_size_t ulPin, PinMode ulMode);
extern "C" PinStatus __digitalRead(pin_size_t ulPin);

extern "C" void pinMode(pin_size_t ulPin, PinMode ulMode) {
  if (ulPin == HEARTH_BOOTSEL_PIN) {
    /* BOOTSEL has a fixed input configuration with its own pull-up. There is
     * nothing to configure and nothing to fail, so accept any mode silently
     * rather than reject INPUT_PULLUP, which is what every sketch asks for. */
    (void)ulMode;
    return;
  }
  __pinMode(ulPin, ulMode);
}

extern "C" PinStatus digitalRead(pin_size_t ulPin) {
  if (ulPin == HEARTH_BOOTSEL_PIN) {
    return hearthReadBootsel() ? LOW : HIGH;
  }
  return __digitalRead(ulPin);
}

#endif /* RP2040 / RP2350 */

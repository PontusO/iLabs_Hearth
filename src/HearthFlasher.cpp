/*
 * HearthFlasher.cpp: the flasher registry (forModel) and the two pin
 * helpers every flasher family shares.
 *
 * The helpers are the only place in the library that touches the
 * co-processor's reset and strap lines, so the host tests exercise the
 * flashers' full logic against a MockStream: here they report the pin's
 * availability and do nothing. Under ARDUINO they drive the actual pins
 * (mode, level, the pulse width).
 */
#include "HearthFlasher.h"
#include "HearthFlasherSmp.h"   /* Task 5a: the SMP family, the only one in this build so far */

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#endif

/*
 * Task 5a registers the SMP client for the two nRF model strings. The
 * XMODEM flasher (MGM240P, Task 5b) and the ESP loader (ESP32-C6, Task
 * 5c) add their branches below when they land: the model strings are the
 * AT+CGMM answers the update's per-port check already trusts, so no other
 * registry is needed.
 */
HearthFlasher *HearthFlasher::forModel(const char *model) {
  if (!model) {
    return nullptr;
  }
  if (strcmp(model, "nRF54L15 Hearth") == 0 || strcmp(model, "nRF54LM20A Hearth") == 0) {
    return new HearthFlasherSmp();
  }
  /* Task 5b: "MGM240P Hearth" -> the XMODEM client. Task 5c: "ESP32-C6 Hearth" -> the ESP loader. */
  return nullptr;
}

bool hearthCoprocStrap(const HearthCoprocPins &p, bool recovery) {
#ifdef ARDUINO
  if (p.strap < 0) {
    return false;
  }
  pinMode(p.strap, OUTPUT);
  /* Active low: recovery asserts the strap LOW (ROM/serial recovery on
   * reset). Release is the high (unasserted) level. */
  digitalWrite(p.strap, (p.strapActiveLow == recovery) ? LOW : HIGH);
  return true;
#else
  (void)recovery;   /* host no-op: only the pin's availability is reported */
  return p.strap >= 0;
#endif
}

bool hearthCoprocReset(const HearthCoprocPins &p, uint32_t pulseMs) {
#ifdef ARDUINO
  if (p.reset < 0) {
    return false;
  }
  pinMode(p.reset, OUTPUT);
  const int assertLevel = p.resetActiveLow ? LOW : HIGH;
  const int releaseLevel = p.resetActiveLow ? HIGH : LOW;
  digitalWrite(p.reset, assertLevel);
  delay(pulseMs);
  digitalWrite(p.reset, releaseLevel);
  return true;
#else
  (void)pulseMs;    /* host no-op: only the pin's availability is reported */
  return p.reset >= 0;
#endif
}

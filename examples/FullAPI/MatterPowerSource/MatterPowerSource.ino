/*
 * FullAPI reference: MatterPowerSource
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth-original class: arduino-esp32's Matter library
 * ships no Power Source class or example at all (see the class header),
 * so this is this port's own design against the firmware's C5 wire
 * contract, not a mirror of anything upstream.
 *
 *   MatterPowerSource()             global object below
 *   begin()                         setup(), no initial battery state to
 *                                   reconcile
 *   setBatChargeLevel(lvl)          menu 'c', cycles 0 Ok / 1 Warning /
 *                                   2 Critical
 *   setBatPercentRemaining(percent) menu '+'/'-', step 5.0 percent
 *   setBatReplacementNeeded(v)      menu 'n', toggles
 *   end()                       not called: tearing the endpoint down
 *                                mid-demo is not a usable demo; call it
 *                                when retiring the endpoint
 *
 * This device type is a flat sibling endpoint, not composed onto another
 * one (AT_MT_SPEC.md's own decision log, quoted in the class header),
 * enabling the Battery feature only. Unlike the Smoke/CO Alarm's eleven
 * Set* methods, none of this cluster's battery state has a dedicated
 * command: every setter here writes through the ordinary AT+MTATTR path,
 * the same shape MatterAirQualitySensor::setAirQuality() already
 * establishes for a host-authoritative reading pushed up to subscribers.
 *
 * **No getters exist on this class.** These are host-authoritative
 * readings pushed up to the fabric, not values a sketch reads back; the
 * task brief's own API for this class lists only begin() and the three
 * setters. This sketch prints what it just sent, since that is the only
 * copy of the value it has.
 *
 * BatPercentRemaining halves on the wire: the Matter spec's own type for
 * this attribute is a percentage in half-percent steps (0-200 for
 * 0-100%). setBatPercentRemaining()'s double percent argument is this
 * class's own convenience, doubled and rounded to the nearest wire
 * integer before the write; the percent argument is clamped to 0..100
 * first (an out-of-range double cast to uint8_t is undefined behaviour in
 * C++, not merely a wire-validation gap).
 *
 * Observe controller-side:
 *   chip-tool powersource read bat-charge-level <node> <ep>
 *   chip-tool powersource read bat-percent-remaining <node> <ep>
 *   chip-tool powersource read bat-replacement-needed <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Power.setBatChargeLevel(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterPowerSource Power;

double batPercent = 100.0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Power.begin();
  Matter.begin();
  Serial.println("FullAPI MatterPowerSource ready; '?' for menu");
}

void printHelp() {
  Serial.println("c=setBatChargeLevel(cycle 0/1/2)  +/-=setBatPercentRemaining step5.0");
  Serial.println("n=setBatReplacementNeeded(toggle)  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch for every other
   * endpoint and command in the sketch, even though this class publishes
   * only and never reads anything back. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'c':
        {
          static uint8_t level = 0;
          level = (level + 1) % 3;
          Serial.print("setBatChargeLevel(");
          Serial.print(level);
          Serial.print("): ");
          Serial.println(Power.setBatChargeLevel(level) ? "OK" : "failed");
        }
        break;
      case '+':
        batPercent += 5.0;
        if (batPercent > 100.0) batPercent = 100.0;
        Serial.print("setBatPercentRemaining(");
        Serial.print(batPercent);
        Serial.print("): ");
        Serial.println(Power.setBatPercentRemaining(batPercent) ? "OK" : "failed");
        break;
      case '-':
        batPercent -= 5.0;
        if (batPercent < 0.0) batPercent = 0.0;
        Serial.print("setBatPercentRemaining(");
        Serial.print(batPercent);
        Serial.print("): ");
        Serial.println(Power.setBatPercentRemaining(batPercent) ? "OK" : "failed");
        break;
      case 'n':
        {
          static bool needed = false;
          needed = !needed;
          Serial.print("setBatReplacementNeeded(");
          Serial.print(needed);
          Serial.print("): ");
          Serial.println(Power.setBatReplacementNeeded(needed) ? "OK" : "failed");
        }
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

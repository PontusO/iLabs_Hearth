/*
 * FullAPI reference: MatterGenericSwitch
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. The public section is upstream arduino-esp32's
 * own MatterGenericSwitch API, reproduced with one documented signature
 * change: click() returns bool here, not upstream's void, because this
 * port's click() is a real AT+MTSWITCH round trip that can come back
 * +MTERR:2 (unknown endpoint) or +MTERR:3 (no Switch cluster there), and
 * a caller needs a way to tell that apart from "the controller just
 * never reacted" (see the class header's deviation note).
 *
 * NO WRITABLE ATTRIBUTE: the switch_cluster's CurrentPosition and
 * NumberOfPositions attributes are both read-only; neither upstream's
 * click() nor this port's ever writes them, and this class exposes no
 * getter for either. The class drives no attribute at all: it is a pure
 * event source. click() raises an InitialPress event at position 1
 * (AT_MT_SPEC.md S3.15's default action 0, the only action this class
 * exposes). There is no setter and no onChange() to register here,
 * because there is no attribute change to notify: the header offers
 * none, so none appears below.
 *
 *   MatterGenericSwitch()          global object below
 *   begin()                        setup()
 *   click()                        menu 'c'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool switch subscribe-event initial-press 0 10 <node> <ep>
 *   chip-tool switch read current-position <node> <ep>   (always 0:
 *   this class never writes an attribute; shown for contrast with the
 *   event line above)
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Switch.click()) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterGenericSwitch Switch;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Switch.begin();
  Matter.begin();
  Serial.println("FullAPI MatterGenericSwitch ready; '?' for menu");
}

void printHelp() {
  Serial.println("c=click (raises InitialPress at position 1)  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch. This class has no
   * onChange to dispatch (see the NO WRITABLE ATTRIBUTE note above), but
   * poll() still drains the link for every other endpoint and command
   * in the sketch. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'c': Serial.println(Switch.click() ? "click: OK" : "click: failed"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

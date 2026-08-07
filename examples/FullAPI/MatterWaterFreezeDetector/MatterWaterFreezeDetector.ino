/*
 * FullAPI reference: MatterWaterFreezeDetector
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. onChange is a Hearth addition beyond upstream's
 * own public section (see the class header's DEVIATION FROM VERBATIM
 * note and the library README's "Supported device types"); every
 * boolean-state sensor in this library (also MatterContactSensor,
 * MatterRainSensor, MatterWaterLeakDetector) carries the same addition,
 * for the same reason.
 *
 *   MatterWaterFreezeDetector()    global object below
 *   begin(bool)                    setup()
 *   setFreeze(bool)                menu 't' (toggle)
 *   getFreeze()                    menu 's'
 *   onChange(cb)                   setup(), prints the new state
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool booleanstate read state-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Freeze.setFreeze(true)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterWaterFreezeDetector Freeze;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Freeze.onChange([](bool state) {
    Serial.print("onChange: ");
    Serial.println(state);
    return true;
  });

  Freeze.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterWaterFreezeDetector ready; '?' for menu");
}

void printHelp() {
  Serial.println("t=toggle s=getFreeze ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 't': Serial.println(Freeze.setFreeze(!Freeze.getFreeze()) ? "toggled" : "toggle failed"); break;
      case 's': Serial.print("freeze: "); Serial.println(Freeze.getFreeze());                        break;
      case '?': printHelp();                                                                          break;
    }
  }
  delay(10);
}

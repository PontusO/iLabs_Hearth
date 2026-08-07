/*
 * FullAPI reference: MatterCooktop
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 * OffOnly, by design: a cooktop is not a remote-controllable load in the
 * same sense as a plug or a light. Turning a burner on from a phone app is
 * the failure mode the device class exists to prevent, so remote ON is not
 * part of the device class at all. There is no on(), no toggle(), no
 * operator=(bool) or operator bool() write path: no public method on this
 * class can drive a true value onto the OnOff attribute. That is why the
 * menu below has no '1' key, unlike every other OnOff-shaped class in this
 * library.
 *
 * URC path note: a human at the physical cooktop (or the appliance's own
 * internal logic) can still turn it on, and that shows up over a +MTATTR
 * URC exactly like any other class's attributeChangeCB. getOnOff() can
 * therefore read true even though nothing in this sketch ever put it
 * there; watch the 's' key after a controller- or bench-side ON to see it.
 *
 *   MatterCooktop()                global object below
 *   begin()                        setup()
 *   off()                          menu '0'; the ONE remote action this
 *                                  class allows
 *   getOnOff()                     menu 's' (see URC path note above)
 *   onChange(cb)                   setup(), prints on any change
 *   onChangeOnOff(cb)              setup(), prints the new state
 *   updateAccessory()              menu 'u' (see its header comment)
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:  chip-tool onoff read on-off <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Cooktop.off()) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterCooktop Cooktop;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Cooktop.onChange([](bool state) {
    Serial.print("onChange: ");
    Serial.println(state);
    return true;
  });
  Cooktop.onChangeOnOff([](bool state) {
    Serial.print("onChangeOnOff: ");
    Serial.println(state);
    return true;
  });

  Cooktop.begin();
  Matter.begin();
  Serial.println("FullAPI MatterCooktop ready; '?' for menu");
}

void printHelp() {
  /* Deliberately no '1'/on key: see the OffOnly note above. */
  Serial.println("0=off s=getOnOff u=updateAccessory ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '0': Serial.println(Cooktop.off() ? "off: OK" : "off: failed"); break;
      case 's': Serial.print("onOff: "); Serial.println(Cooktop.getOnOff()); break;
      case 'u': Cooktop.updateAccessory(); Serial.println("updateAccessory called"); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

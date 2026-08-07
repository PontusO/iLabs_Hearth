/*
 * FullAPI reference: MatterMountedOnOffControl
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Public surface is copied unchanged from
 * MatterOnOffPlugin (see this class's own header comment); only the
 * device type differs (0x010F, mounted_on_off_control). There is no
 * arduino-esp32 counterpart: this is a Hearth-original class on a
 * house-shape clone, not a verbatim upstream port.
 *
 *   MatterMountedOnOffControl()   global object below
 *   begin(bool initialState)      setup()
 *   setOnOff(bool)                menu '1' / '0'
 *   getOnOff()                    menu 's'
 *   toggle()                      menu 't'
 *   onChange(cb)                  setup(), prints on any change
 *   onChangeOnOff(cb)             setup(), prints the new state
 *   updateAccessory()             menu 'u' (see its header comment)
 *   end()                         not called: tearing the endpoint
 *                                 down mid-demo is not a usable demo;
 *                                 call it when retiring the endpoint
 *
 * Observe controller-side:  chip-tool onoff read on-off <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Control.setOnOff(true)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterMountedOnOffControl Control;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Control.onChange([](bool state) {
    Serial.print("onChange: ");
    Serial.println(state);
    return true;
  });
  Control.onChangeOnOff([](bool state) {
    Serial.print("onChangeOnOff: ");
    Serial.println(state);
    return true;
  });

  Control.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterMountedOnOffControl ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff u=updateAccessory ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Control.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Control.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 't': Serial.println(Control.toggle() ? "toggled" : "toggle failed");      break;
      case 's': Serial.print("state: "); Serial.println(Control.getOnOff());         break;
      case 'u': Control.updateAccessory(); Serial.println("updateAccessory called"); break;
      case '?': printHelp();                                                         break;
    }
  }
  delay(10);
}

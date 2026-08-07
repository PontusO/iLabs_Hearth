/*
 * FullAPI reference: MatterDimmableLight
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 *   MAX_BRIGHTNESS                 setup(), printed once
 *   MatterDimmableLight()          global object below
 *   begin(bool, uint8_t)           setup()
 *   setOnOff(bool)                 menu '1' / '0'
 *   getOnOff()                     menu 's'
 *   toggle()                       menu 't'
 *   setBrightness(uint8_t)         menu 'b', cycles two presets
 *   getBrightness()                menu 'g'
 *   onChangeOnOff(cb)              setup(), prints the new state
 *   onChangeBrightness(cb)         setup(), prints the new level
 *   onChange(cb)                   setup(), prints on any change
 *   updateAccessory()              menu 'u' (see its header comment)
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool levelcontrol read current-level <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Light.setBrightness(200)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterDimmableLight Light;

const uint8_t brightnessPresets[2] = { 64, 220 };
uint8_t brightnessIdx = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_BRIGHTNESS: ");
  Serial.println(MatterDimmableLight::MAX_BRIGHTNESS);

  Light.onChangeOnOff([](bool state) {
    Serial.print("onChangeOnOff: ");
    Serial.println(state);
    return true;
  });
  Light.onChangeBrightness([](uint8_t level) {
    Serial.print("onChangeBrightness: ");
    Serial.println(level);
    return true;
  });
  Light.onChange([](bool state, uint8_t level) {
    Serial.print("onChange: ");
    Serial.print(state);
    Serial.print(" ");
    Serial.println(level);
    return true;
  });

  Light.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterDimmableLight ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff b=setBrightness(cycle) g=getBrightness u=updateAccessory ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Light.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Light.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 't': Serial.println(Light.toggle() ? "toggled" : "toggle failed");      break;
      case 's': Serial.print("state: "); Serial.println(Light.getOnOff());         break;
      case 'b':
        brightnessIdx = (brightnessIdx + 1) % 2;
        Serial.print("setBrightness(");
        Serial.print(brightnessPresets[brightnessIdx]);
        Serial.print("): ");
        Serial.println(Light.setBrightness(brightnessPresets[brightnessIdx]) ? "OK" : "failed");
        break;
      case 'g': Serial.print("brightness: "); Serial.println(Light.getBrightness());  break;
      case 'u': Light.updateAccessory(); Serial.println("updateAccessory called");    break;
      case '?': printHelp();                                                          break;
    }
  }
  delay(10);
}

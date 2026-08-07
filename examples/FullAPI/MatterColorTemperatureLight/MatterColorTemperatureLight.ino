/*
 * FullAPI reference: MatterColorTemperatureLight
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 *   MAX_BRIGHTNESS                     setup(), printed once
 *   MAX_COLOR_TEMPERATURE               setup(), printed once
 *   MIN_COLOR_TEMPERATURE               setup(), printed once
 *   MatterColorTemperatureLight()       global object below
 *   begin(bool, uint8_t, uint16_t)      setup()
 *   setOnOff(bool)                      menu '1' / '0'
 *   getOnOff()                          menu 's'
 *   toggle()                            menu 't'
 *   setBrightness(uint8_t)              menu 'b', cycles two presets
 *   getBrightness()                     menu 'g'
 *   setColorTemperature(uint16_t)       menu 'k', cycles two mireds presets
 *   getColorTemperature()               menu 'm'
 *   onChangeOnOff(cb)                   setup(), prints the new state
 *   onChangeBrightness(cb)              setup(), prints the new level
 *   onChangeColorTemperature(cb)        setup(), prints the new mireds
 *   onChange(cb)                        setup(), prints on any change
 *   updateAccessory()                   menu 'u' (see its header comment)
 *   end()                               not called: tearing the endpoint
 *                                       down mid-demo is not a usable demo;
 *                                       call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool levelcontrol read current-level <node> <ep>
 *   chip-tool colorcontrol read color-temperature-mireds <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Light.setColorTemperature(200)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterColorTemperatureLight Light;

const uint8_t brightnessPresets[2] = { 64, 220 };
uint8_t brightnessIdx = 0;

/* Both within [MIN_COLOR_TEMPERATURE, MAX_COLOR_TEMPERATURE] = [100, 500]. */
const uint16_t miredsPresets[2] = { 200, 400 };
uint8_t miredsIdx = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_BRIGHTNESS: ");
  Serial.println(MatterColorTemperatureLight::MAX_BRIGHTNESS);
  Serial.print("MAX_COLOR_TEMPERATURE: ");
  Serial.println(MatterColorTemperatureLight::MAX_COLOR_TEMPERATURE);
  Serial.print("MIN_COLOR_TEMPERATURE: ");
  Serial.println(MatterColorTemperatureLight::MIN_COLOR_TEMPERATURE);

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
  Light.onChangeColorTemperature([](uint16_t mireds) {
    Serial.print("onChangeColorTemperature: ");
    Serial.println(mireds);
    return true;
  });
  Light.onChange([](bool state, uint8_t level, uint16_t mireds) {
    Serial.print("onChange: ");
    Serial.print(state);
    Serial.print(" ");
    Serial.print(level);
    Serial.print(" ");
    Serial.println(mireds);
    return true;
  });

  Light.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterColorTemperatureLight ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff b=setBrightness(cycle) g=getBrightness");
  Serial.println("k=setColorTemperature(cycle) m=getColorTemperature u=updateAccessory ?=help");
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
      case 'k':
        miredsIdx = (miredsIdx + 1) % 2;
        Serial.print("setColorTemperature(");
        Serial.print(miredsPresets[miredsIdx]);
        Serial.print("): ");
        Serial.println(Light.setColorTemperature(miredsPresets[miredsIdx]) ? "OK" : "failed");
        break;
      case 'm': Serial.print("mireds: "); Serial.println(Light.getColorTemperature());  break;
      case 'u': Light.updateAccessory(); Serial.println("updateAccessory called");      break;
      case '?': printHelp();                                                            break;
    }
  }
  delay(10);
}

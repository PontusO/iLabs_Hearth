/*
 * FullAPI reference: MatterEnhancedColorLight
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 *   MAX_BRIGHTNESS                         setup(), printed once
 *   MAX_COLOR_TEMPERATURE                  setup(), printed once
 *   MIN_COLOR_TEMPERATURE                  setup(), printed once
 *   MatterEnhancedColorLight()             global object below
 *   begin(bool, espHsvColor_t, uint8_t,
 *         uint16_t)                        setup()
 *   setOnOff(bool)                         menu '1' / '0'
 *   getOnOff()                             menu 's'
 *   toggle()                               menu 't'
 *   setColorTemperature(uint16_t)          menu 'k', cycles two mireds presets
 *   getColorTemperature()                  menu 'm'
 *   setBrightness(uint8_t)                 menu 'b', cycles two presets
 *   getBrightness()                        menu 'g'
 *   setColorRGB(espRgbColor_t)             menu 'r', cycles two presets
 *   getColorRGB()                          menu 'x'
 *   setColorHSV(espHsvColor_t)             menu 'h', cycles two presets
 *   getColorHSV()                          menu 'v'
 *   onChangeOnOff(cb)                      setup(), prints the new state
 *   onChangeBrightness(cb)                 setup(), prints the new level
 *   onChangeColorHSV(cb)                   setup(), prints the new HSV triple
 *   onChangeColorTemperature(cb)           setup(), prints the new mireds
 *   onChange(cb)                           setup(), prints on any change
 *   updateAccessory()                      menu 'u' (see its header comment)
 *   end()                                  not called: tearing the endpoint
 *                                          down mid-demo is not a usable demo;
 *                                          call it when retiring the endpoint
 *
 * House truth (this class's header comment, point 3): brightnessLevel and
 * colorHSV.v are two SEPARATE cached fields that both nominally track
 * LevelControl's CurrentLevel. A local 'b' here converges both (the write's
 * synchronous echo drives attributeChangeCB too), but a controller-driven
 * brightness change leaves getBrightness() ('g') stale while getColorHSV()
 * ('v')'s v field is fresh. That divergence is reproduced upstream
 * behaviour, not a bug in this sketch.
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool levelcontrol read current-level <node> <ep>
 *   chip-tool colorcontrol read color-temperature-mireds <node> <ep>
 *   chip-tool colorcontrol read current-hue <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Light.setColorHSV({ 21, 216, 25 })) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterEnhancedColorLight Light;

const uint8_t brightnessPresets[2] = { 64, 220 };
uint8_t brightnessIdx = 0;

/* Both within [MIN_COLOR_TEMPERATURE, MAX_COLOR_TEMPERATURE] = [100, 500]. */
const uint16_t miredsPresets[2] = { 200, 400 };
uint8_t miredsIdx = 0;

const espHsvColor_t hsvPresets[2] = { { 21, 216, 25 }, { 169, 254, 254 } };
uint8_t hsvIdx = 0;

const espRgbColor_t rgbPresets[2] = { { 255, 0, 0 }, { 0, 0, 255 } };
uint8_t rgbIdx = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_BRIGHTNESS: ");
  Serial.println(MatterEnhancedColorLight::MAX_BRIGHTNESS);
  Serial.print("MAX_COLOR_TEMPERATURE: ");
  Serial.println(MatterEnhancedColorLight::MAX_COLOR_TEMPERATURE);
  Serial.print("MIN_COLOR_TEMPERATURE: ");
  Serial.println(MatterEnhancedColorLight::MIN_COLOR_TEMPERATURE);

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
  Light.onChangeColorHSV([](espHsvColor_t hsv) {
    Serial.printf("onChangeColorHSV: (%u,%u,%u)\r\n", hsv.h, hsv.s, hsv.v);
    return true;
  });
  Light.onChangeColorTemperature([](uint16_t mireds) {
    Serial.print("onChangeColorTemperature: ");
    Serial.println(mireds);
    return true;
  });
  Light.onChange([](bool state, espHsvColor_t hsv, uint8_t level, uint16_t mireds) {
    Serial.printf("onChange: %u (%u,%u,%u) %u %u\r\n", state, hsv.h, hsv.s, hsv.v, level, mireds);
    return true;
  });

  Light.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterEnhancedColorLight ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff b=setBrightness(cycle) g=getBrightness");
  Serial.println("k=setColorTemperature(cycle) m=getColorTemperature");
  Serial.println("h=setColorHSV(cycle) v=getColorHSV r=setColorRGB(cycle) x=getColorRGB");
  Serial.println("u=updateAccessory ?=help");
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
      case 'h':
        {
          hsvIdx = (hsvIdx + 1) % 2;
          espHsvColor_t hsv = hsvPresets[hsvIdx];
          Serial.printf("setColorHSV(%u,%u,%u): %s\r\n", hsv.h, hsv.s, hsv.v, Light.setColorHSV(hsv) ? "OK" : "failed");
        }
        break;
      case 'v':
        {
          espHsvColor_t hsv = Light.getColorHSV();
          Serial.printf("HSV: (%u,%u,%u)\r\n", hsv.h, hsv.s, hsv.v);
        }
        break;
      case 'r':
        {
          rgbIdx = (rgbIdx + 1) % 2;
          espRgbColor_t rgb = rgbPresets[rgbIdx];
          Serial.printf("setColorRGB(%u,%u,%u): %s\r\n", rgb.r, rgb.g, rgb.b, Light.setColorRGB(rgb) ? "OK" : "failed");
        }
        break;
      case 'x':
        {
          espRgbColor_t rgb = Light.getColorRGB();
          Serial.printf("RGB: (%u,%u,%u)\r\n", rgb.r, rgb.g, rgb.b);
        }
        break;
      case 'u': Light.updateAccessory(); Serial.println("updateAccessory called"); break;
      case '?': printHelp();                                                       break;
    }
  }
  delay(10);
}

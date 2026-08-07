/*
 * FullAPI reference: MatterColorLight
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. This class has no separate brightness accessor:
 * colorHSV.v IS the cached brightness (see the header comment), so 'v'
 * in the HSV triple below stands in for brightness.
 *
 *   MatterColorLight()                setup() (global object below)
 *   begin(bool, espHsvColor_t)        setup()
 *   setOnOff(bool)                    menu '1' / '0'
 *   getOnOff()                        menu 's'
 *   toggle()                          menu 't'
 *   setColorRGB(espRgbColor_t)        menu 'r', cycles two presets
 *   getColorRGB()                     menu 'x'
 *   setColorHSV(espHsvColor_t)        menu 'h', cycles two presets
 *   getColorHSV()                     menu 'v'
 *   onChangeOnOff(cb)                 setup(), prints the new state
 *   onChangeColorHSV(cb)              setup(), prints the new HSV triple
 *   onChange(cb)                      setup(), prints on any change
 *   updateAccessory()                 menu 'u' (see its header comment)
 *   end()                             not called: tearing the endpoint
 *                                     down mid-demo is not a usable demo;
 *                                     call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool colorcontrol read current-hue <node> <ep>
 *   chip-tool colorcontrol read current-saturation <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Light.setColorHSV({ 0, 254, 31 })) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterColorLight Light;

/* Red 12% intensity, then blue full intensity: the same two colors the
 * upstream-parity examples use, so the printed values are recognisable. */
const espHsvColor_t hsvPresets[2] = { { 0, 254, 31 }, { 169, 254, 254 } };
uint8_t hsvIdx = 0;

const espRgbColor_t rgbPresets[2] = { { 255, 0, 0 }, { 0, 0, 255 } };
uint8_t rgbIdx = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Light.onChangeOnOff([](bool state) {
    Serial.print("onChangeOnOff: ");
    Serial.println(state);
    return true;
  });
  Light.onChangeColorHSV([](espHsvColor_t hsv) {
    Serial.printf("onChangeColorHSV: (%u,%u,%u)\r\n", hsv.h, hsv.s, hsv.v);
    return true;
  });
  Light.onChange([](bool state, espHsvColor_t hsv) {
    Serial.printf("onChange: %u (%u,%u,%u)\r\n", state, hsv.h, hsv.s, hsv.v);
    return true;
  });

  Light.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterColorLight ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff h=setColorHSV(cycle) v=getColorHSV");
  Serial.println("r=setColorRGB(cycle) x=getColorRGB u=updateAccessory ?=help");
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

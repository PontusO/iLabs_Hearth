/*
 * FullAPI reference: MatterDimmablePlugin
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Same shape as MatterDimmableLight, but the level
 * accessor pair is named setLevel()/getLevel()/onChangeLevel() here (the
 * upstream-chosen name for this class; see its header comment) rather than
 * setBrightness()/getBrightness()/onChangeBrightness().
 *
 *   MAX_LEVEL                      setup(), printed once
 *   MatterDimmablePlugin()         global object below
 *   begin(bool, uint8_t)           setup()
 *   setOnOff(bool)                 menu '1' / '0'
 *   getOnOff()                     menu 's'
 *   toggle()                       menu 't'
 *   setLevel(uint8_t)              menu 'b', cycles two presets
 *   getLevel()                     menu 'g'
 *   onChangeOnOff(cb)              setup(), prints the new state
 *   onChangeLevel(cb)              setup(), prints the new level
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
 *   if (!Plugin.setLevel(200)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterDimmablePlugin Plugin;

const uint8_t levelPresets[2] = { 64, 220 };
uint8_t levelIdx = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_LEVEL: ");
  Serial.println(MatterDimmablePlugin::MAX_LEVEL);

  Plugin.onChangeOnOff([](bool state) {
    Serial.print("onChangeOnOff: ");
    Serial.println(state);
    return true;
  });
  Plugin.onChangeLevel([](uint8_t level) {
    Serial.print("onChangeLevel: ");
    Serial.println(level);
    return true;
  });
  Plugin.onChange([](bool state, uint8_t level) {
    Serial.print("onChange: ");
    Serial.print(state);
    Serial.print(" ");
    Serial.println(level);
    return true;
  });

  Plugin.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterDimmablePlugin ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff b=setLevel(cycle) g=getLevel u=updateAccessory ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Plugin.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Plugin.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 't': Serial.println(Plugin.toggle() ? "toggled" : "toggle failed");      break;
      case 's': Serial.print("state: "); Serial.println(Plugin.getOnOff());         break;
      case 'b':
        levelIdx = (levelIdx + 1) % 2;
        Serial.print("setLevel(");
        Serial.print(levelPresets[levelIdx]);
        Serial.print("): ");
        Serial.println(Plugin.setLevel(levelPresets[levelIdx]) ? "OK" : "failed");
        break;
      case 'g': Serial.print("level: "); Serial.println(Plugin.getLevel());        break;
      case 'u': Plugin.updateAccessory(); Serial.println("updateAccessory called"); break;
      case '?': printHelp();                                                        break;
    }
  }
  delay(10);
}

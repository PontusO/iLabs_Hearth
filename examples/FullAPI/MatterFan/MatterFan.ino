/*
 * FullAPI reference: MatterFan
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. On/off has no separate attribute here: it is a
 * computed view over FanMode (see the class header), so setOnOff()/
 * toggle() drive FanMode under the hood rather than a distinct OnOff
 * cluster.
 *
 *   MAX_SPEED / MIN_SPEED / OFF_SPEED   setup(), printed once
 *   FanMode_t         seven values, cycled by menu 'm'; printed via
 *                     getFanModeString()
 *   FanModeSequence_t begin() argument (FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO,
 *                     chosen so every FanMode_t value below is reachable)
 *   MatterFan()                    global object below
 *   begin(uint8_t, FanMode_t,
 *         FanModeSequence_t)       setup()
 *   getFanModeString(uint8_t)      menu 'm' / 'g', prints the mode name
 *   setOnOff(bool, bool)           menu '1' / '0' (performUpdate defaults
 *                                  true; menu 'S' shows the silent form)
 *   getOnOff()                     menu 's'
 *   toggle(bool)                   menu 't'
 *   setSpeedPercent(uint8_t, bool) menu '+' / '-', step 10; 'S' silent
 *   getSpeedPercent()              menu 'p'
 *   setMode(FanMode_t, bool)       menu 'm', cycles all seven values
 *   getMode()                      menu 'g'
 *   updateAccessory()              menu 'u' (see its header comment)
 *   onChangeMode(cb)               setup(), prints the new mode
 *   onChangeSpeedPercent(cb)       setup(), prints the new percent
 *   onChange(cb)                   setup(), prints on any change
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Silent write note: setOnOff(), toggle() and setSpeedPercent() all take a
 * trailing performUpdate flag (default true -> AT+MTATTR mode 1, reported;
 * false -> mode 0, silent). The 'S' key demonstrates the false form.
 *
 * Observe controller-side:
 *   chip-tool fancontrol read fan-mode <node> <ep>
 *   chip-tool fancontrol read percent-current <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Fan.setSpeedPercent(50)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterFan Fan;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_SPEED: ");
  Serial.print(MatterFan::MAX_SPEED);
  Serial.print("  MIN_SPEED: ");
  Serial.print(MatterFan::MIN_SPEED);
  Serial.print("  OFF_SPEED: ");
  Serial.println(MatterFan::OFF_SPEED);

  Fan.onChangeMode([](MatterFan::FanMode_t mode) {
    Serial.print("onChangeMode: ");
    Serial.println(MatterFan::getFanModeString(mode));
    return true;
  });
  Fan.onChangeSpeedPercent([](uint8_t percent) {
    Serial.print("onChangeSpeedPercent: ");
    Serial.println(percent);
    return true;
  });
  Fan.onChange([](MatterFan::FanMode_t mode, uint8_t percent) {
    Serial.print("onChange: ");
    Serial.print(MatterFan::getFanModeString(mode));
    Serial.print(" ");
    Serial.println(percent);
    return true;
  });

  Fan.begin(0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO);
  Matter.begin();
  Serial.println("FullAPI MatterFan ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff  +/-=speed step10  S=silent speed step10");
  Serial.println("m=setMode(cycle 7) g=getMode  p=getSpeedPercent  u=updateAccessory  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Fan.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Fan.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 't': Serial.println(Fan.toggle() ? "toggled" : "toggle failed");      break;
      case 's': Serial.print("onOff: "); Serial.println(Fan.getOnOff());         break;
      case '+':
        {
          uint16_t raw = (uint16_t)Fan.getSpeedPercent() + 10;
          uint8_t p = (raw > MatterFan::MAX_SPEED) ? MatterFan::MAX_SPEED : (uint8_t)raw;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Fan.setSpeedPercent(p) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          int16_t p = (int16_t)Fan.getSpeedPercent() - 10;
          if (p < MatterFan::OFF_SPEED) p = MatterFan::OFF_SPEED;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Fan.setSpeedPercent((uint8_t)p) ? "OK" : "failed");
        }
        break;
      case 'S':
        {
          uint16_t raw = (uint16_t)Fan.getSpeedPercent() + 10;
          uint8_t p = (raw > MatterFan::MAX_SPEED) ? MatterFan::MAX_SPEED : (uint8_t)raw;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print(", silent): ");
          Serial.println(Fan.setSpeedPercent(p, false) ? "OK" : "failed");
        }
        break;
      case 'm':
        {
          uint8_t next = (static_cast<uint8_t>(Fan.getMode()) + 1) % 7;
          MatterFan::FanMode_t mode = static_cast<MatterFan::FanMode_t>(next);
          Serial.print("setMode(");
          Serial.print(MatterFan::getFanModeString(mode));
          Serial.print("): ");
          Serial.println(Fan.setMode(mode) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("mode: "); Serial.println(MatterFan::getFanModeString(Fan.getMode())); break;
      case 'p': Serial.print("speedPercent: "); Serial.println(Fan.getSpeedPercent());              break;
      case 'u': Fan.updateAccessory(); Serial.println("updateAccessory called");                    break;
      case '?': printHelp();                                                                         break;
    }
  }
  delay(10);
}

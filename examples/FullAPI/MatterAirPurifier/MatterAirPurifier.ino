/*
 * FullAPI reference: MatterAirPurifier
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Public surface is copied unchanged from MatterFan
 * (see this class's own header comment); only the device type differs
 * (0x002D, air_purifier). On/off has no separate attribute: it is a
 * computed view over FanMode, so setOnOff()/toggle() drive FanMode under
 * the hood rather than a distinct OnOff cluster.
 *
 *   MAX_SPEED / MIN_SPEED / OFF_SPEED   setup(), printed once
 *   FanMode_t         seven values, cycled by menu 'm'; printed via
 *                     getFanModeString()
 *   FanModeSequence_t begin() argument (FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO,
 *                     chosen so every FanMode_t value below is reachable)
 *   MatterAirPurifier()            global object below
 *   begin(uint8_t, FanMode_t,
 *         FanModeSequence_t)       setup()
 *   getFanModeString(uint8_t)      menu 'm' / 'g', prints the mode name
 *   setOnOff(bool, bool)           menu '1' / '0' (performUpdate defaults
 *                                  true)
 *   getOnOff()                     menu 's'
 *   toggle(bool)                   menu 't' (performUpdate defaults true);
 *                                  menu 'X' shows the silent form (see the
 *                                  SILENT WRITE NOTE below)
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
 * SILENT WRITE NOTE: setOnOff(), toggle() and setSpeedPercent() all take a
 * trailing performUpdate flag (default true). Per MatterEndPoint.h's header
 * comment: true -> updateAttributeVal(), AT+MTATTR mode 1, "reported to
 * subscribers and bound devices"; false -> setAttributeVal(), mode 0, "no
 * report to the fabric". The false form still changes the cached attribute
 * on this device (confirm with 's'/'g' afterward) but a Matter controller
 * subscribed to FanMode/PercentCurrent receives no report of the change;
 * only an explicit read reveals it. 'X' demonstrates this on toggle();
 * 'S' demonstrates it on setSpeedPercent().
 *
 * Observe controller-side:
 *   chip-tool fancontrol read fan-mode <node> <ep>
 *   chip-tool fancontrol read percent-current <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Purifier.setSpeedPercent(50)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterAirPurifier Purifier;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("MAX_SPEED: ");
  Serial.print(MatterAirPurifier::MAX_SPEED);
  Serial.print("  MIN_SPEED: ");
  Serial.print(MatterAirPurifier::MIN_SPEED);
  Serial.print("  OFF_SPEED: ");
  Serial.println(MatterAirPurifier::OFF_SPEED);

  Purifier.onChangeMode([](MatterAirPurifier::FanMode_t mode) {
    Serial.print("onChangeMode: ");
    Serial.println(MatterAirPurifier::getFanModeString(mode));
    return true;
  });
  Purifier.onChangeSpeedPercent([](uint8_t percent) {
    Serial.print("onChangeSpeedPercent: ");
    Serial.println(percent);
    return true;
  });
  Purifier.onChange([](MatterAirPurifier::FanMode_t mode, uint8_t percent) {
    Serial.print("onChange: ");
    Serial.print(MatterAirPurifier::getFanModeString(mode));
    Serial.print(" ");
    Serial.println(percent);
    return true;
  });

  Purifier.begin(0, MatterAirPurifier::FAN_MODE_OFF, MatterAirPurifier::FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO);
  Matter.begin();
  Serial.println("FullAPI MatterAirPurifier ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off t=toggle s=getOnOff  X=silent toggle  +/-=speed step10  S=silent speed step10");
  Serial.println("m=setMode(cycle 7) g=getMode  p=getSpeedPercent  u=updateAccessory  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Purifier.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Purifier.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 't': Serial.println(Purifier.toggle() ? "toggled" : "toggle failed");      break;
      case 's': Serial.print("onOff: "); Serial.println(Purifier.getOnOff());         break;
      case 'X':
        /* performUpdate=false: still flips currentFanMode locally (verify
         * with 's'/'g'), but per MatterEndPoint.h's header comment this is
         * AT+MTATTR mode 0, "no report to the fabric", so a controller
         * subscribed to FanMode gets no report of this change. */
        Serial.println(Purifier.toggle(false) ? "toggled (silent)" : "toggle (silent) failed");
        break;
      case '+':
        {
          uint16_t raw = (uint16_t)Purifier.getSpeedPercent() + 10;
          uint8_t p = (raw > MatterAirPurifier::MAX_SPEED) ? MatterAirPurifier::MAX_SPEED : (uint8_t)raw;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Purifier.setSpeedPercent(p) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          int16_t p = (int16_t)Purifier.getSpeedPercent() - 10;
          if (p < MatterAirPurifier::OFF_SPEED) p = MatterAirPurifier::OFF_SPEED;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Purifier.setSpeedPercent((uint8_t)p) ? "OK" : "failed");
        }
        break;
      case 'S':
        {
          uint16_t raw = (uint16_t)Purifier.getSpeedPercent() + 10;
          uint8_t p = (raw > MatterAirPurifier::MAX_SPEED) ? MatterAirPurifier::MAX_SPEED : (uint8_t)raw;
          Serial.print("setSpeedPercent(");
          Serial.print(p);
          Serial.print(", silent): ");
          Serial.println(Purifier.setSpeedPercent(p, false) ? "OK" : "failed");
        }
        break;
      case 'm':
        {
          uint8_t next = (static_cast<uint8_t>(Purifier.getMode()) + 1) % 7;
          MatterAirPurifier::FanMode_t mode = static_cast<MatterAirPurifier::FanMode_t>(next);
          Serial.print("setMode(");
          Serial.print(MatterAirPurifier::getFanModeString(mode));
          Serial.print("): ");
          Serial.println(Purifier.setMode(mode) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("mode: "); Serial.println(MatterAirPurifier::getFanModeString(Purifier.getMode())); break;
      case 'p': Serial.print("speedPercent: "); Serial.println(Purifier.getSpeedPercent());                      break;
      case 'u': Purifier.updateAccessory(); Serial.println("updateAccessory called");                            break;
      case '?': printHelp();                                                                                      break;
    }
  }
  delay(10);
}

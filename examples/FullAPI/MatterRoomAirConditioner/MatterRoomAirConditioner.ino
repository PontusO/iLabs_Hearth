/*
 * FullAPI reference: MatterRoomAirConditioner
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 * DEAD-FRONT NOTE: setOnOff(false) is an ordinary OnOff cluster write
 * (esp_matter_bool(false) to cluster 0x0006 attribute 0x0000), exactly
 * like MatterOnOffPlugin's. Per the Room Air Conditioner device type's
 * own spec, turning the device off dead-fronts (disables) its thermostat
 * function on the DEVICE side, in the C6 firmware's data model. This
 * class neither models nor enforces that: it sends the one OnOff write
 * and nothing else. setLocalTemperature()/setCoolingSetpoint()/
 * setHeatingSetpoint()/setMode() remain ordinary Thermostat-cluster
 * writes whether the device is on or off; there is no dead-front logic
 * in this sketch or the class to go looking for.
 *
 * Callback surface: this class has exactly THREE onChange callbacks
 * (cooling setpoint, heating setpoint, mode); there is no plain onChange()
 * and no onChangeOnOff() here (unlike MatterThermostat, which has neither
 * an OnOff leg nor these gaps). All three are void-returning, a deliberate
 * divergence from MatterThermostat's bool-returning ones: this class's
 * attributeChangeCB is commit-then-notify (cache is written from the URC
 * first, callback informed after), so there is no veto point and nothing
 * for a return value to control. All three are registered below.
 *
 *   MatterRoomAirConditioner()     global object below
 *   begin(bool)                    setup()
 *   setOnOff(bool)                 menu '1' / '0' (see DEAD-FRONT NOTE)
 *   getOnOff()                     menu 's'
 *   setLocalTemperature(double)    menu 'T' / 't', step 0.5C (push-from-
 *                                  sketch; no getter, see class header)
 *   setCoolingSetpoint(double)     menu ']' / '[', step 0.5C
 *   getCoolingSetpoint()           menu 'c'
 *   setHeatingSetpoint(double)     menu '+' / '-', step 0.5C
 *   getHeatingSetpoint()           menu 'h'
 *   setMode(uint8_t)               menu 'm', cycles 0..3 (raw SystemMode;
 *                                  0 is SystemModeEnum::kOff; no
 *                                  validation, see class header)
 *   getMode()                      menu 'g'
 *   onChangeCoolingSetpoint(cb)    setup(), prints the new setpoint
 *   onChangeHeatingSetpoint(cb)    setup(), prints the new setpoint
 *   onChangeMode(cb)               setup(), prints the new mode
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool thermostat read occupied-cooling-setpoint <node> <ep>
 *   chip-tool thermostat read occupied-heating-setpoint <node> <ep>
 *   chip-tool thermostat read system-mode <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!AC.setCoolingSetpoint(24.0)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterRoomAirConditioner AC;

const double kStep = 0.5;
double localTemp = 20.0; /* no getter on this class; track what we last sent */

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  AC.onChangeCoolingSetpoint([](double setpoint) {
    Serial.print("onChangeCoolingSetpoint: ");
    Serial.println(setpoint);
  });
  AC.onChangeHeatingSetpoint([](double setpoint) {
    Serial.print("onChangeHeatingSetpoint: ");
    Serial.println(setpoint);
  });
  AC.onChangeMode([](uint8_t mode) {
    Serial.print("onChangeMode: ");
    Serial.println(mode);
  });

  AC.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterRoomAirConditioner ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off(dead-fronts thermostat, see banner) s=getOnOff");
  Serial.println("+/-=heatingSetpoint step0.5  ]/[=coolingSetpoint step0.5  T/t=localTemperature step0.5");
  Serial.println("h=getHeatingSetpoint c=getCoolingSetpoint m=setMode(cycle 0-3) g=getMode ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(AC.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(AC.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 's': Serial.print("onOff: "); Serial.println(AC.getOnOff());         break;
      case '+':
        {
          double t = AC.getHeatingSetpoint() + kStep;
          Serial.print("setHeatingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(AC.setHeatingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double t = AC.getHeatingSetpoint() - kStep;
          Serial.print("setHeatingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(AC.setHeatingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case ']':
        {
          double t = AC.getCoolingSetpoint() + kStep;
          Serial.print("setCoolingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(AC.setCoolingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case '[':
        {
          double t = AC.getCoolingSetpoint() - kStep;
          Serial.print("setCoolingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(AC.setCoolingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case 'T':
        localTemp += kStep;
        Serial.print("setLocalTemperature(");
        Serial.print(localTemp);
        Serial.print("): ");
        Serial.println(AC.setLocalTemperature(localTemp) ? "OK" : "failed");
        break;
      case 't':
        localTemp -= kStep;
        Serial.print("setLocalTemperature(");
        Serial.print(localTemp);
        Serial.print("): ");
        Serial.println(AC.setLocalTemperature(localTemp) ? "OK" : "failed");
        break;
      case 'h': Serial.print("heatingSetpoint: "); Serial.println(AC.getHeatingSetpoint()); break;
      case 'c': Serial.print("coolingSetpoint: "); Serial.println(AC.getCoolingSetpoint()); break;
      case 'm':
        {
          uint8_t mode = (AC.getMode() + 1) % 4;
          Serial.print("setMode(");
          Serial.print(mode);
          Serial.print("): ");
          Serial.println(AC.setMode(mode) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("mode: "); Serial.println(AC.getMode()); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

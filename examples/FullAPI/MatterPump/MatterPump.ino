/*
 * FullAPI reference: MatterPump
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it.
 *
 * Two different read-only shapes on this class, both worth telling apart:
 *  - MaxPressure/MaxSpeed/MaxFlow are telemetry the SKETCH publishes AS
 *    the device (a real controller cannot write them; they are Read-only
 *    in the cluster spec). No getter exists for any of the three: this
 *    sketch is the only place that ever knows the value it just sent.
 *  - EffectiveOperationMode/EffectiveControlMode are the opposite shape:
 *    fed exclusively by attributeChangeCB() when the C6's own cluster
 *    server computes and reports them. There is no setter for either.
 *    getEffectiveOperationMode()/getEffectiveControlMode() read back
 *    whatever the last URC delivered (0 until the first one arrives); a
 *    controller-side write to the real device, or the device's own
 *    internal logic, is what feeds these, not this sketch.
 *
 *   MatterPump()                   global object below
 *   begin(bool)                    setup()
 *   setOnOff(bool)                 menu '1' / '0'
 *   getOnOff()                     menu 's'
 *   setOperationMode(uint8_t)      menu 'm', cycles four OperationModeEnum
 *                                  presets (0 Normal, 1 Minimum, 2 Maximum,
 *                                  3 Local)
 *   getOperationMode()             menu 'g'
 *   setMaxPressure(int16_t)        menu 'p', telemetry the sketch publishes
 *   setMaxSpeed(uint16_t)          menu 'v', telemetry the sketch publishes
 *   setMaxFlow(uint16_t)           menu 'f', telemetry the sketch publishes
 *   getEffectiveOperationMode()    menu 'e' (URC-fed only: a controller-
 *                                  side write to the real device is what
 *                                  changes this, not this sketch; expect 0
 *                                  until one arrives)
 *   getEffectiveControlMode()      menu 'E' (URC-fed only, see above)
 *   onChangeOperationMode(cb)      setup(), prints the new mode
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool onoff read on-off <node> <ep>
 *   chip-tool pumpconfigurationandcontrol read operation-mode <node> <ep>
 *   chip-tool pumpconfigurationandcontrol read max-pressure <node> <ep>
 *   chip-tool pumpconfigurationandcontrol read effective-operation-mode <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Pump.setOperationMode(1)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterPump Pump;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Pump.onChangeOperationMode([](uint8_t mode) {
    Serial.print("onChangeOperationMode: ");
    Serial.println(mode);
  });

  Pump.begin(false);
  Matter.begin();
  Serial.println("FullAPI MatterPump ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=on 0=off s=getOnOff  m=setOperationMode(cycle 4) g=getOperationMode");
  Serial.println("p=setMaxPressure v=setMaxSpeed f=setMaxFlow (telemetry, no getter)");
  Serial.println("e=getEffectiveOperationMode E=getEffectiveControlMode (URC-fed) ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1': Serial.println(Pump.setOnOff(true)  ? "on: OK"  : "on: failed");  break;
      case '0': Serial.println(Pump.setOnOff(false) ? "off: OK" : "off: failed"); break;
      case 's': Serial.print("onOff: "); Serial.println(Pump.getOnOff());         break;
      case 'm':
        {
          uint8_t mode = (Pump.getOperationMode() + 1) % 4;
          Serial.print("setOperationMode(");
          Serial.print(mode);
          Serial.print("): ");
          Serial.println(Pump.setOperationMode(mode) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("operationMode: "); Serial.println(Pump.getOperationMode()); break;
      case 'p':
        {
          static int16_t pressure = 0;
          pressure += 100;
          Serial.print("setMaxPressure(");
          Serial.print(pressure);
          Serial.print("): ");
          Serial.println(Pump.setMaxPressure(pressure) ? "OK" : "failed");
        }
        break;
      case 'v':
        {
          static uint16_t speed = 0;
          speed += 100;
          Serial.print("setMaxSpeed(");
          Serial.print(speed);
          Serial.print("): ");
          Serial.println(Pump.setMaxSpeed(speed) ? "OK" : "failed");
        }
        break;
      case 'f':
        {
          static uint16_t flow = 0;
          flow += 10;
          Serial.print("setMaxFlow(");
          Serial.print(flow);
          Serial.print("): ");
          Serial.println(Pump.setMaxFlow(flow) ? "OK" : "failed");
        }
        break;
      case 'e':
        Serial.print("effectiveOperationMode (URC-fed, a controller change feeds this): ");
        Serial.println(Pump.getEffectiveOperationMode());
        break;
      case 'E':
        Serial.print("effectiveControlMode (URC-fed, a controller change feeds this): ");
        Serial.println(Pump.getEffectiveControlMode());
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

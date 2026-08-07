/*
 * FullAPI reference: MatterLightSensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. A Hearth-original class (no arduino-esp32
 * counterpart); its one attribute, IlluminanceMeasurement::MeasuredValue,
 * is a raw uint16 passthrough with no float conversion in the API (see
 * the class header for the Matter lux formula, documented but not
 * implemented as API).
 *
 * NO onChange: this attribute is kView-only per the C2 adjudication
 * (see the library README's "The ten-type swoop"), so a controller can
 * never write it and no genuine controller-driven URC for it can ever
 * arrive. An onChange callback here would have nothing honest to fire
 * on but the sketch's own writes, so this class does not add one
 * (contrast MatterHumiditySensor/MatterPressureSensor, which do,
 * because their onChange also legitimately echoes the sketch's own
 * writes as every onChange in this library does).
 *
 *   MatterLightSensor()            global object below
 *   begin(uint16_t)                setup()
 *   setRawMeasuredValue(uint16_t)  menu '+' / '-', 'a' fixed raw 1
 *   getRawMeasuredValue()          menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool illuminancemeasurement read measured-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Lux.setRawMeasuredValue(300)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterLightSensor Lux;

const uint16_t kStep = 100;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Lux.begin(1);
  Matter.begin();
  Serial.println("FullAPI MatterLightSensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/- =step raw 100  a=setRawMeasuredValue(1) [lux=1, header formula]");
  Serial.println("s=getRawMeasuredValue ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch. This class has no
   * onChange to dispatch, but poll() still drains the link for every
   * other endpoint and command in the sketch. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        {
          uint16_t v = Lux.getRawMeasuredValue() + kStep;
          Serial.print("setRawMeasuredValue(");
          Serial.print(v);
          Serial.print("): ");
          Serial.println(Lux.setRawMeasuredValue(v) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          uint16_t v = Lux.getRawMeasuredValue() >= kStep ? Lux.getRawMeasuredValue() - kStep : 0;
          Serial.print("setRawMeasuredValue(");
          Serial.print(v);
          Serial.print("): ");
          Serial.println(Lux.setRawMeasuredValue(v) ? "OK" : "failed");
        }
        break;
      case 'a':
        /* raw=1 maps to lux = 10^((1-1)/10000) = 1, the class header's
         * documented formula's simplest concrete point. */
        Serial.println(Lux.setRawMeasuredValue(1) ? "setRawMeasuredValue(1): OK" : "setRawMeasuredValue(1): failed");
        break;
      case 's': Serial.print("rawMeasuredValue: "); Serial.println(Lux.getRawMeasuredValue()); break;
      case '?': printHelp();                                                                    break;
    }
  }
  delay(10);
}

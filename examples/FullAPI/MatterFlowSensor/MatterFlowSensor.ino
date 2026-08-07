/*
 * FullAPI reference: MatterFlowSensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. A Hearth-original class (no arduino-esp32
 * counterpart); its one attribute, FlowMeasurement::MeasuredValue, is a
 * raw uint16 passthrough, the identical shape as MatterLightSensor over
 * a different cluster. No unit conversion is documented in the class
 * header, unlike MatterLightSensor's lux formula.
 *
 * NO onChange: this attribute is kView-only per the C2 adjudication
 * (see the library README's "The ten-type swoop"), so a controller can
 * never write it and no genuine controller-driven URC for it can ever
 * arrive. An onChange callback here would have nothing honest to fire
 * on but the sketch's own writes.
 *
 *   MatterFlowSensor()             global object below
 *   begin(uint16_t)                setup()
 *   setRawMeasuredValue(uint16_t)  menu '+' / '-', 'a' fixed raw 100
 *   getRawMeasuredValue()          menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool flowmeasurement read measured-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Flow.setRawMeasuredValue(100)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterFlowSensor Flow;

const uint16_t kStep = 50;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Flow.begin(0);
  Matter.begin();
  Serial.println("FullAPI MatterFlowSensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/- =step raw 50  a=setRawMeasuredValue(100) [fixed demo reading]");
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
          uint16_t v = Flow.getRawMeasuredValue() + kStep;
          Serial.print("setRawMeasuredValue(");
          Serial.print(v);
          Serial.print("): ");
          Serial.println(Flow.setRawMeasuredValue(v) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          uint16_t v = Flow.getRawMeasuredValue() >= kStep ? Flow.getRawMeasuredValue() - kStep : 0;
          Serial.print("setRawMeasuredValue(");
          Serial.print(v);
          Serial.print("): ");
          Serial.println(Flow.setRawMeasuredValue(v) ? "OK" : "failed");
        }
        break;
      case 'a': Serial.println(Flow.setRawMeasuredValue(100) ? "setRawMeasuredValue(100): OK" : "setRawMeasuredValue(100): failed"); break;
      case 's': Serial.print("rawMeasuredValue: "); Serial.println(Flow.getRawMeasuredValue());                                      break;
      case '?': printHelp();                                                                                                          break;
    }
  }
  delay(10);
}

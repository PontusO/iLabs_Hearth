/*
 * FullAPI reference: MatterTemperatureSensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Read-direction: the sketch pushes readings up to
 * the fabric, nothing arrives back down. Unlike its siblings
 * MatterHumiditySensor and MatterPressureSensor, this class exposes no
 * onChange: it was the first read-direction sensor added and predates
 * that pattern (see the two later headers' own DEVIATION FROM VERBATIM
 * notes), so there is nothing to register here.
 *
 *   MatterTemperatureSensor()      global object below
 *   begin(double)                  setup()
 *   setTemperature(double)         menu '+' / '-', 'n' fixed sub-zero
 *   getTemperature()               menu 's'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool temperaturemeasurement read measured-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Temp.setTemperature(21.5)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterTemperatureSensor Temp;

const double kStep = 0.5;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Temp.begin(21.00);
  Matter.begin();
  Serial.println("FullAPI MatterTemperatureSensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/- =step 0.5C  n=setTemperature(-10.00)  s=getTemperature  ?=help");
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
          double t = Temp.getTemperature() + kStep;
          Serial.print("setTemperature(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Temp.setTemperature(t) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double t = Temp.getTemperature() - kStep;
          Serial.print("setTemperature(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Temp.setTemperature(t) ? "OK" : "failed");
        }
        break;
      case 'n':
        /* Fixed sub-zero demo value: the header notes signedness matters
         * here, since a negative reading must survive the int16 round trip. */
        Serial.println(Temp.setTemperature(-10.00) ? "setTemperature(-10.00): OK" : "setTemperature(-10.00): failed");
        break;
      case 's': Serial.print("temperature: "); Serial.println(Temp.getTemperature()); break;
      case '?': printHelp();                                                          break;
    }
  }
  delay(10);
}

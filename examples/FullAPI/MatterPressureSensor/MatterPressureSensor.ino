/*
 * FullAPI reference: MatterPressureSensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Read-direction: the sketch pushes readings up to
 * the fabric, nothing arrives back down. onChange, setRawPressure() and
 * getRawPressure() are Hearth additions beyond upstream's own public
 * section (see the class header's DEVIATION FROM VERBATIM note and the
 * library README's "Supported device types").
 *
 *   MatterPressureSensor()         global object below
 *   begin(double)                  setup()
 *   setPressure(double)            menu '+' / '-', 'a' fixed 1013 hPa
 *   getPressure()                  menu 's'
 *   onChange(cb)                   setup(), prints the new pressure
 *   setRawPressure(int16_t)        menu 'r', fixed negative raw demo
 *   getRawPressure()               menu 'g'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool pressuremeasurement read measured-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Pressure.setPressure(1000.0)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterPressureSensor Pressure;

const double kStep = 1.0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Pressure.onChange([](double pressure) {
    Serial.print("onChange: ");
    Serial.println(pressure);
    return true;
  });

  Pressure.begin(1000.00);
  Matter.begin();
  Serial.println("FullAPI MatterPressureSensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/- =step 1 hPa  a=setPressure(1013) [std atm]  s=getPressure");
  Serial.println("r=setRawPressure(-5) g=getRawPressure ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        {
          double p = Pressure.getPressure() + kStep;
          Serial.print("setPressure(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Pressure.setPressure(p) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double p = Pressure.getPressure() - kStep;
          Serial.print("setPressure(");
          Serial.print(p);
          Serial.print("): ");
          Serial.println(Pressure.setPressure(p) ? "OK" : "failed");
        }
        break;
      case 'a': Serial.println(Pressure.setPressure(1013.0) ? "setPressure(1013): OK" : "setPressure(1013): failed"); break;
      case 's': Serial.print("pressure: "); Serial.println(Pressure.getPressure());                                   break;
      case 'r':
        /* setRawPressure() bypasses the double conversion; a negative
         * value exercises the int16 two's-complement round trip the
         * header calls out. */
        Serial.println(Pressure.setRawPressure(-5) ? "setRawPressure(-5): OK" : "setRawPressure(-5): failed");
        break;
      case 'g': Serial.print("rawPressure: "); Serial.println(Pressure.getRawPressure()); break;
      case '?': printHelp();                                                              break;
    }
  }
  delay(10);
}

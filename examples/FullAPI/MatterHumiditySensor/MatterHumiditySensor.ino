/*
 * FullAPI reference: MatterHumiditySensor
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. Read-direction: the sketch pushes readings up to
 * the fabric, nothing arrives back down. onChange, setRawHumidity() and
 * getRawHumidity() are Hearth additions beyond upstream's own public
 * section (see the class header's DEVIATION FROM VERBATIM note and the
 * library README's "Supported device types").
 *
 *   MatterHumiditySensor()         global object below
 *   begin(double)                  setup()
 *   setHumidity(double)            menu '+' / '-', 'a' fixed 50.00%
 *   getHumidity()                  menu 's'
 *   onChange(cb)                   setup(), prints the new humidity
 *   setRawHumidity(uint16_t)       menu 'r', fixed raw demo value
 *   getRawHumidity()               menu 'g'
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool relativehumiditymeasurement read measured-value <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Humidity.setHumidity(55.0)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterHumiditySensor Humidity;

const double kStep = 0.5;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Humidity.onChange([](double humidity) {
    Serial.print("onChange: ");
    Serial.println(humidity);
    return true;
  });

  Humidity.begin(40.00);
  Matter.begin();
  Serial.println("FullAPI MatterHumiditySensor ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/- =step 0.5%  a=setHumidity(50.00)  s=getHumidity");
  Serial.println("r=setRawHumidity(1234) g=getRawHumidity ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '+':
        {
          double h = Humidity.getHumidity() + kStep;
          Serial.print("setHumidity(");
          Serial.print(h);
          Serial.print("): ");
          Serial.println(Humidity.setHumidity(h) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double h = Humidity.getHumidity() - kStep;
          Serial.print("setHumidity(");
          Serial.print(h);
          Serial.print("): ");
          Serial.println(Humidity.setHumidity(h) ? "OK" : "failed");
        }
        break;
      case 'a': Serial.println(Humidity.setHumidity(50.00) ? "setHumidity(50.00): OK" : "setHumidity(50.00): failed"); break;
      case 's': Serial.print("humidity: "); Serial.println(Humidity.getHumidity());                                   break;
      case 'r':
        /* setRawHumidity() bypasses the double conversion, writing the
         * 1/100th-percent value straight to the wire. */
        Serial.println(Humidity.setRawHumidity(1234) ? "setRawHumidity(1234): OK" : "setRawHumidity(1234): failed");
        break;
      case 'g': Serial.print("rawHumidity: "); Serial.println(Humidity.getRawHumidity()); break;
      case '?': printHelp();                                                              break;
    }
  }
  delay(10);
}

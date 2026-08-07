/*
 * FullAPI reference: MatterTemperatureControlledCabinet
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this sketch
 * exercises it, across BOTH mutually exclusive feature modes.
 *
 * DEMO_MODE_TN below selects which of the two begin() overloads (and
 * which menu) is compiled in: 1 for TemperatureNumber (TN), 0 for
 * TemperatureLevel (TL). The two modes are mutually exclusive on the
 * device itself (class header: "Feature mode: true = TemperatureNumber,
 * false = TemperatureLevel"), so a single sketch cannot exercise both at
 * once; this file is compiled once in each mode (see the report) so every
 * banner line below is traced to real, built code in one build or the
 * other. Both begin() overloads are still listed and both #if branches
 * are present in the source either way.
 *
 *   MatterTemperatureControlledCabinet()  global object below
 *   begin(double, double, double, double)      TN mode: setup()
 *   begin(uint8_t*, uint16_t, uint8_t)         TL mode: setup()
 *   setTemperatureSetpoint(double)  TN menu '+' / '-', step 0.5C
 *   getTemperatureSetpoint()        TN menu 'a'
 *   setMinTemperature(double)       TN menu 'b', cycles -10.0/-5.0
 *   getMinTemperature()             TN menu 'c'
 *   setMaxTemperature(double)       TN menu 'd', cycles 32.0/25.0
 *   getMaxTemperature()             TN menu 'e'
 *   setStep(double)                 TN menu 'f', cycles 0.5/1.0
 *   getStep()                       TN menu 'g'
 *   setSelectedTemperatureLevel(uint8_t)  TL menu 'l', cycles the
 *                                         supported level identifiers
 *   getSelectedTemperatureLevel()   TL menu 'k'
 *   setSupportedTemperatureLevels(uint8_t*, uint16_t)  TL menu 'v',
 *                                  installs a new numeric identifier set
 *   getSupportedTemperatureLevelsCount()  TL menu 'c'
 *   setSupportedTemperatureLevelLabels(const char *const *, uint16_t)
 *                                  TL menu 'w', replaces the generated
 *                                  "Level <n>" defaults with real text,
 *                                  including one label containing a comma
 *                                  ("Warm, ish") to exercise the
 *                                  AT+MTTEMPLEVELS quoting path
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool temperaturecontrol read temperature-setpoint <node> <ep>
 *   chip-tool temperaturecontrol read selected-temperature-level <node> <ep>
 *   AT+MTTEMPLEVELS is the only wire path to level labels (they are not an
 *   ordinary AT+MTATTR-reachable attribute on the pinned esp-matter, see
 *   the class header); a controller sees them through the cluster's own
 *   SupportedTemperatureLevels iterator, not a plain attribute read.
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Cabinet.setTemperatureSetpoint(4.0)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

#define DEMO_MODE_TN 1

MatterTemperatureControlledCabinet Cabinet;

#if DEMO_MODE_TN
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Cabinet.begin(4.00, -10.0, 32.0, 0.50);
  Matter.begin();
  Serial.println("FullAPI MatterTemperatureControlledCabinet (TN mode) ready; '?' for menu");
}

void printHelp() {
  Serial.println("+/-=setTemperatureSetpoint step0.5  a=getTemperatureSetpoint");
  Serial.println("b=setMinTemperature(cycle) c=getMinTemperature  d=setMaxTemperature(cycle) e=getMaxTemperature");
  Serial.println("f=setStep(cycle) g=getStep  ?=help");
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
          double t = Cabinet.getTemperatureSetpoint() + Cabinet.getStep();
          Serial.print("setTemperatureSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Cabinet.setTemperatureSetpoint(t) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double t = Cabinet.getTemperatureSetpoint() - Cabinet.getStep();
          Serial.print("setTemperatureSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Cabinet.setTemperatureSetpoint(t) ? "OK" : "failed");
        }
        break;
      case 'a': Serial.print("temperatureSetpoint: "); Serial.println(Cabinet.getTemperatureSetpoint()); break;
      case 'b':
        {
          double m = (Cabinet.getMinTemperature() == -10.0) ? -5.0 : -10.0;
          Serial.print("setMinTemperature(");
          Serial.print(m);
          Serial.print("): ");
          Serial.println(Cabinet.setMinTemperature(m) ? "OK" : "failed");
        }
        break;
      case 'c': Serial.print("minTemperature: "); Serial.println(Cabinet.getMinTemperature()); break;
      case 'd':
        {
          double m = (Cabinet.getMaxTemperature() == 32.0) ? 25.0 : 32.0;
          Serial.print("setMaxTemperature(");
          Serial.print(m);
          Serial.print("): ");
          Serial.println(Cabinet.setMaxTemperature(m) ? "OK" : "failed");
        }
        break;
      case 'e': Serial.print("maxTemperature: "); Serial.println(Cabinet.getMaxTemperature()); break;
      case 'f':
        {
          double s = (Cabinet.getStep() == 0.50) ? 1.00 : 0.50;
          Serial.print("setStep(");
          Serial.print(s);
          Serial.print("): ");
          Serial.println(Cabinet.setStep(s) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("step: "); Serial.println(Cabinet.getStep()); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

#else /* !DEMO_MODE_TN: TemperatureLevel mode */

uint8_t supportedLevels[3] = { 0, 1, 2 };
const char *kDefaultLabels[3] = { "Cool", "Warm, ish", "Hot" };

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Cabinet.begin(supportedLevels, 3, 0);
  Matter.begin();
  /* Real display text, in place of the generated "Level <n>" defaults;
   * "Warm, ish" carries a comma to exercise AT+MTTEMPLEVELS' quoting. */
  Serial.println(Cabinet.setSupportedTemperatureLevelLabels(kDefaultLabels, 3) ? "labels: OK" : "labels: failed");
  Serial.println("FullAPI MatterTemperatureControlledCabinet (TL mode) ready; '?' for menu");
}

void printHelp() {
  Serial.println("l=setSelectedTemperatureLevel(cycle) k=getSelectedTemperatureLevel");
  Serial.println("v=setSupportedTemperatureLevels(new set) c=getSupportedTemperatureLevelsCount");
  Serial.println("w=setSupportedTemperatureLevelLabels(comma label demo)  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch. This class has no
   * onChange to dispatch, but poll() still drains the link for every
   * other endpoint and command in the sketch. */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'l':
        {
          uint8_t next = (Cabinet.getSelectedTemperatureLevel() + 1) % 3;
          Serial.print("setSelectedTemperatureLevel(");
          Serial.print(next);
          Serial.print("): ");
          Serial.println(Cabinet.setSelectedTemperatureLevel(next) ? "OK" : "failed");
        }
        break;
      case 'k': Serial.print("selectedTemperatureLevel: "); Serial.println(Cabinet.getSelectedTemperatureLevel()); break;
      case 'v':
        {
          uint8_t levels[4] = { 0, 1, 2, 3 };
          Serial.print("setSupportedTemperatureLevels(4 entries): ");
          Serial.println(Cabinet.setSupportedTemperatureLevels(levels, 4) ? "OK" : "failed");
        }
        break;
      case 'c': Serial.print("supportedTemperatureLevelsCount: "); Serial.println(Cabinet.getSupportedTemperatureLevelsCount()); break;
      case 'w':
        {
          static const char *kLabels[3] = { "Cool", "Warm, ish", "Hot" };
          Serial.print("setSupportedTemperatureLevelLabels(\"Cool\",\"Warm, ish\",\"Hot\"): ");
          Serial.println(Cabinet.setSupportedTemperatureLevelLabels(kLabels, 3) ? "OK" : "failed");
        }
        break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

#endif /* DEMO_MODE_TN */

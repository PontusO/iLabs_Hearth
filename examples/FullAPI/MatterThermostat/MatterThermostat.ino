/*
 * FullAPI reference: MatterThermostat
 *
 * Demonstrates the complete public API of this class. The banner below
 * is the coverage checklist: every public member, and where this
 * sketch exercises it. The firmware creates BOTH heating and cooling
 * features regardless of controlSequence, so both setpoints always exist
 * on the wire; controlSequence and autoMode only gate setMode() and
 * setCoolingHeatingSetpoints() client-side (see the class header).
 *
 *   ControlSequenceOfOperation_t   begin() argument (COOLING_HEATING,
 *                                  chosen so both setpoints are usable)
 *   ThermostatMode_t   nine values defined (note the gap: COOL is 3, not
 *                       2, a verbatim-preserved upstream quirk). Menu 'm'
 *                       cycles OFF/AUTO/COOL/HEAT only: thermostatModeString
 *                       (upstream's own table, reproduced verbatim) has just
 *                       5 entries indexed 0..4, so getThermostatModeString()
 *                       is only safe for those four plus the UNKNOWN=2
 *                       sentinel; EMERGENCY_HEAT..SLEEP (5-9) have no string
 *                       and are not driven through that helper here
 *   ThermostatAutoMode_t           begin() argument (ENABLED, so AUTO mode
 *                                  passes setMode()'s gate)
 *   MatterThermostat()             global object below
 *   begin(ControlSequenceOfOperation_t,
 *         ThermostatAutoMode_t)    setup()
 *   setMode(ThermostatMode_t)      menu 'm', cycles OFF/AUTO/COOL/HEAT
 *   getMode()                      menu 'g'
 *   getThermostatModeString(uint8_t) menu 'm' / 'g', prints the mode name
 *   getControlSequence()           menu 'q'
 *   getMinHeatSetpoint()           setup(), printed once
 *   getMaxHeatSetpoint()           setup(), printed once
 *   getMinCoolSetpoint()           setup(), printed once
 *   getMaxCoolSetpoint()           setup(), printed once
 *   getDeadBand()                  setup(), printed once
 *   setCoolingHeatingSetpoints(double, double)  menu 'b', sets both at once
 *   setHeatingSetpoint(double)     menu '+' / '-', step 0.5C
 *   getHeatingSetpoint()           menu 'h'
 *   setCoolingSetpoint(double)     menu ']' / '[', step 0.5C
 *   getCoolingSetpoint()           menu 'c'
 *   setLocalTemperature(double)    menu 'T' / 't', step 0.5C (push-from-
 *                                  sketch: the sketch is the temperature
 *                                  source, like MatterTemperatureSensor)
 *   getLocalTemperature()          menu 'l'
 *   onChangeMode(cb)               setup(), prints the new mode (returns
 *                                  true; bool-returning callbacks in this
 *                                  class notify-then-conditionally-commit)
 *   onChangeLocalTemperature(cb)   setup(), prints the new temperature
 *   onChangeCoolingSetpoint(cb)    setup(), prints the new setpoint
 *   onChangeHeatingSetpoint(cb)    setup(), prints the new setpoint
 *   onChange(cb)                   setup(), prints on any change (no
 *                                  argument; read back via the getters)
 *   end()                          not called: tearing the endpoint
 *                                  down mid-demo is not a usable demo;
 *                                  call it when retiring the endpoint
 *
 * Observe controller-side:
 *   chip-tool thermostat read system-mode <node> <ep>
 *   chip-tool thermostat read occupied-heating-setpoint <node> <ep>
 *   chip-tool thermostat read occupied-cooling-setpoint <node> <ep>
 *
 * Error handling pattern (applies to every setter in this library):
 *   if (!Thermo.setHeatingSetpoint(20.0)) {
 *     // Hearth.lastError() holds the +MTERR code (0 after a bare
 *     // ERROR); see the library README's error section.
 *   }
 */
#include <Matter.h>

MatterThermostat Thermo;

const double kStep = 0.5;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Serial.print("minHeat: ");
  Serial.print(Thermo.getMinHeatSetpoint());
  Serial.print("  maxHeat: ");
  Serial.print(Thermo.getMaxHeatSetpoint());
  Serial.print("  minCool: ");
  Serial.print(Thermo.getMinCoolSetpoint());
  Serial.print("  maxCool: ");
  Serial.print(Thermo.getMaxCoolSetpoint());
  Serial.print("  deadBand: ");
  Serial.println(Thermo.getDeadBand());

  Thermo.onChangeMode([](MatterThermostat::ThermostatMode_t mode) {
    Serial.print("onChangeMode: ");
    Serial.println(MatterThermostat::getThermostatModeString(mode));
    return true;
  });
  Thermo.onChangeLocalTemperature([](float temperature) {
    Serial.print("onChangeLocalTemperature: ");
    Serial.println(temperature);
    return true;
  });
  Thermo.onChangeCoolingSetpoint([](double setpoint) {
    Serial.print("onChangeCoolingSetpoint: ");
    Serial.println(setpoint);
    return true;
  });
  Thermo.onChangeHeatingSetpoint([](double setpoint) {
    Serial.print("onChangeHeatingSetpoint: ");
    Serial.println(setpoint);
    return true;
  });
  Thermo.onChange([]() {
    Serial.println("onChange (see getMode()/getLocalTemperature()/getCoolingSetpoint()/getHeatingSetpoint())");
    return true;
  });

  Thermo.begin(MatterThermostat::THERMOSTAT_SEQ_OP_COOLING_HEATING, MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);
  Matter.begin();
  Serial.println("FullAPI MatterThermostat ready; '?' for menu");
}

void printHelp() {
  Serial.println("m=setMode(cycle OFF/AUTO/COOL/HEAT) g=getMode q=getControlSequence");
  Serial.println("+/-=heatingSetpoint step0.5  ]/[=coolingSetpoint step0.5  b=setCoolingHeatingSetpoints(both)");
  Serial.println("h=getHeatingSetpoint c=getCoolingSetpoint");
  Serial.println("T/t=localTemperature step0.5  l=getLocalTemperature  ?=help");
}

void loop() {
  /* Every iteration, unconditionally: URC dispatch, and therefore every
   * onChange* callback in this sketch, only runs from inside poll(). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case 'm':
        {
          /* OFF/AUTO/COOL/HEAT only: see the banner note on
           * getThermostatModeString()'s 5-entry table. */
          static const uint8_t kModes[4] = { 0, 1, 3, 4 };
          static uint8_t idx = 0;
          idx = (idx + 1) % 4;
          MatterThermostat::ThermostatMode_t mode = static_cast<MatterThermostat::ThermostatMode_t>(kModes[idx]);
          Serial.print("setMode(");
          Serial.print(MatterThermostat::getThermostatModeString(mode));
          Serial.print("): ");
          Serial.println(Thermo.setMode(mode) ? "OK" : "failed");
        }
        break;
      case 'g': Serial.print("mode: "); Serial.println(MatterThermostat::getThermostatModeString(Thermo.getMode())); break;
      case 'q': Serial.print("controlSequence: "); Serial.println(Thermo.getControlSequence());                      break;
      case '+':
        {
          double t = Thermo.getHeatingSetpoint() + kStep;
          Serial.print("setHeatingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setHeatingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case '-':
        {
          double t = Thermo.getHeatingSetpoint() - kStep;
          Serial.print("setHeatingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setHeatingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case ']':
        {
          double t = Thermo.getCoolingSetpoint() + kStep;
          Serial.print("setCoolingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setCoolingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case '[':
        {
          double t = Thermo.getCoolingSetpoint() - kStep;
          Serial.print("setCoolingSetpoint(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setCoolingSetpoint(t) ? "OK" : "failed");
        }
        break;
      case 'b':
        {
          double h = Thermo.getHeatingSetpoint() + kStep;
          double c = Thermo.getCoolingSetpoint() + kStep;
          Serial.print("setCoolingHeatingSetpoints(");
          Serial.print(h);
          Serial.print(", ");
          Serial.print(c);
          Serial.print("): ");
          Serial.println(Thermo.setCoolingHeatingSetpoints(h, c) ? "OK" : "failed");
        }
        break;
      case 'h': Serial.print("heatingSetpoint: "); Serial.println(Thermo.getHeatingSetpoint()); break;
      case 'c': Serial.print("coolingSetpoint: "); Serial.println(Thermo.getCoolingSetpoint()); break;
      case 'T':
        {
          double t = Thermo.getLocalTemperature() + kStep;
          Serial.print("setLocalTemperature(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setLocalTemperature(t) ? "OK" : "failed");
        }
        break;
      case 't':
        {
          double t = Thermo.getLocalTemperature() - kStep;
          Serial.print("setLocalTemperature(");
          Serial.print(t);
          Serial.print("): ");
          Serial.println(Thermo.setLocalTemperature(t) ? "OK" : "failed");
        }
        break;
      case 'l': Serial.print("localTemperature: "); Serial.println(Thermo.getLocalTemperature()); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

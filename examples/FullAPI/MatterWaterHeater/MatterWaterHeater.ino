/*
 * FullAPI reference: MatterWaterHeater
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * water heater class at all (see the class header), so this is this port's
 * own design against the firmware's wire contract (energy round B,
 * AT_MT_SPEC.md S3.17/S3.20.1/S3.25).
 *
 *   Variant_t:  FULL=0 (WaterHeaterManagement with EnergyManagement and
 *               TankPercent, plus the composed Electrical Sensor graft)
 *               MINIMAL=1 (HeaterTypes/HeatDemand/BoostState only,
 *               disclosed sub-conformant; the tank trio and the whole
 *               measurement surface refuse host-side with error 1)
 *   ThermostatMode_t: OFF=0, AUTO=1, HEAT=4, EMERGENCY_HEAT=5 (the
 *               heating-only SystemModeEnum subset; AUTO is the device's
 *               own boot default)
 *   BoostInfo:  the unpacked Boost command (duration plus has-flagged
 *               optionals; the library unpacks the wire's presence mask)
 *
 *   MatterWaterHeater()            global object below
 *   begin(variant)                 setup(), FULL; declares only and seeds
 *                                  the thermostat cache from the device's
 *                                  own defaults (class header)
 *   setHeaterTypes(bitmap)         setup() after Matter.begin() (0x04 =
 *                                  heat pump element; re-pushed on every
 *                                  reconcile, it is configuration)
 *   setHeatDemand(bitmap)          menu 'd' (volatile, B229 semantics)
 *   setTankVolume(litres)          setup() (FULL only, configuration)
 *   setEstimatedHeatRequired(mwh)  menu 'e' (FULL only, int64 mWh)
 *   setTankPercentage(pct)         menu '%' (FULL only)
 *   onBoost(cb)                    setup(); the verdict answers the
 *                                  controller, an accept makes the library
 *                                  push BoostState Active itself (the
 *                                  firmware derives BoostStarted); this
 *                                  sketch arms a timer for the duration
 *   onCancelBoost(cb)              setup(); accept pushes Inactive
 *   endBoost()                     loop() when the boost timer expires,
 *                                  and menu 'x' (pushes Inactive, the
 *                                  firmware derives BoostEnded)
 *   setSupportedModes(...)         setup() after Matter.begin() (cluster
 *                                  158, tag 0 = kManual; re-sent on every
 *                                  reconcile)
 *   onChangeWaterHeaterMode(cb)    setup(); the ChangeToMode verdict
 *   getCurrentWaterHeaterMode()    menu 's' (cache, allow-updated only)
 *   setMode(mode)                  menu 'm' (cycles OFF/AUTO/HEAT)
 *   getMode()                      menu 's'
 *   setHeatingSetpoint(c)          menu 'h' (55 C; deliberately no 7..30
 *                                  clamp, see the class header)
 *   getHeatingSetpoint()           menu 's'
 *   setLocalTemperature(c)         menu 'l' and the simulation tick (the
 *                                  tank temperature; first push always
 *                                  writes, the fabric value starts null)
 *   getLocalTemperature()          menu 's'
 *   onChangeMode(cb)               setup() (controller SystemMode writes,
 *                                  +MTATTR-driven)
 *   onChangeLocalTemperature(cb)   setup()
 *   onChangeHeatingSetpoint(cb)    setup()
 *   onChange(cb)                   setup() (any of the three)
 *   setVoltage/setActiveCurrent/   menu 'v'/'c'/'p'/'f' (FULL only, the
 *   setActivePower/setFrequency    electrical sensor's exact surface)
 *   pushMeasurements(mv,ma,mw)     the simulation tick while heating
 *   addEnergyImported(mwh)         the simulation tick (element energy)
 *   addEnergyExported(mwh)         menu 'o'
 *   getVoltage/getActiveCurrent/   menu 's'
 *   getActivePower/getFrequency/
 *   getEnergyImported/getEnergyExported
 *   end()                          not called: tearing the endpoint down
 *                                  mid-demo is not a usable demo
 *
 * The simulation: a 2 kW element heats a 200 l tank. While HeatDemand is
 * non-zero the tick raises the tank temperature, pushes it as
 * LocalTemperature, pushes an electrical sample and accumulates imported
 * energy. A controller Boost (or menu 'd') turns the element on; the boost
 * timer calls endBoost() when the accepted duration expires.
 *
 * Observe controller-side:
 *   chip-tool waterheatermanagement read heater-types <node> <ep>
 *   chip-tool waterheatermanagement read heat-demand <node> <ep>
 *   chip-tool waterheatermanagement read boost-state <node> <ep>
 *   chip-tool waterheatermanagement read tank-volume <node> <ep>
 *   chip-tool waterheatermanagement read estimated-heat-required <node> <ep>
 *   chip-tool waterheatermanagement read tank-percentage <node> <ep>
 *   chip-tool waterheatermanagement read-event boost-started <node> <ep>
 *   chip-tool waterheatermanagement read-event boost-ended <node> <ep>
 *   chip-tool waterheatermanagement boost '{"duration": 60, "oneShot": true, "targetPercentage": 80}' <node> <ep>
 *   chip-tool waterheatermanagement cancel-boost <node> <ep>
 *   chip-tool waterheatermode read supported-modes <node> <ep>
 *   chip-tool waterheatermode change-to-mode 2 <node> <ep>
 *   chip-tool thermostat read local-temperature <node> <ep>
 *   chip-tool thermostat write occupied-heating-setpoint 6000 <node> <ep>
 *   chip-tool electricalpowermeasurement read active-power <node> <ep>
 *   chip-tool electricalenergymeasurement read-event cumulative-energy-measured <node> <ep>
 * read-event boost-started shows the accepted Boost's own parameters: the
 * firmware caches them from the adjudicated command and consumes them when
 * this library's Active push fires the event (S3.25).
 */
#include <Matter.h>

MatterWaterHeater WaterHeater;

// Simulation state: a 2 kW element on a 200 l tank.
double tankTempC = 20.0;
uint32_t boostEndsAtMs = 0;   // 0 = no boost timer armed
bool boostRunning = false;
uint32_t lastTickMs = 0;

void startElement(const char *why) {
  if (WaterHeater.setHeatDemand(0x04)) {  // heat pump / element demanding heat
    Serial.print("element ON (");
    Serial.print(why);
    Serial.println(")");
  }
}

void stopElement(const char *why) {
  if (WaterHeater.setHeatDemand(0)) {
    Serial.print("element OFF (");
    Serial.print(why);
    Serial.println(")");
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  WaterHeater.begin(MatterWaterHeater::FULL);  // declares only; caches seed from the device's own defaults

  // Boost: the verdict answers the controller; on accept the library
  // pushes BoostState Active itself and the firmware derives BoostStarted.
  // This sketch just arms its own timer and turns the element on.
  WaterHeater.onBoost([](const MatterWaterHeater::BoostInfo &info) {
    Serial.print("Boost: duration ");
    Serial.print(info.duration);
    if (info.hasOneShot) {
      Serial.print(info.oneShot ? " oneShot" : " oneShot=false");
    }
    if (info.hasEmergency) {
      Serial.print(info.emergency ? " emergency" : " emergency=false");
    }
    if (info.hasSetpoint) {
      Serial.print(" setpoint=");
      Serial.print(info.setpoint / 100.0);
    }
    if (info.hasTargetPct) {
      Serial.print(" targetPct=");
      Serial.print(info.targetPct);
    }
    if (info.hasReheat) {
      Serial.print(" reheat=");
      Serial.print(info.reheat);
    }
    Serial.println(" -> accepted");
    boostEndsAtMs = millis() + info.duration * 1000UL;
    boostRunning = true;
    return true;  // accept: the library pushes BoostState Active after the verdict
  });
  WaterHeater.onCancelBoost([]() {
    Serial.println("CancelBoost -> accepted");
    boostRunning = false;
    boostEndsAtMs = 0;
    return true;  // accept: the library pushes BoostState Inactive
  });

  WaterHeater.onChangeWaterHeaterMode([](uint8_t mode) {
    Serial.print("WaterHeaterMode ChangeToMode -> ");
    Serial.print(mode);
    Serial.println(" (accepted)");
    return true;
  });

  // Controller-driven Thermostat changes arrive as +MTATTR URCs.
  WaterHeater.onChangeMode([](MatterWaterHeater::ThermostatMode_t mode) {
    Serial.print("controller set SystemMode ");
    Serial.println((int)mode);
    return true;
  });
  WaterHeater.onChangeHeatingSetpoint([](double c) {
    Serial.print("controller set heating setpoint ");
    Serial.println(c);
    return true;
  });
  WaterHeater.onChangeLocalTemperature([](float c) {
    Serial.print("controller wrote LocalTemperature ");
    Serial.println(c);
    return true;
  });
  WaterHeater.onChange([]() {
    return true;
  });

  Matter.begin();

  // Endpoint-addressed configuration goes AFTER Matter.begin(): the
  // endpoint id is adopted there (the seven-type batch lesson).
  WaterHeater.setHeaterTypes(0x04);  // heat pump element fitted
  WaterHeater.setTankVolume(200);
  const uint8_t modes[3] = {1, 2, 3};
  const uint16_t tags[3] = {0, 0, 0};  // 0 = kManual, the cluster's tag-0 default
  const char *labels[3] = {"Eco", "Comfort", "Vacation"};
  WaterHeater.setSupportedModes(modes, tags, labels, 3);
  WaterHeater.setLocalTemperature(tankTempC);  // first push: the fabric value starts null
  WaterHeater.setFrequency(50000);

  lastTickMs = millis();
  Serial.println("FullAPI MatterWaterHeater ready; '?' for menu");
}

void printHelp() {
  Serial.println("d=toggle heat demand  x=endBoost now  %=tank 75%  e=estimated heat 1.2kWh");
  Serial.println("m=cycle setMode OFF/AUTO/HEAT  h=setHeatingSetpoint(55)  l=setLocalTemperature(45.5)");
  Serial.println("v/c/p/f=individual electrical setters  o=addEnergyExported(250)  s=status  ?=help");
}

void statusDump() {
  Serial.print("tank ");
  Serial.print(tankTempC);
  Serial.print(" C, boost ");
  Serial.print(boostRunning ? "RUNNING" : "idle");
  Serial.print(", WH mode ");
  Serial.print(WaterHeater.getCurrentWaterHeaterMode());
  Serial.print(", SystemMode ");
  Serial.print((int)WaterHeater.getMode());
  Serial.print(", setpoint ");
  Serial.print(WaterHeater.getHeatingSetpoint());
  Serial.print(" C, local ");
  Serial.print(WaterHeater.getLocalTemperature());
  Serial.println(" C");
  Serial.print("power(mW): ");
  Serial.print((long)WaterHeater.getActivePower());
  Serial.print("  voltage(mV): ");
  Serial.print((long)WaterHeater.getVoltage());
  Serial.print("  current(mA): ");
  Serial.print((long)WaterHeater.getActiveCurrent());
  Serial.print("  freq(mHz): ");
  Serial.println((long)WaterHeater.getFrequency());
  Serial.print("energy imported(mWh): ");
  Serial.print((unsigned long)WaterHeater.getEnergyImported());
  Serial.print("  exported(mWh): ");
  Serial.println((unsigned long)WaterHeater.getEnergyExported());
}

void simulationTick() {
  // one tick per second; heat only while the element demands heat
  if (millis() - lastTickMs < 1000) {
    return;
  }
  lastTickMs = millis();
  if (boostRunning) {
    tankTempC += 0.5;  // 2 kW into 200 l, generously rounded for the demo
    WaterHeater.setLocalTemperature(tankTempC);
    WaterHeater.pushMeasurements(230000, 8700, 2000000);  // 230 V, 8.7 A, 2 kW
    WaterHeater.addEnergyImported(556);                   // 2 kW for 1 s in mWh
    uint8_t pct = (uint8_t)((tankTempC - 20.0) * 2.0);
    if (pct > 100) {
      pct = 100;
    }
    WaterHeater.setTankPercentage(pct);
  }
}

void loop() {
  /* URC dispatch (verdict forwards, +MTATTR-driven callbacks and the
   * post-verdict BoostState push) all live here: poll unconditionally. */
  Hearth.poll();

  // the boost timer: end the boost from ordinary sketch context
  if (boostRunning && boostEndsAtMs != 0 && (int32_t)(millis() - boostEndsAtMs) >= 0) {
    boostRunning = false;
    boostEndsAtMs = 0;
    stopElement("boost timer");
    Serial.println(WaterHeater.endBoost() ? "endBoost: OK (BoostEnded derived)" : "endBoost: failed");
  }

  simulationTick();

  if (Serial.available()) {
    static uint8_t modeCycle = 0;
    switch (Serial.read()) {
      case 'd':
        if (boostRunning) {
          stopElement("menu");
          boostRunning = false;
        } else {
          startElement("menu");
          boostRunning = true;
        }
        break;
      case 'x': Serial.println(WaterHeater.endBoost() ? "endBoost: OK" : "endBoost: failed"); break;
      case '%': Serial.println(WaterHeater.setTankPercentage(75) ? "setTankPercentage(75): OK" : "setTankPercentage: failed"); break;
      case 'e':
        Serial.println(WaterHeater.setEstimatedHeatRequired(1200000) ? "setEstimatedHeatRequired(1200000 mWh): OK" : "setEstimatedHeatRequired: failed");
        break;
      case 'm': {
        static const MatterWaterHeater::ThermostatMode_t cycle[3] = {MatterWaterHeater::THERMOSTAT_MODE_OFF, MatterWaterHeater::THERMOSTAT_MODE_AUTO,
                                                                     MatterWaterHeater::THERMOSTAT_MODE_HEAT};
        modeCycle = (uint8_t)((modeCycle + 1) % 3);
        Serial.println(WaterHeater.setMode(cycle[modeCycle]) ? "setMode: OK" : "setMode: failed");
        break;
      }
      case 'h': Serial.println(WaterHeater.setHeatingSetpoint(55.0) ? "setHeatingSetpoint(55.0): OK" : "setHeatingSetpoint: failed"); break;
      case 'l': Serial.println(WaterHeater.setLocalTemperature(45.5) ? "setLocalTemperature(45.5): OK" : "setLocalTemperature: failed"); break;
      case 'v': Serial.println(WaterHeater.setVoltage(231000) ? "setVoltage(231000 mV): OK" : "setVoltage: failed"); break;
      case 'c': Serial.println(WaterHeater.setActiveCurrent(8700) ? "setActiveCurrent(8700 mA): OK" : "setActiveCurrent: failed"); break;
      case 'p': Serial.println(WaterHeater.setActivePower(2000000) ? "setActivePower(2000000 mW): OK" : "setActivePower: failed"); break;
      case 'f': Serial.println(WaterHeater.setFrequency(50000) ? "setFrequency(50000 mHz): OK" : "setFrequency: failed"); break;
      case 'o': Serial.println(WaterHeater.addEnergyExported(250) ? "addEnergyExported(250 mWh): OK" : "addEnergyExported: failed"); break;
      case 's': statusDump(); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

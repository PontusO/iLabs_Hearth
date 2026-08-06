/*
 * HearthSensorsAndAppliances.ino
 *
 * This is a Hearth-original example: there is no upstream arduino-esp32
 * sketch to copy. The ten-type swoop's own classes (Tasks C2-C4) have no
 * arduino-esp32 counterpart at all, so unlike every sketch under examples/
 * that mirrors a real upstream source, this one is not a byte-identical
 * copy of anything. See README.md, "The ten-type swoop" and
 * "Hearth originals".
 *
 * It composes a representative subset of the ten: MatterAirQualitySensor
 * (a Hearth-original sensor, C2), MatterPump and MatterRoomAirConditioner
 * (the two C4 classes with real surface design of their own: an OnOff leg
 * plus a second domain cluster each). CDC (`Serial`)-driven and
 * `Hearth.poll()`-in-`loop()`, the same interactive shape as
 * `examples/MatterDoorLockAdjudicated/`.
 *
 * -------------------------------------------------------------------------
 * Why loop() must never block
 * -------------------------------------------------------------------------
 * MatterPump::onChangeOperationMode() and MatterRoomAirConditioner's
 * onChangeCoolingSetpoint()/onChangeHeatingSetpoint()/onChangeMode() (the C5
 * scope addition; see README.md, "The ten-type swoop") all run synchronously
 * from inside whichever library call dispatched the underlying +MTATTR URC,
 * most often Hearth.poll() itself. A loop() that blocks for any real
 * fraction of a second delays every one of them from firing until the next
 * iteration, so nothing here ever calls delay() or busy-waits; the periodic
 * air-quality push below is timed with millis() instead.
 * -------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <Matter.h>

// The three endpoints. begin() only declares each one; the state itself
// reaches the C6 at the next Matter.begin() reconcile, not from begin()
// itself.
MatterAirQualitySensor AirQuality;
MatterPump WaterPump;
MatterRoomAirConditioner RoomAC;

// Cycled by the 'q' Serial command, and also advanced automatically every
// AIR_QUALITY_PERIOD_MS so the endpoint has something to report even with no
// operator present.
static const uint32_t AIR_QUALITY_PERIOD_MS = 30000;
uint8_t airQualityLevel = MatterAirQualitySensor::kGood;
uint32_t lastAirQualityPushMs = 0;

// Simulated local temperature reading for the room air conditioner, nudged
// by the 'w'/'c' Serial commands (warmer/cooler). A real sketch would read
// this from an actual sensor instead of a variable.
double simulatedRoomTemperature = 23.0;

// Runs synchronously from inside Hearth.poll() (or any other library call
// that dispatches a +MTATTR URC) whenever the controller writes
// PumpConfigurationAndControl's OperationMode. Logging only: this sketch has
// no physical pump speed to actually drive.
void onPumpOperationModeChanged(uint8_t mode) {
  Serial.print("Pump: controller set OperationMode to ");
  Serial.println(mode);
}

// Same calling context as onPumpOperationModeChanged(), for the room air
// conditioner's three controller-writable attributes.
void onRoomACCoolingSetpointChanged(double c) {
  Serial.print("RoomAC: controller set CoolingSetpoint to ");
  Serial.print(c);
  Serial.println("C");
}

void onRoomACHeatingSetpointChanged(double c) {
  Serial.print("RoomAC: controller set HeatingSetpoint to ");
  Serial.print(c);
  Serial.println("C");
}

void onRoomACModeChanged(uint8_t mode) {
  Serial.print("RoomAC: controller set SystemMode to ");
  Serial.println(mode);
}

void pushAirQuality() {
  AirQuality.setAirQuality((MatterAirQualitySensor::AirQuality_t)airQualityLevel);
  Serial.print("AirQuality: pushed level ");
  Serial.println(airQualityLevel);
}

void printHelp() {
  Serial.println("Commands: q = cycle air quality, p = toggle pump, r = toggle room AC,");
  Serial.println("          w = room AC warmer, c = room AC cooler, ? = this help.");
}

void setup() {
  Serial.begin(115200);
  Serial.println("HearthSensorsAndAppliances: air quality sensor + pump + room air conditioner.");
  printHelp();

  WaterPump.onChangeOperationMode(onPumpOperationModeChanged);
  RoomAC.onChangeCoolingSetpoint(onRoomACCoolingSetpointChanged);
  RoomAC.onChangeHeatingSetpoint(onRoomACHeatingSetpointChanged);
  RoomAC.onChangeMode(onRoomACModeChanged);

  AirQuality.begin(MatterAirQualitySensor::kGood);
  WaterPump.begin(false);      // starts off
  RoomAC.begin(false);         // starts off

  // Matter beginning - last step, after every endpoint's own begin().
  Matter.begin();
  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter Node is commissioned and connected to the network. Ready for use.");
  } else {
    Serial.println("Not commissioned yet. Manual pairing code:");
    Serial.println(Matter.getManualPairingCode());
  }

  // Publish the pump's sketch-owned telemetry once at startup; there is no
  // getter for any of the three (see MatterPump.h), the wire write is the
  // only observable effect.
  WaterPump.setMaxPressure(1500);
  WaterPump.setMaxSpeed(4000);
  WaterPump.setMaxFlow(2500);

  RoomAC.setLocalTemperature(simulatedRoomTemperature);
  pushAirQuality();
}

void loop() {
  // Every iteration, unconditionally: this is what lets the onChange*
  // callbacks above run at all. See the header comment.
  Hearth.poll();

  if (millis() - lastAirQualityPushMs > AIR_QUALITY_PERIOD_MS) {
    lastAirQualityPushMs = millis();
    airQualityLevel = (airQualityLevel + 1) % (MatterAirQualitySensor::kExtremelyPoor + 1);
    pushAirQuality();
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'q') {
      airQualityLevel = (airQualityLevel + 1) % (MatterAirQualitySensor::kExtremelyPoor + 1);
      lastAirQualityPushMs = millis();
      pushAirQuality();
    } else if (c == 'p') {
      bool newState = !WaterPump.getOnOff();
      WaterPump.setOnOff(newState);
      Serial.println(newState ? "Pump: on" : "Pump: off");
    } else if (c == 'r') {
      bool newState = !RoomAC.getOnOff();
      RoomAC.setOnOff(newState);
      Serial.println(newState ? "RoomAC: on" : "RoomAC: off");
    } else if (c == 'w') {
      simulatedRoomTemperature += 0.5;
      RoomAC.setLocalTemperature(simulatedRoomTemperature);
      Serial.print("RoomAC: local temperature now ");
      Serial.println(simulatedRoomTemperature);
    } else if (c == 'c') {
      simulatedRoomTemperature -= 0.5;
      RoomAC.setLocalTemperature(simulatedRoomTemperature);
      Serial.print("RoomAC: local temperature now ");
      Serial.println(simulatedRoomTemperature);
    } else if (c == '?') {
      printHelp();
    }
  }
}

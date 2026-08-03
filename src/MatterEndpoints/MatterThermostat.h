/*
 * MatterThermostat.h - the sixteenth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterThermostat (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterThermostat.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included (two exceptions, called out in deviation 3
 * below). Device type 0x0301 is thermostat, cluster 0x0201 is Thermostat:
 * LocalTemperature 0x0000 (int16), OccupiedCoolingSetpoint 0x0011 (int16),
 * OccupiedHeatingSetpoint 0x0012 (int16), SystemMode 0x001C (enum8, per
 * thermostat-cluster.xml's SystemModeEnum type declaration for that
 * attribute). IDs verified against connectedhomeip's zap-generated
 * ids/Attributes.h and ids/Clusters.h at the exact 3.3.8-bundled revision,
 * cluster-enums.h for ControlSequenceOfOperationEnum's and SystemModeEnum's
 * actual numeric values, and esp_matter_endpoint.h's
 * ESP_MATTER_THERMOSTAT_DEVICE_TYPE_ID; there is no such header on a host
 * build, so they are given as plain integers/enum values below and in the
 * .cpp.
 *
 * SystemModeEnum has a gap: kCool is 3, not 2 (kAuto is 1, then 2 is
 * reserved for kUnknownEnumValue, an internal CHIP sentinel never
 * transmitted on the wire). Upstream's own thermostatModeString table has
 * "UNKNOWN" sitting at index 2 for exactly this reason. Reproduced exactly,
 * gap included: THERMOSTAT_MODE_COOL below is 3.
 *
 * The firmware creates the Thermostat cluster with BOTH heating and cooling
 * features regardless of controlSequence, so OccupiedCoolingSetpoint and
 * OccupiedHeatingSetpoint both exist on the wire no matter which
 * ControlSequenceOfOperation_t begin() is given. controlSequence and
 * autoMode still gate setMode()/setCoolingHeatingSetpoints() client-side,
 * exactly as upstream's own logic does, independent of the live cluster
 * shape.
 *
 * setMode()'s controlSequence switch is reproduced exactly as upstream
 * wrote it, including a restriction that reads backwards at first glance:
 * under a COOLING (or COOLING_REHEAT) sequence, only HEAT and AUTO pass;
 * COOL itself is refused. Verified against
 * MatterThermostat.cpp's setMode() directly, not a transcription slip:
 * this is upstream's own shipped 3.3.8 behaviour. Parity is with upstream's
 * real behaviour, quirks included; test_thermostat.cpp pins it down
 * explicitly so a future reader does not mistake it for a Hearth bug.
 *
 * Three deviations from a literal transcription of upstream's .cpp/.h, all
 * following Task 6/7's established pattern and documented again in the
 * .cpp:
 *
 * 1. begin() issues no AT traffic (declares only) and validates the
 *    autoMode/controlSequence combination BEFORE calling hearthDeclare(),
 *    matching upstream's own order of validating before creating anything
 *    on the wire (upstream checks this before ever calling
 *    multi_mode_thermostat::create()). An invalid combination therefore
 *    consumes no registry slot / endpoint id, exactly as upstream consumes
 *    no C6 endpoint for the same rejected call.
 * 2. setMode() and setRawTemperature() skip upstream's read-before-write
 *    via getAttributeVal(): on this stack that is a real AT+MTATTR round
 *    trip, and every sibling class relies on the cache equality check
 *    alone (see MatterFan.cpp's header comment for the same reasoning).
 * 3. setLocalTemperature() is declared here and defined (not inlined) in
 *    the .cpp, unlike upstream's fully-inline version. Upstream's inline
 *    body reaches directly for the real
 *    Thermostat::Attributes::LocalTemperature::Id constant, available in
 *    its header because it includes the real connectedhomeip headers; this
 *    library's convention (every sibling class) keeps cluster/attribute ID
 *    constants private to each .cpp's anonymous namespace, so the call
 *    site needs to live there too. The public signature and behaviour
 *    (bool setLocalTemperature(double)) are unchanged; only where the body
 *    lives differs.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterThermostat : public MatterEndPoint {
public:
  // clang-format off
  enum ControlSequenceOfOperation_t {
    THERMOSTAT_SEQ_OP_COOLING                = 0,
    THERMOSTAT_SEQ_OP_COOLING_REHEAT         = 1,
    THERMOSTAT_SEQ_OP_HEATING                = 2,
    THERMOSTAT_SEQ_OP_HEATING_REHEAT         = 3,
    THERMOSTAT_SEQ_OP_COOLING_HEATING        = 4,
    THERMOSTAT_SEQ_OP_COOLING_HEATING_REHEAT = 5,
  };

  enum ThermostatMode_t {
    THERMOSTAT_MODE_OFF            = 0,
    THERMOSTAT_MODE_AUTO           = 1,
    THERMOSTAT_MODE_COOL           = 3,
    THERMOSTAT_MODE_HEAT           = 4,
    THERMOSTAT_MODE_EMERGENCY_HEAT = 5,
    THERMOSTAT_MODE_PRECOOLING     = 6,
    THERMOSTAT_MODE_FAN_ONLY       = 7,
    THERMOSTAT_MODE_DRY            = 8,
    THERMOSTAT_MODE_SLEEP          = 9
  };

  enum ThermostatAutoMode_t {
    THERMOSTAT_AUTO_MODE_DISABLED = 0,
    THERMOSTAT_AUTO_MODE_ENABLED  = 1,
  };
  // clang-format on

  MatterThermostat();
  ~MatterThermostat();
  // begin Matter Thermostat endpoint with initial Operation Mode
  bool begin(ControlSequenceOfOperation_t controlSequence = THERMOSTAT_SEQ_OP_COOLING, ThermostatAutoMode_t autoMode = THERMOSTAT_AUTO_MODE_DISABLED);
  // this will stop processing Thermostat Matter events
  void end();

  // set the Thermostat Mode
  bool setMode(ThermostatMode_t mode);
  // get the Thermostat Mode
  ThermostatMode_t getMode() {
    return currentMode;
  }
  // returns a friendly string for the Thermostat Mode
  static const char *getThermostatModeString(uint8_t mode) {
    return thermostatModeString[mode];
  }

  // get the Thermostat Control Sequence of Operation
  ControlSequenceOfOperation_t getControlSequence() {
    return controlSequence;
  }

  // get the minimum heating setpoint in Celsius degrees
  float getMinHeatSetpoint() {
    return (float)kDefaultMinHeatSetpointLimit / 100.00;
  }
  // get the maximum heating setpoint in Celsius degrees
  float getMaxHeatSetpoint() {
    return (float)kDefaultMaxHeatSetpointLimit / 100.00;
  }
  // get the minimum cooling setpoint in Celsius degrees
  float getMinCoolSetpoint() {
    return (float)kDefaultMinCoolSetpointLimit / 100.00;
  }
  // get the maximum cooling setpoint in Celsius degrees
  float getMaxCoolSetpoint() {
    return (float)kDefaultMaxCoolSetpointLimit / 100.00;
  }
  // get the deadband in Celsius degrees
  float getDeadBand() {
    return (float)kDefaultDeadBand / 10.00;
  }

  // generic function for setting the cooling and heating setpoints - checks if the setpoints are valid
  // it can be used to set both setpoints at the same time or only one of them, by setting the other to (double)0xffff
  // Heating Setpoint must be lower than Cooling Setpoint
  // When using AUTO mode the Cooling Setpoint must be higher than Heating Setpoint by at least the deadband
  bool setCoolingHeatingSetpoints(double setpointHeatingTemperature, double setpointCoolingTemperature);

  // set the heating setpoint in Celsius degrees
  bool setHeatingSetpoint(double setpointHeatingTemperature) {
    return setCoolingHeatingSetpoints(setpointHeatingTemperature, (double)0xffff);
  }
  // get the heating setpoint in Celsius degrees
  double getHeatingSetpoint() {
    return heatingSetpointTemperature / 100.0;
  }
  // set the cooling setpoint in Celsius degrees
  bool setCoolingSetpoint(double setpointCoolingTemperature) {
    return setCoolingHeatingSetpoints((double)0xffff, setpointCoolingTemperature);
  }
  // get the cooling setpoint in Celsius degrees
  double getCoolingSetpoint() {
    return coolingSetpointTemperature / 100.0;
  }

  // set the local Thermostat temperature in Celsius degrees (push-from-sketch: the sketch is the
  // temperature source, matching MatterTemperatureSensor's read-direction shape). See deviation 3.
  bool setLocalTemperature(double temperature);
  // returns the local Thermostat float temperature with 1/100th of a Celsius degree precision
  double getLocalTemperature() {
    return (double)localTemperature / 100.0;
  }

  // User Callback for whenever the Thermostat Mode is changed by the Matter Controller
  using EndPointModeCB = std::function<bool(ThermostatMode_t)>;
  void onChangeMode(EndPointModeCB onChangeCB) {
    _onChangeModeCB = onChangeCB;
  }

  // User Callback for whenever the Local Temperature is changed by the Matter Controller
  using EndPointTemperatureCB = std::function<bool(float)>;
  void onChangeLocalTemperature(EndPointTemperatureCB onChangeCB) {
    _onChangeTemperatureCB = onChangeCB;
  }

  // User Callback for whenever the Cooling Setpoint is changed by the Matter Controller
  using EndPointCoolingSetpointCB = std::function<bool(double)>;
  void onChangeCoolingSetpoint(EndPointCoolingSetpointCB onChangeCB) {
    _onChangeCoolingSetpointCB = onChangeCB;
  }

  // User Callback for whenever the Heating Setpoint is changed by the Matter Controller
  using EndPointHeatingSetpointCB = std::function<bool(double)>;
  void onChangeHeatingSetpoint(EndPointHeatingSetpointCB onChangeCB) {
    _onChangeHeatingSetpointCB = onChangeCB;
  }

  // User Callback for whenever any parameter is changed by the Matter Controller
  // Main parameters are Thermostat Mode, Local Temperature, Cooling Setpoint and Heating Setpoint
  // Those can be obtained using getMode(), getLocalTemperature(), getCoolingSetpoint() and getHeatingSetpoint()
  using EndPointCB = std::function<bool(void)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  // implementation keeps temperature in 1/100th of a Celsius degree
  int16_t coolingSetpointTemperature = 2400;  // 24C cooling setpoint
  int16_t localTemperature = 2000;            // 20C local temperature
  int16_t heatingSetpointTemperature = 1600;  // 16C heating setpoint

  ThermostatMode_t currentMode = THERMOSTAT_MODE_OFF;
  ControlSequenceOfOperation_t controlSequence = THERMOSTAT_SEQ_OP_COOLING;
  ThermostatAutoMode_t autoMode = THERMOSTAT_AUTO_MODE_DISABLED;

  EndPointModeCB _onChangeModeCB = NULL;
  EndPointTemperatureCB _onChangeTemperatureCB = NULL;
  EndPointCoolingSetpointCB _onChangeCoolingSetpointCB = NULL;
  EndPointHeatingSetpointCB _onChangeHeatingSetpointCB = NULL;
  EndPointCB _onChangeCB = NULL;

  // internal function to set the raw temperature value (Matter Cluster)
  bool setRawTemperature(int16_t rawTemperature, uint32_t attribute_id, int16_t *internalValue);

  // clang-format off
  // Default Thermostat values - can't be changed - defined in the Thermostat Cluster Server code
  static const int16_t kDefaultAbsMinHeatSetpointLimit = 700;  // 7C (44.5 F)
  static const int16_t kDefaultMinHeatSetpointLimit    = 700;  // 7C (44.5 F)
  static const int16_t kDefaultAbsMaxHeatSetpointLimit = 3000; // 30C (86 F)
  static const int16_t kDefaultMaxHeatSetpointLimit    = 3000; // 30C (86 F)

  static const int16_t kDefaultAbsMinCoolSetpointLimit = 1600; // 16C (61 F)
  static const int16_t kDefaultMinCoolSetpointLimit    = 1600; // 16C (61 F)
  static const int16_t kDefaultAbsMaxCoolSetpointLimit = 3200; // 32C (90 F)
  static const int16_t kDefaultMaxCoolSetpointLimit    = 3200; // 32C (90 F)

  static const int8_t  kDefaultDeadBand                = 25; // 2.5C when in AUTO mode
  // clang-format on

  // string helper for the THERMOSTAT MODE
  static const char *thermostatModeString[5];
};

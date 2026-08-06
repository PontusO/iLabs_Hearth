/*
 * MatterRoomAirConditioner.h - Hearth-original class, C4 of the ten-type
 * swoop.
 *
 * There is no arduino-esp32 counterpart for this device type: the class is
 * modeled on this library's OWN MatterThermostat (the setpoint/mode surface
 * and its double-to-hundredths i16 conversion idiom) plus an OnOff leg in
 * MatterOnOffPlugin's shape, per the task brief, not on any upstream source.
 *
 * Device type 0x0072 is room_air_conditioner
 * (esp_matter_endpoint.h:79, ESP_MATTER_ROOM_AIR_CONDITIONER_DEVICE_TYPE_ID).
 * Two clusters:
 *
 *   - 0x0006 OnOff, attribute 0x0000 (OnOff::Attributes::OnOff::Id, boolean),
 *     the same IDs every other OnOff-cluster class in this library reads.
 *   - 0x0201 Thermostat (513 decimal), the same cluster and attribute IDs as
 *     MatterThermostat.cpp, reverified here directly against the pinned
 *     esp-matter v1.5.1 connectedhomeip headers (there is no such header on
 *     a host build, so both files give them as plain integers):
 *       * zzz_generated/app-common/clusters/Thermostat/ClusterId.h:14,
 *         `inline constexpr ClusterId Id = 0x00000201;` (513).
 *       * .../Thermostat/AttributeIds.h:20 LocalTemperature `Id = 0x00000000`
 *         (0); Attributes.h:46-56 TypeInfo `Nullable<int16_t>`. This class
 *         does not expose null, matching every other sensor-shaped class in
 *         this library (MatterFlowSensor et al.): the sentinel passes
 *         through as a plain int16 like any other value, no special casing.
 *       * .../Thermostat/AttributeIds.h:64 OccupiedCoolingSetpoint
 *         `Id = 0x00000011` (17); Attributes.h:178-189 TypeInfo `int16_t`,
 *         not nullable.
 *       * .../Thermostat/AttributeIds.h:68 OccupiedHeatingSetpoint
 *         `Id = 0x00000012` (18); Attributes.h:190-201 TypeInfo `int16_t`,
 *         not nullable.
 *       * .../Thermostat/AttributeIds.h:108 SystemMode `Id = 0x0000001C`
 *         (28); Attributes.h:310-321 TypeInfo `SystemModeEnum`, an
 *         `enum class : uint8_t` (Enums.h:177), i.e. enum8 on the wire.
 *
 * The firmware side of this device type (per the ten-type swoop's own
 * design) pre-satisfies the Thermostat cluster's feature-flag trap:
 * esp-matter's endpoint.cpp ORs in the cooling and dead_front features
 * before create(), so the boot-time thunk seeds setpoints only and adds no
 * feature flags of its own. That is a firmware-side concern (a different
 * repository's task); nothing here depends on it beyond the fact that both
 * setpoints and SystemMode exist on the wire regardless of which mode is
 * live, matching the OnOff-independent Thermostat cluster shape every
 * MatterThermostat instance already has.
 *
 * setOnOff(false) is an ordinary OnOff write, `esp_matter_bool(false)` to
 * cluster 0x0006 attribute 0x0000, exactly like MatterOnOffPlugin's. THE
 * DEAD-FRONT BEHAVIOUR IS DOCUMENTATION, NOT CODE: per the Room Air
 * Conditioner device type's own spec, a controller or sketch turning the
 * device off dead-fronts (disables) the thermostat function, but that
 * happens on the DEVICE side, in the C6 firmware's data model. This class
 * neither models nor enforces it: it sends the one OnOff write and nothing
 * else, and setLocalTemperature()/setCoolingSetpoint()/setHeatingSetpoint()/
 * setMode() remain ordinary Thermostat-cluster writes whether the device is
 * on or off, mirroring how MatterThermostat itself has no OnOff concept at
 * all. test_roomac.cpp pins the exact OnOff-off wire string down explicitly
 * so a future reader does not go looking for dead-front logic that was never
 * meant to live here (see C5's deferred header comment / README sentence).
 *
 * begin() calls hearthDeclare(this, 0x0072) and resets every cached field to
 * the same baseline MatterThermostat uses (24.0C cooling, 16.0C heating,
 * 20.0C local, mode OFF/0): there is no independent design reason for a
 * Room Air Conditioner to default differently, and reusing the constants
 * keeps the two classes' cache behaviour easy to compare. It issues no AT
 * traffic, matching every other endpoint class in this library.
 *
 * Unlike MatterThermostat, this class validates nothing: no
 * ControlSequenceOfOperation, no auto-mode gate, no setpoint limits, no
 * deadband. The brief's public surface has none of MatterThermostat's
 * configuration surface either (no begin(controlSequence, autoMode), no
 * getMinHeatSetpoint() family), so there is nothing here to validate
 * against; setMode() accepts any uint8_t and setCoolingSetpoint()/
 * setHeatingSetpoint() accept any double, exactly like
 * MatterFlowSensor::setRawMeasuredValue() accepts any uint16_t. A future
 * task adding limits would need to add the brief surface for them too.
 *
 * setLocalTemperature() has no getter, matching the brief's own listed
 * surface exactly (unlike MatterThermostat's getLocalTemperature()). The
 * cached value still exists internally, only to support the standard
 * no-write-if-unchanged optimisation every setter in this library uses, and
 * attributeChangeCB() still updates it from a URC for the same reason
 * MatterFlowSensor's does: to keep that cache accurate even though nothing
 * public reads it back.
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's
 * hearthDispatchAttr), and echoing it through setAttributeVal/
 * updateAttributeVal would be an infinite loop with the real device. This is
 * the entire reason AT+MTATTR has a silent write mode; see
 * MatterEndPoint.h's header comment.
 */
#pragma once

#include <cstddef>
#include <stdint.h>
#include "MatterEndPoint.h"

class MatterRoomAirConditioner : public MatterEndPoint {
public:
  MatterRoomAirConditioner();
  ~MatterRoomAirConditioner();
  virtual bool begin(bool on = false);
  void end();

  bool setOnOff(bool on);
  bool getOnOff();

  /* set the reported LocalTemperature in Celsius degrees; i16 hundredths on
   * the wire. No getter: see the header comment. */
  bool setLocalTemperature(double c);

  bool setCoolingSetpoint(double c);
  double getCoolingSetpoint();

  bool setHeatingSetpoint(double c);
  double getHeatingSetpoint();

  /* raw SystemModeEnum value; 0 is OFF (SystemModeEnum::kOff). No
   * validation: see the header comment. */
  bool setMode(uint8_t systemMode);
  uint8_t getMode();

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;
  /* implementation keeps temperature/setpoints in 1/100th of a Celsius
   * degree, matching MatterThermostat. */
  int16_t localTemperature = 2000;            // 20C local temperature
  int16_t coolingSetpointTemperature = 2400;   // 24C cooling setpoint
  int16_t heatingSetpointTemperature = 1600;   // 16C heating setpoint
  uint8_t systemMode = 0;                      // SystemModeEnum::kOff
};

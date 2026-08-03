/*
 * MatterOccupancySensor.h - occupancy detection sensor. The sketch pushes
 * readings up to the fabric, nothing arrives back down except for the
 * occupancy attribute itself.
 *
 * Mirrors arduino-esp32's Matter library MatterOccupancySensor (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterOccupancySensor.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0107 is occupancy_sensor, cluster 0x0406 is
 * OccupancySensing (attribute 0x0000, Occupancy, a uint8 bitmap).
 * The same IDs upstream's .cpp reads from connectedhomeip; there is no such
 * header on a host build, so they are given as plain integers in the .cpp
 * instead.
 *
 * HoldTime and HoldTimeLimits are AttributeAccessInterface territory in the
 * firmware (design spec section 3) and deferred: the methods exist for parity
 * but return false, with doc comments explaining why. Test asserts the false
 * return.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterOccupancySensor : public MatterEndPoint {
public:
  /* Different Occupancy Sensor Types */
  enum OccupancySensorType_t {
    OCCUPANCY_SENSOR_TYPE_PIR = 0,
    OCCUPANCY_SENSOR_TYPE_ULTRASONIC = 1,
    OCCUPANCY_SENSOR_TYPE_PIR_AND_ULTRASONIC = 2,
    OCCUPANCY_SENSOR_TYPE_PHYSICAL_CONTACT = 3
  };

  MatterOccupancySensor();
  ~MatterOccupancySensor();
  /* begin Matter Occupancy Sensor endpoint with initial occupancy state and default PIR sensor type */
  bool begin(bool _occupancyState = false, OccupancySensorType_t _occupancySensorType = OCCUPANCY_SENSOR_TYPE_PIR);
  /* this will just stop processing Occupancy Sensor Matter events */
  void end();

  /* set the occupancy state */
  bool setOccupancy(bool _occupancyState);
  /* returns the occupancy state */
  bool getOccupancy() {
    return occupancyState;
  }

  /* set the hold time (in seconds) - deferred to firmware, returns false */
  bool setHoldTime(uint16_t _holdTime_seconds) {
    (void)_holdTime_seconds;  /* HoldTime management is deferred to the firmware's AttributeAccessInterface
     * (design spec section 3), not exposed over AT+MTATTR. */
    return false;
  }
  /* returns the hold time (in seconds) */
  uint16_t getHoldTime() {
    return holdTime_seconds;
  }

  /* set the hold time limits (min, max, default in seconds) - deferred to firmware, returns false */
  bool setHoldTimeLimits(uint16_t _holdTimeMin_seconds, uint16_t _holdTimeMax_seconds, uint16_t _holdTimeDefault_seconds) {
    (void)_holdTimeMin_seconds;  /* HoldTimeLimits management is deferred to the firmware's AttributeAccessInterface
     * (design spec section 3), not exposed over AT+MTATTR. */
    (void)_holdTimeMax_seconds;
    (void)_holdTimeDefault_seconds;
    return false;
  }

  /* User callback for HoldTime changes - deferred to firmware, stored but never fired */
  using HoldTimeChangeCB = std::function<bool(uint16_t holdTime_seconds)>;
  void onHoldTimeChange(HoldTimeChangeCB onHoldTimeChangeCB) {
    /* HoldTime change notifications are managed by the firmware's AttributeAccessInterface
     * (design spec section 3). The callback is stored for parity with upstream, but Hearth
     * never fires it since changes are not exposed over AT+MTATTR. */
    _onHoldTimeChangeCB = onHoldTimeChangeCB;
  }

  /* bool conversion operator */
  void operator=(bool _occupancyState) {
    setOccupancy(_occupancyState);
  }
  /* bool conversion operator */
  operator bool() {
    return getOccupancy();
  }

  /* this function is called by Matter internal event processor. It could be overwritten by the application, if necessary. */
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /* Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that OccupancySensing::Id / Occupancy::Id
   * is an unsigned int8, so attributeChangeCB() above (and any sketch override
   * of it) receives val->val.u8 already populated with the right type,
   * matching upstream's own MatterOccupancySensor.cpp. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  /* User callback for value changes */
  using OccupancyChangeCB = std::function<bool(bool occupancy)>;
  void onChange(OccupancyChangeCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

protected:
  bool started = false;
  bool occupancyState = false;
  uint16_t holdTime_seconds = 0;

  /* User callbacks */
  OccupancyChangeCB _onChangeCB = nullptr;
  HoldTimeChangeCB _onHoldTimeChangeCB = nullptr;
};

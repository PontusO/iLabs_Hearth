/*
 * MatterOccupancySensor.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, this is a read-direction
 * endpoint so attributeChangeCB handles occupancy but does nothing more,
 * and HoldTime/HoldTimeLimits are deferred to the firmware).
 */
#include "MatterEndpoints/MatterOccupancySensor.h"

namespace {
/* occupancy_sensor (esp_matter_endpoint.h), chip::app::Clusters::
 * OccupancySensing::Id and OccupancySensing::Attributes::
 * Occupancy::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kOccupancySensorDeviceType = 0x0107;
const uint32_t kOccupancySensingClusterId = 0x0406;
const uint32_t kOccupancyAttributeId = 0x0000;
}  // namespace

MatterOccupancySensor::MatterOccupancySensor() {}

MatterOccupancySensor::~MatterOccupancySensor() {
  end();
}

bool MatterOccupancySensor::begin(bool _occupancyState, OccupancySensorType_t _occupancySensorType) {
  (void)_occupancySensorType;  /* type info not exposed over AT interface */
  if (!hearthDeclare(this, kOccupancySensorDeviceType)) {
    return false;
  }
  occupancyState = _occupancyState;
  started = true;
  return true;
}

void MatterOccupancySensor::end() {
  started = false;
}

bool MatterOccupancySensor::setOccupancy(bool _occupancyState) {
  if (!started) {
    return false;
  }
  /* avoid a write if there was no change, matching upstream */
  if (occupancyState == _occupancyState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(_occupancyState ? 1 : 0);
  if (!updateAttributeVal(kOccupancySensingClusterId, kOccupancyAttributeId, &val)) {
    return false;  /* the cache is left untouched: the device's idea of the
                    * state and the host's idea of it must not diverge */
  }
  occupancyState = _occupancyState;
  return true;
}

/*
 * This endpoint is read-direction: the sketch pushes readings up to the fabric.
 * When attributeChangeCB is called (which happens on controller-driven changes),
 * we call the user callback and only update the cache if it accepts the
 * change, matching every other Hearth endpoint's attributeChangeCB (match
 * endpoint_id, gate the cache update on the callback's return).
 */
bool MatterOccupancySensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kOccupancySensingClusterId && attribute_id == kOccupancyAttributeId) {
    bool newState = (val->val.u8 != 0);
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState);
    }
    if (ret) {
      occupancyState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterOccupancySensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOccupancySensingClusterId && attribute_id == kOccupancyAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

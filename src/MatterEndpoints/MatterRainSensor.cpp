/*
 * MatterRainSensor.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back).
 */
#include "MatterEndpoints/MatterRainSensor.h"

namespace {
/* rain_sensor (esp_matter_endpoint.h), chip::app::Clusters::BooleanState::Id and
 * BooleanState::Attributes::StateValue::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kRainSensorDeviceType = 0x0044;
const uint32_t kBooleanStateClusterId = 0x0045;
const uint32_t kStateValueAttributeId = 0x0000;
}  // namespace

MatterRainSensor::MatterRainSensor() {}

MatterRainSensor::~MatterRainSensor() {
  end();
}

bool MatterRainSensor::begin(bool initialState) {
  if (!hearthDeclare(this, kRainSensorDeviceType)) {
    return false;
  }
  rainState = initialState;
  started = true;
  return true;
}

void MatterRainSensor::end() {
  started = false;
}

bool MatterRainSensor::setRain(bool newState) {
  if (!started) {
    return false;
  }
  if (rainState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kBooleanStateClusterId, kStateValueAttributeId, &val)) {
    return false;
  }
  rainState = newState;
  return true;
}

bool MatterRainSensor::getRain() {
  return rainState;
}

MatterRainSensor::operator bool() {
  return getRain();
}

void MatterRainSensor::operator=(bool newState) {
  setRain(newState);
}

bool MatterRainSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kBooleanStateClusterId && attribute_id == kStateValueAttributeId) {
    bool newState = val->val.b;
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState);
    }
    if (ret) {
      rainState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterRainSensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kBooleanStateClusterId && attribute_id == kStateValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

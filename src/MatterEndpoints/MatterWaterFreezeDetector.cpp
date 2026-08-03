/*
 * MatterWaterFreezeDetector.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back).
 */
#include "MatterEndpoints/MatterWaterFreezeDetector.h"

namespace {
/* water_freeze_detector (esp_matter_endpoint.h), chip::app::Clusters::BooleanState::Id and
 * BooleanState::Attributes::StateValue::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kWaterFreezeDetectorDeviceType = 0x0041;
const uint32_t kBooleanStateClusterId = 0x0045;
const uint32_t kStateValueAttributeId = 0x0000;
}  // namespace

MatterWaterFreezeDetector::MatterWaterFreezeDetector() {}

MatterWaterFreezeDetector::~MatterWaterFreezeDetector() {
  end();
}

bool MatterWaterFreezeDetector::begin(bool initialState) {
  if (!hearthDeclare(this, kWaterFreezeDetectorDeviceType)) {
    return false;
  }
  freezeState = initialState;
  started = true;
  return true;
}

void MatterWaterFreezeDetector::end() {
  started = false;
}

bool MatterWaterFreezeDetector::setFreeze(bool newState) {
  if (!started) {
    return false;
  }
  if (freezeState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kBooleanStateClusterId, kStateValueAttributeId, &val)) {
    return false;
  }
  freezeState = newState;
  return true;
}

bool MatterWaterFreezeDetector::getFreeze() {
  return freezeState;
}

MatterWaterFreezeDetector::operator bool() {
  return getFreeze();
}

void MatterWaterFreezeDetector::operator=(bool newState) {
  setFreeze(newState);
}

bool MatterWaterFreezeDetector::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      freezeState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterWaterFreezeDetector::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kBooleanStateClusterId && attribute_id == kStateValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

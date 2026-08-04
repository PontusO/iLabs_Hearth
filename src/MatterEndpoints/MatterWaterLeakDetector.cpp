/*
 * MatterWaterLeakDetector.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back).
 */
#include "MatterEndpoints/MatterWaterLeakDetector.h"

namespace {
/* water_leak_detector (esp_matter_endpoint.h), chip::app::Clusters::BooleanState::Id and
 * BooleanState::Attributes::StateValue::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kWaterLeakDetectorDeviceType = 0x0043;
const uint32_t kBooleanStateClusterId = 0x0045;
const uint32_t kStateValueAttributeId = 0x0000;
}  // namespace

MatterWaterLeakDetector::MatterWaterLeakDetector() {}

MatterWaterLeakDetector::~MatterWaterLeakDetector() {
  end();
}

bool MatterWaterLeakDetector::begin(bool initialState) {
  if (!hearthDeclare(this, kWaterLeakDetectorDeviceType)) {
    return false;
  }
  leakState = initialState;
  started = true;
  return true;
}

void MatterWaterLeakDetector::end() {
  started = false;
}

bool MatterWaterLeakDetector::setLeak(bool newState) {
  if (!started) {
    return false;
  }
  if (leakState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kBooleanStateClusterId, kStateValueAttributeId, &val)) {
    return false;
  }
  leakState = newState;
  return true;
}

bool MatterWaterLeakDetector::getLeak() {
  return leakState;
}

MatterWaterLeakDetector::operator bool() {
  return getLeak();
}

void MatterWaterLeakDetector::operator=(bool newState) {
  setLeak(newState);
}

bool MatterWaterLeakDetector::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      leakState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterWaterLeakDetector::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kBooleanStateClusterId && attribute_id == kStateValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

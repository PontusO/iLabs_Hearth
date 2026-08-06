/*
 * MatterCooktop.cpp - implementation. See the header for the OffOnly
 * design: no method here accepts or can produce a true value on the wire.
 * off() is the only setter and it always writes esp_matter_bool(false).
 */
#include "MatterEndpoints/MatterCooktop.h"

namespace {
/* cooktop (esp_matter_endpoint.h:122, ESP_MATTER_COOKTOP_DEVICE_TYPE_ID),
 * chip::app::Clusters::OnOff::Id and OnOff::Attributes::OnOff::Id. Given as
 * plain integers: there is no connectedhomeip header on a host build to
 * pull the named constants from. */
const uint32_t kCooktopDeviceType = 0x0078;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterCooktop::MatterCooktop() {}

MatterCooktop::~MatterCooktop() {
  end();
}

bool MatterCooktop::begin() {
  if (!hearthDeclare(this, kCooktopDeviceType)) {
    return false;
  }
  onOffState = false;
  started = true;
  return true;
}

void MatterCooktop::end() {
  started = false;
}

void MatterCooktop::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState);
  }
}

bool MatterCooktop::off() {
  if (!started) {
    return false;
  }
  if (onOffState == false) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(false);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;
  }
  onOffState = false;
  return true;
}

bool MatterCooktop::getOnOff() {
  return onOffState;
}

MatterCooktop::operator bool() {
  return getOnOff();
}

bool MatterCooktop::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    bool newState = val->val.b;
    if (_onChangeOnOffCB != NULL) {
      ret &= _onChangeOnOffCB(newState);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState);
    }
    if (ret) {
      onOffState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterCooktop::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

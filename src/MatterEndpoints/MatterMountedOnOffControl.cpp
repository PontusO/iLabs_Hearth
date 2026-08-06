/*
 * MatterMountedOnOffControl.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, attributeChangeCB does not
 * write back); the body below is MatterOnOffPlugin.cpp's, unchanged except
 * for the class name and the device type constant.
 */
#include "MatterEndpoints/MatterMountedOnOffControl.h"

namespace {
/* mounted_on_off_control (esp_matter_endpoint.h:60,
 * ESP_MATTER_MOUNTED_ON_OFF_CONTROL_DEVICE_TYPE_ID), chip::app::Clusters::
 * OnOff::Id and OnOff::Attributes::OnOff::Id. Given as plain integers: there
 * is no connectedhomeip header on a host build to pull the named constants
 * from. */
const uint32_t kMountedOnOffControlDeviceType = 0x010F;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterMountedOnOffControl::MatterMountedOnOffControl() {}

MatterMountedOnOffControl::~MatterMountedOnOffControl() {
  end();
}

bool MatterMountedOnOffControl::begin(bool initialState) {
  if (!hearthDeclare(this, kMountedOnOffControlDeviceType)) {
    return false;
  }
  onOffState = initialState;
  started = true;
  return true;
}

void MatterMountedOnOffControl::end() {
  started = false;
}

void MatterMountedOnOffControl::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState);
  }
}

bool MatterMountedOnOffControl::setOnOff(bool newState) {
  if (!started) {
    return false;
  }
  if (onOffState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;
  }
  onOffState = newState;
  return true;
}

bool MatterMountedOnOffControl::getOnOff() {
  return onOffState;
}

bool MatterMountedOnOffControl::toggle() {
  return setOnOff(!onOffState);
}

MatterMountedOnOffControl::operator bool() {
  return getOnOff();
}

void MatterMountedOnOffControl::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterMountedOnOffControl::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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

esp_matter_val_type_t MatterMountedOnOffControl::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

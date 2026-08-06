/*
 * MatterMountedDimmableLoadControl.cpp - implementation. See the header for
 * the design notes (begin() issues no AT traffic, attributeChangeCB does
 * not write back, and the brightness naming choice over MatterDimmablePlugin's
 * level naming). The body below is MatterDimmablePlugin.cpp's, with the
 * class name, device type constant and level->brightness renaming applied.
 */
#include "MatterEndpoints/MatterMountedDimmableLoadControl.h"

namespace {
/* mounted_dimmable_load_control (esp_matter_endpoint.h:62,
 * ESP_MATTER_MOUNTED_DIMMABLE_LOAD_CONTROL_DEVICE_TYPE_ID),
 * chip::app::Clusters::OnOff::Id, OnOff::Attributes::OnOff::Id,
 * chip::app::Clusters::LevelControl::Id and
 * LevelControl::Attributes::CurrentLevel::Id. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kMountedDimmableLoadControlDeviceType = 0x0110;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
}  // namespace

MatterMountedDimmableLoadControl::MatterMountedDimmableLoadControl() {}

MatterMountedDimmableLoadControl::~MatterMountedDimmableLoadControl() {
  end();
}

bool MatterMountedDimmableLoadControl::begin(bool initialState, uint8_t brightness) {
  if (!hearthDeclare(this, kMountedDimmableLoadControlDeviceType)) {
    return false;
  }
  onOffState = initialState;
  brightnessLevel = brightness;
  started = true;
  return true;
}

void MatterMountedDimmableLoadControl::end() {
  started = false;
}

void MatterMountedDimmableLoadControl::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, brightnessLevel);
  }
}

bool MatterMountedDimmableLoadControl::setOnOff(bool newState) {
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

bool MatterMountedDimmableLoadControl::getOnOff() {
  return onOffState;
}

bool MatterMountedDimmableLoadControl::toggle() {
  return setOnOff(!onOffState);
}

bool MatterMountedDimmableLoadControl::setBrightness(uint8_t newBrightness) {
  if (!started) {
    return false;
  }
  if (brightnessLevel == newBrightness) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(newBrightness);
  if (!updateAttributeVal(kLevelControlClusterId, kCurrentLevelAttributeId, &val)) {
    return false;
  }
  brightnessLevel = newBrightness;
  return true;
}

uint8_t MatterMountedDimmableLoadControl::getBrightness() {
  return brightnessLevel;
}

MatterMountedDimmableLoadControl::operator bool() {
  return getOnOff();
}

void MatterMountedDimmableLoadControl::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterMountedDimmableLoadControl::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      ret &= _onChangeCB(newState, brightnessLevel);
    }
    if (ret) {
      onOffState = newState;
    }
  } else if (endpoint_id == getEndPointId() && cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    uint8_t newBrightness = val->val.u8;
    if (_onChangeBrightnessCB != NULL) {
      ret &= _onChangeBrightnessCB(newBrightness);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, newBrightness);
    }
    if (ret) {
      brightnessLevel = newBrightness;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterMountedDimmableLoadControl::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

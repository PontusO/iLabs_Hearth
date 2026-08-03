/*
 * MatterDimmablePlugin.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back, and the onChange() signature differs from MatterOnOffPlugin's).
 */
#include "MatterEndpoints/MatterDimmablePlugin.h"

namespace {
/* dimmable_plug_in_unit (esp_matter_endpoint.h, the pinned esp-matter v1.5.1 name;
 * 1.4.1 called it dimmable_plugin_unit), chip::app::Clusters::OnOff::Id,
 * OnOff::Attributes::OnOff::Id, chip::app::Clusters::LevelControl::Id and
 * LevelControl::Attributes::CurrentLevel::Id. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kDimmablePluginDeviceType = 0x010B;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
}  // namespace

MatterDimmablePlugin::MatterDimmablePlugin() {}

MatterDimmablePlugin::~MatterDimmablePlugin() {
  end();
}

bool MatterDimmablePlugin::begin(bool initialState, uint8_t level) {
  if (!hearthDeclare(this, kDimmablePluginDeviceType)) {
    return false;
  }
  onOffState = initialState;
  this->level = level;
  started = true;
  return true;
}

void MatterDimmablePlugin::end() {
  started = false;
}

void MatterDimmablePlugin::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, level);
  }
}

bool MatterDimmablePlugin::setOnOff(bool newState) {
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

bool MatterDimmablePlugin::getOnOff() {
  return onOffState;
}

bool MatterDimmablePlugin::toggle() {
  return setOnOff(!onOffState);
}

bool MatterDimmablePlugin::setLevel(uint8_t newLevel) {
  if (!started) {
    return false;
  }
  if (level == newLevel) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(newLevel);
  if (!updateAttributeVal(kLevelControlClusterId, kCurrentLevelAttributeId, &val)) {
    return false;
  }
  level = newLevel;
  return true;
}

uint8_t MatterDimmablePlugin::getLevel() {
  return level;
}

MatterDimmablePlugin::operator bool() {
  return getOnOff();
}

void MatterDimmablePlugin::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterDimmablePlugin::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      ret &= _onChangeCB(newState, level);
    }
    if (ret) {
      onOffState = newState;
    }
  } else if (endpoint_id == getEndPointId() && cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    uint8_t newLevel = val->val.u8;
    if (_onChangeLevelCB != NULL) {
      ret &= _onChangeLevelCB(newLevel);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, newLevel);
    }
    if (ret) {
      level = newLevel;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterDimmablePlugin::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * MatterOnOffPlugin.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back).
 */
#include "MatterEndpoints/MatterOnOffPlugin.h"

namespace {
/* on_off_plug_in_unit (esp_matter_endpoint.h, the pinned esp-matter v1.5.1 name;
 * 1.4.1 called it on_off_plugin_unit), chip::app::Clusters::OnOff::Id and
 * OnOff::Attributes::OnOff::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kOnOffPluginDeviceType = 0x010A;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterOnOffPlugin::MatterOnOffPlugin() {}

MatterOnOffPlugin::~MatterOnOffPlugin() {
  end();
}

bool MatterOnOffPlugin::begin(bool initialState) {
  if (!hearthDeclare(this, kOnOffPluginDeviceType)) {
    return false;
  }
  onOffState = initialState;
  started = true;
  return true;
}

void MatterOnOffPlugin::end() {
  started = false;
}

void MatterOnOffPlugin::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState);
  }
}

bool MatterOnOffPlugin::setOnOff(bool newState) {
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

bool MatterOnOffPlugin::getOnOff() {
  return onOffState;
}

bool MatterOnOffPlugin::toggle() {
  return setOnOff(!onOffState);
}

MatterOnOffPlugin::operator bool() {
  return getOnOff();
}

void MatterOnOffPlugin::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterOnOffPlugin::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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

esp_matter_val_type_t MatterOnOffPlugin::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

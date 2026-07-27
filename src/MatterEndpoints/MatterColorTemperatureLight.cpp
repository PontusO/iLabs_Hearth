/*
 * MatterColorTemperatureLight.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, attributeChangeCB does not
 * write back, and onChange()'s signature carries all three values).
 */
#include "MatterEndpoints/MatterColorTemperatureLight.h"

namespace {
/* color_temperature_light (esp_matter_endpoint.h), chip::app::Clusters::
 * OnOff::Id, OnOff::Attributes::OnOff::Id, LevelControl::Id,
 * LevelControl::Attributes::CurrentLevel::Id, ColorControl::Id and
 * ColorControl::Attributes::ColorTemperatureMireds::Id. Given as plain
 * integers: there is no connectedhomeip header on a host build to pull the
 * named constants from. */
const uint32_t kColorTemperatureLightDeviceType = 0x010C;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
const uint32_t kColorControlClusterId = 0x0300;
const uint32_t kColorTemperatureMiredsAttributeId = 0x0007;
}  // namespace

MatterColorTemperatureLight::MatterColorTemperatureLight() {}

MatterColorTemperatureLight::~MatterColorTemperatureLight() {
  end();
}

bool MatterColorTemperatureLight::begin(bool initialState, uint8_t brightness, uint16_t colorTemperature) {
  if (!hearthDeclare(this, kColorTemperatureLightDeviceType)) {
    return false;
  }
  onOffState = initialState;
  brightnessLevel = brightness;
  colorTemperatureLevel = colorTemperature;
  started = true;
  return true;
}

void MatterColorTemperatureLight::end() {
  started = false;
}

void MatterColorTemperatureLight::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, brightnessLevel, colorTemperatureLevel);
  }
}

bool MatterColorTemperatureLight::setOnOff(bool newState) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (onOffState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  onOffState = newState;
  return true;
}

bool MatterColorTemperatureLight::getOnOff() {
  return onOffState;
}

bool MatterColorTemperatureLight::toggle() {
  return setOnOff(!onOffState);
}

bool MatterColorTemperatureLight::setBrightness(uint8_t newBrightness) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (brightnessLevel == newBrightness) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(newBrightness);
  if (!updateAttributeVal(kLevelControlClusterId, kCurrentLevelAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  brightnessLevel = newBrightness;
  return true;
}

uint8_t MatterColorTemperatureLight::getBrightness() {
  return brightnessLevel;
}

bool MatterColorTemperatureLight::setColorTemperature(uint16_t newTemperature) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (colorTemperatureLevel == newTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(newTemperature);
  if (!updateAttributeVal(kColorControlClusterId, kColorTemperatureMiredsAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  colorTemperatureLevel = newTemperature;
  return true;
}

uint16_t MatterColorTemperatureLight::getColorTemperature() {
  return colorTemperatureLevel;
}

MatterColorTemperatureLight::operator bool() {
  return getOnOff();
}

void MatterColorTemperatureLight::operator=(bool newState) {
  setOnOff(newState);
}

/*
 * hearthAttrTypeFor() below tells hearthDispatchAttr() (Hearth.cpp) the
 * real type for each attribute this class owns, so by the time it calls
 * attributeChangeCB(), val->val.b / val->val.u8 / val->val.u16 are the
 * members the wire's value actually landed in. Reading val->val.i here, as
 * MatterOnOffLight's first revision mistakenly did, would read a union
 * member the dispatcher never wrote once the type stops being the generic
 * INTEGER fallback.
 */
bool MatterColorTemperatureLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId()) {
    return ret;
  }
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    bool newState = val->val.b;
    if (_onChangeOnOffCB != NULL) {
      ret &= _onChangeOnOffCB(newState);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState, brightnessLevel, colorTemperatureLevel);
    }
    if (ret) {
      // no write back here: the change already came from the fabric
      // (mode 0 exists in AT+MTATTR precisely so this path never echoes)
      onOffState = newState;
    }
  } else if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    uint8_t newLevel = val->val.u8;
    if (_onChangeBrightnessCB != NULL) {
      ret &= _onChangeBrightnessCB(newLevel);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, newLevel, colorTemperatureLevel);
    }
    if (ret) {
      // no write back here, same reasoning as the on/off branch above
      brightnessLevel = newLevel;
    }
  } else if (cluster_id == kColorControlClusterId && attribute_id == kColorTemperatureMiredsAttributeId) {
    uint16_t newTemperature = val->val.u16;
    if (_onChangeTemperatureCB != NULL) {
      ret &= _onChangeTemperatureCB(newTemperature);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, brightnessLevel, newTemperature);
    }
    if (ret) {
      // no write back here, same reasoning as the on/off branch above
      colorTemperatureLevel = newTemperature;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterColorTemperatureLight::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  if (cluster_id == kColorControlClusterId && attribute_id == kColorTemperatureMiredsAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

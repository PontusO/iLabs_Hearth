/*
 * MatterDimmableLight.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back, and the onChange() signature differs from MatterOnOffLight's).
 */
#include "MatterEndpoints/MatterDimmableLight.h"

namespace {
/* dimmable_light (esp_matter_endpoint.h), chip::app::Clusters::OnOff::Id,
 * OnOff::Attributes::OnOff::Id, chip::app::Clusters::LevelControl::Id and
 * LevelControl::Attributes::CurrentLevel::Id. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kDimmableLightDeviceType = 0x0101;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
}  // namespace

MatterDimmableLight::MatterDimmableLight() {}

MatterDimmableLight::~MatterDimmableLight() {
  end();
}

bool MatterDimmableLight::begin(bool initialState, uint8_t brightness) {
  if (!hearthDeclare(this, kDimmableLightDeviceType)) {
    return false;
  }
  onOffState = initialState;
  brightnessLevel = brightness;
  started = true;
  return true;
}

void MatterDimmableLight::end() {
  started = false;
}

void MatterDimmableLight::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, brightnessLevel);
  }
}

bool MatterDimmableLight::setOnOff(bool newState) {
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

bool MatterDimmableLight::getOnOff() {
  return onOffState;
}

bool MatterDimmableLight::toggle() {
  return setOnOff(!onOffState);
}

bool MatterDimmableLight::setBrightness(uint8_t newBrightness) {
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

uint8_t MatterDimmableLight::getBrightness() {
  return brightnessLevel;
}

MatterDimmableLight::operator bool() {
  return getOnOff();
}

void MatterDimmableLight::operator=(bool newState) {
  setOnOff(newState);
}

/*
 * hearthAttrTypeFor() below tells hearthDispatchAttr() (Hearth.cpp) the
 * real type for each attribute this class owns, so by the time it calls
 * attributeChangeCB(), val->val.b / val->val.u8 are the members the wire's
 * value actually landed in, matching upstream's own MatterDimmableLight.cpp
 * exactly (it reads val->val.u8 for CurrentLevel). Reading val->val.i here,
 * as MatterOnOffLight's first revision mistakenly did, would read a union
 * member the dispatcher never wrote once the type stops being the generic
 * INTEGER fallback.
 */
bool MatterDimmableLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      // no write back here: the change already came from the fabric
      // (mode 0 exists in AT+MTATTR precisely so this path never echoes)
      onOffState = newState;
    }
  } else if (endpoint_id == getEndPointId() && cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    uint8_t newLevel = val->val.u8;
    if (_onChangeBrightnessCB != NULL) {
      ret &= _onChangeBrightnessCB(newLevel);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, newLevel);
    }
    if (ret) {
      // no write back here, same reasoning as the on/off branch above
      brightnessLevel = newLevel;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterDimmableLight::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * MatterEnhancedColorLight.cpp - implementation. See the header for the
 * three documented deviations from a literal transcription of upstream's
 * .cpp (no wire traffic from begin(), no read-before-write, and the
 * verified brightnessLevel/colorHSV.v quirk reproduced exactly).
 */
#include "MatterEndpoints/MatterEnhancedColorLight.h"

namespace {
/* extended_color_light (esp_matter_endpoint.h's
 * ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID), chip::app::Clusters::
 * OnOff::Id, OnOff::Attributes::OnOff::Id, LevelControl::Id,
 * LevelControl::Attributes::CurrentLevel::Id, ColorControl::Id and
 * ColorControl::Attributes::{CurrentHue,CurrentSaturation,
 * ColorTemperatureMireds}::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kEnhancedColorLightDeviceType = 0x010D;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
const uint32_t kColorControlClusterId = 0x0300;
const uint32_t kCurrentHueAttributeId = 0x0000;
const uint32_t kCurrentSaturationAttributeId = 0x0001;
const uint32_t kColorTemperatureMiredsAttributeId = 0x0007;
}  // namespace

MatterEnhancedColorLight::MatterEnhancedColorLight() {}

MatterEnhancedColorLight::~MatterEnhancedColorLight() {
  end();
}

bool MatterEnhancedColorLight::begin(bool initialState, espHsvColor_t _colorHSV, uint8_t brightness, uint16_t colorTemperature) {
  if (!hearthDeclare(this, kEnhancedColorLightDeviceType)) {
    return false;
  }
  onOffState = initialState;
  brightnessLevel = brightness;
  colorHSV = { _colorHSV.h, _colorHSV.s, _colorHSV.v };
  colorTemperatureLevel = colorTemperature;
  started = true;
  return true;
}

void MatterEnhancedColorLight::end() {
  started = false;
}

void MatterEnhancedColorLight::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, colorHSV, brightnessLevel, colorTemperatureLevel);
  }
}

bool MatterEnhancedColorLight::setOnOff(bool newState) {
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

bool MatterEnhancedColorLight::getOnOff() {
  return onOffState;
}

bool MatterEnhancedColorLight::toggle() {
  return setOnOff(!onOffState);
}

/*
 * Updates brightnessLevel only, never colorHSV.v -- see the header's
 * deviation 3. This stack's synchronous echo (the mode-1 write's own
 * +MTATTR reply dispatches attributeChangeCB before this call returns, per
 * AT_MT_SPEC.md S3.8) drives the LevelControl branch below too, which is
 * what updates colorHSV.v; the two fields converge for a LOCAL call, but
 * not for a controller-driven one with no matching setBrightness() call.
 */
bool MatterEnhancedColorLight::setBrightness(uint8_t newBrightness) {
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

uint8_t MatterEnhancedColorLight::getBrightness() {
  return brightnessLevel;
}

bool MatterEnhancedColorLight::setColorTemperature(uint16_t newTemperature) {
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

uint16_t MatterEnhancedColorLight::getColorTemperature() {
  return colorTemperatureLevel;
}

bool MatterEnhancedColorLight::setColorRGB(espRgbColor_t rgbColor) {
  return setColorHSV(espRgbColorToHsvColor(rgbColor));
}

espRgbColor_t MatterEnhancedColorLight::getColorRGB() {
  return espHsvColorToRgbColor(colorHSV);
}

/*
 * Per-field guarded writes, one for whichever of hue/saturation/level
 * actually differ from the cache -- see the header's deviation 2. Order
 * (hue, then saturation, then level) matches
 * MatterEnhancedColorLight.cpp's own write sequence exactly, and that
 * order is load-bearing, not incidental: this stack's mode-1 writes echo
 * attributeChangeCB synchronously (AT_MT_SPEC.md S3.8), and that
 * callback's ColorControl branch rebuilds colorHSV from whatever is
 * ALREADY cached plus the one field that just changed. A write must land
 * (and its echo run) before the NEXT field's write is issued, or that next
 * echo would rebuild off a stale copy of a field this same call is in the
 * middle of changing. None of the three writes short-circuits on another
 * one failing, matching MatterFan.cpp's setSpeedPercent() silent path: a
 * partial failure (say saturation rejected) still attempts the level
 * write, and leaves exactly the fields whose own write succeeded updated.
 */
bool MatterEnhancedColorLight::setColorHSV(espHsvColor_t hsvColor) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (colorHSV.h == hsvColor.h && colorHSV.s == hsvColor.s && colorHSV.v == hsvColor.v) {
    return true;
  }
  bool ret = true;
  if (colorHSV.h != hsvColor.h) {
    esp_matter_attr_val_t val = esp_matter_uint8((uint8_t)hsvColor.h);
    if (updateAttributeVal(kColorControlClusterId, kCurrentHueAttributeId, &val)) {
      colorHSV.h = hsvColor.h;
    } else {
      ret = false;
    }
  }
  if (colorHSV.s != hsvColor.s) {
    esp_matter_attr_val_t val = esp_matter_uint8(hsvColor.s);
    if (updateAttributeVal(kColorControlClusterId, kCurrentSaturationAttributeId, &val)) {
      colorHSV.s = hsvColor.s;
    } else {
      ret = false;
    }
  }
  if (colorHSV.v != hsvColor.v) {
    esp_matter_attr_val_t val = esp_matter_uint8(hsvColor.v);
    if (updateAttributeVal(kLevelControlClusterId, kCurrentLevelAttributeId, &val)) {
      colorHSV.v = hsvColor.v;
    } else {
      ret = false;
    }
  }
  return ret;
}

espHsvColor_t MatterEnhancedColorLight::getColorHSV() {
  return colorHSV;
}

MatterEnhancedColorLight::operator bool() {
  return getOnOff();
}

void MatterEnhancedColorLight::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterEnhancedColorLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      ret &= _onChangeCB(newState, colorHSV, brightnessLevel, colorTemperatureLevel);
    }
    if (ret) {
      // no write back here: this callback is itself the notification of a
      // change already applied, and issuing a write from inside it would loop
      onOffState = newState;
    }
  } else if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    uint8_t newLevel = val->val.u8;
    if (_onChangeBrightnessCB != NULL) {
      ret &= _onChangeBrightnessCB(newLevel);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, colorHSV, newLevel, colorTemperatureLevel);
    }
    if (ret) {
      /* Verified upstream quirk, reproduced exactly: this branch updates
       * colorHSV.v, never brightnessLevel. See the header's deviation 3. */
      colorHSV.v = newLevel;
    }
  } else if (cluster_id == kColorControlClusterId) {
    if (attribute_id == kColorTemperatureMiredsAttributeId) {
      uint16_t newTemperature = val->val.u16;
      if (_onChangeTemperatureCB != NULL) {
        ret &= _onChangeTemperatureCB(newTemperature);
      }
      if (_onChangeCB != NULL) {
        ret &= _onChangeCB(onOffState, colorHSV, brightnessLevel, newTemperature);
      }
      if (ret) {
        colorTemperatureLevel = newTemperature;
      }
    } else if (attribute_id == kCurrentHueAttributeId || attribute_id == kCurrentSaturationAttributeId) {
      espHsvColor_t newHsv = { colorHSV.h, colorHSV.s, colorHSV.v };
      if (attribute_id == kCurrentHueAttributeId) {
        newHsv.h = val->val.u8;
      } else {  // attribute_id == kCurrentSaturationAttributeId
        newHsv.s = val->val.u8;
      }
      if (_onChangeColorCB != NULL) {
        ret &= _onChangeColorCB(newHsv);
      }
      if (_onChangeCB != NULL) {
        ret &= _onChangeCB(onOffState, newHsv, brightnessLevel, colorTemperatureLevel);
      }
      if (ret) {
        // no write back here, same reasoning as the on/off branch above
        colorHSV = { newHsv.h, newHsv.s, newHsv.v };
      }
    }
  }
  return ret;
}

esp_matter_val_type_t MatterEnhancedColorLight::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  if (cluster_id == kColorControlClusterId) {
    if (attribute_id == kCurrentHueAttributeId || attribute_id == kCurrentSaturationAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT8;
    }
    if (attribute_id == kColorTemperatureMiredsAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

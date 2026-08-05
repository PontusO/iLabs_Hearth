/*
 * MatterColorLight.cpp - implementation. See the header for the documented
 * deviations (no wire traffic from begin(), no read-before-write, the
 * hue-then-saturation-then-level echo-ordering discipline copied from
 * MatterEnhancedColorLight.cpp) and for why there is no brightnessLevel/
 * colorHSV.v split here: colorHSV.v is the only cached brightness value,
 * matching upstream exactly.
 */
#include "MatterEndpoints/MatterColorLight.h"

namespace {
/* extended_color_light (esp_matter_endpoint.h's
 * ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID, the same 0x010D
 * MatterEnhancedColorLight.cpp uses -- see this class's own header comment
 * for why that is not a copy/paste error), chip::app::Clusters::OnOff::Id,
 * OnOff::Attributes::OnOff::Id, LevelControl::Id,
 * LevelControl::Attributes::CurrentLevel::Id, ColorControl::Id and
 * ColorControl::Attributes::{CurrentHue,CurrentSaturation}::Id. Given as
 * plain integers: there is no connectedhomeip header on a host build to
 * pull the named constants from. */
const uint32_t kColorLightDeviceType = 0x010D;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kLevelControlClusterId = 0x0008;
const uint32_t kCurrentLevelAttributeId = 0x0000;
const uint32_t kColorControlClusterId = 0x0300;
const uint32_t kCurrentHueAttributeId = 0x0000;
const uint32_t kCurrentSaturationAttributeId = 0x0001;
}  // namespace

MatterColorLight::MatterColorLight() {}

MatterColorLight::~MatterColorLight() {
  end();
}

bool MatterColorLight::begin(bool initialState, espHsvColor_t _colorHSV) {
  if (!hearthDeclare(this, kColorLightDeviceType)) {
    return false;
  }
  onOffState = initialState;
  colorHSV = { _colorHSV.h, _colorHSV.s, _colorHSV.v };
  started = true;
  return true;
}

void MatterColorLight::end() {
  started = false;
}

void MatterColorLight::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState, colorHSV);
  }
}

bool MatterColorLight::setOnOff(bool newState) {
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

bool MatterColorLight::getOnOff() {
  return onOffState;
}

bool MatterColorLight::toggle() {
  return setOnOff(!onOffState);
}

bool MatterColorLight::setColorRGB(espRgbColor_t rgbColor) {
  return setColorHSV(espRgbColorToHsvColor(rgbColor));
}

espRgbColor_t MatterColorLight::getColorRGB() {
  return espHsvColorToRgbColor(colorHSV);
}

/*
 * Per-field guarded writes, one for whichever of hue/saturation/level
 * actually differ from the cache -- see the header's deviation 2. Order
 * (hue, then saturation, then level) matches
 * MatterEnhancedColorLight.cpp's own write sequence exactly, and for the
 * same load-bearing reason: this stack's mode-1 writes echo
 * attributeChangeCB synchronously (AT_MT_SPEC.md S3.8), and that callback's
 * ColorControl/LevelControl branches rebuild colorHSV from whatever is
 * ALREADY cached plus the one field that just changed. A write must land
 * (and its echo run) before the NEXT field's write is issued, or that next
 * echo would rebuild off a stale copy of a field this same call is in the
 * middle of changing.
 */
bool MatterColorLight::setColorHSV(espHsvColor_t hsvColor) {
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

espHsvColor_t MatterColorLight::getColorHSV() {
  return colorHSV;
}

MatterColorLight::operator bool() {
  return getOnOff();
}

void MatterColorLight::operator=(bool newState) {
  setOnOff(newState);
}

bool MatterColorLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      ret &= _onChangeCB(newState, colorHSV);
    }
    if (ret) {
      // no write back here: this callback is itself the notification of a
      // change already applied, and issuing a write from inside it would loop
      onOffState = newState;
    }
  } else if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    /* No separate brightness field or callback here (see the header): a
     * level change fires the same HSV callback a hue/saturation change
     * does, exactly matching upstream's own LevelControl branch. */
    espHsvColor_t newHsv = { colorHSV.h, colorHSV.s, val->val.u8 };
    if (_onChangeColorCB != NULL) {
      ret &= _onChangeColorCB(newHsv);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(onOffState, newHsv);
    }
    if (ret) {
      colorHSV.v = val->val.u8;
    }
  } else if (cluster_id == kColorControlClusterId) {
    if (attribute_id == kCurrentHueAttributeId || attribute_id == kCurrentSaturationAttributeId) {
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
        ret &= _onChangeCB(onOffState, newHsv);
      }
      if (ret) {
        // no write back here, same reasoning as the on/off branch above
        colorHSV = { newHsv.h, newHsv.s, newHsv.v };
      }
    }
  }
  return ret;
}

esp_matter_val_type_t MatterColorLight::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kLevelControlClusterId && attribute_id == kCurrentLevelAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  if (cluster_id == kColorControlClusterId && (attribute_id == kCurrentHueAttributeId || attribute_id == kCurrentSaturationAttributeId)) {
    return ESP_MATTER_VAL_TYPE_UINT8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

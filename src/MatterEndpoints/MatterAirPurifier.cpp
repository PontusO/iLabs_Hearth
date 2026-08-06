/*
 * MatterAirPurifier.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, the on/off view over FanMode, and
 * the cache-only PercentSetting/PercentCurrent coupling). The body below is
 * MatterFan.cpp's, unchanged except for the class name and the device type
 * constant.
 */
#include "MatterEndpoints/MatterAirPurifier.h"

namespace {
/* air_purifier (esp_matter_endpoint.h:102,
 * ESP_MATTER_AIR_PURIFIER_DEVICE_TYPE_ID), chip::app::Clusters::
 * FanControl::Id, and FanControl::Attributes::
 * {FanMode,PercentSetting,PercentCurrent}::Id. Given as plain integers:
 * there is no connectedhomeip header on a host build to pull the named
 * constants from. */
const uint32_t kAirPurifierDeviceType = 0x002D;
const uint32_t kFanControlClusterId = 0x0202;
const uint32_t kFanModeAttributeId = 0x0000;
const uint32_t kPercentSettingAttributeId = 0x0002;
const uint32_t kPercentCurrentAttributeId = 0x0003;
}  // namespace

const char *MatterAirPurifier::fanModeString[7] = { "OFF", "LOW", "MEDIUM", "HIGH", "ON", "AUTO", "SMART" };
const uint8_t MatterAirPurifier::fanModeSequence[6] = {
  fanSeqModeOffLowMedHigh, fanSeqModeOffLowHigh, fanSeqModeOffLowMedHighAuto,
  fanSeqModeOffLowHighAuto, fanSeqModeOffHighAuto, fanSeqModeOffHigh
};

MatterAirPurifier::MatterAirPurifier() {}

MatterAirPurifier::~MatterAirPurifier() {
  end();
}

bool MatterAirPurifier::begin(uint8_t percent, FanMode_t fanMode, FanModeSequence_t fanModeSeq) {
  if (!hearthDeclare(this, kAirPurifierDeviceType)) {
    return false;
  }
  currentPercent = percent;
  currentFanMode = fanMode;
  validFanModes = fanModeSequence[fanModeSeq];
  started = true;
  return true;
}

void MatterAirPurifier::end() {
  started = false;
}

bool MatterAirPurifier::setMode(FanMode_t newMode, bool performUpdate) {
  if (!started) {
    return false;
  }
  if (currentFanMode == newMode) {
    return true;
  }
  if (!(validFanModes & (1 << newMode))) {
    return false;
  }
  esp_matter_attr_val_t val = esp_matter_enum8((uint8_t)newMode);
  bool ret = performUpdate ? updateAttributeVal(kFanControlClusterId, kFanModeAttributeId, &val)
                           : setAttributeVal(kFanControlClusterId, kFanModeAttributeId, &val);
  if (!ret) {
    return false;
  }
  currentFanMode = newMode;
  return true;
}

/*
 * setSpeedPercent()'s if/else mirrors MatterFan.cpp line for line: the
 * silent path issues TWO writes, PercentSetting then PercentCurrent, and
 * `ret` ends up holding the second call's result, not a combination of
 * both. That is upstream's own behaviour (see MatterFan.h's header
 * comment), not a bug introduced here.
 */
bool MatterAirPurifier::setSpeedPercent(uint8_t newPercent, bool performUpdate) {
  if (!started) {
    return false;
  }
  if (currentPercent == newPercent) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(newPercent);
  bool ret;
  if (performUpdate) {
    ret = updateAttributeVal(kFanControlClusterId, kPercentSettingAttributeId, &val);
  } else {
    ret = setAttributeVal(kFanControlClusterId, kPercentSettingAttributeId, &val);
    ret = setAttributeVal(kFanControlClusterId, kPercentCurrentAttributeId, &val);
  }
  if (!ret) {
    return false;
  }
  currentPercent = newPercent;
  return true;
}

bool MatterAirPurifier::setOnOff(bool newState, bool performUpdate) {
  if (!started) {
    return false;
  }
  if (getOnOff() == newState) {
    return true;
  }
  FanMode_t newMode = newState ? FAN_MODE_ON : FAN_MODE_OFF;
  return setMode(newMode, performUpdate);
}

bool MatterAirPurifier::getOnOff() {
  return currentFanMode != FAN_MODE_OFF;
}

bool MatterAirPurifier::toggle(bool performUpdate) {
  return setOnOff(!getOnOff(), performUpdate);
}

bool MatterAirPurifier::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kFanControlClusterId) {
    return ret;
  }
  if (attribute_id == kFanModeAttributeId) {
    FanMode_t newMode = (FanMode_t)val->val.u8;
    if (_onChangeModeCB != NULL) {
      ret &= _onChangeModeCB(newMode);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newMode, currentPercent);
    }
    if (ret) {
      currentFanMode = newMode;
    }
  } else if (attribute_id == kPercentSettingAttributeId || attribute_id == kPercentCurrentAttributeId) {
    uint8_t newPercent = val->val.u8;
    if (_onChangeSpeedCB != NULL) {
      ret &= _onChangeSpeedCB(newPercent);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(currentFanMode, newPercent);
    }
    if (ret) {
      /* Cache-only coupling: PercentSetting and PercentCurrent collapse to
       * this one field regardless of which attribute the URC named. No
       * write-back; see the header comment. */
      currentPercent = newPercent;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterAirPurifier::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kFanControlClusterId) {
    if (attribute_id == kFanModeAttributeId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kPercentSettingAttributeId || attribute_id == kPercentCurrentAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT8;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

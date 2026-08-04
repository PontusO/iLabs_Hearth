/*
 * MatterFan.cpp - implementation. See the header for the two documented
 * deviations from a literal transcription of upstream's .cpp (the on/off
 * view over FanMode, which is upstream's own design and not a deviation at
 * all; and the no-write-back rule that turns attributeChangeCB's
 * PercentSetting/PercentCurrent mirroring into a cache-only operation).
 *
 * setMode()/setSpeedPercent() also skip upstream's own read-before-write
 * via getAttributeVal(): upstream calls it immediately after the
 * currentFanMode/currentPercent equality check that already short-circuits
 * a no-op call, so the two checks are redundant in upstream's own process
 * -local attribute store. On this stack getAttributeVal() is a real
 * AT+MTATTR read command; every sibling class in this library (see
 * MatterDimmablePlugin.cpp) relies solely on the cache equality check with
 * no read-before-write at all, and this class follows that established
 * convention rather than adding a redundant wire round trip upstream could
 * only afford because it never left the process.
 */
#include "MatterEndpoints/MatterFan.h"

namespace {
/* fan (esp_matter_endpoint.h's ESP_MATTER_FAN_DEVICE_TYPE_ID),
 * chip::app::Clusters::FanControl::Id, and
 * FanControl::Attributes::{FanMode,PercentSetting,PercentCurrent}::Id.
 * Given as plain integers: there is no connectedhomeip header on a host
 * build to pull the named constants from. */
const uint32_t kFanDeviceType = 0x002B;
const uint32_t kFanControlClusterId = 0x0202;
const uint32_t kFanModeAttributeId = 0x0000;
const uint32_t kPercentSettingAttributeId = 0x0002;
const uint32_t kPercentCurrentAttributeId = 0x0003;
}  // namespace

const char *MatterFan::fanModeString[7] = { "OFF", "LOW", "MEDIUM", "HIGH", "ON", "AUTO", "SMART" };
const uint8_t MatterFan::fanModeSequence[6] = {
  fanSeqModeOffLowMedHigh, fanSeqModeOffLowHigh, fanSeqModeOffLowMedHighAuto,
  fanSeqModeOffLowHighAuto, fanSeqModeOffHighAuto, fanSeqModeOffHigh
};

MatterFan::MatterFan() {}

MatterFan::~MatterFan() {
  end();
}

bool MatterFan::begin(uint8_t percent, FanMode_t fanMode, FanModeSequence_t fanModeSeq) {
  if (!hearthDeclare(this, kFanDeviceType)) {
    return false;
  }
  currentPercent = percent;
  currentFanMode = fanMode;
  validFanModes = fanModeSequence[fanModeSeq];
  started = true;
  return true;
}

void MatterFan::end() {
  started = false;
}

bool MatterFan::setMode(FanMode_t newMode, bool performUpdate) {
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
 * both. That is upstream's own behaviour (see the header comment), not a
 * bug introduced here.
 */
bool MatterFan::setSpeedPercent(uint8_t newPercent, bool performUpdate) {
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

bool MatterFan::setOnOff(bool newState, bool performUpdate) {
  if (!started) {
    return false;
  }
  if (getOnOff() == newState) {
    return true;
  }
  FanMode_t newMode = newState ? FAN_MODE_ON : FAN_MODE_OFF;
  return setMode(newMode, performUpdate);
}

bool MatterFan::getOnOff() {
  return currentFanMode != FAN_MODE_OFF;
}

bool MatterFan::toggle(bool performUpdate) {
  return setOnOff(!getOnOff(), performUpdate);
}

bool MatterFan::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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

esp_matter_val_type_t MatterFan::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
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

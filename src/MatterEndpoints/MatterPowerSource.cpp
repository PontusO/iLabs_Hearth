/*
 * MatterPowerSource.cpp - implementation. See the header for the quoted
 * pinned-header source of every transcribed constant, the plain-AT+MTATTR
 * design rationale, and the BatPercentRemaining half-percent/clamp
 * reasoning.
 */
#include "MatterEndpoints/MatterPowerSource.h"

namespace {
/* power_source (ESP_MATTER_POWER_SOURCE_DEVICE_TYPE_ID), PowerSource::Id,
 * and PowerSource::Attributes::{BatPercentRemaining,BatChargeLevel,
 * BatReplacementNeeded}::Id. See MatterPowerSource.h's header comment for
 * the quoted lines from the pinned esp-matter checkout's generated
 * headers. Given as plain integers: there is no connectedhomeip header on
 * a host build to pull the named constants from. */
const uint32_t kPowerSourceDeviceType = 0x0011;
const uint32_t kPowerSourceClusterId = 0x002F;  // 47 decimal
const uint32_t kBatPercentRemainingAttributeId = 0x000C;
const uint32_t kBatChargeLevelAttributeId = 0x000E;
const uint32_t kBatReplacementNeededAttributeId = 0x000F;
}  // namespace

MatterPowerSource::MatterPowerSource() {}

MatterPowerSource::~MatterPowerSource() {
  end();
}

bool MatterPowerSource::begin() {
  if (!hearthDeclare(this, kPowerSourceDeviceType)) {
    return false;
  }
  batChargeLevel = 0;
  batPercentRemainingWire = 0;
  batReplacementNeeded = false;
  started = true;
  return true;
}

void MatterPowerSource::end() {
  started = false;
}

bool MatterPowerSource::setBatChargeLevel(uint8_t lvl) {
  if (!started) {
    return false;
  }
  if (batChargeLevel == lvl) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8(lvl);
  if (!updateAttributeVal(kPowerSourceClusterId, kBatChargeLevelAttributeId, &val)) {
    return false;  // cache untouched on a failed write
  }
  batChargeLevel = lvl;
  return true;
}

/*
 * percent is clamped to 0..100 before doubling: a double outside a
 * uint8_t's representable range is undefined behaviour to cast directly
 * (header comment), not merely a value the firmware would otherwise
 * reject with +MTERR:1. + 0.5 before truncation rounds to the nearest
 * half-percent wire step rather than always rounding down.
 */
bool MatterPowerSource::setBatPercentRemaining(double percent) {
  if (!started) {
    return false;
  }
  if (percent < 0.0) {
    percent = 0.0;
  } else if (percent > 100.0) {
    percent = 100.0;
  }
  uint8_t wire = (uint8_t)(percent * 2.0 + 0.5);
  if (batPercentRemainingWire == wire) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint8(wire);
  if (!updateAttributeVal(kPowerSourceClusterId, kBatPercentRemainingAttributeId, &val)) {
    return false;  // cache untouched on a failed write
  }
  batPercentRemainingWire = wire;
  return true;
}

bool MatterPowerSource::setBatReplacementNeeded(bool v) {
  if (!started) {
    return false;
  }
  if (batReplacementNeeded == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(v);
  if (!updateAttributeVal(kPowerSourceClusterId, kBatReplacementNeededAttributeId, &val)) {
    return false;  // cache untouched on a failed write
  }
  batReplacementNeeded = v;
  return true;
}

/*
 * +MTATTR-driven cache update: a controller or the firmware itself changed
 * one of the three attributes out from under this host, and the generic
 * dispatch (Hearth.cpp's hearthDispatchAttr()) routes it here via the base
 * class's attributeChangeCB() contract, the same shape as
 * MatterAirQualitySensor's own handling.
 */
bool MatterPowerSource::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id != kPowerSourceClusterId || val == nullptr) {
    return true;
  }
  if (attribute_id == kBatChargeLevelAttributeId) {
    batChargeLevel = val->val.u8;
  } else if (attribute_id == kBatPercentRemainingAttributeId) {
    batPercentRemainingWire = val->val.u8;
  } else if (attribute_id == kBatReplacementNeededAttributeId) {
    batReplacementNeeded = val->val.b;
  }
  return true;
}

esp_matter_val_type_t MatterPowerSource::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kPowerSourceClusterId) {
    if (attribute_id == kBatChargeLevelAttributeId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kBatPercentRemainingAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT8;
    }
    if (attribute_id == kBatReplacementNeededAttributeId) {
      return ESP_MATTER_VAL_TYPE_BOOLEAN;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

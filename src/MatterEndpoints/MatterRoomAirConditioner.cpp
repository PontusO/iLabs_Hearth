/*
 * MatterRoomAirConditioner.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, no validation on any setter,
 * setLocalTemperature() has no getter, dead-front is documentation only).
 */
#include "MatterEndpoints/MatterRoomAirConditioner.h"

namespace {
/* room_air_conditioner (esp_matter_endpoint.h:79,
 * ESP_MATTER_ROOM_AIR_CONDITIONER_DEVICE_TYPE_ID), chip::app::Clusters::
 * OnOff::Id, OnOff::Attributes::OnOff::Id, chip::app::Clusters::
 * Thermostat::Id and Thermostat::Attributes::{LocalTemperature,
 * OccupiedCoolingSetpoint,OccupiedHeatingSetpoint,SystemMode}::Id. Given as
 * plain integers: there is no connectedhomeip header on a host build to pull
 * the named constants from. See the header comment for the exact
 * file:line evidence each one was reverified against. */
const uint32_t kRoomAirConditionerDeviceType = 0x0072;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kThermostatClusterId = 0x0201;
const uint32_t kLocalTemperatureAttributeId = 0x0000;
const uint32_t kOccupiedCoolingSetpointAttributeId = 0x0011;
const uint32_t kOccupiedHeatingSetpointAttributeId = 0x0012;
const uint32_t kSystemModeAttributeId = 0x001C;
}  // namespace

MatterRoomAirConditioner::MatterRoomAirConditioner() {}

MatterRoomAirConditioner::~MatterRoomAirConditioner() {
  end();
}

bool MatterRoomAirConditioner::begin(bool on) {
  if (!hearthDeclare(this, kRoomAirConditionerDeviceType)) {
    return false;
  }
  onOffState = on;
  localTemperature = 2000;            // 20C local temperature
  coolingSetpointTemperature = 2400;  // 24C cooling setpoint
  heatingSetpointTemperature = 1600;  // 16C heating setpoint
  systemMode = 0;                     // SystemModeEnum::kOff
  started = true;
  return true;
}

void MatterRoomAirConditioner::end() {
  started = false;
}

bool MatterRoomAirConditioner::setOnOff(bool on) {
  if (!started) {
    return false;
  }
  if (onOffState == on) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(on);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  onOffState = on;
  return true;
}

bool MatterRoomAirConditioner::getOnOff() {
  return onOffState;
}

bool MatterRoomAirConditioner::setLocalTemperature(double c) {
  int16_t rawValue = static_cast<int16_t>(c * 100.0f);
  if (!started) {
    return false;
  }
  if (localTemperature == rawValue) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(rawValue);
  if (!updateAttributeVal(kThermostatClusterId, kLocalTemperatureAttributeId, &val)) {
    return false;
  }
  localTemperature = rawValue;
  return true;
}

bool MatterRoomAirConditioner::setCoolingSetpoint(double c) {
  int16_t rawValue = static_cast<int16_t>(c * 100.0f);
  if (!started) {
    return false;
  }
  if (coolingSetpointTemperature == rawValue) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(rawValue);
  if (!updateAttributeVal(kThermostatClusterId, kOccupiedCoolingSetpointAttributeId, &val)) {
    return false;
  }
  coolingSetpointTemperature = rawValue;
  return true;
}

double MatterRoomAirConditioner::getCoolingSetpoint() {
  return coolingSetpointTemperature / 100.0;
}

bool MatterRoomAirConditioner::setHeatingSetpoint(double c) {
  int16_t rawValue = static_cast<int16_t>(c * 100.0f);
  if (!started) {
    return false;
  }
  if (heatingSetpointTemperature == rawValue) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(rawValue);
  if (!updateAttributeVal(kThermostatClusterId, kOccupiedHeatingSetpointAttributeId, &val)) {
    return false;
  }
  heatingSetpointTemperature = rawValue;
  return true;
}

double MatterRoomAirConditioner::getHeatingSetpoint() {
  return heatingSetpointTemperature / 100.0;
}

bool MatterRoomAirConditioner::setMode(uint8_t systemModeValue) {
  if (!started) {
    return false;
  }
  if (systemMode == systemModeValue) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8(systemModeValue);
  if (!updateAttributeVal(kThermostatClusterId, kSystemModeAttributeId, &val)) {
    return false;
  }
  systemMode = systemModeValue;
  return true;
}

uint8_t MatterRoomAirConditioner::getMode() {
  return systemMode;
}

bool MatterRoomAirConditioner::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId()) {
    return true;
  }
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    onOffState = val->val.b;
  } else if (cluster_id == kThermostatClusterId) {
    if (attribute_id == kLocalTemperatureAttributeId) {
      localTemperature = val->val.i16;
    } else if (attribute_id == kOccupiedCoolingSetpointAttributeId) {
      coolingSetpointTemperature = val->val.i16;
      if (_onChangeCoolingSetpointCB != NULL) {
        _onChangeCoolingSetpointCB((double)coolingSetpointTemperature / 100.0);
      }
    } else if (attribute_id == kOccupiedHeatingSetpointAttributeId) {
      heatingSetpointTemperature = val->val.i16;
      if (_onChangeHeatingSetpointCB != NULL) {
        _onChangeHeatingSetpointCB((double)heatingSetpointTemperature / 100.0);
      }
    } else if (attribute_id == kSystemModeAttributeId) {
      systemMode = val->val.u8;
      if (_onChangeModeCB != NULL) {
        _onChangeModeCB(systemMode);
      }
    }
  }
  return true;
}

esp_matter_val_type_t MatterRoomAirConditioner::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kThermostatClusterId) {
    if (attribute_id == kSystemModeAttributeId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kLocalTemperatureAttributeId || attribute_id == kOccupiedCoolingSetpointAttributeId
        || attribute_id == kOccupiedHeatingSetpointAttributeId) {
      return ESP_MATTER_VAL_TYPE_INT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

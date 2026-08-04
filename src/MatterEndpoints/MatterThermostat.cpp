/*
 * MatterThermostat.cpp - implementation. See the header for the three
 * documented deviations from a literal transcription of upstream's .cpp
 * (validate-before-declare in begin(), no read-before-write in
 * setMode()/setRawTemperature(), and setLocalTemperature() living here
 * instead of inline).
 */
#include "MatterEndpoints/MatterThermostat.h"

namespace {
/* thermostat (esp_matter_endpoint.h's ESP_MATTER_THERMOSTAT_DEVICE_TYPE_ID),
 * chip::app::Clusters::Thermostat::Id, and Thermostat::Attributes::
 * {LocalTemperature,OccupiedCoolingSetpoint,OccupiedHeatingSetpoint,
 * SystemMode}::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kThermostatDeviceType = 0x0301;
const uint32_t kThermostatClusterId = 0x0201;
const uint32_t kLocalTemperatureAttributeId = 0x0000;
const uint32_t kOccupiedCoolingSetpointAttributeId = 0x0011;
const uint32_t kOccupiedHeatingSetpointAttributeId = 0x0012;
const uint32_t kSystemModeAttributeId = 0x001C;
}  // namespace

// string helper for the THERMOSTAT MODE. Index 2 is "UNKNOWN": SystemModeEnum
// has a gap there (kAuto=1, kCool=3), see the header comment.
const char *MatterThermostat::thermostatModeString[5] = { "OFF", "AUTO", "UNKNOWN", "COOL", "HEAT" };

MatterThermostat::MatterThermostat() {}

MatterThermostat::~MatterThermostat() {
  end();
}

bool MatterThermostat::begin(ControlSequenceOfOperation_t _controlSequence, ThermostatAutoMode_t _autoMode) {
  // check if auto mode is allowed with the control sequence of operation - only allowed for Cooling & Heating
  if (_autoMode == THERMOSTAT_AUTO_MODE_ENABLED && _controlSequence != THERMOSTAT_SEQ_OP_COOLING_HEATING
      && _controlSequence != THERMOSTAT_SEQ_OP_COOLING_HEATING_REHEAT) {
    return false;
  }
  if (!hearthDeclare(this, kThermostatDeviceType)) {
    return false;
  }
  controlSequence = _controlSequence;
  autoMode = _autoMode;
  coolingSetpointTemperature = 2400;  // 24C cooling setpoint
  heatingSetpointTemperature = 1600;  // 16C heating setpoint
  localTemperature = 2000;            // 20C local temperature
  currentMode = THERMOSTAT_MODE_OFF;
  started = true;
  return true;
}

void MatterThermostat::end() {
  started = false;
}

/*
 * Reproduces MatterThermostat.cpp's setMode() switch verbatim, including
 * the COOLING/HEATING branches that read backwards at a glance (COOLING
 * sequence rejects COOL, HEATING sequence rejects HEAT). See the header
 * comment: this is upstream's own shipped behaviour, not a transcription
 * error, and test_thermostat.cpp pins it down explicitly.
 */
bool MatterThermostat::setMode(ThermostatMode_t mode) {
  if (!started) {
    return false;
  }

  if (autoMode == THERMOSTAT_AUTO_MODE_DISABLED && mode == THERMOSTAT_MODE_AUTO) {
    return false;
  }
  // check if the requested mode is valid based on the control sequence of operation
  // no restrictions for OFF mode
  if (mode != THERMOSTAT_MODE_OFF) {
    switch (controlSequence) {
      case THERMOSTAT_SEQ_OP_COOLING:
      case THERMOSTAT_SEQ_OP_COOLING_REHEAT:
        if (mode == THERMOSTAT_MODE_HEAT || mode == THERMOSTAT_MODE_AUTO) {
          break;
        }
        return false;
      case THERMOSTAT_SEQ_OP_HEATING:
      case THERMOSTAT_SEQ_OP_HEATING_REHEAT:
        if (mode == THERMOSTAT_MODE_COOL || mode == THERMOSTAT_MODE_AUTO) {
          break;
        }
        return false;
      default:
        // THERMOSTAT_SEQ_OP_COOLING_HEATING(_REHEAT): no restriction
        break;
    }
  }

  // avoid processing if there was no change
  if (currentMode == mode) {
    return true;
  }

  esp_matter_attr_val_t val = esp_matter_enum8((uint8_t)mode);
  if (!updateAttributeVal(kThermostatClusterId, kSystemModeAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  currentMode = mode;
  return true;
}

bool MatterThermostat::setRawTemperature(int16_t rawTemperature, uint32_t attribute_id, int16_t *internalValue) {
  if (!started) {
    return false;
  }
  // avoid processing if there was no change
  if (*internalValue == rawTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(rawTemperature);
  if (!updateAttributeVal(kThermostatClusterId, attribute_id, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  *internalValue = rawTemperature;
  return true;
}

bool MatterThermostat::setLocalTemperature(double temperature) {
  // stores up to 1/100th of a Celsius degree precision, matching upstream's exact conversion
  int16_t rawValue = static_cast<int16_t>(temperature * 100.0f);
  return setRawTemperature(rawValue, kLocalTemperatureAttributeId, &localTemperature);
}

/*
 * setCoolingHeatingSetpoints() mirrors MatterThermostat.cpp line for line,
 * including its wire ORDER: cooling is written before heating even though
 * heating is the function's first parameter (see
 * "if (settingCooling) {...} if (settingHeating) {...}" in upstream, in
 * that order). test_thermostat.cpp pins this ordering down explicitly.
 */
bool MatterThermostat::setCoolingHeatingSetpoints(double setpointHeatingTemperature, double setpointCoolingTemperature) {
  // at least one of the setpoints must be valid
  bool settingCooling = setpointCoolingTemperature != (double)0xffff;
  bool settingHeating = setpointHeatingTemperature != (double)0xffff;
  if (!settingCooling && !settingHeating) {
    return false;
  }
  int16_t rawHeatValue = static_cast<int16_t>(setpointHeatingTemperature * 100.0f);
  int16_t rawCoolValue = static_cast<int16_t>(setpointCoolingTemperature * 100.0f);

  // check limits for the setpoints
  if (settingHeating && (rawHeatValue < kDefaultMinHeatSetpointLimit || rawHeatValue > kDefaultMaxHeatSetpointLimit)) {
    return false;
  }
  if (settingCooling && (rawCoolValue < kDefaultMinCoolSetpointLimit || rawCoolValue > kDefaultMaxCoolSetpointLimit)) {
    return false;
  }

  // AUTO mode requires both setpoints to be valid to each other and respect the deadband
  if (currentMode == THERMOSTAT_MODE_AUTO) {
    // only setting Cooling Setpoint
    if (settingCooling && !settingHeating && rawCoolValue < (heatingSetpointTemperature + (kDefaultDeadBand * 10))) {
      return false;
    }
    // only setting Heating Setpoint
    if (!settingCooling && settingHeating && rawHeatValue > (coolingSetpointTemperature - (kDefaultDeadBand * 10))) {
      return false;
    }
    // setting both setpoints
    if (settingCooling && settingHeating && (rawCoolValue <= rawHeatValue || rawCoolValue - rawHeatValue < kDefaultDeadBand * 10.0)) {
      return false;
    }
  }

  bool ret = true;
  if (settingCooling) {
    ret &= setRawTemperature(rawCoolValue, kOccupiedCoolingSetpointAttributeId, &coolingSetpointTemperature);
  }
  if (settingHeating) {
    ret &= setRawTemperature(rawHeatValue, kOccupiedHeatingSetpointAttributeId, &heatingSetpointTemperature);
  }
  return ret;
}

bool MatterThermostat::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId() || cluster_id != kThermostatClusterId) {
    return ret;
  }

  if (attribute_id == kSystemModeAttributeId) {
    ThermostatMode_t newMode = (ThermostatMode_t)val->val.u8;
    if (_onChangeModeCB != NULL) {
      ret &= _onChangeModeCB(newMode);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      // no write back here: this callback is itself the notification of a
      // change already applied, and issuing a write from inside it would loop
      currentMode = newMode;
    }
  } else if (attribute_id == kLocalTemperatureAttributeId) {
    int16_t newTemperature = val->val.i16;
    if (_onChangeTemperatureCB != NULL) {
      ret &= _onChangeTemperatureCB((float)newTemperature / 100.0f);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      localTemperature = newTemperature;
    }
  } else if (attribute_id == kOccupiedCoolingSetpointAttributeId) {
    int16_t newTemperature = val->val.i16;
    if (_onChangeCoolingSetpointCB != NULL) {
      ret &= _onChangeCoolingSetpointCB((double)newTemperature / 100.0);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      coolingSetpointTemperature = newTemperature;
    }
  } else if (attribute_id == kOccupiedHeatingSetpointAttributeId) {
    int16_t newTemperature = val->val.i16;
    if (_onChangeHeatingSetpointCB != NULL) {
      ret &= _onChangeHeatingSetpointCB((double)newTemperature / 100.0);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB();
    }
    if (ret) {
      heatingSetpointTemperature = newTemperature;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterThermostat::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
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

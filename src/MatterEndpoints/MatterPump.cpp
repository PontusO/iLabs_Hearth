/*
 * MatterPump.cpp - implementation. See the header for the design notes
 * (begin() issues no AT traffic, MaxPressure/MaxSpeed/MaxFlow have no
 * getters and are not wired into attributeChangeCB, Effective* are
 * URC-only with no setter).
 */
#include "MatterEndpoints/MatterPump.h"

namespace {
/* pump (esp_matter_endpoint.h:108, ESP_MATTER_PUMP_DEVICE_TYPE_ID),
 * chip::app::Clusters::OnOff::Id, OnOff::Attributes::OnOff::Id,
 * chip::app::Clusters::PumpConfigurationAndControl::Id and
 * PumpConfigurationAndControl::Attributes::{MaxPressure,MaxSpeed,MaxFlow,
 * EffectiveOperationMode,EffectiveControlMode,OperationMode}::Id. Given as
 * plain integers: there is no connectedhomeip header on a host build to
 * pull the named constants from. See the header comment for the exact
 * file:line evidence each one was reverified against. */
const uint32_t kPumpDeviceType = 0x0303;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
const uint32_t kPumpConfigClusterId = 0x0200;
const uint32_t kMaxPressureAttributeId = 0x0000;
const uint32_t kMaxSpeedAttributeId = 0x0001;
const uint32_t kMaxFlowAttributeId = 0x0002;
const uint32_t kEffectiveOperationModeAttributeId = 0x0011;
const uint32_t kEffectiveControlModeAttributeId = 0x0012;
const uint32_t kOperationModeAttributeId = 0x0020;
}  // namespace

MatterPump::MatterPump() {}

MatterPump::~MatterPump() {
  end();
}

bool MatterPump::begin(bool on) {
  if (!hearthDeclare(this, kPumpDeviceType)) {
    return false;
  }
  onOffState = on;
  operationMode = 0;
  maxPressure = 0;
  maxSpeed = 0;
  maxFlow = 0;
  effectiveOperationMode = 0;
  effectiveControlMode = 0;
  started = true;
  return true;
}

void MatterPump::end() {
  started = false;
}

bool MatterPump::setOnOff(bool on) {
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

bool MatterPump::getOnOff() {
  return onOffState;
}

bool MatterPump::setOperationMode(uint8_t m) {
  if (!started) {
    return false;
  }
  if (operationMode == m) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8(m);
  if (!updateAttributeVal(kPumpConfigClusterId, kOperationModeAttributeId, &val)) {
    return false;
  }
  operationMode = m;
  return true;
}

uint8_t MatterPump::getOperationMode() {
  return operationMode;
}

bool MatterPump::setMaxPressure(int16_t v) {
  if (!started) {
    return false;
  }
  if (maxPressure == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(v);
  if (!updateAttributeVal(kPumpConfigClusterId, kMaxPressureAttributeId, &val)) {
    return false;
  }
  maxPressure = v;
  return true;
}

bool MatterPump::setMaxSpeed(uint16_t v) {
  if (!started) {
    return false;
  }
  if (maxSpeed == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(v);
  if (!updateAttributeVal(kPumpConfigClusterId, kMaxSpeedAttributeId, &val)) {
    return false;
  }
  maxSpeed = v;
  return true;
}

bool MatterPump::setMaxFlow(uint16_t v) {
  if (!started) {
    return false;
  }
  if (maxFlow == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(v);
  if (!updateAttributeVal(kPumpConfigClusterId, kMaxFlowAttributeId, &val)) {
    return false;
  }
  maxFlow = v;
  return true;
}

uint8_t MatterPump::getEffectiveOperationMode() {
  return effectiveOperationMode;
}

uint8_t MatterPump::getEffectiveControlMode() {
  return effectiveControlMode;
}

bool MatterPump::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  if (!started) {
    return false;
  }
  if (endpoint_id != getEndPointId()) {
    return true;
  }
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    onOffState = val->val.b;
  } else if (cluster_id == kPumpConfigClusterId) {
    if (attribute_id == kOperationModeAttributeId) {
      operationMode = val->val.u8;
    } else if (attribute_id == kEffectiveOperationModeAttributeId) {
      effectiveOperationMode = val->val.u8;
    } else if (attribute_id == kEffectiveControlModeAttributeId) {
      effectiveControlMode = val->val.u8;
    }
    /* MaxPressure/MaxSpeed/MaxFlow: deliberately not handled here. See the
     * header comment: they are Read-only per the Matter spec, so no genuine
     * controller-driven URC for them exists, and there is no public getter
     * to feed. */
  }
  return true;
}

esp_matter_val_type_t MatterPump::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  if (cluster_id == kPumpConfigClusterId) {
    if (attribute_id == kOperationModeAttributeId || attribute_id == kEffectiveOperationModeAttributeId
        || attribute_id == kEffectiveControlModeAttributeId) {
      return ESP_MATTER_VAL_TYPE_ENUM8;
    }
    if (attribute_id == kMaxPressureAttributeId) {
      return ESP_MATTER_VAL_TYPE_INT16;
    }
    if (attribute_id == kMaxSpeedAttributeId || attribute_id == kMaxFlowAttributeId) {
      return ESP_MATTER_VAL_TYPE_UINT16;
    }
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

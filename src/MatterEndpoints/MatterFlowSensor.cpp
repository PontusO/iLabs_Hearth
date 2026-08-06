/*
 * MatterFlowSensor.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, this is a read-direction endpoint so
 * attributeChangeCB does not write back, and the raw value is an unsigned
 * 16-bit passthrough).
 *
 * Follows Task 6/7's established pattern (MatterPressureSensor,
 * MatterTemperatureSensor, MatterHumiditySensor): comparing against the
 * cached rawMeasuredValue and writing only on an actual change, gating the
 * cache update on the write succeeding, matching every other Hearth
 * endpoint's setter.
 */
#include "MatterEndpoints/MatterFlowSensor.h"

namespace {
/* flow_sensor (esp_matter_endpoint.h:75), chip::app::Clusters::
 * FlowMeasurement::Id and FlowMeasurement::Attributes::MeasuredValue::Id.
 * Given as plain integers: there is no connectedhomeip header on a host
 * build to pull the named constants from. */
const uint32_t kFlowSensorDeviceType = 0x0306;
const uint32_t kFlowMeasurementClusterId = 0x0404;
const uint32_t kMeasuredValueAttributeId = 0x0000;
}  // namespace

MatterFlowSensor::MatterFlowSensor() {}

MatterFlowSensor::~MatterFlowSensor() {
  end();
}

bool MatterFlowSensor::begin(uint16_t rawValue) {
  if (!hearthDeclare(this, kFlowSensorDeviceType)) {
    return false;
  }
  rawMeasuredValue = rawValue;
  started = true;
  return true;
}

void MatterFlowSensor::end() {
  started = false;
}

bool MatterFlowSensor::setRawMeasuredValue(uint16_t v) {
  if (!started) {
    return false;
  }
  /* avoid a write if there was no change */
  if (rawMeasuredValue == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(v);
  if (!updateAttributeVal(kFlowMeasurementClusterId, kMeasuredValueAttributeId, &val)) {
    return false;  /* the cache is left untouched: the device's idea of the
                    * state and the host's idea of it must not diverge */
  }
  rawMeasuredValue = v;
  return true;
}

/*
 * This endpoint is read-direction: the sketch pushes readings up to the
 * fabric. When attributeChangeCB is called (which happens on
 * controller-driven changes), we update the cache. There is no user
 * callback here: the brief's API for this class has no onChange, unlike
 * MatterPressureSensor/MatterHumiditySensor's Hearth additions.
 */
bool MatterFlowSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id == kFlowMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    if (val) {
      rawMeasuredValue = val->val.u16;
    }
  }
  return true;
}

esp_matter_val_type_t MatterFlowSensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kFlowMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

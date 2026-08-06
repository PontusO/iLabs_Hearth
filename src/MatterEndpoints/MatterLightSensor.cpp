/*
 * MatterLightSensor.cpp - implementation. See the header for the design
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
#include "MatterEndpoints/MatterLightSensor.h"

namespace {
/* light_sensor (esp_matter_endpoint.h:71), chip::app::Clusters::
 * IlluminanceMeasurement::Id and IlluminanceMeasurement::Attributes::
 * MeasuredValue::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kLightSensorDeviceType = 0x0106;
const uint32_t kIlluminanceMeasurementClusterId = 0x0400;
const uint32_t kMeasuredValueAttributeId = 0x0000;
}  // namespace

MatterLightSensor::MatterLightSensor() {}

MatterLightSensor::~MatterLightSensor() {
  end();
}

bool MatterLightSensor::begin(uint16_t rawValue) {
  if (!hearthDeclare(this, kLightSensorDeviceType)) {
    return false;
  }
  rawMeasuredValue = rawValue;
  started = true;
  return true;
}

void MatterLightSensor::end() {
  started = false;
}

bool MatterLightSensor::setRawMeasuredValue(uint16_t v) {
  if (!started) {
    return false;
  }
  /* avoid a write if there was no change */
  if (rawMeasuredValue == v) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(v);
  if (!updateAttributeVal(kIlluminanceMeasurementClusterId, kMeasuredValueAttributeId, &val)) {
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
bool MatterLightSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id == kIlluminanceMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    if (val) {
      rawMeasuredValue = val->val.u16;
    }
  }
  return true;
}

esp_matter_val_type_t MatterLightSensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kIlluminanceMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * MatterTemperatureSensor.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, this is a read-direction
 * endpoint so attributeChangeCB does nothing but must still never write
 * back, and signedness of the raw value is load-bearing).
 *
 * Deviates from upstream's own .cpp in one respect: upstream's
 * setRawTemperature() queries the live attribute from the data model
 * (attribute::get_val) before comparing and writing, because on real
 * hardware that live value is the actual source of truth. A host with no
 * data model of its own has nothing to query there without issuing an extra
 * AT+MTATTR round trip the brief's own test does not expect; this port
 * follows Task 6/7's established pattern instead, comparing against the
 * cached rawTemperature and writing only on an actual change, gating the
 * cache update on the write succeeding, matching every other Hearth
 * endpoint's setter.
 */
#include "MatterEndpoints/MatterTemperatureSensor.h"

namespace {
/* temperature_sensor (esp_matter_endpoint.h), chip::app::Clusters::
 * TemperatureMeasurement::Id and TemperatureMeasurement::Attributes::
 * MeasuredValue::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kTemperatureSensorDeviceType = 0x0302;
const uint32_t kTemperatureMeasurementClusterId = 0x0402;
const uint32_t kMeasuredValueAttributeId = 0x0000;
}  // namespace

MatterTemperatureSensor::MatterTemperatureSensor() {}

MatterTemperatureSensor::~MatterTemperatureSensor() {
  end();
}

bool MatterTemperatureSensor::begin(int16_t _rawTemperature) {
  if (!hearthDeclare(this, kTemperatureSensorDeviceType)) {
    return false;
  }
  rawTemperature = _rawTemperature;
  started = true;
  return true;
}

void MatterTemperatureSensor::end() {
  started = false;
}

bool MatterTemperatureSensor::setRawTemperature(int16_t _rawTemperature) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (rawTemperature == _rawTemperature) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawTemperature);
  if (!updateAttributeVal(kTemperatureMeasurementClusterId, kMeasuredValueAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  rawTemperature = _rawTemperature;
  return true;
}

/*
 * This endpoint is read-direction: nothing on the fabric writes to
 * MeasuredValue in practice, so there is no user callback to fire and
 * nothing to cache here beyond what setRawTemperature already maintains.
 * Matches upstream's own implementation, which does the same (logs and
 * returns true). hearthAttrTypeFor() below still declares the real type for
 * parity, in case a sketch overrides this virtual and inspects val itself.
 */
bool MatterTemperatureSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  if (!started) {
    return false;
  }
  return true;
}

esp_matter_val_type_t MatterTemperatureSensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kTemperatureMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_INT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

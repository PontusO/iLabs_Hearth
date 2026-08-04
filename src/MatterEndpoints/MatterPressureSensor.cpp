/*
 * MatterPressureSensor.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, this is a read-direction
 * endpoint so attributeChangeCB does nothing but must still never write
 * back, and signedness of the raw value is load-bearing).
 *
 * Deviates from upstream's own .cpp in one respect: upstream's
 * setRawPressure() queries the live attribute from the data model
 * (attribute::get_val) before comparing and writing, because on real
 * hardware that live value is the actual source of truth. A host with no
 * data model of its own has nothing to query there without issuing an extra
 * AT+MTATTR round trip the brief's own test does not expect; this port
 * follows Task 6/7's established pattern instead, comparing against the
 * cached rawPressure and writing only on an actual change, gating the
 * cache update on the write succeeding, matching every other Hearth
 * endpoint's setter.
 */
#include "MatterEndpoints/MatterPressureSensor.h"

namespace {
/* pressure_sensor (esp_matter_endpoint.h), chip::app::Clusters::
 * PressureMeasurement::Id and PressureMeasurement::Attributes::
 * MeasuredValue::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kPressureSensorDeviceType = 0x0305;
const uint32_t kPressureMeasurementClusterId = 0x0403;
const uint32_t kMeasuredValueAttributeId = 0x0000;
}  // namespace

MatterPressureSensor::MatterPressureSensor() {}

MatterPressureSensor::~MatterPressureSensor() {
  end();
}

bool MatterPressureSensor::begin(int16_t _rawPressure) {
  if (!hearthDeclare(this, kPressureSensorDeviceType)) {
    return false;
  }
  rawPressure = _rawPressure;
  started = true;
  return true;
}

void MatterPressureSensor::end() {
  started = false;
}

bool MatterPressureSensor::setRawPressure(int16_t _rawPressure) {
  if (!started) {
    return false;
  }
  /* avoid a write if there was no change, matching upstream */
  if (rawPressure == _rawPressure) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_int16(_rawPressure);
  if (!updateAttributeVal(kPressureMeasurementClusterId, kMeasuredValueAttributeId, &val)) {
    return false;  /* the cache is left untouched: the device's idea of the
                    * state and the host's idea of it must not diverge */
  }
  rawPressure = _rawPressure;
  return true;
}

/*
 * This endpoint is read-direction: the sketch pushes readings up to the fabric.
 * When attributeChangeCB is called (which happens on controller-driven changes),
 * we update the cache and call the user callback if set.
 */
bool MatterPressureSensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id == kPressureMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    if (val) {
      rawPressure = val->val.i16;
      if (_onChangeCB) {
        double pressure = (double)val->val.i16;
        return _onChangeCB(pressure);
      }
    }
  }
  return true;
}

esp_matter_val_type_t MatterPressureSensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kPressureMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_INT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

/*
 * MatterHumiditySensor.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, this is a read-direction
 * endpoint so attributeChangeCB does nothing but must still never write
 * back, and the raw value is an unsigned 16-bit).
 *
 * Deviates from upstream's own .cpp in one respect: upstream's
 * setRawHumidity() queries the live attribute from the data model
 * (attribute::get_val) before comparing and writing, because on real
 * hardware that live value is the actual source of truth. A host with no
 * data model of its own has nothing to query there without issuing an extra
 * AT+MTATTR round trip the brief's own test does not expect; this port
 * follows Task 6/7's established pattern instead, comparing against the
 * cached rawHumidity and writing only on an actual change, gating the
 * cache update on the write succeeding, matching every other Hearth
 * endpoint's setter.
 */
#include "MatterEndpoints/MatterHumiditySensor.h"

namespace {
/* humidity_sensor (esp_matter_endpoint.h), chip::app::Clusters::
 * RelativeHumidityMeasurement::Id and RelativeHumidityMeasurement::Attributes::
 * MeasuredValue::Id. Given as plain integers: there is no connectedhomeip
 * header on a host build to pull the named constants from. */
const uint32_t kHumiditySensorDeviceType = 0x0307;
const uint32_t kRelativeHumidityMeasurementClusterId = 0x0405;
const uint32_t kMeasuredValueAttributeId = 0x0000;
}  // namespace

MatterHumiditySensor::MatterHumiditySensor() {}

MatterHumiditySensor::~MatterHumiditySensor() {
  end();
}

bool MatterHumiditySensor::begin(uint16_t _rawHumidity) {
  if (!hearthDeclare(this, kHumiditySensorDeviceType)) {
    return false;
  }
  if (_rawHumidity > 10000) {
    return false;
  }
  rawHumidity = _rawHumidity;
  started = true;
  return true;
}

void MatterHumiditySensor::end() {
  started = false;
}

bool MatterHumiditySensor::setRawHumidity(uint16_t _rawHumidity) {
  if (!started) {
    return false;
  }
  if (_rawHumidity > 10000) {
    return false;
  }
  /* avoid a write if there was no change, matching upstream */
  if (rawHumidity == _rawHumidity) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_uint16(_rawHumidity);
  if (!updateAttributeVal(kRelativeHumidityMeasurementClusterId, kMeasuredValueAttributeId, &val)) {
    return false;  /* the cache is left untouched: the device's idea of the
                    * state and the host's idea of it must not diverge */
  }
  rawHumidity = _rawHumidity;
  return true;
}

/*
 * This endpoint is read-direction: the sketch pushes readings up to the fabric.
 * When attributeChangeCB is called (which happens on controller-driven changes),
 * we update the cache and call the user callback if set.
 */
bool MatterHumiditySensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id == kRelativeHumidityMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    if (val) {
      rawHumidity = val->val.u16;
      if (_onChangeCB) {
        double humidity = (double)val->val.u16 / 100.0;
        return _onChangeCB(humidity);
      }
    }
  }
  return true;
}

esp_matter_val_type_t MatterHumiditySensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kRelativeHumidityMeasurementClusterId && attribute_id == kMeasuredValueAttributeId) {
    return ESP_MATTER_VAL_TYPE_UINT16;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

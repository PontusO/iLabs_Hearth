/*
 * MatterAirQualitySensor.cpp - implementation. See the header for the
 * design notes (begin() issues no AT traffic, this is a read-direction
 * endpoint so attributeChangeCB does not write back, and the attribute is
 * an enum8, esp_matter_enum8()/val->val.u8, same pattern as
 * MatterThermostat's SystemMode).
 *
 * Follows Task 6/7's established pattern: comparing against the cached
 * airQuality and writing only on an actual change, gating the cache update
 * on the write succeeding, matching every other Hearth endpoint's setter.
 */
#include "MatterEndpoints/MatterAirQualitySensor.h"

namespace {
/* air_quality_sensor (esp_matter_endpoint.h:100), chip::app::Clusters::
 * AirQuality::Id and AirQuality::Attributes::AirQuality::Id. Given as plain
 * integers: there is no connectedhomeip header on a host build to pull the
 * named constants from. */
const uint32_t kAirQualitySensorDeviceType = 0x002C;
const uint32_t kAirQualityClusterId = 0x005B;
const uint32_t kAirQualityAttributeId = 0x0000;
}  // namespace

MatterAirQualitySensor::MatterAirQualitySensor() {}

MatterAirQualitySensor::~MatterAirQualitySensor() {
  end();
}

bool MatterAirQualitySensor::begin(AirQuality_t q) {
  if (!hearthDeclare(this, kAirQualitySensorDeviceType)) {
    return false;
  }
  airQuality = q;
  started = true;
  return true;
}

void MatterAirQualitySensor::end() {
  started = false;
}

bool MatterAirQualitySensor::setAirQuality(AirQuality_t q) {
  if (!started) {
    return false;
  }
  /* avoid a write if there was no change */
  if (airQuality == q) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_enum8((uint8_t)q);
  if (!updateAttributeVal(kAirQualityClusterId, kAirQualityAttributeId, &val)) {
    return false;  /* the cache is left untouched: the device's idea of the
                    * state and the host's idea of it must not diverge */
  }
  airQuality = q;
  return true;
}

/*
 * This endpoint is read-direction: the sketch pushes readings up to the
 * fabric. When attributeChangeCB is called (which happens on
 * controller-driven changes), we update the cache. There is no user
 * callback here: the brief's API for this class has no onChange, unlike
 * MatterPressureSensor/MatterHumiditySensor's Hearth additions.
 */
bool MatterAirQualitySensor::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  if (!started) {
    return false;
  }
  if (cluster_id == kAirQualityClusterId && attribute_id == kAirQualityAttributeId) {
    if (val) {
      airQuality = (AirQuality_t)val->val.u8;
    }
  }
  return true;
}

esp_matter_val_type_t MatterAirQualitySensor::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kAirQualityClusterId && attribute_id == kAirQualityAttributeId) {
    return ESP_MATTER_VAL_TYPE_ENUM8;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

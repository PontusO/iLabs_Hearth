/*
 * MatterAirQualitySensor.h - air quality sensor. The sketch pushes readings
 * up to the fabric, nothing arrives back down.
 *
 * A Hearth-original class: no arduino-esp32 counterpart exists. See
 * MatterLightSensor.h's header comment for the full rationale (same
 * pattern: implementation modeled on MatterPressureSensor/
 * MatterTemperatureSensor, public API taken verbatim from the brief).
 * MatterThermostat's SystemMode is the nearest precedent for an enum8
 * attribute (esp_matter_enum8()/val->val.u8).
 *
 * Device type 0x002C is air_quality_sensor
 * (esp_matter_endpoint.h:100, ESP_MATTER_AIR_QUALITY_SENSOR_DEVICE_TYPE_ID).
 * Cluster 0x005B (91 decimal) is AirQuality
 * (zzz_generated/app-common/clusters/AirQuality/ClusterId.h:14,
 * `inline constexpr ClusterId Id = 0x0000005B;`). Attribute 0x0000 is the
 * AirQuality attribute itself
 * (zzz_generated/app-common/clusters/AirQuality/AttributeIds.h:20,
 * `inline constexpr AttributeId Id = 0x00000000;`), an enum8. The
 * AirQuality_t values below are transcribed from AirQualityEnum
 * (zzz_generated/app-common/clusters/AirQuality/Enums.h:32-46): kUnknown =
 * 0x00, kGood = 0x01, kFair = 0x02, kModerate = 0x03, kPoor = 0x04,
 * kVeryPoor = 0x05, kExtremelyPoor = 0x06.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"

class MatterAirQualitySensor : public MatterEndPoint {
public:
  enum AirQuality_t : uint8_t {
    kUnknown = 0,
    kGood,
    kFair,
    kModerate,
    kPoor,
    kVeryPoor,
    kExtremelyPoor
  };

  MatterAirQualitySensor();
  ~MatterAirQualitySensor();

  /* begin Matter Air Quality Sensor endpoint with an initial air quality */
  bool begin(AirQuality_t q = kUnknown);
  /* this will stop processing Air Quality Sensor Matter events */
  void end();

  /* set the reported air quality; wire write, cache updated on OK */
  bool setAirQuality(AirQuality_t q);
  /* returns the cached air quality */
  AirQuality_t getAirQuality() const {
    return airQuality;
  }

  /* this function is called by Matter internal event processor. It could be overwritten by the application, if necessary. */
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /* Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that AirQuality::Id / AirQuality::Id is an
   * enum8, so attributeChangeCB() above (and any sketch override of it)
   * receives val->val.u8 already populated with the right type. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  /* implementation keeps the air quality as reported on the wire */
  AirQuality_t airQuality = kUnknown;
};

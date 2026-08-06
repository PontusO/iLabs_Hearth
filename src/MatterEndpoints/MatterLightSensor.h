/*
 * MatterLightSensor.h - illuminance measurement sensor. The sketch pushes
 * readings up to the fabric, nothing arrives back down.
 *
 * A Hearth-original class: no arduino-esp32 counterpart exists (the ten
 * device types this task and its siblings cover are not part of upstream's
 * Matter library). The public API is therefore Hearth's own design, not a
 * verbatim port, but the *implementation* pattern (hearthDeclare in a
 * two-arg, variant-0 begin(); started/re-begin refusal through the
 * registry's +MTERR:10 convention; updateAttributeVal for the wire write;
 * a cache updated only on OK; attributeChangeCB feeding the getter from a
 * controller-driven URC; hearthAttrTypeFor for the wire's type mapping)
 * follows MatterPressureSensor and MatterTemperatureSensor, the nearest
 * siblings among the existing read-direction sensors.
 *
 * Device type 0x0106 is light_sensor
 * (esp_matter_endpoint.h:71, ESP_MATTER_LIGHT_SENSOR_DEVICE_TYPE_ID). Cluster
 * 0x0400 (1024 decimal) is IlluminanceMeasurement
 * (zzz_generated/app-common/clusters/IlluminanceMeasurement/ClusterId.h:14,
 * `inline constexpr ClusterId Id = 0x00000400;`). Attribute 0x0000 is
 * MeasuredValue
 * (zzz_generated/app-common/clusters/IlluminanceMeasurement/AttributeIds.h:20,
 * `inline constexpr AttributeId Id = 0x00000000;`), a nullable uint16 per the
 * same header's Attributes.h TypeInfo
 * (`chip::app::DataModel::Nullable<uint16_t>`). This class does not expose
 * null itself: the wire only ever carries a bare integer (see
 * MatterEndPoint.h's header comment), and the brief's API is a raw
 * passthrough, matching MatterPressureSensor/MatterTemperatureSensor's own
 * choice not to surface Matter's null encoding.
 *
 * Upstream-style convenience, documented but not implemented as API: Matter
 * defines lux = 10 ^ ((raw - 1) / 10000), so a caller wanting a float value
 * converts on its own side. No float ever crosses the wire; the raw uint16
 * passthrough given by the brief is the whole API.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"

class MatterLightSensor : public MatterEndPoint {
public:
  MatterLightSensor();
  ~MatterLightSensor();

  /* begin Matter Light Sensor endpoint with an initial raw MeasuredValue */
  bool begin(uint16_t rawValue = 0);
  /* this will stop processing Light Sensor Matter events */
  void end();

  /* set the reported raw MeasuredValue; wire write, cache updated on OK */
  bool setRawMeasuredValue(uint16_t v);
  /* returns the cached raw MeasuredValue */
  uint16_t getRawMeasuredValue() const {
    return rawMeasuredValue;
  }

  /* this function is called by Matter internal event processor. It could be overwritten by the application, if necessary. */
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /* Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that IlluminanceMeasurement::Id / MeasuredValue::Id
   * is an unsigned int16, so attributeChangeCB() above (and any sketch
   * override of it) receives val->val.u16 already populated with the right
   * type. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  /* implementation keeps the raw MeasuredValue as reported on the wire */
  uint16_t rawMeasuredValue = 0;
};

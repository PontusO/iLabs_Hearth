/*
 * MatterFlowSensor.h - flow measurement sensor. The sketch pushes readings
 * up to the fabric, nothing arrives back down.
 *
 * A Hearth-original class: no arduino-esp32 counterpart exists. See
 * MatterLightSensor.h's header comment for the full rationale (same
 * pattern: implementation modeled on MatterPressureSensor/
 * MatterTemperatureSensor, public API taken verbatim from the brief since
 * there is no upstream surface to mirror).
 *
 * Device type 0x0306 is flow_sensor
 * (esp_matter_endpoint.h:75, ESP_MATTER_FLOW_SENSOR_DEVICE_TYPE_ID). Cluster
 * 0x0404 (1028 decimal) is FlowMeasurement
 * (zzz_generated/app-common/clusters/FlowMeasurement/ClusterId.h:14,
 * `inline constexpr ClusterId Id = 0x00000404;`). Attribute 0x0000 is
 * MeasuredValue
 * (zzz_generated/app-common/clusters/FlowMeasurement/AttributeIds.h:20,
 * `inline constexpr AttributeId Id = 0x00000000;`), a nullable uint16 per the
 * same header's Attributes.h TypeInfo
 * (`chip::app::DataModel::Nullable<uint16_t>`). This class does not expose
 * null itself, matching MatterLightSensor and the earlier read-direction
 * sensors.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"

class MatterFlowSensor : public MatterEndPoint {
public:
  MatterFlowSensor();
  ~MatterFlowSensor();

  /* begin Matter Flow Sensor endpoint with an initial raw MeasuredValue */
  bool begin(uint16_t rawValue = 0);
  /* this will stop processing Flow Sensor Matter events */
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
   * the +MTATTR dispatcher that FlowMeasurement::Id / MeasuredValue::Id
   * is an unsigned int16, so attributeChangeCB() above (and any sketch
   * override of it) receives val->val.u16 already populated with the right
   * type. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  /* implementation keeps the raw MeasuredValue as reported on the wire */
  uint16_t rawMeasuredValue = 0;
};

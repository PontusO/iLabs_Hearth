/*
 * MatterHumiditySensor.h - humidity measurement sensor. The sketch pushes
 * readings up to the fabric, nothing arrives back down.
 *
 * Mirrors arduino-esp32's Matter library MatterHumiditySensor (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterHumiditySensor.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0307 is humidity_sensor, cluster 0x0405 is
 * RelativeHumidityMeasurement (attribute 0x0000, MeasuredValue, a uint16 in
 * hundredths of a percent). The same IDs upstream's .cpp reads from
 * connectedhomeip; there is no such header on a host build, so they are
 * given as plain integers in the .cpp instead.
 *
 * begin(double) delegates to the private begin(uint16_t) exactly as upstream
 * does, converting via `static_cast<uint16_t>(humidityPercent * 100.0f)`.
 * setHumidity follows the same conversion through setRawHumidity.
 *
 * DEVIATION FROM VERBATIM: three public members below are Hearth additions,
 * not present in upstream's MatterHumiditySensor.h public section.
 * `onChange(HumidityChangeCB)` (and the `_onChangeCB` member) has no
 * upstream equivalent at all: upstream's class exposes no way for a sketch
 * to learn about a controller-driven change. `setRawHumidity()` is public
 * here; upstream declares it protected. `getRawHumidity()` does not exist
 * upstream at all; it exists here only so hearthAttrTypeFor()'s doc comment
 * has something concrete to point at. Naming is provisional pending a
 * decision on whether to align it with a future upstream addition; see
 * README's "Supported device types" section. Nothing existing is renamed.
 * MatterPressureSensor carries the identical pattern (onChange, a promoted
 * setRaw*, and a new getRaw*), for the same reason.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterHumiditySensor : public MatterEndPoint {
public:
  MatterHumiditySensor();
  ~MatterHumiditySensor();
  /* begin Matter Humidity Sensor endpoint with initial float humidity percent */
  bool begin(double humidityPercent = 0.00) {
    if (humidityPercent < 0.0 || humidityPercent > 100.0) {
      return false;
    }
    return begin(static_cast<uint16_t>(humidityPercent * 100.0f));
  }
  /* this will stop processing Humidity Sensor Matter events */
  void end();

  /* set the humidity percent with 1/100th of a percent precision */
  bool setHumidity(double humidityPercent) {
    if (humidityPercent < 0.0 || humidityPercent > 100.0) {
      return false;
    }
    return setRawHumidity(static_cast<uint16_t>(humidityPercent * 100.0f));
  }
  /* returns the reported float humidity percent with 1/100th of precision */
  double getHumidity() {
    return (double)rawHumidity / 100.0;
  }

  /* double conversion operator */
  void operator=(double humidityPercent) {
    setHumidity(humidityPercent);
  }
  /* double conversion operator */
  operator double() {
    return (double)getHumidity();
  }

  /* this function is called by Matter internal event processor. It could be overwritten by the application, if necessary. */
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /* Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that RelativeHumidityMeasurement::Id / MeasuredValue::Id
   * is an unsigned int16, so attributeChangeCB() above (and any sketch override
   * of it) receives val->val.u16 already populated with the right type,
   * matching upstream's own MatterHumiditySensor.cpp. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  /* User callback for value changes */
  using HumidityChangeCB = std::function<bool(double humidity)>;
  void onChange(HumidityChangeCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  /* internal function to set the raw humidity value (Matter Cluster) */
  bool setRawHumidity(uint16_t _rawHumidity);
  /* for hearthAttrTypeFor() */
  uint16_t getRawHumidity() const {
    return rawHumidity;
  }

protected:
  bool started = false;
  /* implementation keeps humidity in 1/100th percent (uint16_t) normalized value */
  uint16_t rawHumidity = 0;
  bool begin(uint16_t _rawHumidity);

  /* User callback */
  HumidityChangeCB _onChangeCB = nullptr;
};

/*
 * MatterPressureSensor.h - pressure measurement sensor. The sketch pushes
 * readings up to the fabric, nothing arrives back down.
 *
 * Mirrors arduino-esp32's Matter library MatterPressureSensor (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterPressureSensor.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0305 is pressure_sensor, cluster 0x0403 is
 * PressureMeasurement (attribute 0x0000, MeasuredValue, an int16 in hPa).
 * The same IDs upstream's .cpp reads from connectedhomeip; there is no such
 * header on a host build, so they are given as plain integers in the .cpp
 * instead.
 *
 * begin(double) delegates to the private begin(int16_t) exactly as upstream
 * does, converting via `static_cast<int16_t>(pressure)`. setPressure follows
 * the same conversion through setRawPressure. Signedness matters here in a way
 * it does not for the humidity: HearthCompat.h's hearthAttrValFromLong writes
 * a negative INT16 into .val.i as a sign-extended int32_t, and two's-complement
 * plus little-endian byte order means the low 16 bits still read back correctly
 * through .val.i16, which is what an upstream sketch's own attributeChangeCB
 * override would read.
 *
 * DEVIATION FROM VERBATIM: three public members below are Hearth additions,
 * not present in upstream's MatterPressureSensor.h public section.
 * `onChange(PressureChangeCB)` (and the `_onChangeCB` member) has no
 * upstream equivalent at all: upstream's class exposes no way for a sketch
 * to learn about a controller-driven change. `setRawPressure()` is public
 * here; upstream declares it protected. `getRawPressure()` does not exist
 * upstream at all; it exists here only so hearthAttrTypeFor()'s doc comment
 * has something concrete to point at. Naming is provisional pending a
 * decision on whether to align it with a future upstream addition; see
 * README's "Supported device types" section. Nothing existing is renamed.
 * MatterHumiditySensor carries the identical pattern (onChange, a promoted
 * setRaw*, and a new getRaw*), for the same reason.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterPressureSensor : public MatterEndPoint {
public:
  MatterPressureSensor();
  ~MatterPressureSensor();
  /* begin Matter Pressure Sensor endpoint with initial float pressure */
  bool begin(double pressure = 0.00) {
    return begin(static_cast<int16_t>(pressure));
  }
  /* this will stop processing Pressure Sensor Matter events */
  void end();

  /* set the reported raw pressure in hPa */
  bool setPressure(double pressure) {
    int16_t rawValue = static_cast<int16_t>(pressure);
    return setRawPressure(rawValue);
  }
  /* returns the reported float pressure in hPa */
  double getPressure() {
    return (double)rawPressure;
  }

  /* double conversion operator */
  void operator=(double pressure) {
    setPressure(pressure);
  }
  /* double conversion operator */
  operator double() {
    return (double)getPressure();
  }

  /* this function is called by Matter internal event processor. It could be overwritten by the application, if necessary. */
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /* Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that PressureMeasurement::Id / MeasuredValue::Id
   * is a signed int16, so attributeChangeCB() above (and any sketch override
   * of it) receives val->val.i16 already populated with the right type,
   * matching upstream's own MatterPressureSensor.cpp. */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  /* User callback for value changes */
  using PressureChangeCB = std::function<bool(double pressure)>;
  void onChange(PressureChangeCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  /* internal function to set the raw pressure value (Matter Cluster) */
  bool setRawPressure(int16_t _rawPressure);
  /* for hearthAttrTypeFor() */
  int16_t getRawPressure() const {
    return rawPressure;
  }

protected:
  bool started = false;
  /* implementation keeps pressure in hPa (int16_t) normalized value */
  int16_t rawPressure = 0;
  bool begin(int16_t _rawPressure);

  /* User callback */
  PressureChangeCB _onChangeCB = nullptr;
};

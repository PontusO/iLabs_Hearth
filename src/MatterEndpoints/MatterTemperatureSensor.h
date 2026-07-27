/*
 * MatterTemperatureSensor.h - the fourth Hearth endpoint type, and the
 * first read-direction one: the sketch pushes readings up to the fabric,
 * nothing arrives back down. Contrast with the three write-direction light
 * types alongside it.
 *
 * Mirrors arduino-esp32's Matter library MatterTemperatureSensor (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterTemperatureSensor.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0302 is temperature_sensor, cluster 0x0402 is
 * TemperatureMeasurement (attribute 0x0000, MeasuredValue, an int16 in
 * hundredths of a degree Celsius, which goes negative below freezing). The
 * same IDs upstream's .cpp reads from connectedhomeip; there is no such
 * header on a host build, so they are given as plain integers in the .cpp
 * instead.
 *
 * begin(double) delegates to the private begin(int16_t) exactly as upstream
 * does, converting via `static_cast<int16_t>(temperature * 100.0f)`. setTemperature
 * follows the same conversion through setRawTemperature. Signedness matters
 * here in a way it did not for the three light classes: HearthCompat.h's
 * hearthAttrValFromLong writes a negative INT16 into .val.i as a sign-extended
 * int32_t, and two's-complement plus little-endian byte order means the low
 * 16 bits still read back correctly through .val.i16, which is what an
 * upstream sketch's own attributeChangeCB override would read.
 */
#pragma once

#include <cstddef>  // NULL, used below for parity with upstream's exact default member initializers
#include "MatterEndPoint.h"

class MatterTemperatureSensor : public MatterEndPoint {
public:
  MatterTemperatureSensor();
  ~MatterTemperatureSensor();
  // begin Matter Temperature Sensor endpoint with initial float temperature in Celsius
  bool begin(double temperature = 0.00) {
    return begin(static_cast<int16_t>(temperature * 100.0f));
  }
  // this will stop processing Temperature Sensor Matter events
  void end();

  // set the reported raw temperature
  bool setTemperature(double temperature) {
    // stores up to 1/100th Celsius precision
    int16_t rawValue = static_cast<int16_t>(temperature * 100.0f);
    return setRawTemperature(rawValue);
  }
  // returns the reported float temperature with 1/100th Celsius precision
  double getTemperature() {
    return (double)rawTemperature / 100.0;
  }

  // double conversion operator
  void operator=(double temperature) {
    setTemperature(temperature);
  }
  // double conversion operator
  operator double() {
    return (double)getTemperature();
  }

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /*
   * Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that TemperatureMeasurement::Id / MeasuredValue::Id
   * is a signed int16, so attributeChangeCB() above (and any sketch override
   * of it) receives val->val.i16 already populated with the right type,
   * matching upstream's own MatterTemperatureSensor.cpp, which never reads
   * from a controller-driven change in practice (this endpoint is
   * read-direction) but declares the type for parity regardless.
   */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  // implementation keeps temperature in 1/100th Celsius x 100 (int16_t) normalized value
  int16_t rawTemperature = 0;
  // internal function to set the raw temperature value (Matter Cluster)
  bool setRawTemperature(int16_t _rawTemperature);
  bool begin(int16_t _rawTemperature);
};

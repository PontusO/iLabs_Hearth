/*
 * MatterColorTemperatureLight.h - the third Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterColorTemperatureLight (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterColorTemperatureLight.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x010C is color_temperature_light, cluster 0x0006 is
 * OnOff (attribute 0x0000, boolean), cluster 0x0008 is LevelControl
 * (attribute 0x0000, CurrentLevel, a uint8), cluster 0x0300 is ColorControl
 * (attribute 0x0007, ColorTemperatureMireds, a uint16). The same IDs
 * upstream's .cpp reads from connectedhomeip; there is no such header on a
 * host build, so they are given as plain integers in the .cpp instead.
 *
 * Follows Task 6/7's corrected pattern exactly: begin() issues no AT
 * traffic, each setter gates the cache update on the write succeeding, and
 * attributeChangeCB never writes back (the change already arrived from a
 * +MTATTR URC; echoing it via setAttributeVal/updateAttributeVal would loop).
 * hearthAttrTypeFor() is overridden for all three (cluster, attribute) pairs
 * this class owns, so the dispatcher hands attributeChangeCB the exact
 * typed union member upstream's own implementation reads (val->val.b,
 * val->val.u8, val->val.u16), not the generic INTEGER fallback.
 */
#pragma once

#include <cstddef>  // NULL, used below for parity with upstream's exact default member initializers
#include "MatterEndPoint.h"

class MatterColorTemperatureLight : public MatterEndPoint {
public:
  static const uint8_t MAX_BRIGHTNESS = 255;
  static const uint16_t MAX_COLOR_TEMPERATURE = 500;
  static const uint16_t MIN_COLOR_TEMPERATURE = 100;

  MatterColorTemperatureLight();
  ~MatterColorTemperatureLight();
  // default initial state is off, brightness is 64 (25%) and temperature is 370 (Soft White)
  virtual bool begin(bool initialState = false, uint8_t brightness = 64, uint16_t colorTemperature = 370);
  // this will just stop processing Light Matter events
  void end();

  bool setOnOff(bool newState);  // returns true if successful
  bool getOnOff();               // returns current light state
  bool toggle();                 // returns true if successful

  bool setBrightness(uint8_t newBrightness);  // returns true if successful
  uint8_t getBrightness();                    // returns current brightness

  bool setColorTemperature(uint16_t newTemperature);  // returns true if successful
  uint16_t getColorTemperature();                     // returns current temperature

  // User Callback for whenever the Light On/Off state is changed by the Matter Controller
  using EndPointOnOffCB = std::function<bool(bool)>;
  void onChangeOnOff(EndPointOnOffCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  // User Callback for whenever the Light brightness value [0..255] is changed by the Matter Controller
  using EndPointBrightnessCB = std::function<bool(uint8_t)>;
  void onChangeBrightness(EndPointBrightnessCB onChangeCB) {
    _onChangeBrightnessCB = onChangeCB;
  }

  // User Callbqck for whenever the Light temperature value is changed by the Matter Controller
  using EndPointTemperatureCB = std::function<bool(uint16_t)>;
  void onChangeColorTemperature(EndPointTemperatureCB onChangeCB) {
    _onChangeTemperatureCB = onChangeCB;
  }

  // User Callback for whenever any parameter is changed by the Matter Controller
  using EndPointCB = std::function<bool(bool, uint8_t, uint16_t)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  // used to update the state of the light using the current Matter Light internal state
  // It is necessary to set a user callback function using onChange() to handle the physical light state
  void updateAccessory();

  operator bool();             // returns current on/off light state
  void operator=(bool state);  // turns light on or off

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /*
   * Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher the real esp_matter_val_type_t for each of the
   * three (cluster, attribute) pairs this class owns, so attributeChangeCB()
   * above (and any sketch override of it) receives val->val.b / val->val.u8
   * / val->val.u16 already populated with the right type, matching
   * upstream's own MatterColorTemperatureLight::attributeChangeCB.
   */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;             // default initial state is off, but it can be changed by begin(bool)
  uint8_t brightnessLevel = 0;         // default initial brightness is 0, but it can be changed by begin(bool, uint8_t)
  uint16_t colorTemperatureLevel = 0;  // default initial color temperature is 0, but it can be changed by begin(bool, uint8_t, uint16_t)
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointBrightnessCB _onChangeBrightnessCB = NULL;
  EndPointTemperatureCB _onChangeTemperatureCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

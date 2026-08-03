/*
 * MatterEnhancedColorLight.h - the seventeenth concrete Hearth endpoint
 * type.
 *
 * Mirrors arduino-esp32's Matter library MatterEnhancedColorLight (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterEnhancedColorLight.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included. Device type 0x010D is extended_color_light,
 * cluster 0x0006 is OnOff (attribute 0x0000, boolean), cluster 0x0008 is
 * LevelControl (attribute 0x0000, CurrentLevel, a uint8), cluster 0x0300 is
 * ColorControl (CurrentHue 0x0000, CurrentSaturation 0x0001, both uint8;
 * ColorTemperatureMireds 0x0007, uint16). IDs verified against
 * connectedhomeip's zap-generated ids/Attributes.h and ids/Clusters.h at
 * the exact 3.3.8-bundled revision, and against esp_matter_endpoint.h's
 * ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID; there is no such header
 * on a host build, so they are given as plain integers in the .cpp.
 *
 * espHsvColor_t/espRgbColor_t and the two conversions setColorRGB()/
 * getColorRGB() need come from HearthColorUtil.h, a minimal port of
 * arduino-esp32's cores/esp32/ColorFormat.{h,c} (see that file's own header
 * comment for exactly what was and was not ported).
 *
 * CurrentHue/CurrentSaturation exist on the wire because the firmware
 * creates the ColorControl cluster with the hue_saturation feature bolted
 * on; this class drives them as plain u8 writes, exactly like brightness
 * and color temperature.
 *
 * Three deviations from a literal transcription of upstream's .cpp, all
 * following Task 6/7's established pattern and documented again in the
 * .cpp:
 *
 * 1. begin() issues no AT traffic (declares only), matching every other
 *    endpoint class in this library; upstream calls
 *    ArduinoMatter::_init()/creates the endpoint here.
 * 2. setOnOff()/setBrightness()/setColorTemperature()/setColorHSV() skip
 *    upstream's read-before-write via attribute::get_val(): on this stack
 *    that is a real AT+MTATTR round trip, and every sibling class relies on
 *    the cache equality check alone (MatterFan.cpp's header comment has the
 *    same reasoning). setColorHSV() keeps upstream's per-field skip (only
 *    whichever of hue/saturation/level actually differ get written) but
 *    compares against the CACHE, not a live re-read.
 * 3. Verified, deliberately-reproduced upstream quirk, not a Hearth bug:
 *    brightnessLevel and colorHSV.v are two SEPARATE cached fields that
 *    both nominally track LevelControl's CurrentLevel, but only one code
 *    path ever writes each. setBrightness() updates brightnessLevel only;
 *    attributeChangeCB's LevelControl/CurrentLevel branch updates
 *    colorHSV.v only -- read straight out of
 *    MatterEnhancedColorLight.cpp's attributeChangeCB: "if (ret == true) {
 *    colorHSV.v = val->val.u8; }", never brightnessLevel. The two fields
 *    agree after a LOCAL setBrightness() call on this stack, because a
 *    mode-1 write's synchronous echo (AT_MT_SPEC.md S3.8) drives the
 *    attributeChangeCB branch too, converging both fields to the same
 *    value from two different lines. But a CONTROLLER-driven brightness
 *    change (an URC with no matching local setBrightness() call) leaves
 *    getBrightness() stale while getColorHSV().v is fresh --
 *    test_enhancedcolor.cpp pins exactly this case down. Reproduced
 *    exactly: parity is with upstream's real behaviour, bugs included.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "HearthColorUtil.h"
#include "MatterEndPoint.h"

class MatterEnhancedColorLight : public MatterEndPoint {
public:
  static const uint8_t MAX_BRIGHTNESS = 255;
  static const uint16_t MAX_COLOR_TEMPERATURE = 500;
  static const uint16_t MIN_COLOR_TEMPERATURE = 100;

  MatterEnhancedColorLight();
  ~MatterEnhancedColorLight();
  // default initial state is off, brightness = 25 (10%), HSV(21, 216, 25), color temperature is 454 (Warm White)
  virtual bool begin(bool initialState = false, espHsvColor_t colorHSV = { 21, 216, 25 }, uint8_t newBrightness = 25, uint16_t colorTemperature = 454);
  // this will just stop processing Light Matter events
  void end();

  bool setOnOff(bool newState);  // returns true if successful
  bool getOnOff();               // returns current light state
  bool toggle();                 // returns true if successful

  bool setColorTemperature(uint16_t newTemperature);  // returns true if successful
  uint16_t getColorTemperature();                     // returns current temperature

  bool setBrightness(uint8_t newBrightness);  // returns true if successful
  uint8_t getBrightness();                    // returns current brightness

  bool setColorRGB(espRgbColor_t rgbColor);  // returns true if successful
  espRgbColor_t getColorRGB();               // returns current RGB Color
  bool setColorHSV(espHsvColor_t hsvColor);  // returns true if successful
  espHsvColor_t getColorHSV();               // returns current HSV Color

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

  // User Callback for whenever the HSV Color value is changed by the Matter Controller
  using EndPointRGBColorCB = std::function<bool(espHsvColor_t)>;
  void onChangeColorHSV(EndPointRGBColorCB onChangeCB) {
    _onChangeColorCB = onChangeCB;
  }

  // User Callbqck for whenever the Light temperature value is changed by the Matter Controller
  using EndPointTemperatureCB = std::function<bool(uint16_t)>;
  void onChangeColorTemperature(EndPointTemperatureCB onChangeCB) {
    _onChangeTemperatureCB = onChangeCB;
  }

  // User Callback for whenever any parameter is changed by the Matter Controller
  using EndPointCB = std::function<bool(bool, espHsvColor_t, uint8_t, uint16_t)>;
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

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;             // default initial state is off, but it can be changed by begin(bool)
  uint8_t brightnessLevel = 0;         // default initial brightness is 0, but it can be changed by begin(bool, espHsvColor_t, uint8_t)
  espHsvColor_t colorHSV = { 0, 0, 0 };  // default initial color HSV is black, but it can be changed by begin(bool, espHsvColor_t)
  uint16_t colorTemperatureLevel = 0;  // default initial color temperature is 0, but it can be changed by begin(..., uint16_t)
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointBrightnessCB _onChangeBrightnessCB = NULL;
  EndPointRGBColorCB _onChangeColorCB = NULL;
  EndPointTemperatureCB _onChangeTemperatureCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

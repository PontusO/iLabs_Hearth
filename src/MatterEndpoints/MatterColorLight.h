/*
 * MatterColorLight.h - the nineteenth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterColorLight (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterColorLight.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included. Cluster 0x0006 is OnOff (attribute 0x0000,
 * boolean), cluster 0x0008 is LevelControl (attribute 0x0000, CurrentLevel,
 * a uint8), cluster 0x0300 is ColorControl (CurrentHue 0x0000, CurrentSaturation
 * 0x0001, both uint8). IDs verified the same way MatterEnhancedColorLight.h's
 * were, against connectedhomeip's zap-generated ids/Attributes.h and
 * ids/Clusters.h at the 3.3.8-bundled revision.
 *
 * Device type: upstream's own rgb_color_light::get_device_type_id() (this
 * file's paired .cpp, line ~42) returns ESP_MATTER_EXTENDED_COLOR_LIGHT_DEVICE_TYPE_ID,
 * the identical 0x010D MatterEnhancedColorLight.h uses, not a distinct
 * "color light" device type. Confirmed by reading upstream's source directly
 * rather than assumed: both classes are host-side views over the same wire
 * device type, differing only in which of its clusters/attributes each class
 * actually drives. The firmware creates one 0x010D endpoint either way (its
 * extended_color_light carries the ColorTemperatureMireds attribute this
 * class never touches); a device declared through MatterColorLight is a
 * strict subset of what the wire endpoint offers, not a different one. See
 * MatterEnhancedColorLight.h's own header comment for the ESP32-Matter
 * device-type verification path.
 *
 * espHsvColor_t/espRgbColor_t and the two conversions setColorRGB()/
 * getColorRGB() come from HearthColorUtil.h, the same port
 * MatterEnhancedColorLight.h uses; see that header's own comment for exactly
 * what was and was not ported.
 *
 * Deviations from a literal transcription of upstream's .cpp, all following
 * Task 6/7's established pattern (MatterEnhancedColorLight.h/.cpp) and
 * documented again in the .cpp:
 *
 * 1. begin() issues no AT traffic (declares only), matching every other
 *    endpoint class in this library; upstream calls
 *    ArduinoMatter::_init()/creates the endpoint here.
 * 2. setOnOff()/setColorHSV() skip upstream's read-before-write via
 *    attribute::get_val(): on this stack that is a real AT+MTATTR round
 *    trip, and every sibling class relies on the cache equality check
 *    alone. setColorHSV() keeps a per-field skip (only whichever of
 *    hue/saturation/level actually differ get written) compared against the
 *    CACHE, not a live re-read, in hue-then-saturation-then-level order:
 *    the same echo-ordering discipline documented in
 *    MatterEnhancedColorLight.cpp's own header comment (a write must land,
 *    and its synchronous mode-1 echo run, before the next field's write is
 *    issued, or that next echo rebuilds colorHSV off a stale copy of the
 *    field this same call is in the middle of changing, AT_MT_SPEC.md
 *    S3.8). None of the three writes short-circuits on another one failing,
 *    matching MatterEnhancedColorLight.cpp's own setColorHSV(): a partial
 *    failure still attempts the remaining writes, and leaves exactly the
 *    fields whose own write succeeded updated.
 *
 * Unlike MatterEnhancedColorLight, this class has no separate brightnessLevel
 * field, and therefore no equivalent of that class's documented
 * brightnessLevel/colorHSV.v divergence quirk: colorHSV.v IS the only cached
 * brightness value here, exactly as upstream has it (upstream's own header
 * declares no setBrightness()/getBrightness()/onChangeBrightness at all).
 * A controller-driven LevelControl change therefore updates colorHSV.v
 * directly and fires onChangeColorHSV/onChange, the same callbacks a
 * hue/saturation change fires; there is no separate brightness callback to
 * fire instead, matching upstream's attributeChangeCB LevelControl branch
 * exactly.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "HearthColorUtil.h"
#include "MatterEndPoint.h"

class MatterColorLight : public MatterEndPoint {
public:
  MatterColorLight();
  ~MatterColorLight();
  // default initial state is off, color is red 12% intensity HSV(0, 254, 31)
  virtual bool begin(bool initialState = false, espHsvColor_t colorHSV = { 0, 254, 31 });
  // this will just stop processing Light Matter events
  void end();

  bool setOnOff(bool newState);  // returns true if successful
  bool getOnOff();               // returns current light state
  bool toggle();                 // returns true if successful

  bool setColorRGB(espRgbColor_t rgbColor);  // returns true if successful
  espRgbColor_t getColorRGB();               // returns current RGB Color
  bool setColorHSV(espHsvColor_t hsvColor);  // returns true if successful
  espHsvColor_t getColorHSV();               // returns current HSV Color

  // User Callback for whenever the Light On/Off state is changed by the Matter Controller
  using EndPointOnOffCB = std::function<bool(bool)>;
  void onChangeOnOff(EndPointOnOffCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }
  // User Callback for whenever the HSV Color value is changed by the Matter Controller
  using EndPointRGBColorCB = std::function<bool(espHsvColor_t)>;
  void onChangeColorHSV(EndPointRGBColorCB onChangeCB) {
    _onChangeColorCB = onChangeCB;
  }

  // User Callback for whenever any parameter is changed by the Matter Controller
  using EndPointCB = std::function<bool(bool, espHsvColor_t)>;
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
  espHsvColor_t colorHSV = { 0, 0, 0 };  // default initial color HSV is black, but it can be changed by begin(bool, espHsvColor_t)
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointRGBColorCB _onChangeColorCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

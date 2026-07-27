/*
 * MatterDimmableLight.h - the second concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterDimmableLight (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterDimmableLight.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0101 is dimmable_light, cluster 0x0006 is OnOff
 * (attribute 0x0000, boolean), cluster 0x0008 is LevelControl (attribute
 * 0x0000, CurrentLevel, a uint8); the same IDs upstream's .cpp reads from
 * connectedhomeip. There is no such header on a host build, so they are
 * given as the plain integers here instead.
 *
 * Note the signature that differs from MatterOnOffLight: onChange() here
 * takes std::function<bool(bool, uint8_t)>, not std::function<bool(bool)>,
 * and there are separate onChangeOnOff/onChangeBrightness callbacks besides
 * it. This is part of the upstream parity contract, so it is reproduced
 * rather than harmonised with MatterOnOffLight's simpler signature.
 *
 * begin() calls hearthDeclare(this, 0x0101) and caches initialState and
 * brightness. It issues no AT traffic: the endpoint ID is not known until
 * Matter.begin() reconciles the declared registry against the C6, which is
 * why upstream's own examples call Matter.begin() last, after every
 * endpoint's begin().
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's hearthDispatchAttr,
 * see Task 5's report), and echoing it through setAttributeVal/
 * updateAttributeVal would be an infinite loop with the real device. This is
 * the entire reason AT+MTATTR has a silent write mode; see
 * MatterEndPoint.h's header comment.
 */
#pragma once

#include <cstddef>  // NULL, used below for parity with upstream's exact default member initializers
#include "MatterEndPoint.h"

class MatterDimmableLight : public MatterEndPoint {
public:
  static const uint8_t MAX_BRIGHTNESS = 255;

  MatterDimmableLight();
  ~MatterDimmableLight();
  // default initial state is off and brightness is 64 (25%)
  virtual bool begin(bool initialState = false, uint8_t brightness = 64);
  // this will just stop processing Light Matter events
  void end();

  bool setOnOff(bool newState);  // returns true if successful
  bool getOnOff();               // returns current light state
  bool toggle();                 // returns true if successful

  bool setBrightness(uint8_t newBrightness);  // returns true if successful
  uint8_t getBrightness();                    // returns current brightness

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

  // User Callback for whenever any parameter is changed by the Matter Controller
  using EndPointCB = std::function<bool(bool, uint8_t)>;
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
   * the +MTATTR dispatcher that OnOff::Id / OnOff::Attributes::OnOff::Id is
   * a boolean and LevelControl::Id / LevelControl::Attributes::CurrentLevel::Id
   * is a uint8, so attributeChangeCB() above (and any sketch override of it)
   * receives val->val.b / val->val.u8 already populated with the right
   * type, matching upstream's own MatterDimmableLight::attributeChangeCB,
   * which reads val->val.b and val->val.u8 respectively.
   */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;      // default initial state is off, but it can be changed by begin(bool)
  uint8_t brightnessLevel = 0;  // default initial brightness is 0, but it can be changed by begin(bool, uint8_t)
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointBrightnessCB _onChangeBrightnessCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

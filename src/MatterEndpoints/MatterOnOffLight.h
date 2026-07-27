/*
 * MatterOnOffLight.h - the first concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterOnOffLight (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterOnOffLight.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0100 is on_off_light, cluster 0x0006 is OnOff,
 * attribute 0x0000 is OnOff::Attributes::OnOff::Id
 * (chip::app::Clusters::OnOff), the same IDs upstream's .cpp reads from
 * connectedhomeip; there is no such header on a host build, so they are
 * given as the plain integers here instead.
 *
 * begin() calls hearthDeclare(this, 0x0100) and caches initialState. It
 * issues no AT traffic: the endpoint ID is not known until Matter.begin()
 * reconciles the declared registry against the C6, which is why upstream's
 * own examples call Matter.begin() last, after every endpoint's begin().
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

class MatterOnOffLight : public MatterEndPoint {
public:
  MatterOnOffLight();
  ~MatterOnOffLight();
  virtual bool begin(bool initialState = false);  // default initial state is off
  void end();                                     // this will just stop processing Light Matter events

  bool setOnOff(bool newState);  // returns true if successful
  bool getOnOff();               // returns current light state
  bool toggle();                 // returns true if successful

  // User Callback for whenever the Light state is changed by the Matter Controller
  using EndPointCB = std::function<bool(bool)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  // User Callback for whenever the Light On/Off state is changed by the Matter Controller
  void onChangeOnOff(EndPointCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  // used to update the state of the light using the current Matter Light internal state
  // It is necessary to set a user callback function using onChange() to handle the physical light state
  void updateAccessory();

  operator bool();             // returns current light state
  void operator=(bool state);  // turns light on or off

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  /*
   * Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
   * the +MTATTR dispatcher that OnOff::Id / OnOff::Attributes::OnOff::Id is
   * a boolean, so attributeChangeCB() above (and any sketch override of it)
   * receives val->val.b already populated, matching upstream's own
   * MatterOnOffLight::attributeChangeCB, which reads exactly that member.
   */
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;  // default initial state is off, but it can be changed by begin(bool)
  EndPointCB _onChangeCB = NULL;
  EndPointCB _onChangeOnOffCB = NULL;
};

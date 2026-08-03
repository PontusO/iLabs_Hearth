/*
 * MatterWaterLeakDetector.h - a boolean-state sensor.
 *
 * Mirrors arduino-esp32's Matter library MatterWaterLeakDetector (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterWaterLeakDetector.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x0043 is water_leak_detector, cluster 0x0045 is BooleanState,
 * attribute 0x0000 is BooleanState::Attributes::StateValue::Id
 * (chip::app::Clusters::BooleanState), the same IDs upstream's .cpp reads from
 * connectedhomeip; there is no such header on a host build, so they are
 * given as the plain integers here instead.
 *
 * begin() calls hearthDeclare(this, 0x0043) and caches initialState. It
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

#include <cstddef>
#include "MatterEndPoint.h"

class MatterWaterLeakDetector : public MatterEndPoint {
public:
  MatterWaterLeakDetector();
  ~MatterWaterLeakDetector();
  virtual bool begin(bool initialState = false);
  void end();

  bool setLeak(bool newState);
  bool getLeak();

  using EndPointCB = std::function<bool(bool)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void operator=(bool state);
  operator bool();

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool leakState = false;
  EndPointCB _onChangeCB = NULL;
};

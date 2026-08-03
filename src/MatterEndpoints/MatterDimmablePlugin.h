/*
 * MatterDimmablePlugin.h - the sixth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterDimmablePlugin (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterDimmablePlugin.h):
 * the public section below is reproduced verbatim, protected members
 * included. Device type 0x010B is dimmable_plugin_unit, cluster 0x0006 is OnOff
 * (attribute 0x0000, boolean), cluster 0x0008 is LevelControl (attribute
 * 0x0000, CurrentLevel, a uint8); the same IDs upstream's .cpp reads from
 * connectedhomeip. There is no such header on a host build, so they are
 * given as the plain integers here instead.
 *
 * Note the signature that differs from MatterOnOffPlugin: onChange() here
 * takes std::function<bool(bool, uint8_t)>, not std::function<bool(bool)>,
 * and there are separate onChangeOnOff/onChangeLevel callbacks besides
 * it. This is part of the upstream parity contract, so it is reproduced
 * rather than harmonised with MatterOnOffPlugin's simpler signature.
 *
 * begin() calls hearthDeclare(this, 0x010B) and caches initialState and
 * level. It issues no AT traffic: the endpoint ID is not known until
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

#include <cstddef>
#include "MatterEndPoint.h"

class MatterDimmablePlugin : public MatterEndPoint {
public:
  static const uint8_t MAX_LEVEL = 255;

  MatterDimmablePlugin();
  ~MatterDimmablePlugin();
  virtual bool begin(bool initialState = false, uint8_t level = 64);
  void end();

  bool setOnOff(bool newState);
  bool getOnOff();
  bool toggle();

  bool setLevel(uint8_t newLevel);
  uint8_t getLevel();

  using EndPointOnOffCB = std::function<bool(bool)>;
  void onChangeOnOff(EndPointOnOffCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  using EndPointLevelCB = std::function<bool(uint8_t)>;
  void onChangeLevel(EndPointLevelCB onChangeCB) {
    _onChangeLevelCB = onChangeCB;
  }

  using EndPointCB = std::function<bool(bool, uint8_t)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void updateAccessory();

  operator bool();
  void operator=(bool state);
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;
  uint8_t level = 0;
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointLevelCB _onChangeLevelCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

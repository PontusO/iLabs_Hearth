/*
 * MatterMountedDimmableLoadControl.h - actuator clone, C3 of the ten-type
 * swoop.
 *
 * Clones MatterDimmablePlugin.h's shape onto device type 0x0110,
 * mounted_dimmable_load_control (esp_matter_endpoint.h:62,
 * ESP_MATTER_MOUNTED_DIMMABLE_LOAD_CONTROL_DEVICE_TYPE_ID; the namespace at
 * line 961 aliases dimmable_light::config_t, the same OnOff+LevelControl
 * cluster pair MatterDimmablePlugin already speaks). Cluster 0x0006 is
 * OnOff (attribute 0x0000, boolean), cluster 0x0008 is LevelControl
 * (attribute 0x0000, CurrentLevel, a uint8); the same IDs upstream's .cpp
 * reads from connectedhomeip. There is no such header on a host build, so
 * they are given as the plain integers here instead.
 *
 * There is no arduino-esp32 counterpart for this device type: this is a
 * Hearth-original class on a house-shape clone, not a verbatim upstream
 * port. Structurally it mirrors MatterDimmablePlugin.h exactly (separate
 * onChangeOnOff/onChangeBrightness callbacks besides the combined onChange,
 * the same begin(bool, uint8_t) signature, the same started/re-begin
 * refusal), but the brief spells out setBrightness()/getBrightness() rather
 * than setLevel()/getLevel() for the level accessor pair. That naming
 * follows MatterDimmableLight's own precedent (dimmable_light's
 * MAX_BRIGHTNESS/setBrightness/getBrightness/onChangeBrightness, not
 * MatterDimmablePlugin's MAX_LEVEL/setLevel/getLevel/onChangeLevel): both
 * upstream classes speak the identical LevelControl::CurrentLevel wire
 * attribute under two different upstream-chosen names, and "Load Control"
 * reads naturally with the brightness terminology since the device has no
 * physical brightness of its own, only a controlled load level.
 *
 * begin() calls hearthDeclare(this, 0x0110) and caches initialState and
 * brightness. It issues no AT traffic: the endpoint ID is not known until
 * Matter.begin() reconciles the declared registry against the C6, matching
 * every other endpoint class in this library.
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's hearthDispatchAttr),
 * and echoing it through setAttributeVal/updateAttributeVal would be an
 * infinite loop with the real device. This is the entire reason AT+MTATTR
 * has a silent write mode; see MatterEndPoint.h's header comment.
 *
 * StartUpOnOff/StartUpCurrentLevel nulling (B63 discipline, mt_startup_*
 * helpers) is a firmware-side thunk concern (the device type's boot-time
 * create()), not a host library one: this class writes no StartUp*
 * attribute at all, matching MatterDimmablePlugin, which does not either.
 */
#pragma once

#include <cstddef>
#include "MatterEndPoint.h"

class MatterMountedDimmableLoadControl : public MatterEndPoint {
public:
  static const uint8_t MAX_BRIGHTNESS = 255;

  MatterMountedDimmableLoadControl();
  ~MatterMountedDimmableLoadControl();
  virtual bool begin(bool initialState = false, uint8_t brightness = 64);
  void end();

  bool setOnOff(bool newState);
  bool getOnOff();
  bool toggle();

  bool setBrightness(uint8_t newBrightness);
  uint8_t getBrightness();

  using EndPointOnOffCB = std::function<bool(bool)>;
  void onChangeOnOff(EndPointOnOffCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  using EndPointBrightnessCB = std::function<bool(uint8_t)>;
  void onChangeBrightness(EndPointBrightnessCB onChangeCB) {
    _onChangeBrightnessCB = onChangeCB;
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
  uint8_t brightnessLevel = 0;
  EndPointOnOffCB _onChangeOnOffCB = NULL;
  EndPointBrightnessCB _onChangeBrightnessCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

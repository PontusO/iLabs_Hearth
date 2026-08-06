/*
 * MatterMountedOnOffControl.h - actuator clone, C3 of the ten-type swoop.
 *
 * Clones MatterOnOffPlugin.h's public surface onto device type 0x010F,
 * mounted_on_off_control (esp_matter_endpoint.h:60,
 * ESP_MATTER_MOUNTED_ON_OFF_CONTROL_DEVICE_TYPE_ID; the namespace at line
 * 948 wraps on_off_with_lighting_config, the same OnOff cluster surface
 * MatterOnOffPlugin already speaks). Cluster 0x0006 is OnOff, attribute
 * 0x0000 is OnOff::Attributes::OnOff::Id (chip::app::Clusters::OnOff); the
 * same IDs upstream's .cpp reads from connectedhomeip. There is no such
 * header on a host build, so they are given as the plain integers here
 * instead, following every other endpoint class in this library.
 *
 * There is no arduino-esp32 counterpart for this device type: this is a
 * Hearth-original class on a house-shape clone, not a verbatim upstream
 * port. The public API is copied unchanged from MatterOnOffPlugin (see that
 * file's header comment), because the brief's own instruction is to mirror
 * MatterOnOffPlugin's surface (begin(bool)/setOnOff/getOnOff/toggle) on the
 * new device type ID.
 *
 * begin() calls hearthDeclare(this, 0x010F) and caches initialState. It
 * issues no AT traffic: the endpoint ID is not known until Matter.begin()
 * reconciles the declared registry against the C6, matching every other
 * endpoint class in this library.
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's hearthDispatchAttr),
 * and echoing it through setAttributeVal/updateAttributeVal would be an
 * infinite loop with the real device. This is the entire reason AT+MTATTR
 * has a silent write mode; see MatterEndPoint.h's header comment.
 *
 * StartUpOnOff nulling (B63 discipline, mt_startup_* helpers) is a
 * firmware-side thunk concern (the device type's boot-time create()), not a
 * host library one: this class writes no StartUp* attribute at all, matching
 * MatterOnOffPlugin, which does not either.
 */
#pragma once

#include <cstddef>
#include "MatterEndPoint.h"

class MatterMountedOnOffControl : public MatterEndPoint {
public:
  MatterMountedOnOffControl();
  ~MatterMountedOnOffControl();
  virtual bool begin(bool initialState = false);
  void end();

  bool setOnOff(bool newState);
  bool getOnOff();
  bool toggle();

  using EndPointCB = std::function<bool(bool)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void onChangeOnOff(EndPointCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  void updateAccessory();

  operator bool();
  void operator=(bool state);

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;
  EndPointCB _onChangeCB = NULL;
  EndPointCB _onChangeOnOffCB = NULL;
};

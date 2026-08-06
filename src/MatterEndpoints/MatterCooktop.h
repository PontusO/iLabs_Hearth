/*
 * MatterCooktop.h - actuator clone, C3 of the ten-type swoop, the odd one.
 *
 * Device type 0x0078 is cooktop (esp_matter_endpoint.h:122,
 * ESP_MATTER_COOKTOP_DEVICE_TYPE_ID; the namespace at line 844 wraps
 * cluster::on_off::config_t, the same OnOff cluster MatterOnOffPlugin
 * speaks). Cluster 0x0006 is OnOff, attribute 0x0000 is
 * OnOff::Attributes::OnOff::Id (chip::app::Clusters::OnOff); the same IDs
 * upstream's other OnOff-cluster classes read from connectedhomeip. There
 * is no such header on a host build, so they are given as the plain
 * integers here instead, in the .cpp.
 *
 * There is no arduino-esp32 counterpart for this device type: this is a
 * Hearth-original class, and it does NOT clone MatterOnOffPlugin/
 * MatterOnOffLight's full shape the way the other four C3 classes clone
 * their sources. A cooktop is not a remote-controllable load in the same
 * sense: turning a burner on from a phone app is the failure mode the
 * device class exists to prevent (nobody should be able to remotely start a
 * physical heating element on a stove). The spec's own decision is
 * "OffOnly": remote ON is not part of the device class, so the public
 * surface is deliberately just three methods:
 *
 *   bool begin();      // declares 0x0078; device boots Off-capable
 *   bool off();         // the one remote action, OnOff write 0
 *   bool getOnOff();    // cache fed by URCs (local turn-on shows here)
 *
 * There is no on(), no toggle(), and no operator=(bool)/operator bool()
 * write path: no public method here accepts or can be driven to write a
 * true value to the OnOff attribute. That is a structural guarantee, not a
 * runtime check: no code path in this class calls updateAttributeVal or
 * setAttributeVal with esp_matter_bool(true). The physical stove can still
 * turn itself on (a human at the cooktop, or its own internal logic) and
 * report that over a +MTATTR URC; attributeChangeCB updates the cache from
 * that URC exactly like every other endpoint class (and, following the
 * house rule, never writes back to the fabric), so getOnOff() can read true
 * even though nothing in this class ever put it there.
 *
 * begin() calls hearthDeclare(this, 0x0078) and starts the cache at false
 * ("boots Off-capable"): a fresh object has never observed the cooktop in
 * any other state. It issues no AT traffic, matching every other endpoint
 * class in this library.
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's
 * hearthDispatchAttr), and echoing it through setAttributeVal/
 * updateAttributeVal would be an infinite loop with the real device. This
 * is the entire reason AT+MTATTR has a silent write mode; see
 * MatterEndPoint.h's header comment.
 */
#pragma once

#include <cstddef>
#include "MatterEndPoint.h"

class MatterCooktop : public MatterEndPoint {
public:
  MatterCooktop();
  ~MatterCooktop();
  virtual bool begin();
  void end();

  /* the one remote action this device class allows: OnOff write 0. */
  bool off();
  bool getOnOff();

  using EndPointCB = std::function<bool(bool)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void onChangeOnOff(EndPointCB onChangeCB) {
    _onChangeOnOffCB = onChangeCB;
  }

  void updateAccessory();

  operator bool();

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;
  EndPointCB _onChangeCB = NULL;
  EndPointCB _onChangeOnOffCB = NULL;
};

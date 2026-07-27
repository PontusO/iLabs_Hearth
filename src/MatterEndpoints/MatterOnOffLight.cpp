/*
 * MatterOnOffLight.cpp - implementation. See the header for the design
 * notes (begin() issues no AT traffic, attributeChangeCB does not write
 * back).
 */
#include "MatterEndpoints/MatterOnOffLight.h"

namespace {
/* on_off_light (esp_matter_endpoint.h), chip::app::Clusters::OnOff::Id and
 * OnOff::Attributes::OnOff::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants from. */
const uint32_t kOnOffLightDeviceType = 0x0100;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterOnOffLight::MatterOnOffLight() {}

MatterOnOffLight::~MatterOnOffLight() {
  end();
}

bool MatterOnOffLight::begin(bool initialState) {
  if (!hearthDeclare(this, kOnOffLightDeviceType)) {
    return false;
  }
  onOffState = initialState;
  started = true;
  return true;
}

void MatterOnOffLight::end() {
  started = false;
}

void MatterOnOffLight::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState);
  }
}

bool MatterOnOffLight::setOnOff(bool newState) {
  if (!started) {
    return false;
  }
  // avoid a write if there was no change, matching upstream
  if (onOffState == newState) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(newState);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  onOffState = newState;
  return true;
}

bool MatterOnOffLight::getOnOff() {
  return onOffState;
}

bool MatterOnOffLight::toggle() {
  return setOnOff(!onOffState);
}

MatterOnOffLight::operator bool() {
  return getOnOff();
}

void MatterOnOffLight::operator=(bool newState) {
  setOnOff(newState);
}

/*
 * The wire never carries a type tag: hearthDispatchAttr() (Hearth.cpp)
 * always rebuilds val as ESP_MATTER_VAL_TYPE_INTEGER with the raw signed
 * value in val.i, regardless of the attribute's real type, so val->val.b
 * must not be read here. See Task 5's report, "What Tasks 6-8 need to know".
 */
bool MatterOnOffLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    bool newState = (val->val.i != 0);
    if (_onChangeOnOffCB != NULL) {
      ret &= _onChangeOnOffCB(newState);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState);
    }
    if (ret) {
      // no write back here: the change already came from the fabric
      // (mode 0 exists in AT+MTATTR precisely so this path never echoes)
      onOffState = newState;
    }
  }
  return ret;
}

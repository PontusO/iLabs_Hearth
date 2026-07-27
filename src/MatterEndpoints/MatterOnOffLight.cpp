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
 * hearthAttrTypeFor() below tells hearthDispatchAttr() (Hearth.cpp) that
 * (kOnOffClusterId, kOnOffAttributeId) is a boolean, so by the time it
 * calls this, val->val.b is the member the wire's value actually landed
 * in. Reading val->val.i here, as an earlier revision did, would read a
 * union member the dispatcher never wrote, since the type is now BOOLEAN,
 * not the generic INTEGER fallback.
 */
bool MatterOnOffLight::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  bool ret = true;
  if (!started) {
    return false;
  }
  if (endpoint_id == getEndPointId() && cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    bool newState = val->val.b;
    if (_onChangeOnOffCB != NULL) {
      ret &= _onChangeOnOffCB(newState);
    }
    if (_onChangeCB != NULL) {
      ret &= _onChangeCB(newState);
    }
    if (ret) {
      // no write back here: this callback is itself the notification of a
      // change already applied (from a controller, or from this host's own
      // write echo), and issuing a write from inside it would loop
      onOffState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterOnOffLight::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

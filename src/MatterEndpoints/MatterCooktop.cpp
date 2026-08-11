/*
 * MatterCooktop.cpp - implementation. See the header for the OffOnly
 * design: no method here accepts or can produce a true value on the wire.
 * off() is the only setter and it always writes esp_matter_bool(false).
 */
#include "MatterEndpoints/MatterCooktop.h"

namespace {
/* cooktop (esp_matter_endpoint.h:122, ESP_MATTER_COOKTOP_DEVICE_TYPE_ID)
 * and cook_surface (ESP_MATTER_COOK_SURFACE_DEVICE_TYPE_ID, AT_MT_SPEC.md
 * S3.9's device type table), chip::app::Clusters::OnOff::Id and
 * OnOff::Attributes::OnOff::Id. Given as plain integers: there is no
 * connectedhomeip header on a host build to pull the named constants
 * from. */
const uint32_t kCooktopDeviceType = 0x0078;
const uint32_t kCookSurfaceDeviceType = 0x0077;
const uint32_t kOnOffClusterId = 0x0006;
const uint32_t kOnOffAttributeId = 0x0000;
}  // namespace

MatterCooktop::MatterCooktop() {
  /* The reject surface: owned AND inert, so its begin() overloads fail on
   * the inert check before anything else, and the temperature/OnOff APIs
   * refuse too. See addSurface(). The flags are the cabinet base class's
   * (hearthOwnedByFridge, reused for any composing appliance; see
   * MatterCookSurface.h), reachable through the surface's friend
   * declaration. The MatterOven pattern, verbatim. */
  _inertSurface.hearthOwnedByFridge = true;
  _inertSurface.hearthOwnedInert = true;
}

MatterCooktop::~MatterCooktop() {
  end();
}

/*
 * Pre-begin only: after begin() has declared the composition, an extra
 * surface could never be declared under the parent (and after reconcile,
 * not at all), so the reject reference is returned instead and the sketch
 * finds out at that surface's begin(). Capacity overflow gets the same
 * treatment for the same reason: silently aliasing a real surface would
 * make the over-capacity begin() succeed against the wrong endpoint.
 */
MatterCookSurface &MatterCooktop::addSurface(MatterCookSurface::CabinetFlavour_t flavour) {
  if (started || _surfaceCount >= kMaxSurfaces) {
    return _inertSurface;
  }
  MatterCookSurface &surf = _surfaces[_surfaceCount];
  _surfaceCount++;
  surf.hearthOwnedByFridge = true;
  surf.hearthOwnedLevels = (flavour == MatterCookSurface::LEVELS);
  return surf;
}

/*
 * Declares the cooktop first, exactly the 0.6.0 declaration (with zero
 * surfaces this function's wire-visible behaviour is byte-identical to
 * what it always was), then every added surface with parentIndex = the
 * cooktop's own registry index (declaration order, exactly what the wire
 * grammar's third field carries, S3.9; 0x0077 REQUIRES it). The surfaces'
 * own begin() calls declare nothing (see MatterCookSurface.h); this is the
 * single place the composed shape enters the registry. hearthDeclare()
 * itself is what refuses a re-begin after reconcile (+MTERR:10), before
 * any member state changes.
 */
bool MatterCooktop::begin() {
  if (!hearthDeclare(this, kCooktopDeviceType)) {
    return false;
  }
  if (_surfaceCount > 0) {
    /* The cooktop's own registry index: found, not assumed, because a
     * re-declare updates in place (this object may sit anywhere if the
     * sketch declared other endpoints first). */
    uint8_t ownIndex = HEARTH_NO_PARENT;
    for (uint8_t i = 0; i < hearthDeclaredCount(); i++) {
      if (hearthDeclaredAt(i) == this) {
        ownIndex = i;
        break;
      }
    }
    if (ownIndex == HEARTH_NO_PARENT) {
      return false;  /* structurally unreachable: the declare above succeeded */
    }
    for (uint8_t i = 0; i < _surfaceCount; i++) {
      uint8_t variant = _surfaces[i].hearthOwnedLevels ? 1 : 0;
      if (!hearthDeclare(&_surfaces[i], kCookSurfaceDeviceType, variant, ownIndex)) {
        return false;
      }
    }
  }
  onOffState = false;
  started = true;
  return true;
}

void MatterCooktop::end() {
  started = false;
}

void MatterCooktop::updateAccessory() {
  if (_onChangeCB != NULL) {
    _onChangeCB(onOffState);
  }
}

bool MatterCooktop::off() {
  if (!started) {
    return false;
  }
  if (onOffState == false) {
    return true;
  }
  esp_matter_attr_val_t val = esp_matter_bool(false);
  if (!updateAttributeVal(kOnOffClusterId, kOnOffAttributeId, &val)) {
    return false;
  }
  onOffState = false;
  return true;
}

bool MatterCooktop::getOnOff() {
  return onOffState;
}

MatterCooktop::operator bool() {
  return getOnOff();
}

bool MatterCooktop::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
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
      onOffState = newState;
    }
  }
  return ret;
}

esp_matter_val_type_t MatterCooktop::hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const {
  if (cluster_id == kOnOffClusterId && attribute_id == kOnOffAttributeId) {
    return ESP_MATTER_VAL_TYPE_BOOLEAN;
  }
  return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
}

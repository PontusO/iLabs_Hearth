/*
 * MatterOven.cpp - implementation. See the header for the design rationale
 * (a bare parent whose function lives in its cavities), MatterOvenCavity.h
 * for the owned cavity's side of the contract, and MatterRefrigerator.cpp
 * for the owner pattern this mirrors line for line.
 */
#include "MatterEndpoints/MatterOven.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"

namespace {
/* oven (ESP_MATTER_OVEN_DEVICE_TYPE_ID) and temperature_controlled_cabinet
 * (ESP_MATTER_TEMPERATURE_CONTROLLED_CABINET_DEVICE_TYPE_ID), AT_MT_SPEC.md
 * S3.9's device type table. Given as plain integers: there is no esp-matter
 * header on a host build to pull the named constants from. */
const uint32_t kOvenDeviceType = 0x007B;
const uint32_t kCabinetDeviceType = 0x0071;
}  // namespace

MatterOven::MatterOven() {
  /* The reject cavity: owned AND inert, so its begin() overloads fail on
   * the inert check before anything else, and the modes/opstate/temperature
   * APIs refuse too. See addCavity(). The flag is the cabinet base class's
   * (hearthOwnedByFridge, reused for any composing appliance; see
   * MatterOvenCavity.h). */
  _inertCavity.hearthOwnedByFridge = true;
  _inertCavity.hearthOwnedInert = true;
}

MatterOven::~MatterOven() {
  started = false;
}

/*
 * Pre-begin only: after begin() has declared the composition, an extra
 * cavity could never be declared under the parent (and after reconcile,
 * not at all), so the reject reference is returned instead and the sketch
 * finds out at that cavity's begin(). Capacity overflow gets the same
 * treatment for the same reason: silently aliasing a real cavity would
 * make the over-capacity begin() succeed against the wrong endpoint.
 */
MatterOvenCavity &MatterOven::addCavity(MatterOvenCavity::CabinetFlavour_t flavour) {
  if (started || _cavityCount >= kMaxCavities) {
    return _inertCavity;
  }
  MatterOvenCavity &cav = _cavities[_cavityCount];
  _cavityCount++;
  cav.hearthOwnedByFridge = true;
  cav.hearthOwnedLevels = (flavour == MatterOvenCavity::LEVELS);
  return cav;
}

/*
 * Declares the oven first, then every added cavity with parentIndex = the
 * oven's own registry index (declaration order, exactly what the wire
 * grammar's third field carries, S3.9). The cavities' own begin() calls
 * declare nothing (see MatterOvenCavity.h); this is the single place the
 * composed shape enters the registry. hearthDeclare() itself is what
 * refuses a re-begin after reconcile (+MTERR:10), before any member state
 * changes.
 */
bool MatterOven::begin() {
  if (!hearthDeclare(this, kOvenDeviceType)) {
    return false;
  }
  /* The oven's own registry index: found, not assumed, because a
   * re-declare updates in place (this object may sit anywhere if the sketch
   * declared other endpoints first). */
  uint8_t ownIndex = HEARTH_NO_PARENT;
  for (uint8_t i = 0; i < hearthDeclaredCount(); i++) {
    if (hearthDeclaredAt(i) == this) {
      ownIndex = i;
      break;
    }
  }
  if (ownIndex == HEARTH_NO_PARENT) {
    return false;  // structurally unreachable: the declare above succeeded
  }
  for (uint8_t i = 0; i < _cavityCount; i++) {
    uint8_t variant = _cavities[i].hearthOwnedLevels ? 1 : 0;
    if (!hearthDeclare(&_cavities[i], kCabinetDeviceType, variant, ownIndex)) {
      return false;
    }
  }
  started = true;
  return true;
}

/*
 * A documented no-op returning `started` (the MatterRefrigerator
 * precedent): the bare parent endpoint (Descriptor + Identify, S3.9's
 * 0x007B note) carries no AT+MTATTR-reachable attribute, so
 * hearthDispatchAttr() (Hearth.cpp) has nothing legitimate to route here.
 * Present only because MatterEndPoint declares this pure virtual.
 */
bool MatterOven::attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) {
  (void)endpoint_id;
  (void)cluster_id;
  (void)attribute_id;
  (void)val;
  return started;
}

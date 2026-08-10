/*
 * MatterOven.h - Task 9 (composed-appliance round): the Oven device type
 * with owned MatterOvenCavity children, the second composed appliance in
 * this library and the first with TYPED children (Task 8's
 * MatterRefrigerator hands back plain cabinets; this hands back
 * MatterOvenCavity references whose compile-time surface is exactly the
 * cavity's legal cluster set).
 *
 * Like MatterRefrigerator and every Hearth-original class, this has NO
 * arduino-esp32 counterpart (see Hearth.h's umbrella comment): upstream's
 * Matter library ships no Oven class at all. The public surface below is
 * this port's own design, built directly against the firmware's wire
 * contract (docs/AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.21) and the task
 * brief's interface sketch.
 *
 * Device type 0x007B is oven (esp_matter_endpoint.h's
 * ESP_MATTER_OVEN_DEVICE_TYPE_ID, AT_MT_SPEC.md S3.9's device type table,
 * "0x007B | Oven"). The parent endpoint is BARE BY DESIGN (S3.9's 0x007B
 * note): Descriptor plus a hand-added Identify and nothing else. The
 * oven's entire function lives in its Temperature Controlled Cabinet
 * children (its cavities), each declared with device type 0x0071, the
 * flavour as the variant byte and parentIndex = this oven's own registry
 * index, which is what makes the firmware derive the Heater conditional
 * cluster set onto them: OvenMode (0x49) and OvenCavityOperationalState
 * (0x48), both served by the MatterOvenCavity class (see its header for
 * that whole surface). This class therefore has no modes, no alarm, no
 * reconcile push and no adjudication of its own; a useful oven is always
 * begin() plus at least one addCavity().
 *
 * Owned cavities. addCavity() (pre-begin only) hands back a
 * MatterOvenCavity this oven owns: begin() declares the oven first, then
 * every added cavity (the owner pattern MatterRefrigerator established,
 * including the registry-index lookup for the parent index and the
 * inert-reject child for capacity/post-begin overflow). The owned cavity's
 * begin() then declares NOTHING (a self-declare would wipe the parent
 * index via the registry's in-place update) and only caches its
 * temperature configuration for the reconcile push.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterOvenCavity.h"

class MatterOven : public MatterEndPoint {
public:
  static constexpr uint8_t kMaxCavities = 3;

  MatterOven();
  ~MatterOven();

  // Add an owned cavity (pre-begin only). Returns a reference the sketch
  // keeps to configure the cavity (its own begin() with the matching
  // flavour, its modes/opstate/temperature APIs). Past kMaxCavities, or
  // after this oven's begin(), returns an inert reject cavity whose every
  // call fails, so the error surfaces at the cavity's begin() rather than
  // as a silent extra endpoint.
  MatterOvenCavity &addCavity(MatterOvenCavity::CabinetFlavour_t flavour);

  // declares self (0x007B), then every added cavity (0x0071, flavour as
  // the variant) with parentIndex = this oven's own registry index.
  bool begin();

  // Documented no-op returning `started` (the MatterRefrigerator
  // precedent): the bare parent endpoint carries no AT+MTATTR-reachable
  // attribute for the URC dispatcher to route here. Present only because
  // MatterEndPoint declares this pure virtual.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

protected:
  bool started = false;

  MatterOvenCavity _cavities[kMaxCavities];
  uint8_t _cavityCount = 0;

private:
  // The reject reference addCavity() returns when it must refuse: marked
  // inert in the constructor, so its begin() (and everything else) fails
  // without ever reaching the registry or the wire. A member, not a
  // function-local static, so two oven objects never share reject state.
  MatterOvenCavity _inertCavity;
};

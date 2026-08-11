/*
 * MatterCookSurface.h - Task 10 (composed-appliance round): the cook
 * surface, an OWNED-ONLY child of a composed MatterCooktop and this
 * library's second TYPED owned child (MatterOvenCavity was the first).
 *
 * Like every Hearth-original class this has NO arduino-esp32 counterpart
 * (see Hearth.h's umbrella comment): upstream's Matter library ships no
 * cook surface class at all. The public surface below is this port's own
 * design against the firmware's wire contract (docs/AT_MT_SPEC.md S3.9's
 * 0x0077 note) and the task brief's interface sketch.
 *
 * Device type 0x0077 is cook_surface (esp_matter_endpoint.h's
 * ESP_MATTER_COOK_SURFACE_DEVICE_TYPE_ID, AT_MT_SPEC.md S3.9's device type
 * table). It is the first device type that REQUIRES a parent on the wire:
 * an AT+MTEP=0x0077 row is only accepted with a <parent_idx> naming a
 * Cooktop (0x0078); the unparented form answers +MTERR:1. That is why,
 * unlike its cabinet base class (standalone-legal by design), this class
 * has no standalone begin path at all: both begin() overloads refuse
 * outright unless MatterCooktop::addSurface() marked this object owned,
 * and never declare anything themselves (the owner pattern Task 8's
 * cabinet established: the parent's declaration carries the parent index,
 * and a self-declare would wipe it via the registry's in-place update).
 *
 * SUBCLASSES MatterTemperatureControlledCabinet rather than reimplementing
 * it: the surface shares 0x0071's TemperatureControl variant scheme
 * verbatim (S3.9's 0x0077 note), so the temperature machinery (both
 * begin() argument shapes, the setters/getters, the reconcile push,
 * attributeChangeCB and hearthAttrTypeFor for cluster 86) is inherited,
 * not transcribed. On top rides what the wire actually adds:
 *
 * - An OnOff cluster (0x0006) with the OffOnly feature. A controller may
 *   switch the surface OFF but never ON: Off is the only command in
 *   AcceptedCommandList, so the only remote-deliverable change is
 *   OnOff=false, arriving as a normal +MTATTR URC on cluster 6 (the
 *   firmware's OnOff server handles the command and the ember attribute
 *   change is reported; there is no +MTCMD adjudication on this cluster).
 *   onOffChange() fires on those remote-delivered changes. Its doc comment
 *   notes the OffOnly consequence but the class does NOT artificially
 *   filter true: the cache and callback mirror whatever the wire reports.
 * - setOnOff(bool) is the host's own act, BOTH directions (S3.9: "turning
 *   a surface on is always the host's act, an AT+MTATTR write of OnOff"),
 *   through updateAttributeVal() (mode 1, reported to subscribers) so a
 *   controller watching the cooktop sees the burner light up. This is the
 *   deliberate asymmetry against the parent MatterCooktop class, which is
 *   structurally incapable of writing OnOff=1: the cooktop's OnOff is the
 *   remote-facing appliance switch (remote ON is the failure mode the
 *   device class exists to prevent), while the surface's OnOff is the
 *   physical burner state the host itself reports.
 *
 * NO mode cluster of any kind exists on a surface endpoint, so the
 * fridge-cabinet modes API inherited from the base class is structurally
 * dead here: setSupportedModes() below hides the base version and refuses
 * outright (no wire traffic; there is no cluster 82 on the endpoint for it
 * to reach), and hearthOnForwardedCommandFields() below defers EVERY
 * forward to MatterEndPoint's fail-closed default directly, deliberately
 * skipping the cabinet's owned-cabinet cluster-82 adjudication: a spurious
 * cluster-82 forward on a surface must be denied without consulting any
 * callback (the MatterOvenCavity precedent, same reasoning). The inherited
 * onChangeMode()/getCurrentMode() members remain callable but inert: the
 * stored verdict callback is never consulted and the cached mode never
 * changes.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"

class MatterCookSurface : public MatterTemperatureControlledCabinet {
public:
  // Maps to the 0x0077 variant byte (AT_MT_SPEC.md S3.9), the same space
  // the cabinet's flavours cover: NUMBER builds a TemperatureNumber plus
  // Step surface, LEVELS a TemperatureLevel one. The owned surface's
  // begin() overload must match the flavour addSurface() declared.
  enum CabinetFlavour_t {
    NUMBER = 0,
    LEVELS = 1
  };

  MatterCookSurface();
  ~MatterCookSurface();

  // begin with the TemperatureNumber feature: the cabinet base class's
  // exact argument shape and defaults (upstream's own), cache-only when
  // owned; a cooktop sketch will normally pass real values (the example
  // uses 90.0, 30.0, 250.0, 5.0). Refused on a surface no cooktop owns:
  // there is no standalone begin path (see the header comment).
  bool begin(double tempSetpoint = 0.00, double minTemperature = -10.0, double maxTemperature = 32.0, double step = 0.50);
  // begin with the TemperatureLevel feature, same base-class shape; same
  // owned-only refusal.
  bool begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel = 0);

  // Fires on a remote-delivered OnOff change (a +MTATTR URC on cluster 6
  // this host did not write). Per the OffOnly feature the only value a
  // controller can ever deliver is false (the Off command is the cluster's
  // entire accepted command list), but the callback is not artificially
  // filtered: it reports whatever the wire reported. NOTE: a successful
  // setOnOff() write is echoed back by the firmware as the same URC (the
  // house convention every sibling class shares), so the callback also
  // observes the host's own writes.
  void onOffChange(std::function<void(bool)> cb);

  // The host's own switch, BOTH directions (S3.9: turning a surface on is
  // always the host's act): an AT+MTATTR write of OnOff, reported to
  // subscribers. Skip-if-equal (cache and device both boot Off); a failed
  // write returns false and leaves the cache untouched.
  bool setOnOff(bool state);
  // returns the cached OnOff state; fed by setOnOff() and by +MTATTR URCs
  // (a controller's Off, or the write echo).
  bool getOnOff();

  // Hides the base class's fridge-cabinet (0x52) version and refuses: no
  // mode cluster of any kind exists on a surface endpoint (see the header
  // comment), so there is nothing on the wire for this to reach.
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // Task 10: the surface adjudicates NOTHING (its OnOff traffic is all
  // attribute URCs, never +MTCMD forwards), so every forward defers to
  // MatterEndPoint's fail-closed default directly, skipping the cabinet
  // base class's owned-cabinet cluster-82 adjudication (see the header
  // comment).
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

  // OnOff URCs (cluster 6) land here; everything else (the temperature
  // machinery on cluster 86) defers to the cabinet base class.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool surfaceOnOff = false;
  std::function<void(bool)> _onOffChangeCB = nullptr;

  // MatterCooktop sets the inherited ownership flags (hearthOwnedByFridge,
  // reused here to mean "owned by the composing appliance", plus
  // hearthOwnedInert/hearthOwnedLevels) from addSurface()/its constructor,
  // exactly the way MatterOven drives MatterOvenCavity.
  friend class MatterCooktop;
};

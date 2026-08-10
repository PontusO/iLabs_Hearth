/*
 * MatterRefrigerator.h - Task 8 (composed-appliance round): the Refrigerator
 * device type with owned Temperature Controlled Cabinets, the first composed
 * appliance in this library and the first concrete consumer of Task 7's
 * parent-aware declaration machinery (MatterEndPoint's 4-arg hearthDeclare()
 * / hearthDeclaredParentAt() / the parented reconcile,
 * test_composition_parent.cpp).
 *
 * Like MatterDoorLock (C3) and every Hearth-original class since, this has
 * NO arduino-esp32 counterpart: upstream's Matter library ships no
 * Refrigerator class at all (see Hearth.h's umbrella comment). The public
 * surface below is this port's own design, built directly against the
 * firmware's wire contract (docs/AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.22)
 * and the task brief's interface sketch.
 *
 * Device type 0x0070 is refrigerator (esp_matter_endpoint.h's
 * ESP_MATTER_REFRIGERATOR_DEVICE_TYPE_ID, AT_MT_SPEC.md S3.9's device type
 * table, "0x0070 | Refrigerator"). The parent endpoint carries two clusters
 * AT+MTATTR does not fully reach (S3.9's 0x0070 note):
 *
 * - RefrigeratorAndTemperatureControlledCabinetMode, cluster 0x0052 (82)
 *   (connectedhomeip's zap-generated clusters/
 *   RefrigeratorAndTemperatureControlledCabinetMode/ClusterId.h, "cluster
 *   code: 82/0x52"); ChangeToMode is ModeBase's command 0x0000. Driven by
 *   AT+MTMODES's cluster-aware form (S3.20.1); tag 0 is kAuto on every
 *   mode, first or not (a ModeBase common tag, S3.20.1's table).
 * - RefrigeratorAlarm, cluster 0x0057 (87), driven by AT+MTALARM's fridge
 *   form (S3.22): AT+MTALARM=<ep>,<bit>,<0|1>, bit 0 = DoorOpen (the only
 *   bit RefrigeratorAlarmServer supports today, 1-7 rejected against the
 *   endpoint's Supported bitmap firmware-side). Writing State through this
 *   command, never AT+MTATTR, is what makes the cluster's Notify event
 *   actually fire; Mask/State/Supported READS by contrast are ordinary
 *   ember attributes a sketch can reach with getAttributeVal() if it wants
 *   them, so this class adds no read wrappers.
 *
 * Owned cabinets. addCabinet() (pre-begin only) hands back a
 * MatterTemperatureControlledCabinet the fridge owns: begin() declares the
 * fridge first, then every added cabinet with device type 0x0071, the
 * flavour as the variant byte (NUMBER=0 TemperatureNumber, LEVELS=1
 * TemperatureLevel, S3.9's 0x0071 note) and parentIndex = the fridge's own
 * registry index. The owned cabinet's begin() then declares NOTHING (the
 * cabinet header's Task 8 comment: a self-declare would wipe the parent
 * index via the registry's in-place update) and only caches its temperature
 * configuration for the reconcile push; composing under a Refrigerator also
 * gives the cabinet the Cooler conditional cluster set (its own 0x0052,
 * S3.9), which is why the cabinet's fridge-cabinet modes API is valid
 * exactly when owned.
 *
 * CurrentMode caching, the 0.6.0 rule (AT_MT_SPEC.md S3.20.1, binding since
 * the RVC batch): CurrentMode on every ModeBase-derived cluster is
 * AttributeAccessInterface-served, never ember-backed, so no +MTATTR URC
 * ever fires for it, from the firmware's own clamp or a controller-driven
 * change. getCurrentMode() below is therefore this host's own bookkeeping,
 * updated ONLY when onChangeMode()'s verdict allows a forwarded
 * ChangeToMode; attributeChangeCB() is a documented no-op (the
 * MatterRoboticVacuum precedent). B196 additionally means a same-mode
 * ChangeToMode short-circuits firmware-side and never reaches this host at
 * all.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"

class MatterRefrigerator : public MatterEndPoint {
public:
  static constexpr uint8_t kMaxCabinets = 4;

  // Maps to the 0x0071 variant byte (AT_MT_SPEC.md S3.9): NUMBER builds a
  // TemperatureNumber cabinet, LEVELS a TemperatureLevel one. The owned
  // cabinet's begin() overload must match the flavour declared here.
  enum CabinetFlavour_t {
    NUMBER = 0,
    LEVELS = 1
  };

  MatterRefrigerator();
  ~MatterRefrigerator();

  // Add an owned cabinet (pre-begin only). Returns a reference the sketch
  // keeps to configure the cabinet (its own begin() with the matching
  // flavour, its modes/temperature APIs). Past kMaxCabinets, or after this
  // fridge's begin(), returns an inert reject cabinet whose every call
  // fails, so the error surfaces at the cabinet's begin() rather than as a
  // silent extra endpoint.
  MatterTemperatureControlledCabinet &addCabinet(CabinetFlavour_t flavour);

  // declares self (0x0070), then every added cabinet (0x0071, flavour as
  // the variant) with parentIndex = this fridge's own registry index.
  bool begin();

  // Replace RefrigeratorAndTemperatureControlledCabinetMode's (0x0052)
  // SupportedModes list on the PARENT endpoint (AT_MT_SPEC.md S3.20.1):
  // 1..8 mode/tag/label triples, each mode 0..255 unique within the call,
  // each tag a bare u16 (0 = kAuto, this cluster's conformance default on
  // every mode), each label 1..32 bytes of printable ASCII with no '"'.
  // Re-sent automatically on every later reconcile (hearthOnReconciled()).
  // An owned cabinet's list is its own setSupportedModes(), against its own
  // endpoint; the two stores are independent (S3.20.1 keys by (ep, cluster)).
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // register the host's verdict for a controller-invoked ChangeToMode on
  // the parent's 0x0052 cluster (S3.17/S3.20.1); the callback's argument is
  // the requested mode. No callback registered denies by default (fail
  // closed). The cache updates only on an allow; see the header comment.
  void onChangeMode(std::function<bool(uint8_t)> cb);
  // cached CurrentMode, the 0.6.0 rule: never a wire read.
  uint8_t getCurrentMode();

  // RefrigeratorAlarm (0x0057, S3.22): report the door-open alarm bit
  // (bit 0, the one bit the cluster supports today). Emits
  // AT+MTALARM=<ep>,0,<0|1>; the firmware fires the cluster's Notify event.
  bool setDoorOpenAlarm(bool active);
  // The generic form, any bit 0..7 (the wire's defensive union bound):
  // AT+MTALARM=<ep>,<bit>,<0|1>. The firmware validates the bit against the
  // endpoint's Supported bitmap (+MTERR:1 for an unsupported one, S3.22);
  // this host passes the bit through rather than duplicating that check,
  // the house "the firmware never transcribes bitmaps into this library"
  // rule.
  bool setAlarmState(uint8_t bit, bool active);

  // Documented no-op returning `started` (the MatterRoboticVacuum
  // precedent): CurrentMode never raises a +MTATTR URC (see the header
  // comment), and this class adds no read wrappers for RefrigeratorAlarm's
  // ember-readable Mask/State/Supported, so there is nothing for the URC
  // dispatcher to route here. Present only because MatterEndPoint declares
  // this pure virtual.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h, Task 6): the firmware forwards
  // a controller-invoked ChangeToMode on the parent's 0x0052 cluster here
  // for a verdict, the requested mode as fields.value[0]. Everything else
  // defers to the base class default (fail closed).
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: re-sends the parent's cached 0x0052 SupportedModes list, which
  // the firmware does not persist across a reboot (S3.20.1). A no-op when
  // nothing has been set yet. The owned cabinets' own hooks (registry
  // order: parent first, then each cabinet) resend their temperature push
  // and their own lists themselves.
  void hearthOnReconciled() override;

  // builds and sends "AT+MTMODES=<ep>,82,<mode1>,<tag1>,\"<label1>\",..."
  // for exactly the triples given; wire-only, no cache update.
  bool hearthSendModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  static const uint8_t kMaxModes = 8;      // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t kMaxLabelLen = 32;  // S3.20.1: 1..32 printable ASCII bytes

  bool started = false;
  uint8_t currentMode = 0;

  uint8_t modes[kMaxModes];
  uint16_t tags[kMaxModes];
  char modeLabels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t modesCount = 0;

  std::function<bool(uint8_t)> _onChangeModeCB = nullptr;

  MatterTemperatureControlledCabinet _cabinets[kMaxCabinets];
  uint8_t _cabinetCount = 0;

private:
  // The reject reference addCabinet() returns when it must refuse: marked
  // inert in the constructor, so its begin() (and everything else) fails
  // without ever reaching the registry or the wire. A member, not a
  // function-local static, so two fridge objects never share reject state.
  MatterTemperatureControlledCabinet _inertCabinet;
};

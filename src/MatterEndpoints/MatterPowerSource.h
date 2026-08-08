/*
 * MatterPowerSource.h - Task C8's Power Source endpoint type.
 *
 * Like MatterDoorLock (C3) and its later siblings, this has NO
 * arduino-esp32 counterpart: upstream's Matter library ships no Power
 * Source class at all (see Hearth.h's umbrella comment). There is nothing
 * to mirror an API from, so the public surface below is this port's own
 * design, built directly against the firmware's C5 wire contract
 * (docs/AT_MT_SPEC.md S3.9) and the task brief's exact signatures.
 *
 * Device type 0x0011 is power_source
 * (esp_matter_endpoint.h's ESP_MATTER_POWER_SOURCE_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:29,
 * "#define ESP_MATTER_POWER_SOURCE_DEVICE_TYPE_ID 0x0011"). Cluster
 * 0x002F (47) is PowerSource (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/PowerSource/ClusterId.h, "inline
 * constexpr ClusterId Id = 0x0000002F;", the file's own header comment
 * reading "cluster code: 47/0x2F"). BatPercentRemaining is attribute
 * 0x000C, BatChargeLevel is attribute 0x000E, and BatReplacementNeeded is
 * attribute 0x000F (.../PowerSource/AttributeIds.h, "namespace
 * BatPercentRemaining { inline constexpr AttributeId Id = 0x0000000C; }",
 * "namespace BatChargeLevel { inline constexpr AttributeId Id =
 * 0x0000000E; }", "namespace BatReplacementNeeded { inline constexpr
 * AttributeId Id = 0x0000000F; }"). BatChargeLevelEnum's three values
 * (.../PowerSource/Enums.h's "enum class BatChargeLevelEnum : uint8_t"):
 * kOk = 0x00, kWarning = 0x01, kCritical = 0x02. All verified against the
 * pinned esp-matter checkout's own generated headers; given as plain
 * integers in the .cpp, this library's usual pattern, since there is no
 * connectedhomeip header on a host build.
 *
 * AT_MT_SPEC.md's own decision log (2026-08-07): "0x0011 Power Source is a
 * flat sibling, not a device composed onto another endpoint... The
 * endpoint enables the Battery feature only... which publishes
 * BatChargeLevel, BatReplacementNeeded and BatReplaceability alongside the
 * base Status/Order/Description, all ordinary AT+MTATTR-reachable
 * integers. BatPercentRemaining is hand-added by the thunk (0-200 in
 * half-percent steps per the Matter spec, nullable, defaulting to null)
 * since no endpoint-creation path in this SDK revision wires it on its
 * own; it too is a plain AT+MTATTR attribute once added." Unlike the
 * Smoke/CO Alarm's eleven Set* methods (AT_MT_SPEC.md S3.22), there is no
 * dedicated command for any of this cluster's battery state: every setter
 * below writes through the ordinary AT+MTATTR path (updateAttributeVal(),
 * mode 1, reported to the fabric), the same shape MatterAirQualitySensor's
 * setAirQuality() already establishes for a host-authoritative reading
 * pushed up to subscribers.
 *
 * **BatPercentRemaining halves on the wire.** The Matter spec's own type
 * for this attribute is a percentage in half-percent steps (0-200 for
 * 0-100%); setBatPercentRemaining()'s double percent argument is this
 * class's own convenience, doubled and rounded to the nearest wire integer
 * before the write. The percent argument is clamped to 0..100 first: an
 * out-of-range double cast to uint8_t is undefined behaviour in C++, not
 * merely a wire-validation gap the firmware would otherwise catch, so this
 * one clamp is a correctness requirement, not a duplicated enum-range
 * check of the kind this library otherwise leaves to the firmware.
 *
 * Design: no getters. The task brief's own API for this class lists only
 * begin() and the three setters (unlike the Smoke/CO Alarm's explicit
 * "getters cached" comment); these are host-authoritative readings pushed
 * up to the fabric, the same "read-direction, no getter beyond what the
 * brief names" shape this library already follows elsewhere. Each setter
 * still tracks its own last-written value internally, purely to skip a
 * redundant wire write when the new value already matches (this library's
 * universal setter discipline), but there is no public accessor for it.
 */
#pragma once

#include <stdint.h>
#include "MatterEndPoint.h"

class MatterPowerSource : public MatterEndPoint {
public:
  MatterPowerSource();
  ~MatterPowerSource();

  // declares only; no initial battery state to reconcile.
  bool begin();
  // this will stop processing Power Source Matter events
  void end();

  // BatChargeLevel (BatChargeLevelEnum wire values: 0 Ok, 1 Warning, 2
  // Critical). Wire write only on an actual change.
  bool setBatChargeLevel(uint8_t lvl);
  // BatPercentRemaining, given as a 0..100 percent; doubled (and clamped)
  // to the wire's half-percent steps (0..200). See the header comment.
  bool setBatPercentRemaining(double percent);
  // BatReplacementNeeded.
  bool setBatReplacementNeeded(bool v);

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (Hearth-prefixed; see MatterEndPoint.h): tells
  // the +MTATTR dispatcher what type each of this cluster's three
  // attributes is, so attributeChangeCB() above receives the right union
  // member already populated.
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;

  uint8_t batChargeLevel = 0;
  uint8_t batPercentRemainingWire = 0;  // half-percent units, as last written
  bool batReplacementNeeded = false;
};

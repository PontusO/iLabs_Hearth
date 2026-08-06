/*
 * MatterPump.h - Hearth-original class, C4 of the ten-type swoop.
 *
 * There is no arduino-esp32 counterpart for this device type: the class is
 * an OnOff leg (MatterOnOffPlugin's shape) plus the PumpConfigurationAndControl
 * cluster, per the task brief, not a port of any upstream source.
 *
 * Device type 0x0303 is pump (esp_matter_endpoint.h:108,
 * ESP_MATTER_PUMP_DEVICE_TYPE_ID). Two clusters:
 *
 *   - 0x0006 OnOff, attribute 0x0000 (OnOff::Attributes::OnOff::Id,
 *     boolean), the same IDs every other OnOff-cluster class in this
 *     library reads.
 *   - 0x0200 PumpConfigurationAndControl (512 decimal). Every ID and type
 *     below was verified directly against the pinned esp-matter v1.5.1
 *     connectedhomeip headers (there is no such header on a host build, so
 *     they are given as plain integers here and in the .cpp):
 *       * zzz_generated/app-common/clusters/PumpConfigurationAndControl/
 *         ClusterId.h:14, `inline constexpr ClusterId Id = 0x00000200;`
 *         (512). Matches the brief.
 *       * .../PumpConfigurationAndControl/AttributeIds.h:20 MaxPressure
 *         `Id = 0x00000000` (0); Attributes.h:46-56 TypeInfo
 *         `Nullable<int16_t>`. Matches the brief's "MaxPressure 0" and its
 *         "nullable" note.
 *       * .../AttributeIds.h:24 MaxSpeed `Id = 0x00000001` (1);
 *         Attributes.h:58-68 TypeInfo `Nullable<uint16_t>`. Matches the
 *         brief's "MaxSpeed 1"; the brief's "i16/u16" hedge resolves to u16
 *         (uint16) specifically for this one, not i16.
 *       * .../AttributeIds.h:28 MaxFlow `Id = 0x00000002` (2);
 *         Attributes.h:70-80 TypeInfo `Nullable<uint16_t>`. Matches the
 *         brief's "MaxFlow 2"; also u16, not i16.
 *       * .../AttributeIds.h:76 EffectiveOperationMode `Id = 0x00000011`
 *         (17); Attributes.h:214-224 TypeInfo `OperationModeEnum`, an
 *         `enum class : uint8_t` (Enums.h:48), i.e. enum8 on the wire, NOT
 *         nullable. Matches the brief's "EffectiveOperationMode 17".
 *       * .../AttributeIds.h:80 EffectiveControlMode `Id = 0x00000012`
 *         (18); Attributes.h:226-236 TypeInfo `ControlModeEnum`, an
 *         `enum class : uint8_t` (Enums.h:32), enum8, NOT nullable. Matches
 *         the brief's "EffectiveControlMode 18".
 *       * .../AttributeIds.h:104 OperationMode `Id = 0x00000020` (32);
 *         Attributes.h:298-308 TypeInfo `OperationModeEnum`, enum8, NOT
 *         nullable (the writable twin of EffectiveOperationMode; the
 *         cluster's own ControlModeEnum::kUnknownEnumValue/
 *         OperationModeEnum::kUnknownEnumValue sentinels are never
 *         transmitted, so this class does not special-case them, same as
 *         every plain enum8 attribute elsewhere in this library). Matches
 *         the brief's "OperationMode 32".
 *     Every one of the brief's transcribed IDs and the cluster ID itself
 *     checked out exactly; nothing needed correcting.
 *
 * MaxPressure/MaxSpeed/MaxFlow are nullable (int16/uint16/uint16
 * respectively) but this class does not special-case null: the sentinel
 * (0x8000 for the nullable int16, 0xFFFF for the nullable uint16s) passes
 * through as an ordinary value on the wire, exactly like every sensor class
 * in this library (MatterFlowSensor's MeasuredValue is the direct
 * precedent). EffectiveOperationMode, EffectiveControlMode and
 * OperationMode are NOT nullable per their TypeInfo above, so there is no
 * null question for them at all.
 *
 * MaxPressure/MaxSpeed/MaxFlow are "read-only telemetry the SKETCH publishes
 * as the device" (the brief's own words): a real Matter controller cannot
 * write them (they are Read-only in the cluster spec), only this library's
 * sketch-facing setters do, over AT+MTATTR mode 1 (reported). There is
 * deliberately no getter for any of the three, matching the brief's public
 * surface exactly; a private cache still exists for each, only to support
 * the standard no-write-if-unchanged optimisation every setter in this
 * library uses. Because nothing public ever reads them back, and no genuine
 * red test can observe attributeChangeCB() acting on them, this class does
 * NOT wire them into attributeChangeCB(): unlike MatterFlowSensor's
 * MeasuredValue (which the sketch both writes AND the class listens for),
 * there is no scenario in which a real Matter controller writes a Read-only
 * attribute back down to this host. A stray URC for one of these three
 * attributes lands as a silent no-op (the default hearthAttrTypeFor()
 * fallback, ESP_MATTER_VAL_TYPE_INTEGER), same as any attribute this class
 * does not otherwise recognise.
 *
 * EffectiveOperationMode/EffectiveControlMode are the opposite shape:
 * genuinely read-only from THIS host's perspective too (there is no
 * setEffectiveOperationMode()/setEffectiveControlMode() at all, matching
 * the brief), fed exclusively by attributeChangeCB() when the C6's own
 * PumpConfigurationAndControl cluster server computes and reports them
 * (per the Matter spec, these reflect the pump's actual running mode, which
 * can differ from the requested OperationMode). getEffectiveOperationMode()/
 * getEffectiveControlMode() read back whatever the last URC delivered,
 * starting at 0 (OperationModeEnum::kNormal / ControlModeEnum::
 * kConstantSpeed) until the first one arrives.
 *
 * OperationMode is an ordinary read-write enum8, the same setter/getter/
 * cache/no-op-if-unchanged/attributeChangeCB shape as MatterThermostat's
 * SystemMode.
 *
 * begin() calls hearthDeclare(this, 0x0303) and resets every cached field
 * to 0/false. It issues no AT traffic, matching every other endpoint class
 * in this library.
 *
 * attributeChangeCB deliberately does not write back to the fabric: the
 * change already arrived from a +MTATTR URC (Hearth.cpp's
 * hearthDispatchAttr), and echoing it through setAttributeVal/
 * updateAttributeVal would be an infinite loop with the real device. This is
 * the entire reason AT+MTATTR has a silent write mode; see
 * MatterEndPoint.h's header comment.
 *
 * The firmware-side "abort trap number six" (VALIDATE_FEATURES_AT_LEAST_ONE
 * over the cluster's operation-mode features, esp_matter_cluster.cpp:2675-
 * 2677) is a firmware repository concern (the boot-time thunk that creates
 * the cluster with the constant_speed feature set); nothing here depends on
 * it beyond the fact that OperationMode/EffectiveOperationMode/
 * EffectiveControlMode exist on the wire regardless of which feature the
 * firmware enabled.
 *
 * C5 scope addition (C4 review): onChangeOperationMode() lets a sketch learn
 * of a controller-driven change to OperationMode without polling
 * getOperationMode(), following MatterThermostat's onChange naming and
 * attributeChangeCB dispatch idiom. OperationMode is controller-writable
 * (unlike EffectiveOperationMode/EffectiveControlMode, which are
 * device-answered only, and MaxPressure/MaxSpeed/MaxFlow, which are Read-only
 * per the cluster spec and carry no attributeChangeCB wiring at all, see
 * above), so a URC for it is a real external signal. The callback is
 * void-returning, not MatterThermostat's bool-returning shape: there is
 * nothing for a bool return to veto, since this class's attributeChangeCB
 * never writes back to the fabric regardless.
 */
#pragma once

#include <cstddef>
#include <functional>
#include <stdint.h>
#include "MatterEndPoint.h"

class MatterPump : public MatterEndPoint {
public:
  MatterPump();
  ~MatterPump();
  virtual bool begin(bool on = false);
  void end();

  bool setOnOff(bool on);
  bool getOnOff();

  bool setOperationMode(uint8_t m);
  uint8_t getOperationMode();

  /* read-only telemetry the sketch publishes as the device; no getters, see
   * the header comment. */
  bool setMaxPressure(int16_t v);
  bool setMaxSpeed(uint16_t v);
  bool setMaxFlow(uint16_t v);

  /* device-answered reads, fed exclusively by attributeChangeCB()/URCs; no
   * setter for either. */
  uint8_t getEffectiveOperationMode();
  uint8_t getEffectiveControlMode();

  // User Callback for whenever the Operation Mode is changed by the Matter Controller
  using EndPointOperationModeCB = std::function<void(uint8_t)>;
  void onChangeOperationMode(EndPointOperationModeCB onChangeCB) {
    _onChangeOperationModeCB = onChangeCB;
  }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  bool onOffState = false;
  uint8_t operationMode = 0;           // OperationModeEnum::kNormal
  int16_t maxPressure = 0;
  uint16_t maxSpeed = 0;
  uint16_t maxFlow = 0;
  uint8_t effectiveOperationMode = 0;  // OperationModeEnum::kNormal
  uint8_t effectiveControlMode = 0;    // ControlModeEnum::kConstantSpeed

  EndPointOperationModeCB _onChangeOperationModeCB = NULL;
};

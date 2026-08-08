/*
 * MatterModeSelect.h - Task C7's Mode Select endpoint type.
 *
 * Like MatterDoorLock (Task C3) and MatterWaterValve (also C7), this has NO
 * arduino-esp32 counterpart: upstream's Matter library ships no Mode Select
 * class at all (see Hearth.h's umbrella comment). There is nothing to
 * mirror an API from, so the public surface below is this port's own
 * design, built directly against the firmware's C3 wire contract
 * (docs/AT_MT_SPEC.md S3.20) and the task brief's exact signatures.
 *
 * Device type 0x0027 is mode_select
 * (esp_matter_endpoint.h's ESP_MATTER_MODE_SELECT_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:112,
 * "#define ESP_MATTER_MODE_SELECT_DEVICE_TYPE_ID 0x0027"). Cluster 0x0050
 * (80) is ModeSelect (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/ModeSelect/ClusterId.h,
 * "inline constexpr ClusterId Id = 0x00000050;", the file's own header
 * comment reading "cluster code: 80/0x50"); CurrentMode is attribute
 * 0x0003 (.../ModeSelect/AttributeIds.h, "namespace CurrentMode { inline
 * constexpr AttributeId Id = 0x00000003; }" -- verified rather than
 * transcribed from the brief, exactly matching AT_MT_SPEC.md S3.20's own
 * worked example, "AT+MTATTR=7,80,3 -> +MTATTR:7,80,3,0"). Both verified
 * against the pinned esp-matter checkout's own generated headers; given as
 * plain integers in the .cpp, this library's usual pattern, since there is
 * no connectedhomeip header on a host build.
 *
 * **SupportedModes has no AT+MTATTR path at all.** S3.20: "this attribute
 * is served by CHIP's own SupportedModesManager mechanism rather than
 * esp_matter's attribute store" -- AT+MTMODES (below) is the only way a
 * host can set it, and it is set-only (there is no read-back command
 * either; a sketch that needs the list it declared keeps its own copy, the
 * same discipline MatterTemperatureControlledCabinet's TemperatureLevel
 * labels already established). **Not persisted**: S3.20, "the store lives
 * in RAM and starts empty every boot", the same policy
 * AT+MTTEMPLEVELS/AT+MTCHIMESOUNDS follow, so setSupportedModes()'s cached
 * list is re-sent verbatim on every later reconcile (hearthOnReconciled()
 * below), the established B120 norm for this shape of state.
 *
 * **CurrentMode is the opposite: a plain esp_matter-managed attribute**
 * (S3.20's own words), readable and writable over AT+MTATTR like any other
 * integer -- setCurrentMode()/getCurrentMode() below go through the base
 * class's updateAttributeVal(), the same shape as e.g. MatterOnOffLight's
 * setOnOff(), not a custom wire verb. A controller's ChangeToMode command
 * sets CurrentMode itself, inside the SDK, after validating the mode is in
 * SupportedModes; the host sees that the ordinary way, a generic +MTATTR
 * URC through attributeChangeCB() below, which is also what feeds
 * onChangeMode() -- there is no dedicated +MTCMD consumer for this class at
 * all (unlike the door lock or the water valve), because ChangeToMode never
 * needs an app-level verdict.
 *
 * setSupportedModes() enforces S3.20's full grammar host-side (1..8 pairs,
 * mode values unique within the list, labels 1..32 bytes of printable ASCII
 * with no '"'), the same discipline
 * MatterTemperatureControlledCabinet::setSupportedTemperatureLevelLabels()
 * established for the identically-shaped AT+MTTEMPLEVELS grammar: an
 * unescaped '"' would corrupt the AT+MTMODES line's own field boundary
 * rather than coming back as a clean +MTERR:1, so it must be caught before
 * ever reaching the wire, and once host-side validation exists for that, it
 * costs nothing extra to enforce the rest of the grammar (count bounds,
 * mode uniqueness, label length) the same way, at no loss of behaviour:
 * every one of these is unconditionally +MTERR:1 on the wire too (S3.20),
 * so there is no case a host-side check rejects that the firmware would
 * have accepted.
 */
#pragma once

#include <cstddef>
#include <stdint.h>
#include <string.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterModeSelect : public MatterEndPoint {
public:
  MatterModeSelect();
  ~MatterModeSelect();

  // declares only; no initial CurrentMode/SupportedModes to reconcile.
  bool begin();
  // this will stop processing Mode Select Matter events
  void end();

  // replace the SupportedModes list (AT_MT_SPEC.md S3.20): 1..8 mode/label
  // pairs, each mode 0..255 unique within the list, each label 1..32 bytes
  // of printable ASCII with no '"' (a comma INSIDE a label is legal and
  // part of its text). Re-sent automatically on every later reconcile; see
  // the header comment.
  bool setSupportedModes(const uint8_t *modes, const char *const *labels, uint8_t count);

  // plain CurrentMode read/write over AT+MTATTR (cluster 80, attribute 3).
  bool setCurrentMode(uint8_t m);
  uint8_t getCurrentMode();

  // fired from attributeChangeCB() when a controller's ChangeToMode command
  // (handled entirely inside the SDK) changes CurrentMode; see the header
  // comment for why there is no +MTCMD verdict to give here.
  void onChangeMode(std::function<void(uint8_t)> cb);

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  uint8_t currentMode = 0;

  // clang-format off
  static const uint8_t kMaxModes   = 8;  // AT_MT_SPEC.md S3.20: 1..8 pairs
  static const uint8_t kMaxLabelLen = 32; // AT_MT_SPEC.md S3.20: 1..32 printable ASCII bytes
  // clang-format on

  uint8_t supportedModesArray[kMaxModes];
  char supportedLabels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t supportedModesCount = 0;

  std::function<void(uint8_t)> _onChangeModeCB = nullptr;

  // builds and sends "AT+MTMODES=<ep>,<mode1>,"<label1>",..." for exactly
  // the pairs given; wire-only, no cache update (house discipline: the
  // caller decides whether/what to commit to the cache afterwards).
  bool hearthSendSupportedModes(const uint8_t *modes, const char *const *labels, uint8_t count);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: resends the cached SupportedModes list, which the firmware does
  // not persist across a reboot (S3.20). A no-op if nothing has been set
  // yet.
  void hearthOnReconciled() override;
};

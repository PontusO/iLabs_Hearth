/*
 * MatterFan.h - the fourteenth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterFan (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterFan.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included. Device type 0x002B is fan, cluster 0x0202 is
 * FanControl (FanMode 0x0000, an enum8; PercentSetting 0x0002 and
 * PercentCurrent 0x0003, both uint8). IDs verified against
 * connectedhomeip's zap-generated ids/Attributes.h and ids/Clusters.h at
 * the exact 3.3.8-bundled revision, and against
 * esp_matter_endpoint.h's ESP_MATTER_FAN_DEVICE_TYPE_ID; there is no such
 * header on a host build, so they are given as plain integers in the .cpp.
 *
 * Two behaviours differ from a literal transcription of upstream's .cpp,
 * both load-bearing and both documented again at the call site in the .cpp:
 *
 * 1. On/off is not a separate attribute. FanControl has no OnOff cluster;
 *    getOnOff() is a computed view over FanMode (false only when
 *    currentFanMode == FAN_MODE_OFF), and setOnOff()/toggle() drive it by
 *    calling setMode(FAN_MODE_ON/FAN_MODE_OFF). This is upstream's own
 *    design, reproduced exactly: there is no coupling to invent here, only
 *    to preserve.
 *
 * 2. attributeChangeCB's PercentSetting/PercentCurrent case does NOT write
 *    back to the wire the way upstream's does. Upstream calls
 *    setAttributeVal() on both attributes from inside the callback, to keep
 *    its process-local zap store's two attributes mirrored; that is a
 *    same-process, no-wire operation there. On this stack setAttributeVal()
 *    is a real AT+MTATTR command, and MatterEndPoint.h's header comment (and
 *    every sibling class, e.g. MatterDimmablePlugin.cpp) is explicit that
 *    attributeChangeCB must never write back: it would echo a
 *    controller-driven change straight back to the fabric it came from. The
 *    coupling is therefore mirrored at the cache level only: a URC on
 *    EITHER PercentSetting or PercentCurrent updates the single
 *    currentPercent field (upstream keeps only one such field too), with no
 *    wire traffic following it.
 *
 * setMode()/setSpeedPercent()/setOnOff()/toggle() keep upstream's
 * performUpdate parameter (default true -> updateAttributeVal / mode 1;
 * false -> setAttributeVal / mode 0), since it is part of the public
 * signature. setSpeedPercent(percent, false) reproduces upstream's own
 * wire sequence for the silent path: TWO writes, PercentSetting then
 * PercentCurrent, matching MatterFan.cpp's setSpeedPercent() line for line
 * (including that the second call's result is what the function actually
 * returns; the first is not short-circuited away).
 *
 * begin() calls hearthDeclare(this, 0x002B) and caches percent, mode and the
 * mode-sequence bitmap. It issues no AT traffic, matching every other
 * endpoint class in this library.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterFan : public MatterEndPoint {
public:
  static const uint8_t MAX_SPEED = 100;
  static const uint8_t MIN_SPEED = 1;
  static const uint8_t OFF_SPEED = 0;

  enum FanMode_t {
    FAN_MODE_OFF = 0,
    FAN_MODE_LOW = 1,
    FAN_MODE_MEDIUM = 2,
    FAN_MODE_HIGH = 3,
    FAN_MODE_ON = 4,
    FAN_MODE_AUTO = 5,
    FAN_MODE_SMART = 6
  };

  enum FanModeSequence_t {
    FAN_MODE_SEQ_OFF_LOW_MED_HIGH = 0,
    FAN_MODE_SEQ_OFF_LOW_HIGH = 1,
    FAN_MODE_SEQ_OFF_LOW_MED_HIGH_AUTO = 2,
    FAN_MODE_SEQ_OFF_LOW_HIGH_AUTO = 3,
    FAN_MODE_SEQ_OFF_HIGH_AUTO = 4,
    FAN_MODE_SEQ_OFF_HIGH = 5
  };

  MatterFan();
  ~MatterFan();
  virtual bool begin(uint8_t percent = 0, FanMode_t fanMode = FAN_MODE_OFF, FanModeSequence_t fanModeSeq = FAN_MODE_SEQ_OFF_HIGH);
  void end();

  static const char *getFanModeString(uint8_t mode) {
    return (mode < 7) ? fanModeString[mode] : "Unknown";
  }

  bool setOnOff(bool newState, bool performUpdate = true);
  bool getOnOff();
  bool toggle(bool performUpdate = true);

  bool setSpeedPercent(uint8_t newPercent, bool performUpdate = true);
  uint8_t getSpeedPercent() {
    return currentPercent;
  }

  bool setMode(FanMode_t newMode, bool performUpdate = true);
  FanMode_t getMode() {
    return currentFanMode;
  }

  void updateAccessory() {
    if (_onChangeCB != NULL) {
      _onChangeCB(currentFanMode, currentPercent);
    }
  }

  operator uint8_t() {
    return getSpeedPercent();
  }

  using EndPointModeCB = std::function<bool(FanMode_t)>;
  void onChangeMode(EndPointModeCB onChangeCB) {
    _onChangeModeCB = onChangeCB;
  }

  using EndPointSpeedCB = std::function<bool(uint8_t)>;
  void onChangeSpeedPercent(EndPointSpeedCB onChangeCB) {
    _onChangeSpeedCB = onChangeCB;
  }

  using EndPointCB = std::function<bool(FanMode_t, uint8_t)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void operator=(uint8_t speedPercent) {
    setSpeedPercent(speedPercent);
  }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  uint8_t validFanModes = 0;

  uint8_t currentPercent = 0;
  FanMode_t currentFanMode = FAN_MODE_OFF;
  EndPointModeCB _onChangeModeCB = NULL;
  EndPointSpeedCB _onChangeSpeedCB = NULL;
  EndPointCB _onChangeCB = NULL;

  static const uint8_t fanSeqModeOff = 0x01;
  static const uint8_t fanSeqModeLow = 0x02;
  static const uint8_t fanSeqModeMedium = 0x04;
  static const uint8_t fanSeqModeHigh = 0x08;
  static const uint8_t fanSeqModeOn = 0x10;
  static const uint8_t fanSeqModeAuto = 0x20;
  static const uint8_t fanSeqModeSmart = 0x40;

  static const uint8_t fanSeqCommonModes = fanSeqModeOff | fanSeqModeOn | fanSeqModeHigh | fanSeqModeSmart;

  static const uint8_t fanSeqModeOffLowMedHigh = fanSeqCommonModes | fanSeqModeLow | fanSeqModeMedium;
  static const uint8_t fanSeqModeOffLowHigh = fanSeqCommonModes | fanSeqModeLow;
  static const uint8_t fanSeqModeOffLowMedHighAuto = fanSeqCommonModes | fanSeqModeLow | fanSeqModeMedium | fanSeqModeAuto;
  static const uint8_t fanSeqModeOffLowHighAuto = fanSeqCommonModes | fanSeqModeLow | fanSeqModeAuto;
  static const uint8_t fanSeqModeOffHighAuto = fanSeqCommonModes | fanSeqModeAuto;
  static const uint8_t fanSeqModeOffHigh = fanSeqCommonModes;

  static const uint8_t fanModeSequence[6];
  static const char *fanModeString[7];
};

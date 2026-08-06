/*
 * MatterAirPurifier.h - actuator clone, C3 of the ten-type swoop.
 *
 * Clones MatterFan.h's public surface onto device type 0x002D, air_purifier
 * (esp_matter_endpoint.h:102, ESP_MATTER_AIR_PURIFIER_DEVICE_TYPE_ID; the
 * namespace at line 455 wraps cluster::fan_control::config_t, the same
 * FanControl cluster MatterFan already speaks). Cluster 0x0202 is
 * FanControl (FanMode 0x0000, an enum8; PercentSetting 0x0002 and
 * PercentCurrent 0x0003, both uint8), the same IDs MatterFan.cpp reads. There
 * is no connectedhomeip header on a host build to pull the named constants
 * from, so they are given as the plain integers here instead.
 *
 * There is no arduino-esp32 counterpart for this device type: this is a
 * Hearth-original class on a house-shape clone, not a verbatim upstream
 * port. The public API is copied unchanged from MatterFan (see that file's
 * header comment for the two documented behaviours this class also carries:
 * on/off is a computed view over FanMode, not a separate attribute, and
 * attributeChangeCB's PercentSetting/PercentCurrent coupling is cache-only,
 * never written back to the wire), because the brief's own instruction is
 * to mirror MatterFan's surface (fan mode enum + speed percent) on the new
 * device type ID.
 *
 * begin() calls hearthDeclare(this, 0x002D) and caches percent, mode and the
 * mode-sequence bitmap. It issues no AT traffic, matching every other
 * endpoint class in this library.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterAirPurifier : public MatterEndPoint {
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

  MatterAirPurifier();
  ~MatterAirPurifier();
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

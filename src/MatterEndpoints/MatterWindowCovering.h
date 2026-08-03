/*
 * MatterWindowCovering.h - the fifteenth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterWindowCovering (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterWindowCovering.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included (plus a small set of Hearth-stack additions,
 * called out below and again at each field). Device type 0x0202 is
 * window_covering, cluster 0x0102 is WindowCovering. Attributes: Type
 * 0x0000 (enum8), ConfigStatus 0x0007 (bitmap8), OperationalStatus 0x000A
 * (bitmap8), TargetPositionLiftPercent100ths 0x000B (uint16),
 * TargetPositionTiltPercent100ths 0x000C (uint16),
 * CurrentPositionLiftPercent100ths 0x000E (uint16),
 * CurrentPositionTiltPercent100ths 0x000F (uint16). IDs verified against
 * connectedhomeip's zap-generated ids/Attributes.h and ids/Clusters.h at
 * the exact 3.3.8-bundled revision, and against esp_matter_endpoint.h's
 * ESP_MATTER_WINDOW_COVERING_DEVICE_TYPE_ID; there is no such header on a
 * host build, so they are given as plain integers in the .cpp.
 *
 * Live surface: the percent100ths lift/tilt attributes above, Type and
 * OperationalStatus (and the per-field OperationalState accessor built on
 * it). Not live, per the design spec (section 3): esp-matter 1.5.1 carries
 * no absolute-position attributes (CurrentPositionLift/Tilt,
 * InstalledOpenLimitLift/Tilt, InstalledClosedLimitLift/Tilt), so
 * setLiftPosition/setTiltPosition, the InstalledOpenLimit(Lift/Tilt) and
 * InstalledClosedLimit(Lift/Tilt) pairs, and their getters exist for upstream parity
 * but return false (or, for the uint16_t getters, 0), with a doc comment
 * naming the reason at each. Upstream also exposes no public method for
 * NumberOfActuationsLift/Tilt at all (it is create()-time configuration
 * only, not a public getter/setter), so none is added here either.
 *
 * Three deviations from a literal reading of upstream's .cpp, all
 * load-bearing and documented again in the .cpp:
 *
 * 1. begin() does not call setLiftPercentage()/setTiltPercentage() the way
 *    upstream's does when liftPercent/tiltPercent are non-zero. That would
 *    put AT traffic on the wire from inside begin(), which the recipe's
 *    "begin() declares only" rule (matching every sibling class) forbids.
 *    begin() here seeds the percent/percent100ths/target caches directly
 *    from its arguments instead.
 *
 * 2. Every getter (getLiftPercentage(), getCoveringType(),
 *    getOperationalStatus(), etc.) returns the cached value rather than
 *    upstream's getAttributeVal()-backed live read. Upstream can afford
 *    that call because its data model lives in the same process; here
 *    getAttributeVal() is a real AT+MTATTR read command, and no other
 *    class in this library performs a wire round trip from inside a
 *    getter (MatterFan::getSpeedPercent(), MatterDimmablePlugin::getLevel(),
 *    etc. are all plain field reads). The caches are kept authoritative by
 *    every successful setter and by attributeChangeCB, exactly as they are
 *    for every other class.
 *
 * 3. Two Hearth-stack-only fields exist beyond upstream's protected
 *    section: currentLiftPercent100ths/currentTiltPercent100ths (the
 *    percent100ths precision upstream re-fetches on demand via
 *    getAttributeVal() and this class instead caches, per point 2 above)
 *    and targetLiftPercent100ths/targetTiltPercent100ths (upstream's
 *    getTargetLiftPercent100ths() has no cache at all, being purely a
 *    live read; this class needs one for the same reason). operationalStatus
 *    is also new: upstream has no cached copy of it either, relying on
 *    getAttributeVal() for both the getter and the read-modify-write inside
 *    setOperationalState(). currentLiftPosition/currentTiltPosition are
 *    kept from upstream's protected section for structural parity, but
 *    stay at 0 forever: nothing in this class ever writes them, since the
 *    absolute-position API that would is one of the false-returning stubs.
 */
#pragma once

#include <cstddef>
#include <functional>
#include "MatterEndPoint.h"

class MatterWindowCovering : public MatterEndPoint {
public:
  enum WindowCoveringType_t {
    ROLLERSHADE = 0,
    ROLLERSHADE_2_MOTOR = 1,
    ROLLERSHADE_EXTERIOR = 2,
    ROLLERSHADE_EXTERIOR_2_MOTOR = 3,
    DRAPERY = 4,
    AWNING = 5,
    SHUTTER = 6,
    BLIND_TILT_ONLY = 7,
    BLIND_LIFT_AND_TILT = 8,
    PROJECTOR_SCREEN = 9,
  };

  enum OperationalState_t {
    STALL = 0,
    MOVING_UP_OR_OPEN = 1,
    MOVING_DOWN_OR_CLOSE = 2,
  };

  enum OperationalStatusField_t {
    GLOBAL = 0x3,
    LIFT = 0xC,
    TILT = 0x30,
  };

  MatterWindowCovering();
  ~MatterWindowCovering();
  virtual bool begin(uint8_t liftPercent = 100, uint8_t tiltPercent = 0, WindowCoveringType_t coveringType = ROLLERSHADE);
  void end();

  /* Lift position control. Absolute (setLiftPosition/getLiftPosition) is
   * unsupported: esp-matter 1.5.1 has no CurrentPositionLift attribute
   * (design spec section 3). setLiftPosition() always returns false;
   * getLiftPosition() always returns 0. */
  bool setLiftPosition(uint16_t liftPosition) {
    (void)liftPosition;
    return false;
  }
  uint16_t getLiftPosition() {
    return currentLiftPosition;
  }
  bool setLiftPercentage(uint8_t liftPercent);
  uint8_t getLiftPercentage() {
    return currentLiftPercent;
  }
  bool setTargetLiftPercent100ths(uint16_t liftPercent100ths);
  uint16_t getTargetLiftPercent100ths() {
    return targetLiftPercent100ths;
  }

  /* Lift limit control. Unsupported for the same reason: no
   * InstalledOpenLimitLift/InstalledClosedLimitLift attribute on this
   * stack. Setters return false; getters always return 0. */
  bool setInstalledOpenLimitLift(uint16_t openLimit) {
    (void)openLimit;
    return false;
  }
  uint16_t getInstalledOpenLimitLift() {
    return 0;
  }
  bool setInstalledClosedLimitLift(uint16_t closedLimit) {
    (void)closedLimit;
    return false;
  }
  uint16_t getInstalledClosedLimitLift() {
    return 0;
  }

  /* Tilt position control. Same absolute-position gap as lift. */
  bool setTiltPosition(uint16_t tiltPosition) {
    (void)tiltPosition;
    return false;
  }
  uint16_t getTiltPosition() {
    return currentTiltPosition;
  }
  bool setTiltPercentage(uint8_t tiltPercent);
  uint8_t getTiltPercentage() {
    return currentTiltPercent;
  }
  bool setTargetTiltPercent100ths(uint16_t tiltPercent100ths);
  uint16_t getTargetTiltPercent100ths() {
    return targetTiltPercent100ths;
  }

  /* Tilt limit control. Same InstalledOpenLimit/InstalledClosedLimit gap
   * as lift. */
  bool setInstalledOpenLimitTilt(uint16_t openLimit) {
    (void)openLimit;
    return false;
  }
  uint16_t getInstalledOpenLimitTilt() {
    return 0;
  }
  bool setInstalledClosedLimitTilt(uint16_t closedLimit) {
    (void)closedLimit;
    return false;
  }
  uint16_t getInstalledClosedLimitTilt() {
    return 0;
  }

  bool setCoveringType(WindowCoveringType_t coveringType);
  WindowCoveringType_t getCoveringType() {
    return coveringType;
  }

  bool setOperationalStatus(uint8_t operationalStatus);
  uint8_t getOperationalStatus() {
    return operationalStatus;
  }

  bool setOperationalState(OperationalStatusField_t field, OperationalState_t state);
  OperationalState_t getOperationalState(OperationalStatusField_t field);

  using EndPointOpenCB = std::function<bool()>;
  void onOpen(EndPointOpenCB onChangeCB) {
    _onOpenCB = onChangeCB;
  }

  using EndPointCloseCB = std::function<bool()>;
  void onClose(EndPointCloseCB onChangeCB) {
    _onCloseCB = onChangeCB;
  }

  using EndPointLiftCB = std::function<bool(uint8_t)>;
  void onGoToLiftPercentage(EndPointLiftCB onChangeCB) {
    _onGoToLiftPercentageCB = onChangeCB;
  }

  using EndPointTiltCB = std::function<bool(uint8_t)>;
  void onGoToTiltPercentage(EndPointTiltCB onChangeCB) {
    _onGoToTiltPercentageCB = onChangeCB;
  }

  using EndPointStopCB = std::function<bool()>;
  void onStop(EndPointStopCB onChangeCB) {
    _onStopCB = onChangeCB;
  }

  using EndPointCB = std::function<bool(uint8_t, uint8_t)>;
  void onChange(EndPointCB onChangeCB) {
    _onChangeCB = onChangeCB;
  }

  void updateAccessory() {
    if (_onChangeCB != NULL) {
      _onChangeCB(currentLiftPercent, currentTiltPercent);
    }
  }

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  uint8_t currentLiftPercent = 0;
  uint16_t currentLiftPosition = 0;
  uint8_t currentTiltPercent = 0;
  uint16_t currentTiltPosition = 0;
  WindowCoveringType_t coveringType = ROLLERSHADE;

  /* Hearth-stack additions, not part of upstream's protected section; see
   * the header comment's point 3. */
  uint16_t currentLiftPercent100ths = 0;
  uint16_t currentTiltPercent100ths = 0;
  uint16_t targetLiftPercent100ths = 0;
  uint16_t targetTiltPercent100ths = 0;
  uint8_t operationalStatus = 0;

  EndPointOpenCB _onOpenCB = NULL;
  EndPointCloseCB _onCloseCB = NULL;
  EndPointLiftCB _onGoToLiftPercentageCB = NULL;
  EndPointTiltCB _onGoToTiltPercentageCB = NULL;
  EndPointStopCB _onStopCB = NULL;
  EndPointCB _onChangeCB = NULL;
};

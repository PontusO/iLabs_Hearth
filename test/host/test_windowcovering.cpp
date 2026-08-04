#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterWindowCovering.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(
  MockStream &s, MatterWindowCovering &wc, uint8_t liftPercent, uint8_t tiltPercent, MatterWindowCovering::WindowCoveringType_t type
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  wc.begin(liftPercent, tiltPercent, type);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0202\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  check("declared as window_covering", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0202);
  check("adopted endpoint 1", wc.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial lift percent cached", wc.getLiftPercentage() == 50);
  check("initial tilt percent cached", wc.getTiltPercentage() == 10);
  check("initial target lift percent100ths seeded from lift percent", wc.getTargetLiftPercent100ths() == 5000);
  check("initial target tilt percent100ths seeded from tilt percent", wc.getTargetTiltPercent100ths() == 1000);
  check("initial covering type cached", wc.getCoveringType() == MatterWindowCovering::ROLLERSHADE);
  check("initial operational status is zero", wc.getOperationalStatus() == 0);
}

static void test_set_lift_percentage_writes_current_position(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,14,7500,1", "+MTATTR:1,258,14,7500\r\nOK\r\n");
  check("setLiftPercentage(75) succeeds", wc.setLiftPercentage(75));
  check("lift percent cached", wc.getLiftPercentage() == 75);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_tilt_percentage_writes_current_position(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,15,3000,1", "+MTATTR:1,258,15,3000\r\nOK\r\n");
  check("setTiltPercentage(30) succeeds", wc.setTiltPercentage(30));
  check("tilt percent cached", wc.getTiltPercentage() == 30);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_target_lift_percent100ths_writes(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,11,6000,1", "+MTATTR:1,258,11,6000\r\nOK\r\n");
  check("setTargetLiftPercent100ths(6000) succeeds", wc.setTargetLiftPercent100ths(6000));
  check("target lift cached", wc.getTargetLiftPercent100ths() == 6000);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_target_tilt_percent100ths_writes(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,12,2500,1", "+MTATTR:1,258,12,2500\r\nOK\r\n");
  check("setTargetTiltPercent100ths(2500) succeeds", wc.setTargetTiltPercent100ths(2500));
  check("target tilt cached", wc.getTargetTiltPercent100ths() == 2500);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_covering_type_writes(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,0,6,1", "+MTATTR:1,258,0,6\r\nOK\r\n");  /* 6 == SHUTTER */
  check("setCoveringType(SHUTTER) succeeds", wc.setCoveringType(MatterWindowCovering::SHUTTER));
  check("covering type cached", wc.getCoveringType() == MatterWindowCovering::SHUTTER);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_operational_status_writes(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,10,5,1", "+MTATTR:1,258,10,5\r\nOK\r\n");
  check("setOperationalStatus(5) succeeds", wc.setOperationalStatus(5));
  check("operational status cached", wc.getOperationalStatus() == 5);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * setOperationalState(LIFT, MOVING_UP_OR_OPEN) from a zero OperationalStatus:
 * fieldMask 0xC, shift 2, so LIFT's bits become 0b01 << 2 = 4; Global then
 * recomputes to follow Lift's non-Stall state (priority rule, mirrored from
 * upstream's setOperationalState()), giving Global bits 0b01. Final byte is
 * 4 | 1 = 5, exactly what AT+MTATTR carries.
 */
static void test_set_operational_state_lift_recomputes_global(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,10,5,1", "+MTATTR:1,258,10,5\r\nOK\r\n");
  check(
    "setOperationalState(LIFT, MOVING_UP_OR_OPEN) succeeds",
    wc.setOperationalState(MatterWindowCovering::LIFT, MatterWindowCovering::MOVING_UP_OR_OPEN)
  );
  check("LIFT field reads back MOVING_UP_OR_OPEN", wc.getOperationalState(MatterWindowCovering::LIFT) == MatterWindowCovering::MOVING_UP_OR_OPEN);
  check(
    "GLOBAL field follows LIFT by priority", wc.getOperationalState(MatterWindowCovering::GLOBAL) == MatterWindowCovering::MOVING_UP_OR_OPEN
  );
  check("cached operational status is the recomputed byte", wc.getOperationalStatus() == 5);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_operational_state_global_refused(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  check(
    "setOperationalState(GLOBAL, ...) is refused: GLOBAL is derived, not settable directly",
    !wc.setOperationalState(MatterWindowCovering::GLOBAL, MatterWindowCovering::MOVING_UP_OR_OPEN)
  );
  check("operational status unchanged", wc.getOperationalStatus() == 0);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_current_lift_fires_onchange(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int changeSeen = 0;
  uint8_t liftAtChange = 0, tiltAtChange = 0;
  wc.onChange([&](uint8_t lift, uint8_t tilt) { changeSeen++; liftAtChange = lift; tiltAtChange = tilt; return true; });
  s.injectURC("+MTATTR:1,258,14,8000");  /* CurrentPositionLiftPercent100ths -> 80% */
  Hearth.poll();
  check("onChange fired", changeSeen == 1);
  check("with the new lift percent", liftAtChange == 80);
  check("and the unchanged cached tilt percent alongside it", tiltAtChange == 10);
  check("cached lift percent updated", wc.getLiftPercentage() == 80);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_current_tilt_fires_onchange(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int changeSeen = 0;
  uint8_t liftAtChange = 0, tiltAtChange = 0;
  wc.onChange([&](uint8_t lift, uint8_t tilt) { changeSeen++; liftAtChange = lift; tiltAtChange = tilt; return true; });
  s.injectURC("+MTATTR:1,258,15,4000");  /* CurrentPositionTiltPercent100ths -> 40% */
  Hearth.poll();
  check("onChange fired", changeSeen == 1);
  check("with the unchanged cached lift percent", liftAtChange == 50);
  check("and the new tilt percent", tiltAtChange == 40);
  check("cached tilt percent updated", wc.getTiltPercentage() == 40);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_target_lift_zero_is_up_or_open(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int openSeen = 0, closeSeen = 0, stopSeen = 0, goToSeen = 0;
  uint8_t goToPercent = 255;
  wc.onOpen([&]() { openSeen++; return true; });
  wc.onClose([&]() { closeSeen++; return true; });
  wc.onStop([&]() { stopSeen++; return true; });
  wc.onGoToLiftPercentage([&](uint8_t p) { goToSeen++; goToPercent = p; return true; });
  s.injectURC("+MTATTR:1,258,11,0");
  Hearth.poll();
  check("onOpen fired for target == 0", openSeen == 1);
  check("onClose did not fire", closeSeen == 0);
  check("onStop did not fire", stopSeen == 0);
  check("the generic onGoToLiftPercentage also fired", goToSeen == 1 && goToPercent == 0);
  check("target lift cache updated", wc.getTargetLiftPercent100ths() == 0);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_target_lift_10000_is_down_or_close(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int openSeen = 0, closeSeen = 0, stopSeen = 0, goToSeen = 0;
  uint8_t goToPercent = 0;
  wc.onOpen([&]() { openSeen++; return true; });
  wc.onClose([&]() { closeSeen++; return true; });
  wc.onStop([&]() { stopSeen++; return true; });
  wc.onGoToLiftPercentage([&](uint8_t p) { goToSeen++; goToPercent = p; return true; });
  s.injectURC("+MTATTR:1,258,11,10000");
  Hearth.poll();
  check("onClose fired for target == 10000", closeSeen == 1);
  check("onOpen did not fire", openSeen == 0);
  check("onStop did not fire", stopSeen == 0);
  check("the generic onGoToLiftPercentage also fired", goToSeen == 1 && goToPercent == 100);
  check("target lift cache updated", wc.getTargetLiftPercent100ths() == 10000);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_target_lift_equal_to_current_is_stop_motion(void) {
  MockStream s; MatterWindowCovering wc;
  /* liftPercent 50 -> currentLiftPercent100ths seeded to 5000, away from
   * either limit, so a target of exactly 5000 is StopMotion, not
   * UpOrOpen/DownOrClose. */
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int openSeen = 0, closeSeen = 0, stopSeen = 0, goToSeen = 0;
  wc.onOpen([&]() { openSeen++; return true; });
  wc.onClose([&]() { closeSeen++; return true; });
  wc.onStop([&]() { stopSeen++; return true; });
  wc.onGoToLiftPercentage([&](uint8_t) { goToSeen++; return true; });
  s.injectURC("+MTATTR:1,258,11,5000");
  Hearth.poll();
  check("onStop fired for target == current, away from the limits", stopSeen == 1);
  check("onOpen did not fire", openSeen == 0);
  check("onClose did not fire", closeSeen == 0);
  check("the generic onGoToLiftPercentage still fired", goToSeen == 1);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_target_tilt_fires_only_the_generic_callback(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  int goToSeen = 0;
  uint8_t goToPercent = 0;
  wc.onGoToTiltPercentage([&](uint8_t p) { goToSeen++; goToPercent = p; return true; });
  s.injectURC("+MTATTR:1,258,12,3500");
  Hearth.poll();
  check("onGoToTiltPercentage fired", goToSeen == 1 && goToPercent == 35);
  check("target tilt cache updated", wc.getTargetTiltPercent100ths() == 3500);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_type_updates_cache_with_no_callback(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.injectURC("+MTATTR:1,258,0,4");  /* DRAPERY */
  Hearth.poll();
  check("covering type cache updated", wc.getCoveringType() == MatterWindowCovering::DRAPERY);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_operational_status_updates_cache(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.injectURC("+MTATTR:1,258,10,9");
  Hearth.poll();
  check("operational status cache updated", wc.getOperationalStatus() == 9);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_config_status_urc_is_a_harmless_no_op(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.injectURC("+MTATTR:1,258,7,32");
  Hearth.poll();
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_lift_write_returns_false(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);
  s.expect("AT+MTATTR=1,258,14,7500,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected lift write returns false", !wc.setLiftPercentage(75));
  check("and does not update the cache", wc.getLiftPercentage() == 50);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Every absolute-position method exists for upstream parity and returns
 * false (or 0, for the uint16_t getters): esp-matter 1.5.1 has no
 * CurrentPositionLift/Tilt or InstalledOpenLimit/InstalledClosedLimit
 * attribute (design spec section 3). None of these issue any AT traffic.
 */
static void test_absolute_position_api_returns_false_and_zero(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);

  check("setLiftPosition() is unsupported", !wc.setLiftPosition(500));
  check("getLiftPosition() always reads 0", wc.getLiftPosition() == 0);
  check("setInstalledOpenLimitLift() is unsupported", !wc.setInstalledOpenLimitLift(10));
  check("getInstalledOpenLimitLift() always reads 0", wc.getInstalledOpenLimitLift() == 0);
  check("setInstalledClosedLimitLift() is unsupported", !wc.setInstalledClosedLimitLift(20000));
  check("getInstalledClosedLimitLift() always reads 0", wc.getInstalledClosedLimitLift() == 0);

  check("setTiltPosition() is unsupported", !wc.setTiltPosition(300));
  check("getTiltPosition() always reads 0", wc.getTiltPosition() == 0);
  check("setInstalledOpenLimitTilt() is unsupported", !wc.setInstalledOpenLimitTilt(5));
  check("getInstalledOpenLimitTilt() always reads 0", wc.getInstalledOpenLimitTilt() == 0);
  check("setInstalledClosedLimitTilt() is unsupported", !wc.setInstalledClosedLimitTilt(6000));
  check("getInstalledClosedLimitTilt() always reads 0", wc.getInstalledClosedLimitTilt() == 0);

  check("none of the above issued any AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingWindowCovering : public MatterWindowCovering {
public:
  esp_matter_val_type_t seenType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenConfigStatus = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenOperationalStatus = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenTargetLift = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenTargetTilt = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenCurrentLift = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenCurrentTilt = ESP_MATTER_VAL_TYPE_INVALID;

  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 258) {
      switch (attribute_id) {
        case 0: seenType = val->type; break;
        case 7: seenConfigStatus = val->type; break;
        case 10: seenOperationalStatus = val->type; break;
        case 11: seenTargetLift = val->type; break;
        case 12: seenTargetTilt = val->type; break;
        case 14: seenCurrentLift = val->type; break;
        case 15: seenCurrentTilt = val->type; break;
        default: break;
      }
    }
    return MatterWindowCovering::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_hearth_attr_type_for_covers_every_owned_attribute(void) {
  MockStream s; TypeCheckingWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);

  s.injectURC("+MTATTR:1,258,0,4");
  Hearth.poll();
  check("Type is enum8", wc.seenType == ESP_MATTER_VAL_TYPE_ENUM8);

  s.injectURC("+MTATTR:1,258,7,1");
  Hearth.poll();
  check("ConfigStatus is bitmap8", wc.seenConfigStatus == ESP_MATTER_VAL_TYPE_BITMAP8);

  s.injectURC("+MTATTR:1,258,10,1");
  Hearth.poll();
  check("OperationalStatus is bitmap8", wc.seenOperationalStatus == ESP_MATTER_VAL_TYPE_BITMAP8);

  s.injectURC("+MTATTR:1,258,11,100");
  Hearth.poll();
  check("TargetPositionLiftPercent100ths is uint16", wc.seenTargetLift == ESP_MATTER_VAL_TYPE_UINT16);

  s.injectURC("+MTATTR:1,258,12,100");
  Hearth.poll();
  check("TargetPositionTiltPercent100ths is uint16", wc.seenTargetTilt == ESP_MATTER_VAL_TYPE_UINT16);

  s.injectURC("+MTATTR:1,258,14,100");
  Hearth.poll();
  check("CurrentPositionLiftPercent100ths is uint16", wc.seenCurrentLift == ESP_MATTER_VAL_TYPE_UINT16);

  s.injectURC("+MTATTR:1,258,15,100");
  Hearth.poll();
  check("CurrentPositionTiltPercent100ths is uint16", wc.seenCurrentTilt == ESP_MATTER_VAL_TYPE_UINT16);

  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterWindowCovering wc;
  bringUp(s, wc, 50, 10, MatterWindowCovering::ROLLERSHADE);

  check(
    "a second begin() after Matter.begin() is refused", !wc.begin(0, 0, MatterWindowCovering::ROLLERSHADE)
  );
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached lift percent was not overwritten", wc.getLiftPercentage() == 50);
  check("the cached tilt percent was not overwritten", wc.getTiltPercentage() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,258,14,0,1", "+MTATTR:1,258,14,0\r\nOK\r\n");
  check("so the next setLiftPercentage(0) really does move the covering", wc.setLiftPercentage(0));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterWindowCovering tests =====\n");
  test_begin_declares_and_adopts();
  test_set_lift_percentage_writes_current_position();
  test_set_tilt_percentage_writes_current_position();
  test_set_target_lift_percent100ths_writes();
  test_set_target_tilt_percent100ths_writes();
  test_set_covering_type_writes();
  test_set_operational_status_writes();
  test_set_operational_state_lift_recomputes_global();
  test_set_operational_state_global_refused();
  test_controller_change_current_lift_fires_onchange();
  test_controller_change_current_tilt_fires_onchange();
  test_target_lift_zero_is_up_or_open();
  test_target_lift_10000_is_down_or_close();
  test_target_lift_equal_to_current_is_stop_motion();
  test_target_tilt_fires_only_the_generic_callback();
  test_controller_change_type_updates_cache_with_no_callback();
  test_controller_change_operational_status_updates_cache();
  test_config_status_urc_is_a_harmless_no_op();
  test_failed_lift_write_returns_false();
  test_absolute_position_api_returns_false_and_zero();
  test_hearth_attr_type_for_covers_every_owned_attribute();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

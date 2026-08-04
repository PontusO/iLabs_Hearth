#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterFan.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterFan &fan, uint8_t percent, MatterFan::FanMode_t mode, MatterFan::FanModeSequence_t seq) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  fan.begin(percent, mode, seq);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x002B\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  check("declared as fan", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x002B);
  check("adopted endpoint 1", fan.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial mode cached", fan.getMode() == MatterFan::FAN_MODE_OFF);
  check("initial percent cached", fan.getSpeedPercent() == 0);
  check("initial on/off is off", fan.getOnOff() == false);
}

static void test_set_mode_writes_update(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,0,3,1", "+MTATTR:1,514,0,3\r\nOK\r\n");
  check("setMode(HIGH) succeeds", fan.setMode(MatterFan::FAN_MODE_HIGH));
  check("mode cached", fan.getMode() == MatterFan::FAN_MODE_HIGH);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_mode_silent_when_performupdate_false(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,0,3,0", "+MTATTR:1,514,0,3\r\nOK\r\n");
  check("setMode(HIGH, false) succeeds", fan.setMode(MatterFan::FAN_MODE_HIGH, false));
  check("mode cached", fan.getMode() == MatterFan::FAN_MODE_HIGH);
  check("script drained", s.scriptDrained());
}

static void test_set_mode_rejects_mode_outside_sequence(void) {
  MockStream s; MatterFan fan;
  /* FAN_MODE_SEQ_OFF_HIGH only allows OFF, ON, HIGH and SMART: LOW is not
   * in that sequence's bitmap (see fanModeSequence[]/fanSeqModeOffHigh
   * upstream), so the mode change must be refused before any AT traffic. */
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  check("setMode(LOW) is refused, LOW is not in this sequence", !fan.setMode(MatterFan::FAN_MODE_LOW));
  check("mode unchanged", fan.getMode() == MatterFan::FAN_MODE_OFF);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The coupling deviation, part 1: upstream's setOnOff()/toggle() are not
 * backed by a separate OnOff attribute at all. FanControl has no such
 * attribute; on/off is entirely a view over FanMode (getOnOff() ==
 * currentFanMode != FAN_MODE_OFF), and setOnOff() drives that by calling
 * setMode(FAN_MODE_ON/FAN_MODE_OFF) under the hood. So turning the fan on
 * writes FanMode, never a separate OnOff attribute; there is no such
 * attribute to write.
 */
static void test_set_on_off_writes_fan_mode_not_a_separate_attribute(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,0,4,1", "+MTATTR:1,514,0,4\r\nOK\r\n");  /* 4 == FAN_MODE_ON */
  check("setOnOff(true) succeeds", fan.setOnOff(true));
  check("on/off now reads true", fan.getOnOff() == true);
  check("mode cache shows FAN_MODE_ON, not some other coupled attribute", fan.getMode() == MatterFan::FAN_MODE_ON);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,0,4,1", "+MTATTR:1,514,0,4\r\nOK\r\n");
  check("toggle from off turns on", fan.toggle() && fan.getOnOff());
  s.expect("AT+MTATTR=1,514,0,0,1", "+MTATTR:1,514,0,0\r\nOK\r\n");
  check("toggle from on turns off", fan.toggle() && !fan.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The coupling deviation, part 2: setSpeedPercent()'s wire sequence depends
 * on performUpdate exactly as upstream's does. performUpdate=true (the
 * default, a live/reported change) writes only PercentSetting. performUpdate
 * =false (a silent/local sync, matching how upstream's own attributeChangeCB
 * mirrors PercentCurrent from PercentSetting and vice versa) writes BOTH
 * PercentSetting and PercentCurrent, in that order -- read straight out of
 * MatterFan.cpp's setSpeedPercent(): "ret = setAttributeVal(..PercentSetting..);
 * ret = setAttributeVal(..PercentCurrent..);" with no short-circuit between
 * them.
 */
static void test_set_speed_percent_writes_update(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,2,50,1", "+MTATTR:1,514,2,50\r\nOK\r\n");
  check("setSpeedPercent(50) succeeds", fan.setSpeedPercent(50));
  check("percent cached", fan.getSpeedPercent() == 50);
  check("operator uint8_t agrees", (uint8_t)fan == 50);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_speed_percent_silent_writes_both_setting_and_current(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,2,60,0", "+MTATTR:1,514,2,60\r\nOK\r\n");
  s.expect("AT+MTATTR=1,514,3,60,0", "+MTATTR:1,514,3,60\r\nOK\r\n");
  check("setSpeedPercent(60, false) succeeds", fan.setSpeedPercent(60, false));
  check("percent cached", fan.getSpeedPercent() == 60);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained: both PercentSetting and PercentCurrent were written", s.scriptDrained());
}

static void test_assignment_operator(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,2,75,1", "+MTATTR:1,514,2,75\r\nOK\r\n");
  fan = (uint8_t)75;
  check("operator= writes and caches", fan.getSpeedPercent() == 75);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_mode_fires_callback(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  int modeSeen = 0, changeSeen = 0;
  MatterFan::FanMode_t mode = MatterFan::FAN_MODE_OFF;
  fan.onChangeMode([&](MatterFan::FanMode_t m) { modeSeen++; mode = m; return true; });
  fan.onChange([&](MatterFan::FanMode_t, uint8_t) { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,514,0,3");  /* FAN_MODE_HIGH */
  Hearth.poll();
  check("onChangeMode fired", modeSeen == 1 && mode == MatterFan::FAN_MODE_HIGH);
  check("onChange also fired", changeSeen == 1);
  check("cached mode updated", fan.getMode() == MatterFan::FAN_MODE_HIGH);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The coupling deviation, part 3: a controller-driven URC on EITHER
 * PercentSetting or PercentCurrent updates the one shared currentPercent
 * cache (upstream keeps a single currentPercent field too; its two
 * attributes are just two zap-generated names for what this class treats
 * as one value). Deliberately NOT mirrored: upstream's attributeChangeCB
 * additionally calls setAttributeVal() on both attributes from inside the
 * callback to keep its process-local zap store in sync. On this stack
 * setAttributeVal() is real wire traffic (AT+MTATTR), and attributeChangeCB
 * must never write back (MatterEndPoint.h's header comment, and
 * MatterDimmablePlugin.cpp's: "the change already arrived from a +MTATTR
 * URC ... echoing it through setAttributeVal/updateAttributeVal would be an
 * infinite loop with the real device"). So here the coupling is cache-only:
 * no wire traffic follows the URC.
 */
static void test_controller_change_percent_setting_fires_callback(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  int speedSeen = 0, changeSeen = 0;
  uint8_t percent = 0;
  fan.onChangeSpeedPercent([&](uint8_t p) { speedSeen++; percent = p; return true; });
  fan.onChange([&](MatterFan::FanMode_t, uint8_t) { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,514,2,80");  /* PercentSetting */
  Hearth.poll();
  check("onChangeSpeedPercent fired", speedSeen == 1 && percent == 80);
  check("onChange also fired", changeSeen == 1);
  check("cached percent updated from the PercentSetting URC", fan.getSpeedPercent() == 80);
  check("no wire write-back for the coupled PercentCurrent attribute", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_percent_current_fires_callback(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  int speedSeen = 0;
  uint8_t percent = 0;
  fan.onChangeSpeedPercent([&](uint8_t p) { speedSeen++; percent = p; return true; });
  s.injectURC("+MTATTR:1,514,3,45");  /* PercentCurrent, the OTHER coupled attribute */
  Hearth.poll();
  check("onChangeSpeedPercent fired from the PercentCurrent URC too", speedSeen == 1 && percent == 45);
  check("cached percent updated", fan.getSpeedPercent() == 45);
  check("no wire write-back", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_mode_write_returns_false(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,0,3,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected mode write returns false", !fan.setMode(MatterFan::FAN_MODE_HIGH));
  check("and does not update the cache", fan.getMode() == MatterFan::FAN_MODE_OFF);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_speed_write_returns_false(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.expect("AT+MTATTR=1,514,2,50,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected speed write returns false", !fan.setSpeedPercent(50));
  check("and does not update the cache", fan.getSpeedPercent() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingFan : public MatterFan {
public:
  esp_matter_val_type_t seenModeType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenPercentType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 514 && attribute_id == 0) {
      seenModeType = val->type;
    } else if (cluster_id == 514 && (attribute_id == 2 || attribute_id == 3)) {
      seenPercentType = val->type;
    }
    return MatterFan::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_mode_and_percent(void) {
  MockStream s; TypeCheckingFan fan;
  bringUp(s, fan, 0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH);
  s.injectURC("+MTATTR:1,514,0,5");  /* FAN_MODE_AUTO */
  Hearth.poll();
  check("mode value type is enum8", fan.seenModeType == ESP_MATTER_VAL_TYPE_ENUM8);

  s.injectURC("+MTATTR:1,514,2,33");
  Hearth.poll();
  check("percent value type is uint8", fan.seenPercentType == ESP_MATTER_VAL_TYPE_UINT8);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterFan fan;
  bringUp(s, fan, 40, MatterFan::FAN_MODE_HIGH, MatterFan::FAN_MODE_SEQ_OFF_HIGH);

  check("a second begin() after Matter.begin() is refused", !fan.begin(0, MatterFan::FAN_MODE_OFF, MatterFan::FAN_MODE_SEQ_OFF_HIGH));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached mode was not overwritten", fan.getMode() == MatterFan::FAN_MODE_HIGH);
  check("the cached percent was not overwritten", fan.getSpeedPercent() == 40);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,514,0,0,1", "+MTATTR:1,514,0,0\r\nOK\r\n");
  check("so the next setMode(OFF) really does turn the fan off", fan.setMode(MatterFan::FAN_MODE_OFF));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterFan tests =====\n");
  test_begin_declares_and_adopts();
  test_set_mode_writes_update();
  test_set_mode_silent_when_performupdate_false();
  test_set_mode_rejects_mode_outside_sequence();
  test_set_on_off_writes_fan_mode_not_a_separate_attribute();
  test_toggle();
  test_set_speed_percent_writes_update();
  test_set_speed_percent_silent_writes_both_setting_and_current();
  test_assignment_operator();
  test_controller_change_mode_fires_callback();
  test_controller_change_percent_setting_fires_callback();
  test_controller_change_percent_current_fires_callback();
  test_failed_mode_write_returns_false();
  test_failed_speed_write_returns_false();
  test_controller_change_delivers_typed_mode_and_percent();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

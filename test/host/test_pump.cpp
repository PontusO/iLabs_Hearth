#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterPump.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterPump &p, bool on) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  p.begin(on);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0303\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  check("declared as pump", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0303);
  check("adopted endpoint 1", p.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial on/off is off", p.getOnOff() == false);
  check("initial operation mode is 0 (NORMAL)", p.getOperationMode() == 0);
  check("initial effective operation mode is 0", p.getEffectiveOperationMode() == 0);
  check("initial effective control mode is 0", p.getEffectiveControlMode() == 0);
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", p.setOnOff(true));
  check("state cached", p.getOnOff() == true);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_on_off_no_op_same_value(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  check("setOnOff(false) when already off succeeds without a write", p.setOnOff(false));
  check("no AT traffic for a no-op write", s.scriptDrained());
}

static void test_set_operation_mode_writes(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,32,3,1", "+MTATTR:1,512,32,3\r\nOK\r\n");  /* 3 == OperationModeEnum::kLocal */
  check("setOperationMode(3) succeeds", p.setOperationMode(3));
  check("mode cached", p.getOperationMode() == 3);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_operation_mode_no_op_same_value(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  check("setOperationMode(0) when already 0 succeeds without a write", p.setOperationMode(0));
  check("no AT traffic for a no-op write", s.scriptDrained());
}

static void test_set_max_pressure_writes(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,0,1500,1", "+MTATTR:1,512,0,1500\r\nOK\r\n");
  check("setMaxPressure(1500) succeeds", p.setMaxPressure(1500));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_max_pressure_negative(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,0,-100,1", "+MTATTR:1,512,0,-100\r\nOK\r\n");
  check("setMaxPressure(-100) sends a negative int16", p.setMaxPressure(-100));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_max_speed_writes(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,1,4000,1", "+MTATTR:1,512,1,4000\r\nOK\r\n");
  check("setMaxSpeed(4000) succeeds", p.setMaxSpeed(4000));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_max_flow_writes(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,2,2500,1", "+MTATTR:1,512,2,2500\r\nOK\r\n");
  check("setMaxFlow(2500) succeeds", p.setMaxFlow(2500));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_operation_mode_write_returns_false(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,32,3,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected operation mode write returns false", !p.setOperationMode(3));
  check("and does not update the cache", p.getOperationMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_max_speed_write_returns_false(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,1,4000,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected max speed write returns false", !p.setMaxSpeed(4000));
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingPump : public MatterPump {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenOperationModeType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenEffectiveOperationModeType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenEffectiveControlModeType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    } else if (cluster_id == 512 && attribute_id == 32) {
      seenOperationModeType = val->type;
    } else if (cluster_id == 512 && attribute_id == 17) {
      seenEffectiveOperationModeType = val->type;
    } else if (cluster_id == 512 && attribute_id == 18) {
      seenEffectiveControlModeType = val->type;
    }
    return MatterPump::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

/*
 * The brief's own pin: OperationMode write plus EffectiveOperationMode URC
 * feeding the getter. EffectiveControlMode and OnOff are exercised the same
 * way alongside it for symmetry.
 */
static void test_controller_change_feeds_effective_getters(void) {
  MockStream s; TypeCheckingPump p;
  bringUp(s, p, false);
  s.injectURC("+MTATTR:1,512,17,2");   /* EffectiveOperationMode == kMaximum */
  s.injectURC("+MTATTR:1,512,18,1");   /* EffectiveControlMode == kConstantPressure */
  s.injectURC("+MTATTR:1,512,32,2");   /* OperationMode == kMaximum, controller-driven */
  s.injectURC("+MTATTR:1,6,0,1");      /* on */
  Hearth.poll();
  check("effective operation mode type is enum8", p.seenEffectiveOperationModeType == ESP_MATTER_VAL_TYPE_ENUM8);
  check("effective operation mode getter fed by the URC", p.getEffectiveOperationMode() == 2);
  check("effective control mode type is enum8", p.seenEffectiveControlModeType == ESP_MATTER_VAL_TYPE_ENUM8);
  check("effective control mode getter fed by the URC", p.getEffectiveControlMode() == 1);
  check("operation mode type is enum8", p.seenOperationModeType == ESP_MATTER_VAL_TYPE_ENUM8);
  check("operation mode getter fed by the URC", p.getOperationMode() == 2);
  check("on/off type is boolean", p.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("on/off getter fed by the URC", p.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterPump p;
  bringUp(s, p, false);
  s.expect("AT+MTATTR=1,512,32,3,1", "+MTATTR:1,512,32,3\r\nOK\r\n");
  check("setOperationMode(3) succeeds", p.setOperationMode(3));

  check("a second begin() after Matter.begin() is refused", !p.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached operation mode was not overwritten", p.getOperationMode() == 3);
  check("the cached on/off state was not overwritten", p.getOnOff() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,512,32,0,1", "+MTATTR:1,512,32,0\r\nOK\r\n");
  check("so the next setOperationMode(0) really does write", p.setOperationMode(0));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterPump tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_set_on_off_no_op_same_value();
  test_set_operation_mode_writes();
  test_set_operation_mode_no_op_same_value();
  test_set_max_pressure_writes();
  test_set_max_pressure_negative();
  test_set_max_speed_writes();
  test_set_max_flow_writes();
  test_failed_operation_mode_write_returns_false();
  test_failed_max_speed_write_returns_false();
  test_controller_change_feeds_effective_getters();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

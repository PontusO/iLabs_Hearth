#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterRoomAirConditioner.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterRoomAirConditioner &r, bool on) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  r.begin(on);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0072\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  check("declared as room_air_conditioner", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0072);
  check("adopted endpoint 1", r.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial on/off is off", r.getOnOff() == false);
  check("initial cooling setpoint is 24.0C", r.getCoolingSetpoint() > 23.99 && r.getCoolingSetpoint() < 24.01);
  check("initial heating setpoint is 16.0C", r.getHeatingSetpoint() > 15.99 && r.getHeatingSetpoint() < 16.01);
  check("initial mode is 0 (OFF)", r.getMode() == 0);
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", r.setOnOff(true));
  check("state cached", r.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_on_off_no_op_same_value(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  check("setOnOff(false) when already off succeeds without a write", r.setOnOff(false));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Pins the dead-front documentation contract: setOnOff(false) is an
 * ordinary OnOff write, the exact same wire shape as setOnOff(true), with
 * no special handling anywhere in this class. See MatterRoomAirConditioner.h.
 */
static void test_set_on_off_false_is_an_ordinary_write(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, true);
  s.expect("AT+MTATTR=1,6,0,0,1", "+MTATTR:1,6,0,0\r\nOK\r\n");
  check("setOnOff(false) sends an ordinary OnOff write (dead-front is documentation, not code)", r.setOnOff(false));
  check("state cached off", r.getOnOff() == false);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_local_temperature_push(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,0,2350,1", "+MTATTR:1,513,0,2350\r\nOK\r\n");
  check("23.50C becomes LocalTemperature 2350", r.setLocalTemperature(23.50));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cooling_setpoint_write(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,17,2600,1", "+MTATTR:1,513,17,2600\r\nOK\r\n");
  check("setCoolingSetpoint(26.0) writes OccupiedCoolingSetpoint", r.setCoolingSetpoint(26.0));
  check("cooling cached", r.getCoolingSetpoint() > 25.99 && r.getCoolingSetpoint() < 26.01);
  check("heating untouched", r.getHeatingSetpoint() > 15.99 && r.getHeatingSetpoint() < 16.01);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_heating_setpoint_write(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,18,1800,1", "+MTATTR:1,513,18,1800\r\nOK\r\n");
  check("setHeatingSetpoint(18.0) writes OccupiedHeatingSetpoint", r.setHeatingSetpoint(18.0));
  check("heating cached", r.getHeatingSetpoint() > 17.99 && r.getHeatingSetpoint() < 18.01);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_mode_writes(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,28,3,1", "+MTATTR:1,513,28,3\r\nOK\r\n");  /* 3 == SystemModeEnum::kCool */
  check("setMode(3) succeeds", r.setMode(3));
  check("mode cached", r.getMode() == 3);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_mode_no_op_same_value(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  check("setMode(0) when already 0 succeeds without a write", r.setMode(0));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_mode_write_returns_false(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected mode write returns false", !r.setMode(4));
  check("and does not update the cache", r.getMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_cooling_setpoint_write_returns_false(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,17,2600,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected cooling setpoint write returns false", !r.setCoolingSetpoint(26.0));
  check("and does not update the cache", r.getCoolingSetpoint() > 23.99 && r.getCoolingSetpoint() < 24.01);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingRAC : public MatterRoomAirConditioner {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenLocalTempType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenCoolingType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenHeatingType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenModeType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    } else if (cluster_id == 513 && attribute_id == 0) {
      seenLocalTempType = val->type;
    } else if (cluster_id == 513 && attribute_id == 17) {
      seenCoolingType = val->type;
    } else if (cluster_id == 513 && attribute_id == 18) {
      seenHeatingType = val->type;
    } else if (cluster_id == 513 && attribute_id == 28) {
      seenModeType = val->type;
    }
    return MatterRoomAirConditioner::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_feeds_setpoint_and_mode_getters(void) {
  MockStream s; TypeCheckingRAC r;
  bringUp(s, r, false);
  s.injectURC("+MTATTR:1,513,17,2550");  /* 25.50C cooling */
  s.injectURC("+MTATTR:1,513,18,1750");  /* 17.50C heating */
  s.injectURC("+MTATTR:1,513,28,4");     /* HEAT */
  s.injectURC("+MTATTR:1,6,0,1");        /* on */
  Hearth.poll();
  check("cooling setpoint type is int16", r.seenCoolingType == ESP_MATTER_VAL_TYPE_INT16);
  check("cooling setpoint getter fed by the URC", r.getCoolingSetpoint() > 25.49 && r.getCoolingSetpoint() < 25.51);
  check("heating setpoint type is int16", r.seenHeatingType == ESP_MATTER_VAL_TYPE_INT16);
  check("heating setpoint getter fed by the URC", r.getHeatingSetpoint() > 17.49 && r.getHeatingSetpoint() < 17.51);
  check("mode type is enum8", r.seenModeType == ESP_MATTER_VAL_TYPE_ENUM8);
  check("mode getter fed by the URC", r.getMode() == 4);
  check("on/off type is boolean", r.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("on/off getter fed by the URC", r.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_local_temperature_typed_but_ungettable(void) {
  MockStream s; TypeCheckingRAC r;
  bringUp(s, r, false);
  s.injectURC("+MTATTR:1,513,0,2150");  /* 21.50C, no public getter for this one */
  Hearth.poll();
  check("local temperature value type is int16", r.seenLocalTempType == ESP_MATTER_VAL_TYPE_INT16);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * C5 scope addition (C4 review): onChangeCoolingSetpoint/onChangeHeatingSetpoint/
 * onChangeMode let a sketch learn of a controller-driven change without
 * polling. Following MatterThermostat's test_controller_change_*_fires_callback
 * shape exactly, but the callbacks here are void-returning (see the header
 * comment), so there is no verdict to gate the cache update on: the getter
 * agreeing with what the callback saw is the only thing to pin.
 */
static void test_controller_change_cooling_setpoint_fires_callback(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  int seen = 0; double sp = 0;
  r.onChangeCoolingSetpoint([&](double v) { seen++; sp = v; });
  s.injectURC("+MTATTR:1,513,17,2550");  /* 25.50C */
  Hearth.poll();
  check("onChangeCoolingSetpoint fired", seen == 1 && sp > 25.49 && sp < 25.51);
  check("cache getter agrees", r.getCoolingSetpoint() > 25.49 && r.getCoolingSetpoint() < 25.51);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_heating_setpoint_fires_callback(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  int seen = 0; double sp = 0;
  r.onChangeHeatingSetpoint([&](double v) { seen++; sp = v; });
  s.injectURC("+MTATTR:1,513,18,1750");  /* 17.50C */
  Hearth.poll();
  check("onChangeHeatingSetpoint fired", seen == 1 && sp > 17.49 && sp < 17.51);
  check("cache getter agrees", r.getHeatingSetpoint() > 17.49 && r.getHeatingSetpoint() < 17.51);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_mode_fires_callback(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  int seen = 0; uint8_t mode = 0xFF;
  r.onChangeMode([&](uint8_t m) { seen++; mode = m; });
  s.injectURC("+MTATTR:1,513,28,4");  /* HEAT */
  Hearth.poll();
  check("onChangeMode fired", seen == 1 && mode == 4);
  check("cache getter agrees", r.getMode() == 4);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterRoomAirConditioner r;
  bringUp(s, r, false);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTATTR:1,513,28,4\r\nOK\r\n");
  check("setMode(4) succeeds", r.setMode(4));

  check("a second begin() after Matter.begin() is refused", !r.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached mode was not overwritten", r.getMode() == 4);
  check("the cached on/off state was not overwritten", r.getOnOff() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,513,28,0,1", "+MTATTR:1,513,28,0\r\nOK\r\n");
  check("so the next setMode(0) really does turn it off", r.setMode(0));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterRoomAirConditioner tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_set_on_off_no_op_same_value();
  test_set_on_off_false_is_an_ordinary_write();
  test_local_temperature_push();
  test_cooling_setpoint_write();
  test_heating_setpoint_write();
  test_set_mode_writes();
  test_set_mode_no_op_same_value();
  test_failed_mode_write_returns_false();
  test_failed_cooling_setpoint_write_returns_false();
  test_controller_change_feeds_setpoint_and_mode_getters();
  test_controller_change_local_temperature_typed_but_ungettable();
  test_controller_change_cooling_setpoint_fires_callback();
  test_controller_change_heating_setpoint_fires_callback();
  test_controller_change_mode_fires_callback();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterThermostat.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(
  MockStream &s, MatterThermostat &t,
  MatterThermostat::ControlSequenceOfOperation_t seq = MatterThermostat::THERMOSTAT_SEQ_OP_COOLING,
  MatterThermostat::ThermostatAutoMode_t autoMode = MatterThermostat::THERMOSTAT_AUTO_MODE_DISABLED
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  t.begin(seq, autoMode);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0301\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  check("declared as thermostat", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0301);
  check("adopted endpoint 1", t.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial mode is OFF", t.getMode() == MatterThermostat::THERMOSTAT_MODE_OFF);
  check("initial cooling setpoint is 24.0C", t.getCoolingSetpoint() > 23.99 && t.getCoolingSetpoint() < 24.01);
  check("initial heating setpoint is 16.0C", t.getHeatingSetpoint() > 15.99 && t.getHeatingSetpoint() < 16.01);
  check("initial local temperature is 20.0C", t.getLocalTemperature() > 19.99 && t.getLocalTemperature() < 20.01);
}

/*
 * Mirrors upstream's own pre-creation validation: AUTO mode is only legal
 * under a Cooling & Heating control sequence. Checked BEFORE hearthDeclare()
 * (see the header's deviation 1), so a rejected begin() consumes no
 * registry slot.
 */
static void test_begin_rejects_auto_mode_incompatible_sequence(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s; MatterThermostat t;
  Hearth.begin(s);
  check(
    "begin() with AUTO under a COOLING-only sequence is refused",
    !t.begin(MatterThermostat::THERMOSTAT_SEQ_OP_COOLING, MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED)
  );
  check("no registry slot was consumed", MatterEndPoint::hearthDeclaredCount() == 0);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_set_mode_writes_update(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTATTR:1,513,28,4\r\nOK\r\n");  /* 4 == THERMOSTAT_MODE_HEAT */
  check("setMode(HEAT) succeeds under the default COOLING sequence", t.setMode(MatterThermostat::THERMOSTAT_MODE_HEAT));
  check("mode cached", t.getMode() == MatterThermostat::THERMOSTAT_MODE_HEAT);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_mode_no_op_same_value(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  check("setMode(OFF) when already OFF succeeds without a write", t.setMode(MatterThermostat::THERMOSTAT_MODE_OFF));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Upstream's own quirk (see MatterThermostat.h's header comment): a COOLING
 * control sequence's switch branch only lets HEAT or AUTO through, so COOL
 * itself is refused under it. Verified directly against
 * MatterThermostat.cpp's setMode(); this is not a Hearth bug.
 */
static void test_set_mode_rejects_cool_under_cooling_sequence(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);  /* default sequence is THERMOSTAT_SEQ_OP_COOLING */
  check(
    "setMode(COOL) under a COOLING sequence is refused, matching upstream's own switch",
    !t.setMode(MatterThermostat::THERMOSTAT_MODE_COOL)
  );
  check("mode unchanged", t.getMode() == MatterThermostat::THERMOSTAT_MODE_OFF);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_mode_auto_rejected_when_auto_mode_disabled(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);  /* default autoMode is THERMOSTAT_AUTO_MODE_DISABLED */
  check("setMode(AUTO) is refused when autoMode is disabled", !t.setMode(MatterThermostat::THERMOSTAT_MODE_AUTO));
  check("mode unchanged", t.getMode() == MatterThermostat::THERMOSTAT_MODE_OFF);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_set_mode_auto_succeeds_under_compatible_sequence(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t, MatterThermostat::THERMOSTAT_SEQ_OP_COOLING_HEATING, MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);
  s.expect("AT+MTATTR=1,513,28,1,1", "+MTATTR:1,513,28,1\r\nOK\r\n");  /* 1 == THERMOSTAT_MODE_AUTO */
  check("setMode(AUTO) succeeds under COOLING_HEATING with autoMode enabled", t.setMode(MatterThermostat::THERMOSTAT_MODE_AUTO));
  check("mode cached", t.getMode() == MatterThermostat::THERMOSTAT_MODE_AUTO);
  check("script drained", s.scriptDrained());
}

static void test_local_temperature_push(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,0,2350,1", "+MTATTR:1,513,0,2350\r\nOK\r\n");
  check("23.50C becomes LocalTemperature 2350", t.setLocalTemperature(23.50));
  check("reads back as 23.5", t.getLocalTemperature() > 23.49 && t.getLocalTemperature() < 23.51);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_local_temperature_negative(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,0,-525,1", "+MTATTR:1,513,0,-525\r\nOK\r\n");
  check("-5.25C is sent as -525", t.setLocalTemperature(-5.25));
  check("reads back negative", t.getLocalTemperature() < -5.24 && t.getLocalTemperature() > -5.26);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_cooling_setpoint_write(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);  /* currentMode is OFF, not AUTO: no deadband check applies */
  s.expect("AT+MTATTR=1,513,17,2600,1", "+MTATTR:1,513,17,2600\r\nOK\r\n");
  check("setCoolingSetpoint(26.0) writes OccupiedCoolingSetpoint only", t.setCoolingSetpoint(26.0));
  check("cooling cached", t.getCoolingSetpoint() > 25.99 && t.getCoolingSetpoint() < 26.01);
  check("heating untouched", t.getHeatingSetpoint() > 15.99 && t.getHeatingSetpoint() < 16.01);
  check("script drained", s.scriptDrained());
}

static void test_heating_setpoint_write(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,18,1800,1", "+MTATTR:1,513,18,1800\r\nOK\r\n");
  check("setHeatingSetpoint(18.0) writes OccupiedHeatingSetpoint only", t.setHeatingSetpoint(18.0));
  check("heating cached", t.getHeatingSetpoint() > 17.99 && t.getHeatingSetpoint() < 18.01);
  check("script drained", s.scriptDrained());
}

/*
 * setCoolingHeatingSetpoints(heating, cooling) writes COOLING first even
 * though heating is its first parameter -- read straight out of
 * MatterThermostat.cpp: "if (settingCooling) {...} if (settingHeating)
 * {...}", cooling checked first. Pinned down explicitly here.
 */
static void test_cooling_heating_setpoints_combined_writes_cooling_then_heating(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,17,2700,1", "+MTATTR:1,513,17,2700\r\nOK\r\n");
  s.expect("AT+MTATTR=1,513,18,1800,1", "+MTATTR:1,513,18,1800\r\nOK\r\n");
  check("setCoolingHeatingSetpoints(18.0, 27.0) succeeds", t.setCoolingHeatingSetpoints(18.0, 27.0));
  check("cooling cached", t.getCoolingSetpoint() > 26.99 && t.getCoolingSetpoint() < 27.01);
  check("heating cached", t.getHeatingSetpoint() > 17.99 && t.getHeatingSetpoint() < 18.01);
  check("both writes happened in cooling-then-heating order", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_heating_setpoint_rejected_above_max_limit(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  check("35.0C exceeds the 30.0C heating limit and is refused", !t.setHeatingSetpoint(35.0));
  check("heating unchanged", t.getHeatingSetpoint() > 15.99 && t.getHeatingSetpoint() < 16.01);
  check("no AT traffic was issued", s.scriptDrained());
}

static void test_cooling_setpoint_rejected_below_min_limit(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  check("10.0C is below the 16.0C cooling limit and is refused", !t.setCoolingSetpoint(10.0));
  check("cooling unchanged", t.getCoolingSetpoint() > 23.99 && t.getCoolingSetpoint() < 24.01);
  check("no AT traffic was issued", s.scriptDrained());
}

/*
 * The deadband rule only applies in AUTO mode. Heating stays at the 16.0C
 * default, so the minimum legal cooling setpoint is 16.0 + 2.5 = 18.5C.
 */
static void test_auto_mode_deadband(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t, MatterThermostat::THERMOSTAT_SEQ_OP_COOLING_HEATING, MatterThermostat::THERMOSTAT_AUTO_MODE_ENABLED);
  s.expect("AT+MTATTR=1,513,28,1,1", "+MTATTR:1,513,28,1\r\nOK\r\n");
  check("setMode(AUTO) succeeds", t.setMode(MatterThermostat::THERMOSTAT_MODE_AUTO));

  check("18.0C is inside the 2.5C deadband and is refused", !t.setCoolingSetpoint(18.0));
  check("cooling unchanged", t.getCoolingSetpoint() > 23.99 && t.getCoolingSetpoint() < 24.01);
  check("no extra AT traffic for the refused deadband write", s.scriptDrained());

  s.expect("AT+MTATTR=1,513,17,1900,1", "+MTATTR:1,513,17,1900\r\nOK\r\n");
  check("19.0C clears the deadband and succeeds", t.setCoolingSetpoint(19.0));
  check("cooling cached", t.getCoolingSetpoint() > 18.99 && t.getCoolingSetpoint() < 19.01);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_mode_fires_callback(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  int modeSeen = 0, changeSeen = 0;
  MatterThermostat::ThermostatMode_t mode = MatterThermostat::THERMOSTAT_MODE_OFF;
  t.onChangeMode([&](MatterThermostat::ThermostatMode_t m) { modeSeen++; mode = m; return true; });
  t.onChange([&]() { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,513,28,4");  /* THERMOSTAT_MODE_HEAT */
  Hearth.poll();
  check("onChangeMode fired", modeSeen == 1 && mode == MatterThermostat::THERMOSTAT_MODE_HEAT);
  check("onChange also fired", changeSeen == 1);
  check("cached mode updated", t.getMode() == MatterThermostat::THERMOSTAT_MODE_HEAT);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_local_temperature_fires_callback(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  int seen = 0; float temp = 0;
  t.onChangeLocalTemperature([&](float v) { seen++; temp = v; return true; });
  s.injectURC("+MTATTR:1,513,0,2150");  /* 21.50C */
  Hearth.poll();
  check("onChangeLocalTemperature fired", seen == 1 && temp > 21.49 && temp < 21.51);
  check("cached local temperature updated", t.getLocalTemperature() > 21.49 && t.getLocalTemperature() < 21.51);
  check("no echo", s.scriptDrained());
}

static void test_controller_change_cooling_setpoint_fires_callback(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  int seen = 0; double sp = 0;
  t.onChangeCoolingSetpoint([&](double v) { seen++; sp = v; return true; });
  s.injectURC("+MTATTR:1,513,17,2550");  /* 25.50C */
  Hearth.poll();
  check("onChangeCoolingSetpoint fired", seen == 1 && sp > 25.49 && sp < 25.51);
  check("cached cooling setpoint updated", t.getCoolingSetpoint() > 25.49 && t.getCoolingSetpoint() < 25.51);
  check("no echo", s.scriptDrained());
}

static void test_controller_change_heating_setpoint_fires_callback(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  int seen = 0; double sp = 0;
  t.onChangeHeatingSetpoint([&](double v) { seen++; sp = v; return true; });
  s.injectURC("+MTATTR:1,513,18,1750");  /* 17.50C */
  Hearth.poll();
  check("onChangeHeatingSetpoint fired", seen == 1 && sp > 17.49 && sp < 17.51);
  check("cached heating setpoint updated", t.getHeatingSetpoint() > 17.49 && t.getHeatingSetpoint() < 17.51);
  check("no echo", s.scriptDrained());
}

static void test_failed_mode_write_returns_false(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected mode write returns false", !t.setMode(MatterThermostat::THERMOSTAT_MODE_HEAT));
  check("and does not update the cache", t.getMode() == MatterThermostat::THERMOSTAT_MODE_OFF);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_local_temperature_write_returns_false(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,0,2350,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected local temperature write returns false", !t.setLocalTemperature(23.50));
  check("and does not update the cache", t.getLocalTemperature() > 19.99 && t.getLocalTemperature() < 20.01);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingThermostat : public MatterThermostat {
public:
  esp_matter_val_type_t seenModeType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenLocalTempType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenCoolingType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenHeatingType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 513 && attribute_id == 28) {
      seenModeType = val->type;
    } else if (cluster_id == 513 && attribute_id == 0) {
      seenLocalTempType = val->type;
    } else if (cluster_id == 513 && attribute_id == 17) {
      seenCoolingType = val->type;
    } else if (cluster_id == 513 && attribute_id == 18) {
      seenHeatingType = val->type;
    }
    return MatterThermostat::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_values(void) {
  MockStream s; TypeCheckingThermostat t;
  bringUp(s, t);
  s.injectURC("+MTATTR:1,513,28,4");
  s.injectURC("+MTATTR:1,513,0,2000");
  s.injectURC("+MTATTR:1,513,17,2400");
  s.injectURC("+MTATTR:1,513,18,1600");
  Hearth.poll();
  check("mode value type is enum8", t.seenModeType == ESP_MATTER_VAL_TYPE_ENUM8);
  check("local temperature value type is int16", t.seenLocalTempType == ESP_MATTER_VAL_TYPE_INT16);
  check("cooling setpoint value type is int16", t.seenCoolingType == ESP_MATTER_VAL_TYPE_INT16);
  check("heating setpoint value type is int16", t.seenHeatingType == ESP_MATTER_VAL_TYPE_INT16);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterThermostat t;
  bringUp(s, t);
  s.expect("AT+MTATTR=1,513,28,4,1", "+MTATTR:1,513,28,4\r\nOK\r\n");
  check("setMode(HEAT) succeeds", t.setMode(MatterThermostat::THERMOSTAT_MODE_HEAT));

  check("a second begin() after Matter.begin() is refused", !t.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached mode was not overwritten", t.getMode() == MatterThermostat::THERMOSTAT_MODE_HEAT);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,513,28,0,1", "+MTATTR:1,513,28,0\r\nOK\r\n");
  check("so the next setMode(OFF) really does turn it off", t.setMode(MatterThermostat::THERMOSTAT_MODE_OFF));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_thermostat_mode_string(void) {
  check("mode string OFF", strcmp(MatterThermostat::getThermostatModeString(0), "OFF") == 0);
  check("mode string AUTO", strcmp(MatterThermostat::getThermostatModeString(1), "AUTO") == 0);
  check("mode string UNKNOWN at the enum gap", strcmp(MatterThermostat::getThermostatModeString(2), "UNKNOWN") == 0);
  check("mode string COOL", strcmp(MatterThermostat::getThermostatModeString(3), "COOL") == 0);
  check("mode string HEAT", strcmp(MatterThermostat::getThermostatModeString(4), "HEAT") == 0);
  /* B146: modes 5-9 are controller-writable SystemModeEnum values; the
   * old 5-entry table read out of bounds for every one of them. */
  check("mode string EMERGENCY_HEAT", strcmp(MatterThermostat::getThermostatModeString(5), "EMERGENCY_HEAT") == 0);
  check("mode string PRECOOLING", strcmp(MatterThermostat::getThermostatModeString(6), "PRECOOLING") == 0);
  check("mode string FAN_ONLY", strcmp(MatterThermostat::getThermostatModeString(7), "FAN_ONLY") == 0);
  check("mode string DRY", strcmp(MatterThermostat::getThermostatModeString(8), "DRY") == 0);
  check("mode string SLEEP", strcmp(MatterThermostat::getThermostatModeString(9), "SLEEP") == 0);
  check("mode string clamps above the enum (10)", strcmp(MatterThermostat::getThermostatModeString(10), "UNKNOWN") == 0);
  check("mode string clamps at 255", strcmp(MatterThermostat::getThermostatModeString(255), "UNKNOWN") == 0);
}

static void test_setpoint_limit_getters(void) {
  MatterThermostat t;
  check("min heat setpoint is 7.0C", t.getMinHeatSetpoint() > 6.99 && t.getMinHeatSetpoint() < 7.01);
  check("max heat setpoint is 30.0C", t.getMaxHeatSetpoint() > 29.99 && t.getMaxHeatSetpoint() < 30.01);
  check("min cool setpoint is 16.0C", t.getMinCoolSetpoint() > 15.99 && t.getMinCoolSetpoint() < 16.01);
  check("max cool setpoint is 32.0C", t.getMaxCoolSetpoint() > 31.99 && t.getMaxCoolSetpoint() < 32.01);
  check("deadband is 2.5C", t.getDeadBand() > 2.49 && t.getDeadBand() < 2.51);
}

int main(void) {
  printf("\n===== MatterThermostat tests =====\n");
  test_begin_declares_and_adopts();
  test_begin_rejects_auto_mode_incompatible_sequence();
  test_set_mode_writes_update();
  test_set_mode_no_op_same_value();
  test_set_mode_rejects_cool_under_cooling_sequence();
  test_set_mode_auto_rejected_when_auto_mode_disabled();
  test_set_mode_auto_succeeds_under_compatible_sequence();
  test_local_temperature_push();
  test_local_temperature_negative();
  test_cooling_setpoint_write();
  test_heating_setpoint_write();
  test_cooling_heating_setpoints_combined_writes_cooling_then_heating();
  test_heating_setpoint_rejected_above_max_limit();
  test_cooling_setpoint_rejected_below_min_limit();
  test_auto_mode_deadband();
  test_controller_change_mode_fires_callback();
  test_controller_change_local_temperature_fires_callback();
  test_controller_change_cooling_setpoint_fires_callback();
  test_controller_change_heating_setpoint_fires_callback();
  test_failed_mode_write_returns_false();
  test_failed_local_temperature_write_returns_false();
  test_controller_change_delivers_typed_values();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  test_thermostat_mode_string();
  test_setpoint_limit_getters();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

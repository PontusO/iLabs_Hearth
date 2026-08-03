#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterHumiditySensor.h"
#include "MatterEndpoints/MatterPressureSensor.h"
#include "MatterEndpoints/MatterOccupancySensor.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUpHumiditySensor(MockStream &s, MatterHumiditySensor &sensor, double initial = 0.00) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0307\r\nOK\r\n");
  Matter.begin();
}

static void bringUpPressureSensor(MockStream &s, MatterPressureSensor &sensor, double initial = 0.00) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0305\r\nOK\r\n");
  Matter.begin();
}

static void bringUpOccupancySensor(MockStream &s, MatterOccupancySensor &sensor, bool initial = false) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0107\r\nOK\r\n");
  Matter.begin();
}

/* MatterHumiditySensor tests */
static void test_humidity_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor, 50.0);
  check("declared as humidity_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0307);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_humidity_sensor_set_humidity_writes(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor);
  s.expect("AT+MTATTR=1,1029,0,5000,1", "+MTATTR:1,1029,0,5000\r\nOK\r\n");
  check("setHumidity(50.0) succeeds", sensor.setHumidity(50.0));
  check("state cached", sensor.getHumidity() == 50.0);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_humidity_sensor_set_raw_humidity_writes(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor);
  s.expect("AT+MTATTR=1,1029,0,7500,1", "+MTATTR:1,1029,0,7500\r\nOK\r\n");
  check("setRawHumidity(7500) succeeds", sensor.setRawHumidity(7500));
  check("state cached", sensor.getRawHumidity() == 7500);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_humidity_sensor_controller_change_fires_callback(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor);
  int changeSeen = 0;
  double humidity = 0.0;
  sensor.onChange([&](double v) { changeSeen++; humidity = v; return true; });
  s.injectURC("+MTATTR:1,1029,0,4500");
  Hearth.poll();
  check("onChange fired", changeSeen == 1);
  check("callback received correct value", humidity == 45.0);
  check("cached state updated", sensor.getHumidity() == 45.0);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_humidity_sensor_failed_write_returns_false(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor);
  s.expect("AT+MTATTR=1,1029,0,5000,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setHumidity(50.0));
  check("and does not update the cache", sensor.getHumidity() == 0.0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_humidity_sensor_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterHumiditySensor sensor;
  bringUpHumiditySensor(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(50.0));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getHumidity() == 0.0);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* MatterPressureSensor tests */
static void test_pressure_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor, 1013.0);
  check("declared as pressure_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0305);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_pressure_sensor_set_pressure_writes(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  s.expect("AT+MTATTR=1,1027,0,500,1", "+MTATTR:1,1027,0,500\r\nOK\r\n");
  check("setPressure(500.0) succeeds", sensor.setPressure(500.0));
  check("state cached", sensor.getPressure() == 500.0);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_pressure_sensor_set_raw_pressure_writes(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  s.expect("AT+MTATTR=1,1027,0,800,1", "+MTATTR:1,1027,0,800\r\nOK\r\n");
  check("setRawPressure(800) succeeds", sensor.setRawPressure(800));
  check("state cached", sensor.getRawPressure() == 800);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_pressure_sensor_negative_value_writes(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  s.expect("AT+MTATTR=1,1027,0,-100,1", "+MTATTR:1,1027,0,-100\r\nOK\r\n");
  check("setPressure(-100.0) succeeds", sensor.setPressure(-100.0));
  check("state cached", sensor.getPressure() == -100.0);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_pressure_sensor_controller_change_fires_callback(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  int changeSeen = 0;
  double pressure = 0.0;
  sensor.onChange([&](double v) { changeSeen++; pressure = v; return true; });
  s.injectURC("+MTATTR:1,1027,0,900");
  Hearth.poll();
  check("onChange fired", changeSeen == 1);
  check("callback received correct value", pressure == 900.0);
  check("cached state updated", sensor.getPressure() == 900.0);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_pressure_sensor_failed_write_returns_false(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  s.expect("AT+MTATTR=1,1027,0,500,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setPressure(500.0));
  check("and does not update the cache", sensor.getPressure() == 0.0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_pressure_sensor_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterPressureSensor sensor;
  bringUpPressureSensor(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(500.0));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getPressure() == 0.0);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* MatterOccupancySensor tests */
static void test_occupancy_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor, true);
  check("declared as occupancy_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0107);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_occupancy_sensor_set_occupancy_writes(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  s.expect("AT+MTATTR=1,1030,0,1,1", "+MTATTR:1,1030,0,1\r\nOK\r\n");
  check("setOccupancy(true) succeeds", sensor.setOccupancy(true));
  check("state cached", sensor.getOccupancy() == true);
  check("operator bool agrees", (bool)sensor == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_occupancy_sensor_controller_change_fires_callback(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  int changeSeen = 0;
  bool state = false;
  sensor.onChange([&](bool v) { changeSeen++; state = v; return true; });
  s.injectURC("+MTATTR:1,1030,0,1");
  Hearth.poll();
  check("onChange fired", changeSeen == 1 && state == true);
  check("cached state updated", sensor.getOccupancy() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_occupancy_sensor_failed_write_returns_false(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  s.expect("AT+MTATTR=1,1030,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setOccupancy(true));
  check("and does not update the cache", sensor.getOccupancy() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_occupancy_sensor_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getOccupancy() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* HoldTime API returns false */
static void test_occupancy_sensor_set_hold_time_deferred(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  check("setHoldTime() is deferred to firmware, returns false", !sensor.setHoldTime(300));
  check("no AT traffic issued", s.scriptDrained());
}

static void test_occupancy_sensor_get_hold_time_returns_zero(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  check("getHoldTime() returns zero before anything is set", sensor.getHoldTime() == 0);
}

static void test_occupancy_sensor_set_hold_time_limits_deferred(void) {
  MockStream s; MatterOccupancySensor sensor;
  bringUpOccupancySensor(s, sensor);
  check("setHoldTimeLimits() is deferred to firmware, returns false", !sensor.setHoldTimeLimits(60, 600, 300));
  check("no AT traffic issued", s.scriptDrained());
}

int main(void) {
  printf("\n===== MatterHumiditySensor, MatterPressureSensor, MatterOccupancySensor tests =====\n");

  printf("\nMatterHumiditySensor:\n");
  test_humidity_sensor_begin_declares_and_adopts();
  test_humidity_sensor_set_humidity_writes();
  test_humidity_sensor_set_raw_humidity_writes();
  test_humidity_sensor_controller_change_fires_callback();
  test_humidity_sensor_failed_write_returns_false();
  test_humidity_sensor_rebegin_after_reconcile_refused();

  printf("\nMatterPressureSensor:\n");
  test_pressure_sensor_begin_declares_and_adopts();
  test_pressure_sensor_set_pressure_writes();
  test_pressure_sensor_set_raw_pressure_writes();
  test_pressure_sensor_negative_value_writes();
  test_pressure_sensor_controller_change_fires_callback();
  test_pressure_sensor_failed_write_returns_false();
  test_pressure_sensor_rebegin_after_reconcile_refused();

  printf("\nMatterOccupancySensor:\n");
  test_occupancy_sensor_begin_declares_and_adopts();
  test_occupancy_sensor_set_occupancy_writes();
  test_occupancy_sensor_controller_change_fires_callback();
  test_occupancy_sensor_failed_write_returns_false();
  test_occupancy_sensor_rebegin_after_reconcile_refused();
  test_occupancy_sensor_set_hold_time_deferred();
  test_occupancy_sensor_get_hold_time_returns_zero();
  test_occupancy_sensor_set_hold_time_limits_deferred();

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

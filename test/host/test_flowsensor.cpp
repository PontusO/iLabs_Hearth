/*
 * test_flowsensor.cpp - MatterFlowSensor: a Hearth-original class, no
 * arduino-esp32 counterpart. FlowMeasurement cluster 0x0404 (1028
 * decimal), MeasuredValue attribute 0x0000, nullable uint16. Recipe minimum
 * per task-C2-brief.md: declaration pin, set-method exact wire string,
 * failed-write cache discipline, controller URC dispatch updating the
 * getter, re-begin refusal.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterFlowSensor.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterFlowSensor &sensor, uint16_t initial = 0) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0306\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor, 300);
  check("declared as flow_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0306);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial raw value cached", sensor.getRawMeasuredValue() == 300);
}

static void test_set_raw_measured_value_writes(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor);
  s.expect("AT+MTATTR=1,1028,0,150,1", "+MTATTR:1,1028,0,150\r\nOK\r\n");
  check("setRawMeasuredValue(150) succeeds", sensor.setRawMeasuredValue(150));
  check("state cached", sensor.getRawMeasuredValue() == 150);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_raw_measured_value_no_op_same_value(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor, 150);
  check("setRawMeasuredValue(150) when already 150 succeeds without a write", sensor.setRawMeasuredValue(150));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_write_returns_false(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor);
  s.expect("AT+MTATTR=1,1028,0,150,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setRawMeasuredValue(150));
  check("and does not update the cache", sensor.getRawMeasuredValue() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_dispatches_and_updates_getter(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor);
  s.injectURC("+MTATTR:1,1028,0,275");
  Hearth.poll();
  check("cached state updated from the URC", sensor.getRawMeasuredValue() == 275);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterFlowSensor sensor;
  bringUp(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(9999));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getRawMeasuredValue() == 0);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterFlowSensor tests =====\n");
  test_begin_declares_and_adopts();
  test_set_raw_measured_value_writes();
  test_set_raw_measured_value_no_op_same_value();
  test_failed_write_returns_false();
  test_controller_change_dispatches_and_updates_getter();
  test_rebegin_after_reconcile_refused();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

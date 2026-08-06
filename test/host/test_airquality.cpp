/*
 * test_airquality.cpp - MatterAirQualitySensor: a Hearth-original class, no
 * arduino-esp32 counterpart. AirQuality cluster 0x005B (91 decimal),
 * AirQuality attribute 0x0000, enum8. Recipe minimum per task-C2-brief.md:
 * declaration pin, set-method exact wire string, failed-write cache
 * discipline, controller URC dispatch updating the getter, re-begin
 * refusal.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterAirQualitySensor.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterAirQualitySensor &sensor, MatterAirQualitySensor::AirQuality_t initial = MatterAirQualitySensor::kUnknown) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x002C\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor, MatterAirQualitySensor::kGood);
  check("declared as air_quality_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x002C);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial quality cached", sensor.getAirQuality() == MatterAirQualitySensor::kGood);
}

static void test_set_air_quality_writes(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor);
  s.expect("AT+MTATTR=1,91,0,4,1", "OK\r\n");  /* 4 == kPoor; B139: AQ writes never echo a URC (AAI-served), bare OK is the real wire */
  check("setAirQuality(kPoor) succeeds", sensor.setAirQuality(MatterAirQualitySensor::kPoor));
  check("state cached", sensor.getAirQuality() == MatterAirQualitySensor::kPoor);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_set_air_quality_no_op_same_value(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor, MatterAirQualitySensor::kFair);
  check("setAirQuality(kFair) when already kFair succeeds without a write", sensor.setAirQuality(MatterAirQualitySensor::kFair));
  check("no AT traffic for a no-op write", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_write_returns_false(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor);
  s.expect("AT+MTATTR=1,91,0,4,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setAirQuality(MatterAirQualitySensor::kPoor));
  check("and does not update the cache", sensor.getAirQuality() == MatterAirQualitySensor::kUnknown);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_dispatches_and_updates_getter(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor);
  s.injectURC("+MTATTR:1,91,0,6");  /* 6 == kExtremelyPoor */
  Hearth.poll();
  check("cached state updated from the URC", sensor.getAirQuality() == MatterAirQualitySensor::kExtremelyPoor);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterAirQualitySensor sensor;
  bringUp(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(MatterAirQualitySensor::kVeryPoor));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getAirQuality() == MatterAirQualitySensor::kUnknown);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterAirQualitySensor tests =====\n");
  test_begin_declares_and_adopts();
  test_set_air_quality_writes();
  test_set_air_quality_no_op_same_value();
  test_failed_write_returns_false();
  test_controller_change_dispatches_and_updates_getter();
  test_rebegin_after_reconcile_refused();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

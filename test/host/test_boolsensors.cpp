#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterContactSensor.h"
#include "MatterEndpoints/MatterRainSensor.h"
#include "MatterEndpoints/MatterWaterFreezeDetector.h"
#include "MatterEndpoints/MatterWaterLeakDetector.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUpContactSensor(MockStream &s, MatterContactSensor &sensor, bool initial = false) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0015\r\nOK\r\n");
  Matter.begin();
}

static void bringUpRainSensor(MockStream &s, MatterRainSensor &sensor, bool initial = false) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sensor.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0044\r\nOK\r\n");
  Matter.begin();
}

static void bringUpWaterFreezeDetector(MockStream &s, MatterWaterFreezeDetector &detector, bool initial = false) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  detector.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0041\r\nOK\r\n");
  Matter.begin();
}

static void bringUpWaterLeakDetector(MockStream &s, MatterWaterLeakDetector &detector, bool initial = false) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  detector.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0043\r\nOK\r\n");
  Matter.begin();
}

/* MatterContactSensor tests */
static void test_contact_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterContactSensor sensor;
  bringUpContactSensor(s, sensor);
  check("declared as contact_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0015);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_contact_sensor_set_contact_writes(void) {
  MockStream s; MatterContactSensor sensor;
  bringUpContactSensor(s, sensor);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTATTR:1,69,0,1\r\nOK\r\n");
  check("setContact(true) succeeds", sensor.setContact(true));
  check("state cached", sensor.getContact() == true);
  check("operator bool agrees", (bool)sensor == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_contact_sensor_controller_change_fires_callback(void) {
  MockStream s; MatterContactSensor sensor;
  bringUpContactSensor(s, sensor);
  int changeSeen = 0;
  bool state = false;
  sensor.onChange([&](bool v) { changeSeen++; state = v; return true; });
  s.injectURC("+MTATTR:1,69,0,1");
  Hearth.poll();
  check("onChange fired", changeSeen == 1 && state == true);
  check("cached state updated", sensor.getContact() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_contact_sensor_failed_write_returns_false(void) {
  MockStream s; MatterContactSensor sensor;
  bringUpContactSensor(s, sensor);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setContact(true));
  check("and does not update the cache", sensor.getContact() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_contact_sensor_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterContactSensor sensor;
  bringUpContactSensor(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getContact() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* MatterRainSensor tests */
static void test_rain_sensor_begin_declares_and_adopts(void) {
  MockStream s; MatterRainSensor sensor;
  bringUpRainSensor(s, sensor);
  check("declared as rain_sensor", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0044);
  check("adopted endpoint 1", sensor.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rain_sensor_set_rain_writes(void) {
  MockStream s; MatterRainSensor sensor;
  bringUpRainSensor(s, sensor);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTATTR:1,0x0045,0,1\r\nOK\r\n");
  check("setRain(true) succeeds", sensor.setRain(true));
  check("state cached", sensor.getRain() == true);
  check("operator bool agrees", (bool)sensor == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_rain_sensor_controller_change_fires_callback(void) {
  MockStream s; MatterRainSensor sensor;
  bringUpRainSensor(s, sensor);
  int changeSeen = 0;
  bool state = false;
  sensor.onChange([&](bool v) { changeSeen++; state = v; return true; });
  s.injectURC("+MTATTR:1,69,0,1");
  Hearth.poll();
  check("onChange fired", changeSeen == 1 && state == true);
  check("cached state updated", sensor.getRain() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rain_sensor_failed_write_returns_false(void) {
  MockStream s; MatterRainSensor sensor;
  bringUpRainSensor(s, sensor);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !sensor.setRain(true));
  check("and does not update the cache", sensor.getRain() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rain_sensor_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterRainSensor sensor;
  bringUpRainSensor(s, sensor);
  check("a second begin() after Matter.begin() is refused", !sensor.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", sensor.getRain() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* MatterWaterFreezeDetector tests */
static void test_freeze_detector_begin_declares_and_adopts(void) {
  MockStream s; MatterWaterFreezeDetector detector;
  bringUpWaterFreezeDetector(s, detector);
  check("declared as water_freeze_detector", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0041);
  check("adopted endpoint 1", detector.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_freeze_detector_set_freeze_writes(void) {
  MockStream s; MatterWaterFreezeDetector detector;
  bringUpWaterFreezeDetector(s, detector);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTATTR:1,0x0045,0,1\r\nOK\r\n");
  check("setFreeze(true) succeeds", detector.setFreeze(true));
  check("state cached", detector.getFreeze() == true);
  check("operator bool agrees", (bool)detector == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_freeze_detector_controller_change_fires_callback(void) {
  MockStream s; MatterWaterFreezeDetector detector;
  bringUpWaterFreezeDetector(s, detector);
  int changeSeen = 0;
  bool state = false;
  detector.onChange([&](bool v) { changeSeen++; state = v; return true; });
  s.injectURC("+MTATTR:1,69,0,1");
  Hearth.poll();
  check("onChange fired", changeSeen == 1 && state == true);
  check("cached state updated", detector.getFreeze() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_freeze_detector_failed_write_returns_false(void) {
  MockStream s; MatterWaterFreezeDetector detector;
  bringUpWaterFreezeDetector(s, detector);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !detector.setFreeze(true));
  check("and does not update the cache", detector.getFreeze() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_freeze_detector_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterWaterFreezeDetector detector;
  bringUpWaterFreezeDetector(s, detector);
  check("a second begin() after Matter.begin() is refused", !detector.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", detector.getFreeze() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

/* MatterWaterLeakDetector tests */
static void test_leak_detector_begin_declares_and_adopts(void) {
  MockStream s; MatterWaterLeakDetector detector;
  bringUpWaterLeakDetector(s, detector);
  check("declared as water_leak_detector", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0043);
  check("adopted endpoint 1", detector.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_leak_detector_set_leak_writes(void) {
  MockStream s; MatterWaterLeakDetector detector;
  bringUpWaterLeakDetector(s, detector);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTATTR:1,0x0045,0,1\r\nOK\r\n");
  check("setLeak(true) succeeds", detector.setLeak(true));
  check("state cached", detector.getLeak() == true);
  check("operator bool agrees", (bool)detector == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_leak_detector_controller_change_fires_callback(void) {
  MockStream s; MatterWaterLeakDetector detector;
  bringUpWaterLeakDetector(s, detector);
  int changeSeen = 0;
  bool state = false;
  detector.onChange([&](bool v) { changeSeen++; state = v; return true; });
  s.injectURC("+MTATTR:1,69,0,1");
  Hearth.poll();
  check("onChange fired", changeSeen == 1 && state == true);
  check("cached state updated", detector.getLeak() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_leak_detector_failed_write_returns_false(void) {
  MockStream s; MatterWaterLeakDetector detector;
  bringUpWaterLeakDetector(s, detector);
  s.expect("AT+MTATTR=1,69,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !detector.setLeak(true));
  check("and does not update the cache", detector.getLeak() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_leak_detector_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterWaterLeakDetector detector;
  bringUpWaterLeakDetector(s, detector);
  check("a second begin() after Matter.begin() is refused", !detector.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", detector.getLeak() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

int main(void) {
  printf("\n===== MatterContactSensor, MatterRainSensor, MatterWaterFreezeDetector, MatterWaterLeakDetector tests =====\n");

  printf("\nMatterContactSensor:\n");
  test_contact_sensor_begin_declares_and_adopts();
  test_contact_sensor_set_contact_writes();
  test_contact_sensor_controller_change_fires_callback();
  test_contact_sensor_failed_write_returns_false();
  test_contact_sensor_rebegin_after_reconcile_refused();

  printf("\nMatterRainSensor:\n");
  test_rain_sensor_begin_declares_and_adopts();
  test_rain_sensor_set_rain_writes();
  test_rain_sensor_controller_change_fires_callback();
  test_rain_sensor_failed_write_returns_false();
  test_rain_sensor_rebegin_after_reconcile_refused();

  printf("\nMatterWaterFreezeDetector:\n");
  test_freeze_detector_begin_declares_and_adopts();
  test_freeze_detector_set_freeze_writes();
  test_freeze_detector_controller_change_fires_callback();
  test_freeze_detector_failed_write_returns_false();
  test_freeze_detector_rebegin_after_reconcile_refused();

  printf("\nMatterWaterLeakDetector:\n");
  test_leak_detector_begin_declares_and_adopts();
  test_leak_detector_set_leak_writes();
  test_leak_detector_controller_change_fires_callback();
  test_leak_detector_failed_write_returns_false();
  test_leak_detector_rebegin_after_reconcile_refused();

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

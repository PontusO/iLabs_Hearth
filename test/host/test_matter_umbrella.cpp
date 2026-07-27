/*
 * test_matter_umbrella.cpp - regression test for "a bare #include <Matter.h>
 * is sufficient to declare a device object at file scope", the parity gap
 * Task 9's compile pass against real upstream examples found.
 *
 * Upstream's own Matter.h aggregates all twenty of its MatterEndpoints/
 * Matter*.h headers, so an unmodified sketch's `#include <Matter.h>`
 * followed by a file-scope `MatterOnOffLight light;` (or any other device
 * type) just works, with no endpoint header included by the sketch itself.
 * This library's Hearth.h (which src/Matter.h shims to) did not do the
 * same: it included only HearthLink.h and HearthCompat.h, so the exact
 * pattern every upstream example uses failed with "'MatterOnOffLight' does
 * not name a type".
 *
 * This file's only iLabs Hearth include is "Matter.h" itself: no
 * "Hearth.h", no "MatterEndPoint.h", no any header under MatterEndpoints/.
 * Before the
 * fix (adding MatterEndPoint.h and the four endpoint headers to Hearth.h's
 * own include list), this file failed to compile with exactly that error
 * for all four device types below. If a future change narrows Matter.h's
 * includes again, this file breaks the build before any example sketch
 * would, which is the point: catch it here, in five seconds, not on a
 * compile pass against real hardware examples.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Matter.h"

/*
 * File-scope device declarations: exactly the pattern every upstream
 * example uses. If Matter.h did not pull in the endpoint headers, none of
 * these four lines would compile, and the build would stop right here
 * rather than reaching main().
 */
MatterOnOffLight umbrellaOnOff;
MatterDimmableLight umbrellaDimmable;
MatterColorTemperatureLight umbrellaColorTemp;
MatterTemperatureSensor umbrellaTempSensor;

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/*
 * Each test brings its device up and issues one real write, so the class
 * is not just forward-declarable but fully linked: attributeChangeCB,
 * hearthAttrTypeFor and the AT+MTATTR path all have to resolve for these
 * to pass, which a merely-declared-but-never-used global would not prove.
 */

static void test_onoff_resolves_and_works(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  umbrellaOnOff.begin(false);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("MatterOnOffLight declared via Matter.h alone", umbrellaOnOff.setOnOff(true));
  check("state reads back", umbrellaOnOff.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_dimmable_resolves_and_works(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  umbrellaDimmable.begin(false, 64);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0101\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTATTR=1,8,0,128,1", "OK\r\n");
  check("MatterDimmableLight declared via Matter.h alone", umbrellaDimmable.setBrightness(128));
  check("brightness reads back", umbrellaDimmable.getBrightness() == 128);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_colortemp_resolves_and_works(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  umbrellaColorTemp.begin(false, 64, 200);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010C\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTATTR=1,768,7,370,1", "OK\r\n");
  check("MatterColorTemperatureLight declared via Matter.h alone", umbrellaColorTemp.setColorTemperature(370));
  check("color temperature reads back", umbrellaColorTemp.getColorTemperature() == 370);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_tempsensor_resolves_and_works(void) {
  MockStream s;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  umbrellaTempSensor.begin(0.00);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0302\r\nOK\r\n");
  Matter.begin();
  s.expect("AT+MTATTR=1,1026,0,2350,1", "OK\r\n");
  check("MatterTemperatureSensor declared via Matter.h alone", umbrellaTempSensor.setTemperature(23.50));
  check("temperature reads back", umbrellaTempSensor.getTemperature() == 23.5);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("===== Matter.h umbrella include tests =====\n");
  test_onoff_resolves_and_works();
  test_dimmable_resolves_and_works();
  test_colortemp_resolves_and_works();
  test_tempsensor_resolves_and_works();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

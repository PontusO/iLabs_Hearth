/*
 * test_log_hook.cpp - U2: outside Arduino the core's warnings reach
 * hearthLogHook (the iLabs_Hearth_C package binds it to the customer's
 * log), and with no hook they are dropped as before.
 */
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterDimmableLight.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static std::vector<std::string> g_lines;
static void capture(const char *line) { g_lines.push_back(line); }

static const char *kActiveFabric =
  "Hearth: endpoint composition is changing on a device with an active fabric; "
  "the commissioned controller's cached data model may need re-pairing to see it.";
static const char *kUnknownFabric =
  "Hearth: could not confirm the fabric count before changing the endpoint "
  "composition; warning as a precaution in case the device is commissioned.";

/* The library's own test_commissioned_change_warns script: the C6 holds an
 * on/off light, the sketch declares a dimmable light, one fabric. */
static void reconcileWithFabricReply(const char *fabricReply) {
  MockStream s;
  MatterDimmableLight light;
  MatterEndPoint::hearthClearDeclarations();
  light.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  s.expect("AT+MTFABRICS?", fabricReply);
  s.expect("AT+MTEPCLEAR", "OK\r\n");
  s.expect("AT+MTEP=0x0101", "OK\r\n");
  s.expect("AT+MTEPAPPLY", "OK\r\n+MTREADY\r\n");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0101\r\nOK\r\n");
  Hearth.begin(s);
  Matter.begin();
  check("reconcile script drained", s.scriptDrained() && s.unexpected().empty());
}

static void test_active_fabric_warning_reaches_hook(void) {
  g_lines.clear();
  hearthLogHook = capture;
  reconcileWithFabricReply("+MTFABRICS:1\r\nOK\r\n");
  check("one line logged", g_lines.size() == 1);
  check("the active-fabric warning, verbatim", g_lines.size() == 1 && g_lines[0] == kActiveFabric);
  hearthLogHook = nullptr;
}

static void test_unknown_fabric_warning_reaches_hook(void) {
  g_lines.clear();
  hearthLogHook = capture;
  reconcileWithFabricReply("ERROR\r\n");
  check("the could-not-confirm warning, verbatim", g_lines.size() == 1 && g_lines[0] == kUnknownFabric);
  hearthLogHook = nullptr;
}

static void test_no_hook_drops_quietly(void) {
  g_lines.clear();
  hearthLogHook = nullptr;
  reconcileWithFabricReply("+MTFABRICS:1\r\nOK\r\n");
  check("nothing captured and nothing crashed", g_lines.empty());
}

static void test_log_e_formats_into_hook(void) {
  g_lines.clear();
  hearthLogHook = capture;
  log_e("mode %d of %s", 3, "seven");
  check("log_e reaches the hook formatted", g_lines.size() == 1 && g_lines[0] == "mode 3 of seven");
  hearthLogHook = nullptr;
}

int main() {
  printf("test_log_hook\n");
  test_active_fabric_warning_reaches_hook();
  test_unknown_fabric_warning_reaches_hook();
  test_no_hook_drops_quietly();
  test_log_e_formats_into_hook();
  printf("===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}

#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void test_version(void) {
  MockStream s;
  s.expect("AT+MTVER?", "+MTVER:0.4.0\r\nOK\r\n");
  Hearth.begin(s);
  check("firmwareVersion parses +MTVER", Hearth.firmwareVersion() == String("0.4.0"));
}

static void test_last_error_cleared_on_success(void) {
  MockStream s;
  s.expect("AT+MTBOGUS", "+MTERR:8\r\nERROR\r\n");
  s.expect("AT", "OK\r\n");
  Hearth.begin(s);
  Hearth.hearthCommand("AT+MTBOGUS");
  check("error code retained", Hearth.lastError() == 8);
  Hearth.hearthCommand("AT");
  check("a later success clears it", Hearth.lastError() == 0);
}

static void test_reboot_urc_raises_event(void) {
  MockStream s;
  Hearth.begin(s);
  hearthEvent_t got = HEARTH_LINK_DOWN;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) { got = e; seen++; });
  s.injectURC("+MTREADY");
  Hearth.poll();
  check("an unexpected +MTREADY is a reboot", seen == 1 && got == HEARTH_COPROCESSOR_REBOOTED);
}

static void test_link_up_probe(void) {
  MockStream s;
  s.expect("AT", "OK\r\n");
  Hearth.begin(s);
  check("linkUp probes with a bare AT", Hearth.linkUp());
  check("script drained", s.scriptDrained());
}

int main(void) {
  printf("\n===== Hearth link tests =====\n");
  test_version();
  test_last_error_cleared_on_success();
  test_reboot_urc_raises_event();
  test_link_up_probe();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterGenericSwitch.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterGenericSwitch &sw) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  sw.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x000F\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterGenericSwitch sw;
  bringUp(s, sw);
  check("declared as generic_switch", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x000F);
  check("adopted endpoint 1", sw.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_click_sends_and_returns_true_on_ok(void) {
  MockStream s; MatterGenericSwitch sw;
  bringUp(s, sw);
  s.expect("AT+MTSWITCH=1", "OK\r\n");
  check("click() sends exactly AT+MTSWITCH=<ep> and succeeds", sw.click());
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * +MTERR:3 is MT_ERR_NO_CLUSTER (AT_MT_SPEC.md S3.15/S5): the endpoint
 * exists but carries no Switch cluster. click() maps that, like any other
 * +MTERR reply, to a plain false: there is no cache to leave untouched,
 * since a switch event is fire-and-forget rather than a state write.
 */
static void test_click_mterr3_returns_false(void) {
  MockStream s; MatterGenericSwitch sw;
  bringUp(s, sw);
  s.expect("AT+MTSWITCH=1", "+MTERR:3\r\nERROR\r\n");
  check("click() reports failure on +MTERR:3 (no Switch cluster)", !sw.click());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * begin() only declares (registry entry, no AT traffic); the endpoint id is
 * not adopted until Matter.begin() reconciles against the C6. click()
 * called in that window must fail without putting anything on the wire,
 * the same "endpoint 0 is unaddressable" reasoning MatterEndPoint's own
 * write helpers use, just enforced locally since click() does not go
 * through updateAttributeVal().
 */
static void test_click_before_reconcile_returns_false_without_traffic(void) {
  MockStream s; MatterGenericSwitch sw;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", sw.begin());
  check("click() before reconcile fails", !sw.click());
  check("no traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_is_refused(void) {
  MockStream s; MatterGenericSwitch sw;
  bringUp(s, sw);

  check("a second begin() after Matter.begin() is refused", !sw.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTSWITCH=1", "OK\r\n");
  check("so the next click() still works", sw.click());
  check("and the command happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterGenericSwitch tests =====\n");
  test_begin_declares_and_adopts();
  test_click_sends_and_returns_true_on_ok();
  test_click_mterr3_returns_false();
  test_click_before_reconcile_returns_false_without_traffic();
  test_rebegin_after_reconcile_is_refused();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

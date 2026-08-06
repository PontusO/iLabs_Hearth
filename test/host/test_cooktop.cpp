/*
 * test_cooktop.cpp - MatterCooktop, the OffOnly device class (C3 of the
 * ten-type swoop). No on(), no toggle(), no operator=(bool): the class is
 * structurally incapable of writing OnOff=1. test_full_api_never_emits_an_
 * onoff_write_of_one below exercises every public method in one run and
 * then asserts scriptDrained()/unexpected().empty() with only OnOff-write-0
 * lines ever having been scripted, pinning that claim at the wire level
 * rather than merely by reading the header.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterCooktop.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterCooktop &cooktop) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  cooktop.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0078\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);
  check("declared as cooktop", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0078);
  check("adopted endpoint 1", cooktop.getEndPointId() == 1);
  check("boots off-capable, cache starts false", cooktop.getOnOff() == false);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_off_when_already_off_is_a_noop(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);
  check("off() on an already-off cache succeeds", cooktop.off());
  check("cache still false", cooktop.getOnOff() == false);
  check("no AT traffic for a no-op off()", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_off_after_controller_turned_it_on_writes_zero(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);
  /* Local turn-on: nothing this class exposes can cause this write. It
   * arrives purely as a URC, as if a human at the physical cooktop (or the
   * device's own logic) turned a burner on and the firmware reported it. */
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("the cache now reads on, fed only by the URC", cooktop.getOnOff() == true);
  check("no echo written back for the URC itself", s.scriptDrained());

  s.expect("AT+MTATTR=1,6,0,0,1", "+MTATTR:1,6,0,0\r\nOK\r\n");
  check("off() succeeds and writes OnOff=0", cooktop.off());
  check("cache now false", cooktop.getOnOff() == false);
  check("the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_fires_callbacks(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  cooktop.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  cooktop.onChange([&](bool v) { changeSeen++; (void)v; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("cached state updated", cooktop.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);

  check("a second begin() after Matter.begin() is refused", !cooktop.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", cooktop.getOnOff() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_off_write_returns_false(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  s.expect("AT+MTATTR=1,6,0,0,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected off() write returns false", !cooktop.off());
  check("and does not update the cache", cooktop.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingCooktop : public MatterCooktop {
public:
  esp_matter_val_type_t seenType = ESP_MATTER_VAL_TYPE_INVALID;
  bool seenBool = false;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    seenType = val->type;
    seenBool = val->val.b;
    return MatterCooktop::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_boolean(void) {
  MockStream s; TypeCheckingCooktop cooktop;
  bringUp(s, cooktop);
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("value type is boolean, not the generic integer", cooktop.seenType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("value lands in val.b", cooktop.seenBool == true);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The pinning test: exercise begin(), getOnOff(), onChange(), onChangeOnOff(),
 * updateAccessory(), operator bool(), attributeChangeCB() (via a URC that
 * turns the cache on with no wire write), off() (twice: once as a no-op
 * before the URC and once as a real write after it), and end() -- the
 * class's ENTIRE public surface -- then assert that scriptDrained() and
 * unexpected().empty() hold with only ONE line ever scripted, and that
 * line is an OnOff write of 0. There is no on(), no toggle(), no
 * operator=(bool): nothing in the public API above could have scripted a
 * write of 1, and this test proves none did.
 */
static void test_full_api_never_emits_an_onoff_write_of_one(void) {
  MockStream s; MatterCooktop cooktop;
  bringUp(s, cooktop);

  int changeSeen = 0, onOffSeen = 0;
  bool lastChange = false, lastOnOff = false;
  cooktop.onChange([&](bool v) { changeSeen++; lastChange = v; return true; });
  cooktop.onChangeOnOff([&](bool v) { onOffSeen++; lastOnOff = v; return true; });

  check("initial getOnOff() is false", cooktop.getOnOff() == false);
  check("initial operator bool() is false", (bool)cooktop == false);
  check("off() on an already-off device is a no-op, no wire traffic", cooktop.off());
  check("still nothing on the wire after the no-op off()", s.scriptDrained());

  /* updateAccessory() is unconditional (matching every sibling class): it
   * hands the current cached state to onChange whether or not it just
   * changed. One call here, so onChange has fired once (with the still-off
   * state) before the URC below. */
  cooktop.updateAccessory();
  check("updateAccessory() alone pushed the off state through onChange", changeSeen == 1 && lastChange == false);

  /* The only way this cache ever becomes true: a URC, not a public setter. */
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("attributeChangeCB (via the URC) turned the cache on", cooktop.getOnOff() == true);
  check("operator bool() agrees", (bool)cooktop == true);
  check("onChangeOnOff fired from the URC", onOffSeen == 1 && lastOnOff == true);
  check("onChange fired again from the URC", changeSeen == 2 && lastChange == true);
  check("the URC itself produced no wire write-back", s.scriptDrained());

  cooktop.updateAccessory();
  check("updateAccessory() again pushed the (now on) state through onChange", changeSeen == 3 && lastChange == true);

  /* The one and only remote action this class exposes, and the one and
   * only place a write can originate: it must write 0, never 1. Per
   * MatterEndPoint.cpp's hearthWriteAttr() comment, a successful write
   * always echoes a +MTATTR URC back to this host, so this call also drives
   * attributeChangeCB a second time (with the same false value off() itself
   * already cached), which is why the counts below land on 2/4, not 1/3. */
  s.expect("AT+MTATTR=1,6,0,0,1", "+MTATTR:1,6,0,0\r\nOK\r\n");
  check("off() now performs the real write", cooktop.off());
  check("cache reflects it", cooktop.getOnOff() == false);
  check("operator bool() reflects it", (bool)cooktop == false);
  check("the write's own echo drove onChangeOnOff once more", onOffSeen == 2 && lastOnOff == false);
  check("and onChange once more", changeSeen == 4 && lastChange == false);

  cooktop.end();

  check("the only line ever scripted was consumed, nothing left over", s.scriptDrained());
  check("and nothing arrived that was not scripted", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterCooktop tests =====\n");
  test_begin_declares_and_adopts();
  test_off_when_already_off_is_a_noop();
  test_off_after_controller_turned_it_on_writes_zero();
  test_controller_change_fires_callbacks();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  test_failed_off_write_returns_false();
  test_controller_change_delivers_typed_boolean();
  test_full_api_never_emits_an_onoff_write_of_one();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

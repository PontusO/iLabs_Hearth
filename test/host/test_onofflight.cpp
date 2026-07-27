#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterOnOffLight.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterOnOffLight &light, bool initial) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  check("declared as on_off_light", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0100);
  check("adopted endpoint 1", light.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_toggle(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  light = true;
  check("operator= writes and caches", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

/* A controller turning the light on arrives as a +MTATTR URC. This is the
 * path the whole library exists for. */
static void test_controller_change_fires_callback(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  light.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  light.onChange([&](bool v) { changeSeen++; (void)v; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("cached state updated", light.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_write_returns_false(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !light.setOnOff(true));
  check("and does not update the cache", light.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * hearthDispatchAttr (Hearth.cpp) must rebuild a +MTATTR URC's value as
 * the attribute's real esp_matter_val_type_t, not a hardcoded INTEGER, or
 * an upstream-style attributeChangeCB override that reads val->val.b (as
 * every upstream endpoint class does) reads a union member that was never
 * written. This subclass mimics exactly that: an override that inspects
 * the raw esp_matter_attr_val_t itself, the way a sketch overriding the
 * virtual would, then defers to the base implementation.
 */
class TypeCheckingLight : public MatterOnOffLight {
public:
  esp_matter_val_type_t seenType = ESP_MATTER_VAL_TYPE_INVALID;
  bool seenBool = false;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    seenType = val->type;
    seenBool = val->val.b;
    return MatterOnOffLight::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_boolean(void) {
  MockStream s; TypeCheckingLight light;
  bringUp(s, light, false);
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("value type is boolean, not the generic integer", light.seenType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("value lands in val.b, where upstream's own callbacks read it", light.seenBool == true);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterOnOffLight tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_toggle();
  test_assignment_operator();
  test_controller_change_fires_callback();
  test_failed_write_returns_false();
  test_controller_change_delivers_typed_boolean();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

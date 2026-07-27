#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterDimmableLight.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterDimmableLight &light, bool initial, uint8_t brightness) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(initial, brightness);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0101\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  check("declared as dimmable_light", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0101);
  check("adopted endpoint 1", light.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * RE-REVIEW, IMPORTANT 2. The C6 answers a mode-1 write with the +MTATTR
 * URC its own attribute callback raises, then OK (AT_MT_SPEC.md S3.8).
 * Scripted here as the firmware actually behaves; see
 * test_onofflight.cpp's own note for the full reasoning.
 */
static void test_set_on_off_writes(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * RE-REVIEW, IMPORTANT 2, this class's own version of the behaviour pin.
 * The echo is dispatched as an ordinary URC, so onChange fires
 * synchronously from inside the setter, with the *echoed* value and the
 * other cached value alongside it. A library call from that callback is
 * refused with the re-entrancy code, which is Hearth's divergence from
 * upstream and is documented in the README.
 */
static void test_local_write_echo_fires_onchange_from_inside_the_setter(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  int changeSeen = 0, nestedRc = 0;
  bool state = false;
  uint8_t levelAtChange = 0;
  light.onChange([&](bool on, uint8_t level) {
    changeSeen++;
    state = on;
    levelAtChange = level;
    nestedRc = Hearth.hearthCommand("AT+MTVER?");
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  bool ok = light.setOnOff(true);

  check("the write succeeds", ok);
  check("onChange fired once, from inside the setter", changeSeen == 1);
  check("with the echoed on/off value", state == true);
  check("and the cached brightness alongside it", levelAtChange == 64);
  check("a library call from the callback is refused", nestedRc == HEARTH_CMD_REENTRANT);
  check("no extra traffic on the wire", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  light = true;
  check("operator= writes and caches", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_write(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, true, 64);
  int brightnessSeen = 0;
  uint8_t level = 0;
  light.onChangeBrightness([&](uint8_t v) {
    brightnessSeen++;
    level = v;
    return true;
  });
  /* Same write echo as the on/off case, on the LevelControl cluster. */
  s.expect("AT+MTATTR=1,8,0,128,1", "+MTATTR:1,8,0,128\r\nOK\r\n");
  check("setBrightness writes CurrentLevel", light.setBrightness(128));
  check("brightness cached", light.getBrightness() == 128);
  check("the echo fired onChangeBrightness from inside the setter", brightnessSeen == 1 && level == 128);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_max_brightness_constant(void) {
  check("MAX_BRIGHTNESS is 255", MatterDimmableLight::MAX_BRIGHTNESS == 255);
}

/* A controller turning the light on arrives as a +MTATTR URC on the OnOff
 * cluster. onChange's two-argument form must fire with the current cached
 * brightness alongside the new on/off state. */
static void test_controller_onoff_change_fires_callback(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false; uint8_t levelAtChange = 0;
  light.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  light.onChange([&](bool on, uint8_t level) { changeSeen++; levelAtChange = level; (void)on; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange fired with both values", changeSeen == 1 && levelAtChange == 64);
  check("cached state updated", light.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_from_controller(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, true, 64);
  int seen = 0; uint8_t level = 0; int bothSeen = 0;
  light.onChangeBrightness([&](uint8_t v) { seen++; level = v; return true; });
  light.onChange([&](bool, uint8_t) { bothSeen++; return true; });
  s.injectURC("+MTATTR:1,8,0,200");
  Hearth.poll();
  check("onChangeBrightness fired", seen == 1 && level == 200);
  check("onChange fired with both values", bothSeen == 1);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_write_returns_false(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !light.setOnOff(true));
  check("and does not update the cache", light.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_failed_write_returns_false(void) {
  MockStream s; MatterDimmableLight light;
  bringUp(s, light, false, 64);
  s.expect("AT+MTATTR=1,8,0,128,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected brightness write returns false", !light.setBrightness(128));
  check("and does not update the cache", light.getBrightness() == 64);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * hearthDispatchAttr (Hearth.cpp) must rebuild a +MTATTR URC's value as the
 * attribute's real esp_matter_val_type_t, not a hardcoded INTEGER. The
 * on/off pair must arrive as BOOLEAN, and the LevelControl/CurrentLevel pair
 * (the type new to this task) must arrive as UINT8, not the INTEGER default
 * a base MatterEndPoint::hearthAttrTypeFor() would give it. Per Task 6's
 * finding, only the .type field distinguishes a correct rebuild from a wrong
 * one: a .val.u assertion alone would pass even against a mistyped rebuild,
 * since .val.u and .val.i alias the same bytes for small positive values.
 */
class TypeCheckingDimmableLight : public MatterDimmableLight {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenBrightnessType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    }
    if (cluster_id == 8 && attribute_id == 0) {
      seenBrightnessType = val->type;
    }
    return MatterDimmableLight::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_values(void) {
  MockStream s; TypeCheckingDimmableLight light;
  bringUp(s, light, false, 64);
  s.injectURC("+MTATTR:1,6,0,1");
  s.injectURC("+MTATTR:1,8,0,200");
  Hearth.poll();
  check("on/off value type is boolean, not the generic integer", light.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("brightness value type is uint8, not the generic integer", light.seenBrightnessType == ESP_MATTER_VAL_TYPE_UINT8);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterDimmableLight tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_local_write_echo_fires_onchange_from_inside_the_setter();
  test_toggle();
  test_assignment_operator();
  test_brightness_write();
  test_max_brightness_constant();
  test_controller_onoff_change_fires_callback();
  test_brightness_from_controller();
  test_failed_write_returns_false();
  test_brightness_failed_write_returns_false();
  test_controller_change_delivers_typed_values();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

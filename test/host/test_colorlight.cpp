#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterColorLight.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterColorLight &light, bool initial = false, espHsvColor_t hsv = { 0, 254, 31 }) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(initial, hsv);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010D\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  check("declared as extended_color_light (shared 0x010D row)", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x010D);
  check("adopted endpoint 1", light.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial on/off is off", light.getOnOff() == false);
  espHsvColor_t hsv = light.getColorHSV();
  check("initial HSV is (0,254,31), upstream's own default", hsv.h == 0 && hsv.s == 254 && hsv.v == 31);
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  light = true;
  check("operator= writes and caches", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * All three of hue, saturation and level differ from the (0,254,31)
 * default, so all three get written, in hue-then-saturation-then-level
 * order: the same echo-ordering discipline as MatterEnhancedColorLight.cpp
 * (a write must land, and its synchronous echo run, before the next
 * field's write is issued, AT_MT_SPEC.md S3.8), copied verbatim since there
 * is no separate brightnessLevel field here to make the quirk moot.
 */
static void test_color_hsv_write_all_three_fields_differ(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,0,100,1", "+MTATTR:1,768,0,100\r\nOK\r\n");
  s.expect("AT+MTATTR=1,768,1,50,1", "+MTATTR:1,768,1,50\r\nOK\r\n");
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 100, 50, 200 };
  check("setColorHSV succeeds", light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("HSV cached as (100,50,200)", hsv.h == 100 && hsv.s == 50 && hsv.v == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_color_hsv_write_only_changed_fields(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);  /* default HSV is (0,254,31) */
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 0, 254, 200 };  /* only v differs */
  check("setColorHSV writes only the level attribute", light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("HSV cached as (0,254,200)", hsv.h == 0 && hsv.s == 254 && hsv.v == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained: hue and saturation were never written", s.scriptDrained());
}

/*
 * setColorRGB() delegates to setColorHSV(espRgbColorToHsvColor(rgb)), the
 * same real conversion arithmetic (not a stub) test_enhancedcolor.cpp
 * exercises. Pure red {255,0,0} converts to HSV(0,255,255); getColorRGB()
 * converts the cached HSV back to {255,0,0}.
 */
static void test_color_rgb_roundtrip(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,1,255,1", "+MTATTR:1,768,1,255\r\nOK\r\n");
  s.expect("AT+MTATTR=1,8,0,255,1", "+MTATTR:1,8,0,255\r\nOK\r\n");
  espRgbColor_t red = { 255, 0, 0 };
  check("setColorRGB(red) succeeds", light.setColorRGB(red));
  espHsvColor_t hsv = light.getColorHSV();
  check("red converts to HSV(0,255,255)", hsv.h == 0 && hsv.s == 255 && hsv.v == 255);
  espRgbColor_t back = light.getColorRGB();
  check("HSV(0,255,255) converts back to red", back.r == 255 && back.g == 0 && back.b == 0);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/* Direct checks of HearthColorUtil.h's ported conversions, independent of
 * the class, matching test_enhancedcolor.cpp's own coverage of the
 * grayscale (s==0) and black (v==0) edge cases the red round trip above
 * does not reach. */
static void test_color_util_edge_cases(void) {
  espHsvColor_t gray = { 0, 0, 200 };
  espRgbColor_t rgbGray = espHsvColorToRgbColor(gray);
  check("HSV with s==0 is grayscale", rgbGray.r == 200 && rgbGray.g == 200 && rgbGray.b == 200);

  espRgbColor_t black = { 0, 0, 0 };
  espHsvColor_t hsvBlack = espRgbColorToHsvColor(black);
  check("RGB black converts to HSV(0,0,0)", hsvBlack.h == 0 && hsvBlack.s == 0 && hsvBlack.v == 0);
}

/*
 * Partial failure: hue and level writes succeed, saturation is rejected.
 * The level write is still attempted after saturation fails (no
 * short-circuit, matching MatterEnhancedColorLight.cpp), and exactly the
 * fields whose own write succeeded end up cached.
 */
static void test_color_hsv_partial_failure_updates_only_succeeded_fields(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,0,100,1", "+MTATTR:1,768,0,100\r\nOK\r\n");
  s.expect("AT+MTATTR=1,768,1,50,1", "+MTERR:2\r\nERROR\r\n");
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 100, 50, 200 };
  check("setColorHSV reports overall failure", !light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("hue updated (its write succeeded)", hsv.h == 100);
  check("saturation NOT updated (its write failed)", hsv.s == 254);
  check("level updated (its write still ran and succeeded)", hsv.v == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_onoff_failed_write_returns_false(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !light.setOnOff(true));
  check("and does not update the cache", light.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_onoff_change_fires_callback(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  light.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  light.onChange([&](bool, espHsvColor_t) { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("cached state updated", light.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * MatterColorLight has no separate brightness accessor or callback: unlike
 * MatterEnhancedColorLight's brightnessLevel/colorHSV.v split, there is
 * only ever colorHSV.v, and upstream's own LevelControl branch fires
 * onChangeColorHSV (not a dedicated brightness callback) with the new v
 * alongside the unchanged h/s. No stale-field quirk to reproduce here: a
 * controller-driven level change converges the one cache field directly.
 */
static void test_level_control_from_controller_fires_color_callback(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int seen = 0, changeSeen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  light.onChange([&](bool, espHsvColor_t) { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,8,0,200");  /* LevelControl CurrentLevel */
  Hearth.poll();
  check("onChangeColorHSV fired for the level change", seen == 1);
  check("callback carried the new level, hue/saturation unchanged", seenHsv.h == 0 && seenHsv.s == 254 && seenHsv.v == 200);
  check("onChange also fired", changeSeen == 1);
  check("cached colorHSV.v updated", light.getColorHSV().v == 200);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_hsv_from_controller(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int seen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  s.injectURC("+MTATTR:1,768,0,90");  /* CurrentHue */
  Hearth.poll();
  check("onChangeColorHSV fired for hue", seen == 1 && seenHsv.h == 90 && seenHsv.s == 254 && seenHsv.v == 31);
  check("cached hue updated", light.getColorHSV().h == 90);

  s.injectURC("+MTATTR:1,768,1,180");  /* CurrentSaturation */
  Hearth.poll();
  check("onChangeColorHSV fired for saturation, carrying forward the new hue", seen == 2 && seenHsv.h == 90 && seenHsv.s == 180);
  check("cached saturation updated", light.getColorHSV().s == 180);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Sibling checks, one field at a time, mirroring test_enhancedcolor.cpp's
 * own bench-driven coverage (a MoveToHueAndSaturation smoke test on
 * hardware once found CurrentSaturation alone failing to update while hue
 * on the same command worked).
 */
static void test_saturation_only_from_controller(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int seen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  s.injectURC("+MTATTR:1,768,1,210");  /* CurrentSaturation alone */
  Hearth.poll();
  check("onChangeColorHSV fired for a saturation-only URC", seen == 1);
  check("callback carried the new saturation, hue/value unchanged", seenHsv.h == 0 && seenHsv.s == 210 && seenHsv.v == 31);
  check("cached saturation updated", light.getColorHSV().s == 210);
  check("cached hue untouched", light.getColorHSV().h == 0);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_hue_only_from_controller(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int seen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  s.injectURC("+MTATTR:1,768,0,100");  /* CurrentHue alone */
  Hearth.poll();
  check("onChangeColorHSV fired for a hue-only URC", seen == 1);
  check("callback carried the new hue, saturation/value unchanged", seenHsv.h == 100 && seenHsv.s == 254 && seenHsv.v == 31);
  check("cached hue updated", light.getColorHSV().h == 100);
  check("cached saturation untouched", light.getColorHSV().s == 254);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The exact shape a real MoveToHueAndSaturation command produces: TWO
 * +MTATTR lines land in the same read burst, because HearthLink::poll()
 * drains every buffered line in one call, not just the first.
 */
static void test_move_to_hue_and_saturation_two_urcs_in_one_poll(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light);
  int seen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  s.injectURC("+MTATTR:1,768,0,90");   /* CurrentHue */
  s.injectURC("+MTATTR:1,768,1,210");  /* CurrentSaturation, both queued before poll() runs */
  Hearth.poll();
  check("onChangeColorHSV fired twice, once per attribute", seen == 2);
  check("the second (saturation) callback carried both new values", seenHsv.h == 90 && seenHsv.s == 210);
  check("cached hue updated", light.getColorHSV().h == 90);
  check("cached saturation updated", light.getColorHSV().s == 210);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingColorLight : public MatterColorLight {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenLevelType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenHueType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenSatType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    } else if (cluster_id == 8 && attribute_id == 0) {
      seenLevelType = val->type;
    } else if (cluster_id == 768 && attribute_id == 0) {
      seenHueType = val->type;
    } else if (cluster_id == 768 && attribute_id == 1) {
      seenSatType = val->type;
    }
    return MatterColorLight::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_values(void) {
  MockStream s; TypeCheckingColorLight light;
  bringUp(s, light);
  s.injectURC("+MTATTR:1,6,0,1");
  s.injectURC("+MTATTR:1,8,0,200");
  s.injectURC("+MTATTR:1,768,0,90");
  s.injectURC("+MTATTR:1,768,1,180");
  Hearth.poll();
  check("on/off value type is boolean", light.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("level value type is uint8", light.seenLevelType == ESP_MATTER_VAL_TYPE_UINT8);
  check("hue value type is uint8", light.seenHueType == ESP_MATTER_VAL_TYPE_UINT8);
  check("saturation value type is uint8", light.seenSatType == ESP_MATTER_VAL_TYPE_UINT8);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterColorLight light;
  bringUp(s, light, true, { 100, 100, 100 });

  check("a second begin() after Matter.begin() is refused", !light.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached HSV was not overwritten", light.getColorHSV().v == 100);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,8,0,0,1", "+MTATTR:1,8,0,0\r\nOK\r\n");
  espHsvColor_t target = { 100, 100, 0 };
  check("so the next setColorHSV really does turn the level down", light.setColorHSV(target));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterColorLight tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_toggle();
  test_assignment_operator();
  test_color_hsv_write_all_three_fields_differ();
  test_color_hsv_write_only_changed_fields();
  test_color_rgb_roundtrip();
  test_color_util_edge_cases();
  test_color_hsv_partial_failure_updates_only_succeeded_fields();
  test_onoff_failed_write_returns_false();
  test_controller_onoff_change_fires_callback();
  test_level_control_from_controller_fires_color_callback();
  test_color_hsv_from_controller();
  test_saturation_only_from_controller();
  test_hue_only_from_controller();
  test_move_to_hue_and_saturation_two_urcs_in_one_poll();
  test_controller_change_delivers_typed_values();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

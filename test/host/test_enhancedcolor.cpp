#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterEnhancedColorLight.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(
  MockStream &s, MatterEnhancedColorLight &light, bool initial = false, espHsvColor_t hsv = { 21, 216, 25 }, uint8_t brightness = 25,
  uint16_t colorTemperature = 454
) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(initial, hsv, brightness, colorTemperature);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010D\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  check("declared as extended_color_light", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x010D);
  check("adopted endpoint 1", light.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial on/off is off", light.getOnOff() == false);
  check("initial brightness is 25", light.getBrightness() == 25);
  espHsvColor_t hsv = light.getColorHSV();
  check("initial HSV is (21,216,25)", hsv.h == 21 && hsv.s == 216 && hsv.v == 25);
  check("initial color temperature is 454", light.getColorTemperature() == 454);
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  light = true;
  check("operator= writes and caches", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The setter's own line updates brightnessLevel; this stack's synchronous
 * mode-1 echo (the reply line dispatches attributeChangeCB before
 * updateAttributeVal() returns, AT_MT_SPEC.md S3.8) drives colorHSV.v too.
 * Both fields therefore agree after a LOCAL call -- see
 * test_brightness_from_controller() below for the case where they do not.
 */
static void test_brightness_write(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,8,0,128,1", "+MTATTR:1,8,0,128\r\nOK\r\n");
  check("setBrightness writes CurrentLevel", light.setBrightness(128));
  check("brightness cached", light.getBrightness() == 128);
  check("colorHSV.v also converges via the synchronous echo", light.getColorHSV().v == 128);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_max_brightness_constant(void) {
  check("MAX_BRIGHTNESS is 255", MatterEnhancedColorLight::MAX_BRIGHTNESS == 255);
}

static void test_color_temperature_write(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,7,300,1", "+MTATTR:1,768,7,300\r\nOK\r\n");
  check("setColorTemperature writes mireds", light.setColorTemperature(300));
  check("cached", light.getColorTemperature() == 300);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_color_temperature_bounds(void) {
  check("MIN is 100", MatterEnhancedColorLight::MIN_COLOR_TEMPERATURE == 100);
  check("MAX is 500", MatterEnhancedColorLight::MAX_COLOR_TEMPERATURE == 500);
}

/*
 * All three of hue, saturation and level differ from the (21,216,25)
 * default, so all three get written, in hue-then-saturation-then-level
 * order (see MatterEnhancedColorLight.cpp's own comment on why that order
 * is load-bearing on this stack).
 */
static void test_color_hsv_write_all_three_fields_differ(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,0,100,1", "+MTATTR:1,768,0,100\r\nOK\r\n");
  s.expect("AT+MTATTR=1,768,1,50,1", "+MTATTR:1,768,1,50\r\nOK\r\n");
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 100, 50, 200 };
  check("setColorHSV succeeds", light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("HSV cached as (100,50,200)", hsv.h == 100 && hsv.s == 50 && hsv.v == 200);
  /* Verified upstream quirk (header deviation 3): setColorHSV() never
   * touches brightnessLevel, only colorHSV.v. */
  check("brightnessLevel is untouched by setColorHSV, matching upstream", light.getBrightness() == 25);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_color_hsv_write_only_changed_fields(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);  /* default HSV is (21,216,25) */
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 21, 216, 200 };  /* only v differs */
  check("setColorHSV writes only the level attribute", light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("HSV cached as (21,216,200)", hsv.h == 21 && hsv.s == 216 && hsv.v == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained: hue and saturation were never written", s.scriptDrained());
}

/*
 * setColorRGB() delegates to setColorHSV(espRgbColorToHsvColor(rgb)).
 * Pure red {255,0,0} converts to HSV(0,255,255) under
 * HearthColorUtil.h's ported algorithm, exercising the real conversion
 * arithmetic (not a stub), and getColorRGB() converts the cached HSV back
 * to {255,0,0}, a round trip through both ported functions.
 */
static void test_color_rgb_roundtrip(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,0,0,1", "+MTATTR:1,768,0,0\r\nOK\r\n");
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
 * the class, covering the grayscale (s==0) and black (v==0) edge cases the
 * red round trip above does not reach. */
static void test_color_util_edge_cases(void) {
  espHsvColor_t gray = { 0, 0, 200 };
  espRgbColor_t rgbGray = espHsvColorToRgbColor(gray);
  check("HSV with s==0 is grayscale", rgbGray.r == 200 && rgbGray.g == 200 && rgbGray.b == 200);

  espRgbColor_t black = { 0, 0, 0 };
  espHsvColor_t hsvBlack = espRgbColorToHsvColor(black);
  check("RGB black converts to HSV(0,0,0)", hsvBlack.h == 0 && hsvBlack.s == 0 && hsvBlack.v == 0);

  espRgbColor_t gray100 = { 100, 100, 100 };
  espHsvColor_t hsvGray100 = espRgbColorToHsvColor(gray100);
  check("equal RGB components convert to saturation 0", hsvGray100.s == 0 && hsvGray100.v == 100);
}

/*
 * Partial failure: hue and level writes succeed, saturation is rejected.
 * The level write is still attempted after saturation fails (no
 * short-circuit, matching MatterFan.cpp's own two-write discipline), and
 * exactly the fields whose own write succeeded end up cached.
 */
static void test_color_hsv_partial_failure_updates_only_succeeded_fields(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,0,100,1", "+MTATTR:1,768,0,100\r\nOK\r\n");
  s.expect("AT+MTATTR=1,768,1,50,1", "+MTERR:2\r\nERROR\r\n");
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  espHsvColor_t target = { 100, 50, 200 };
  check("setColorHSV reports overall failure", !light.setColorHSV(target));
  espHsvColor_t hsv = light.getColorHSV();
  check("hue updated (its write succeeded)", hsv.h == 100);
  check("saturation NOT updated (its write failed)", hsv.s == 216);
  check("level updated (its write still ran and succeeded)", hsv.v == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_onoff_failed_write_returns_false(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !light.setOnOff(true));
  check("and does not update the cache", light.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_failed_write_returns_false(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,8,0,128,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected brightness write returns false", !light.setBrightness(128));
  check("and does not update the cache", light.getBrightness() == 25);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_temperature_failed_write_returns_false(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  s.expect("AT+MTATTR=1,768,7,300,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected color temperature write returns false", !light.setColorTemperature(300));
  check("and does not update the cache", light.getColorTemperature() == 454);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_onoff_change_fires_callback(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  light.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  light.onChange([&](bool, espHsvColor_t, uint8_t, uint16_t) { changeSeen++; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("cached state updated", light.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * A CONTROLLER-driven brightness change (no local setBrightness() call in
 * the loop) updates colorHSV.v via the echo path but leaves
 * brightnessLevel stale -- the exact upstream quirk documented in the
 * header's deviation 3, from the controller-URC angle rather than the
 * local-call angle test_brightness_write() covers.
 */
static void test_brightness_from_controller(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  int seen = 0; uint8_t level = 0;
  light.onChangeBrightness([&](uint8_t v) { seen++; level = v; return true; });
  s.injectURC("+MTATTR:1,8,0,180");
  Hearth.poll();
  check("onChangeBrightness fired", seen == 1 && level == 180);
  check("colorHSV.v updated by the controller-driven change", light.getColorHSV().v == 180);
  check("brightnessLevel stays stale: reproduced upstream quirk", light.getBrightness() == 25);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_hsv_from_controller(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  int seen = 0; espHsvColor_t seenHsv = { 0, 0, 0 };
  light.onChangeColorHSV([&](espHsvColor_t v) { seen++; seenHsv = v; return true; });
  s.injectURC("+MTATTR:1,768,0,90");  /* CurrentHue */
  Hearth.poll();
  check("onChangeColorHSV fired for hue", seen == 1 && seenHsv.h == 90 && seenHsv.s == 216 && seenHsv.v == 25);
  check("cached hue updated", light.getColorHSV().h == 90);

  s.injectURC("+MTATTR:1,768,1,180");  /* CurrentSaturation */
  Hearth.poll();
  check("onChangeColorHSV fired for saturation, carrying forward the new hue", seen == 2 && seenHsv.h == 90 && seenHsv.s == 180);
  check("cached saturation updated", light.getColorHSV().s == 180);
  check("no echo", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_color_temperature_from_controller(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light);
  int seen = 0; uint16_t temp = 0;
  light.onChangeColorTemperature([&](uint16_t v) { seen++; temp = v; return true; });
  s.injectURC("+MTATTR:1,768,7,350");
  Hearth.poll();
  check("onChangeColorTemperature fired", seen == 1 && temp == 350);
  check("cached color temperature updated", light.getColorTemperature() == 350);
  check("no echo", s.scriptDrained());
}

class TypeCheckingEnhancedColorLight : public MatterEnhancedColorLight {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenLevelType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenHueType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenSatType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenMiredsType = ESP_MATTER_VAL_TYPE_INVALID;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 6 && attribute_id == 0) {
      seenOnOffType = val->type;
    } else if (cluster_id == 8 && attribute_id == 0) {
      seenLevelType = val->type;
    } else if (cluster_id == 768 && attribute_id == 0) {
      seenHueType = val->type;
    } else if (cluster_id == 768 && attribute_id == 1) {
      seenSatType = val->type;
    } else if (cluster_id == 768 && attribute_id == 7) {
      seenMiredsType = val->type;
    }
    return MatterEnhancedColorLight::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_values(void) {
  MockStream s; TypeCheckingEnhancedColorLight light;
  bringUp(s, light);
  s.injectURC("+MTATTR:1,6,0,1");
  s.injectURC("+MTATTR:1,8,0,200");
  s.injectURC("+MTATTR:1,768,0,90");
  s.injectURC("+MTATTR:1,768,1,180");
  s.injectURC("+MTATTR:1,768,7,350");
  Hearth.poll();
  check("on/off value type is boolean", light.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("level value type is uint8", light.seenLevelType == ESP_MATTER_VAL_TYPE_UINT8);
  check("hue value type is uint8", light.seenHueType == ESP_MATTER_VAL_TYPE_UINT8);
  check("saturation value type is uint8", light.seenSatType == ESP_MATTER_VAL_TYPE_UINT8);
  check("mireds value type is uint16", light.seenMiredsType == ESP_MATTER_VAL_TYPE_UINT16);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterEnhancedColorLight light;
  bringUp(s, light, true, { 100, 100, 100 }, 128, 300);

  check("a second begin() after Matter.begin() is refused", !light.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached brightness was not overwritten", light.getBrightness() == 128);
  check("the cached HSV was not overwritten", light.getColorHSV().v == 100);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,8,0,0,1", "+MTATTR:1,8,0,0\r\nOK\r\n");
  check("so the next setBrightness(0) really does turn it down", light.setBrightness(0));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterEnhancedColorLight tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_toggle();
  test_assignment_operator();
  test_brightness_write();
  test_max_brightness_constant();
  test_color_temperature_write();
  test_color_temperature_bounds();
  test_color_hsv_write_all_three_fields_differ();
  test_color_hsv_write_only_changed_fields();
  test_color_rgb_roundtrip();
  test_color_util_edge_cases();
  test_color_hsv_partial_failure_updates_only_succeeded_fields();
  test_onoff_failed_write_returns_false();
  test_brightness_failed_write_returns_false();
  test_color_temperature_failed_write_returns_false();
  test_controller_onoff_change_fires_callback();
  test_brightness_from_controller();
  test_color_hsv_from_controller();
  test_color_temperature_from_controller();
  test_controller_change_delivers_typed_values();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

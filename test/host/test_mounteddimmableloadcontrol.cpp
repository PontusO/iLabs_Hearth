#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterMountedDimmableLoadControl.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterMountedDimmableLoadControl &plugin, bool initial, uint8_t brightness) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  plugin.begin(initial, brightness);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0110\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  check("declared as mounted_dimmable_load_control", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0110);
  check("adopted endpoint 1", plugin.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", plugin.setOnOff(true));
  check("state cached", plugin.getOnOff() == true);
  check("operator bool agrees", (bool)plugin == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("toggle from off turns on", plugin.toggle() && plugin.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  plugin = true;
  check("operator= writes and caches", plugin.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_brightness_write(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, true, 64);
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTATTR:1,8,0,200\r\nOK\r\n");
  check("setBrightness(200) succeeds", plugin.setBrightness(200));
  check("brightness cached", plugin.getBrightness() == 200);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_controller_change_onoff_fires_callback(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  uint8_t brightnessAtChange = 0;
  plugin.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  plugin.onChange([&](bool on, uint8_t brightness) { changeSeen++; brightnessAtChange = brightness; (void)on; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("with cached brightness", brightnessAtChange == 64);
  check("cached state updated", plugin.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_brightness_fires_callback(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, true, 64);
  int brightnessSeen = 0, changeSeen = 0;
  uint8_t brightness = 0;
  bool stateAtChange = false;
  plugin.onChangeBrightness([&](uint8_t v) { brightnessSeen++; brightness = v; return true; });
  plugin.onChange([&](bool on, uint8_t b) { changeSeen++; stateAtChange = on; (void)b; return true; });
  s.injectURC("+MTATTR:1,8,0,128");
  Hearth.poll();
  check("onChangeBrightness fired", brightnessSeen == 1 && brightness == 128);
  check("onChange also fired", changeSeen == 1);
  check("with cached on/off state", stateAtChange == true);
  check("cached brightness updated", plugin.getBrightness() == 128);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_onoff_write_returns_false(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, false, 64);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected on/off write returns false", !plugin.setOnOff(true));
  check("and does not update the cache", plugin.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_brightness_write_returns_false(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, true, 64);
  s.expect("AT+MTATTR=1,8,0,200,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected brightness write returns false", !plugin.setBrightness(200));
  check("and does not update the cache", plugin.getBrightness() == 64);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingPlugin : public MatterMountedDimmableLoadControl {
public:
  esp_matter_val_type_t seenOnOffType = ESP_MATTER_VAL_TYPE_INVALID;
  esp_matter_val_type_t seenBrightnessType = ESP_MATTER_VAL_TYPE_INVALID;
  bool seenOnOff = false;
  uint8_t seenBrightness = 0;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    if (cluster_id == 0x0006 && attribute_id == 0x0000) {
      seenOnOffType = val->type;
      seenOnOff = val->val.b;
    } else if (cluster_id == 0x0008 && attribute_id == 0x0000) {
      seenBrightnessType = val->type;
      seenBrightness = val->val.u8;
    }
    return MatterMountedDimmableLoadControl::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterMountedDimmableLoadControl plugin;
  bringUp(s, plugin, true, 128);

  check("a second begin() after Matter.begin() is refused", !plugin.begin(false, 64));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached on/off state was not overwritten", plugin.getOnOff() == true);
  check("the cached brightness was not overwritten", plugin.getBrightness() == 128);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,6,0,0,1", "+MTATTR:1,6,0,0\r\nOK\r\n");
  check("so the next setOnOff(false) really does turn the control off", plugin.setOnOff(false));
  check("and the write happened", s.scriptDrained());

  s.expect("AT+MTATTR=1,8,0,64,1", "+MTATTR:1,8,0,64\r\nOK\r\n");
  check("and setBrightness changes the brightness", plugin.setBrightness(64));
  check("and that write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_delivers_typed_onoff_and_brightness(void) {
  MockStream s; TypeCheckingPlugin plugin;
  bringUp(s, plugin, false, 64);
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("on/off value type is boolean", plugin.seenOnOffType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("on/off value lands in val.b", plugin.seenOnOff == true);

  s.injectURC("+MTATTR:1,8,0,200");
  Hearth.poll();
  check("brightness value type is uint8", plugin.seenBrightnessType == ESP_MATTER_VAL_TYPE_UINT8);
  check("brightness value lands in val.u8", plugin.seenBrightness == 200);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterMountedDimmableLoadControl tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_toggle();
  test_assignment_operator();
  test_brightness_write();
  test_controller_change_onoff_fires_callback();
  test_controller_change_brightness_fires_callback();
  test_failed_onoff_write_returns_false();
  test_failed_brightness_write_returns_false();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  test_controller_change_delivers_typed_onoff_and_brightness();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

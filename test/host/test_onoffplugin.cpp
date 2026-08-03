#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterOnOffPlugin.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterOnOffPlugin &plugin, bool initial) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  plugin.begin(initial);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010A\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_and_adopts(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  check("declared as on_off_plugin", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x010A);
  check("adopted endpoint 1", plugin.getEndPointId() == 1);
  check("begin() itself issued no AT traffic", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_set_on_off_writes(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", plugin.setOnOff(true));
  check("state cached", plugin.getOnOff() == true);
  check("operator bool agrees", (bool)plugin == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_local_write_echo_fires_onchange_from_inside_the_setter(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int changeSeen = 0;
  bool state = false;
  bool seenBeforeReturn = false;
  plugin.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  bool ok = plugin.setOnOff(true);
  seenBeforeReturn = (changeSeen == 1);

  check("the write succeeds", ok);
  check("onChange fired exactly once", changeSeen == 1);
  check("and fired before the setter returned", seenBeforeReturn);
  check("with the value the firmware echoed", state == true);
  check("cached state agrees", plugin.getOnOff() == true);
  check("no extra traffic on the wire", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_library_call_from_onchange_during_a_write_is_refused(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int nestedRc = 0;
  int nestedCalls = 0;
  plugin.onChange([&](bool v) {
    (void)v;
    nestedCalls++;
    nestedRc = Hearth.hearthCommand("AT+MTVER?");
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  bool ok = plugin.setOnOff(true);

  check("the callback really did call back into the library", nestedCalls == 1);
  check("and was refused with the re-entrancy code", nestedRc == HEARTH_CMD_REENTRANT);
  check("the refused call put nothing on the wire", s.unexpected().empty());
  check("the write it interrupted still succeeded", ok);
  check("and still cached its state", plugin.getOnOff() == true);
  check("script drained", s.scriptDrained());
}

static void test_mode_zero_write_also_echoes(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int changeSeen = 0;
  plugin.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  esp_matter_attr_val_t v = esp_matter_bool(true);
  check("the write succeeds", plugin.setAttributeVal(0x0006, 0x0000, &v));
  check("and the echo still fired onChange", changeSeen == 1);
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("toggle from off turns on", plugin.toggle() && plugin.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  plugin = true;
  check("operator= writes and caches", plugin.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_fires_callback(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int onOffSeen = 0, changeSeen = 0;
  bool state = false;
  plugin.onChangeOnOff([&](bool v) { onOffSeen++; state = v; return true; });
  plugin.onChange([&](bool v) { changeSeen++; (void)v; return true; });
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("onChangeOnOff fired", onOffSeen == 1 && state == true);
  check("onChange also fired", changeSeen == 1);
  check("cached state updated", plugin.getOnOff() == true);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_controller_change_fires_from_a_bare_upstream_loop(void) {
  MockStream s;
  MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int changeSeen = 0;
  bool state = false;
  plugin.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  s.injectURC("+MTATTR:1,6,0,1");

  s.expect("AT+MTFABRICS?", "+MTFABRICS:1\r\nOK\r\n");
  bool commissioned = Matter.isDeviceCommissioned();

  check("the sketch's own query still answers correctly", commissioned);
  check("onChange fired with no Hearth.poll() in the sketch", changeSeen == 1 && state == true);
  check("cached state updated", plugin.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

static void test_controller_change_arriving_mid_command_is_dispatched(void) {
  MockStream s;
  MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int changeSeen = 0;
  bool state = false;
  plugin.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  s.expect("AT+MTFABRICS?", "+MTATTR:1,6,0,1\r\n+MTFABRICS:1\r\nOK\r\n");
  bool commissioned = Matter.isDeviceCommissioned();

  check("the in-flight command still parses its own result", commissioned);
  check("the interleaved +MTATTR reached onChange", changeSeen == 1 && state == true);
  check("cached state updated", plugin.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_attribute_read_answer_is_not_a_change_callback(void) {
  MockStream s;
  MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  int changeSeen = 0;
  plugin.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  esp_matter_attr_val_t v = esp_matter_bool(false);
  check("the read succeeds", plugin.getAttributeVal(0x0006, 0x0000, &v));
  check("and returns the reported value", v.val.b == true);
  check("the read's own answer did not fire onChange", changeSeen == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);

  check("a second begin() after Matter.begin() is refused", !plugin.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", plugin.getOnOff() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("so the next setOnOff(true) really does turn the plugin on", plugin.setOnOff(true));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_urcs_arriving_before_reconcile_are_dropped(void) {
  MockStream s;
  MatterOnOffPlugin plugin;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  plugin.begin(false);
  int changeSeen = 0;
  plugin.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  s.injectURC("+MTATTR:1,6,0,1");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x010A\r\nOK\r\n");
  Matter.begin();

  check("reconcile still succeeded", plugin.getEndPointId() == 1);
  check("the pre-reconcile URC fired no callback", changeSeen == 0);
  check("and did not touch the cached state", plugin.getOnOff() == false);
  check("it was consumed, not left in the buffer", s.scriptDrained());

  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("a change after reconcile is delivered", changeSeen == 1);
  check("and updates the cache", plugin.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_failed_write_returns_false(void) {
  MockStream s; MatterOnOffPlugin plugin;
  bringUp(s, plugin, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !plugin.setOnOff(true));
  check("and does not update the cache", plugin.getOnOff() == false);
  check("no unexpected commands", s.unexpected().empty());
}

class TypeCheckingPlugin : public MatterOnOffPlugin {
public:
  esp_matter_val_type_t seenType = ESP_MATTER_VAL_TYPE_INVALID;
  bool seenBool = false;
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override {
    seenType = val->type;
    seenBool = val->val.b;
    return MatterOnOffPlugin::attributeChangeCB(endpoint_id, cluster_id, attribute_id, val);
  }
};

static void test_controller_change_delivers_typed_boolean(void) {
  MockStream s; TypeCheckingPlugin plugin;
  bringUp(s, plugin, false);
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("value type is boolean, not the generic integer", plugin.seenType == ESP_MATTER_VAL_TYPE_BOOLEAN);
  check("value lands in val.b, where upstream's own callbacks read it", plugin.seenBool == true);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterOnOffPlugin tests =====\n");
  test_begin_declares_and_adopts();
  test_set_on_off_writes();
  test_local_write_echo_fires_onchange_from_inside_the_setter();
  test_library_call_from_onchange_during_a_write_is_refused();
  test_mode_zero_write_also_echoes();
  test_toggle();
  test_assignment_operator();
  test_controller_change_fires_callback();
  test_controller_change_fires_from_a_bare_upstream_loop();
  test_controller_change_arriving_mid_command_is_dispatched();
  test_attribute_read_answer_is_not_a_change_callback();
  test_rebegin_after_reconcile_does_not_desync_the_cache();
  test_urcs_arriving_before_reconcile_are_dropped();
  test_failed_write_returns_false();
  test_controller_change_delivers_typed_boolean();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

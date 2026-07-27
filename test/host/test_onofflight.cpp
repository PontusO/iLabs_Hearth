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

/*
 * RE-REVIEW, IMPORTANT 2. What the C6 really answers to a mode-1 write:
 * the +MTATTR URC the firmware's own attribute callback raises, *then* OK
 * (AT_MT_SPEC.md S3.8, "A write echoes a +MTATTR URC ... then OK";
 * main.cpp's mt_matter_attr_write() calls esp_matter::attribute::update()
 * for mode 1, which fires app_attribute_update_cb on POST_UPDATE). Every
 * write test in this suite used to script a bare OK, i.e. a firmware that
 * does not exist. Scripted here as the firmware actually behaves.
 */
static void test_set_on_off_writes(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("setOnOff(true) succeeds", light.setOnOff(true));
  check("state cached", light.getOnOff() == true);
  check("operator bool agrees", (bool)light == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * RE-REVIEW, IMPORTANT 2, the behaviour question. The write echo is
 * dispatched as an ordinary URC (it is not an attribute *read*'s own
 * answer), so it reaches attributeChangeCB and fires the sketch's onChange
 * synchronously, from inside setOnOff(), before setOnOff() has returned.
 *
 * That is upstream's behaviour too, not a Hearth invention:
 * arduino-esp32 3.3.8's Matter.cpp routes PRE_UPDATE to
 * ep->attributeChangeCB(), and MatterOnOffLight::setOnOff() reaches it
 * through attribute::update() on the calling thread. A sketch's onChange
 * fires from inside its own setter there as well. Pinned here so it is a
 * decision on the record rather than a surprise on the bench.
 */
static void test_local_write_echo_fires_onchange_from_inside_the_setter(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  int changeSeen = 0;
  bool state = false;
  bool seenBeforeReturn = false;
  light.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  bool ok = light.setOnOff(true);
  seenBeforeReturn = (changeSeen == 1);

  check("the write succeeds", ok);
  check("onChange fired exactly once", changeSeen == 1);
  check("and fired before the setter returned", seenBeforeReturn);
  check("with the value the firmware echoed", state == true);
  check("cached state agrees", light.getOnOff() == true);
  check("no extra traffic on the wire", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * RE-REVIEW, IMPORTANT 2, the consequence that is genuinely Hearth's and
 * not upstream's. The echo is dispatched from inside HearthLink::command(),
 * which holds the re-entrancy latch, so a sketch whose onChange calls back
 * into the library is refused with HEARTH_CMD_REENTRANT. On arduino-esp32
 * the same call is served, because there is no shared UART to protect.
 *
 * This is a real, documented divergence (README, "Callbacks fire from
 * inside your own setter"), not a bug to be fixed by dropping the latch:
 * serving the nested call would mean two readers on one stream, and the
 * outer command would lose its own terminal OK. The refusal is loud (a code
 * of its own, distinct from a retryable timeout) and puts nothing on the
 * wire. The outer write completes normally regardless.
 */
static void test_library_call_from_onchange_during_a_write_is_refused(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  int nestedRc = 0;
  int nestedCalls = 0;
  light.onChange([&](bool v) {
    (void)v;
    nestedCalls++;
    nestedRc = Hearth.hearthCommand("AT+MTVER?");
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  bool ok = light.setOnOff(true);

  check("the callback really did call back into the library", nestedCalls == 1);
  check("and was refused with the re-entrancy code", nestedRc == HEARTH_CMD_REENTRANT);
  check("the refused call put nothing on the wire", s.unexpected().empty());
  check("the write it interrupted still succeeded", ok);
  check("and still cached its state", light.getOnOff() == true);
  check("script drained", s.scriptDrained());
}

/*
 * CORRECTION. A mode-0 write (setAttributeVal) echoes a +MTATTR URC too:
 * main.cpp:394 calls esp_matter::attribute::set_val(a, &val), whose third
 * parameter (call_callbacks) defaults to true
 * (esp_matter_data_model.h:927), so set_val_internal still fires
 * POST_UPDATE (esp_matter_data_model.cpp:785-787), which is what raises
 * the URC. Mode 0 and mode 1 differ only in whether the change is reported
 * to the fabric, not in whether the host sees an echo; both echo, and this
 * library has no way to tell them apart on the wire, so the echo reaches
 * onChange exactly as a mode-1 write's does.
 */
static void test_mode_zero_write_also_echoes(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  int changeSeen = 0;
  light.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0,1,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  esp_matter_attr_val_t v = esp_matter_bool(true);
  check("the write succeeds", light.setAttributeVal(0x0006, 0x0000, &v));
  check("and the echo still fired onChange", changeSeen == 1);
  check("script drained", s.scriptDrained());
}

static void test_toggle(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("toggle from off turns on", light.toggle() && light.getOnOff());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_assignment_operator(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
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

/*
 * FINAL REVIEW, CRITICAL 1. The test above drives the dispatch with an
 * explicit Hearth.poll(), which no upstream sketch contains: upstream runs
 * the Matter stack in a background task and has nothing for poll() to be a
 * parity of. What every upstream example's loop() *does* contain is
 * `Matter.isDeviceCommissioned()`, so that call, on its own, has to be
 * enough to get a controller-driven change through to onChange(). Nothing
 * below calls poll(); if the library stops pumping pending URCs on its own
 * API calls, this fails and the library's primary function is broken again.
 */
static void test_controller_change_fires_from_a_bare_upstream_loop(void) {
  MockStream s;
  MatterOnOffLight light;
  bringUp(s, light, false);
  int changeSeen = 0;
  bool state = false;
  light.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  /* The controller turns the light on while the sketch is between loop()
   * iterations: the URC lands in the host's UART buffer with no command in
   * flight and nothing else running. */
  s.injectURC("+MTATTR:1,6,0,1");

  /* One iteration of an unmodified upstream loop(). */
  s.expect("AT+MTFABRICS?", "+MTFABRICS:1\r\nOK\r\n");
  bool commissioned = Matter.isDeviceCommissioned();

  check("the sketch's own query still answers correctly", commissioned);
  check("onChange fired with no Hearth.poll() in the sketch", changeSeen == 1 && state == true);
  check("cached state updated", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
  check("script drained", s.scriptDrained());
}

/*
 * FINAL REVIEW, CRITICAL 1, second half. Same change, but arriving while a
 * command is already in flight, which is the far more likely timing on a
 * real link: the C6 interleaves the URC with the reply it is already
 * sending. HearthLink::command() used to refuse to dispatch *any* +MTATTR
 * line mid-command (the exclusion exists for an attribute read claiming its
 * own answer, but was applied unconditionally), so this line was offered to
 * AT+MTFABRICS?'s line handler, dropped on its prefix check, and lost.
 */
static void test_controller_change_arriving_mid_command_is_dispatched(void) {
  MockStream s;
  MatterOnOffLight light;
  bringUp(s, light, false);
  int changeSeen = 0;
  bool state = false;
  light.onChange([&](bool v) {
    changeSeen++;
    state = v;
    return true;
  });

  /* The URC is bundled into the reply to a command that is not an attribute
   * read, which is where it must be treated as a URC and dispatched. */
  s.expect("AT+MTFABRICS?", "+MTATTR:1,6,0,1\r\n+MTFABRICS:1\r\nOK\r\n");
  bool commissioned = Matter.isDeviceCommissioned();

  check("the in-flight command still parses its own result", commissioned);
  check("the interleaved +MTATTR reached onChange", changeSeen == 1 && state == true);
  check("cached state updated", light.getOnOff() == true);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * The other half of the +MTATTR exclusion: narrowing it must not break the
 * case it was written for. An attribute *read*'s own +MTATTR answer is still
 * claimed as the result and never routed to onChange, or every read would
 * fire a spurious change callback.
 */
static void test_attribute_read_answer_is_not_a_change_callback(void) {
  MockStream s;
  MatterOnOffLight light;
  bringUp(s, light, false);
  int changeSeen = 0;
  light.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  s.expect("AT+MTATTR=1,6,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  esp_matter_attr_val_t v = esp_matter_bool(false);
  check("the read succeeds", light.getAttributeVal(0x0006, 0x0000, &v));
  check("and returns the reported value", v.val.b == true);
  check("the read's own answer did not fire onChange", changeSeen == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * RE-REVIEW, MINOR 3, seen from the sketch rather than from the registry.
 * A second begin() after Matter.begin() used to be accepted (an exact
 * repeat declaration returned true), so it overwrote onOffState with its
 * initialState argument without ever putting that value on the wire. The
 * light was then off on the C6 and "on" in the sketch's cache, and the
 * setOnOff(true) that should have fixed it short-circuited on the equality
 * check instead: the light silently never came on.
 *
 * It must be refused, which is also what upstream does
 * (MatterOnOffLight::begin() returns false once getEndPointId() != 0), and
 * the cache must be left alone so the next setOnOff actually writes.
 */
static void test_rebegin_after_reconcile_does_not_desync_the_cache(void) {
  MockStream s; MatterOnOffLight light;
  bringUp(s, light, false);

  check("a second begin() after Matter.begin() is refused", !light.begin(true));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached state was not overwritten", light.getOnOff() == false);
  check("the refused begin() issued no AT traffic", s.scriptDrained());

  /* The point of all of the above: this write must still reach the wire. */
  s.expect("AT+MTATTR=1,6,0,1,1", "+MTATTR:1,6,0,1\r\nOK\r\n");
  check("so the next setOnOff(true) really does turn the light on", light.setOnOff(true));
  check("and the write happened", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * RE-REVIEW, MINOR 4. A +MTATTR that lands before reconcile has finished is
 * consumed and dropped: hearthCommand() drains URCs at the top of every
 * call, including the AT+MTEP? that reconcile itself issues, and at that
 * moment every declared endpoint still has endpoint_id 0, so
 * hearthFindByEndPointId() finds no target.
 *
 * Dropping is the right answer, not merely a tolerable one, so it is pinned
 * here rather than fixed:
 *
 *   - the alternative is matching a real endpoint id against endpoints that
 *     all still carry 0, which is precisely the Root Node confusion the
 *     final review's IMPORTANT 3 closed. Nothing in the registry can
 *     legitimately claim the value;
 *   - a composition change reboots the C6 (AT+MTEPAPPLY), so anything
 *     buffered from before it is stale by definition;
 *   - the values are authoritative on the C6, not on the host, and are
 *     re-readable with getAttributeVal() once ids are adopted. Upstream's
 *     own examples resynchronise here anyway: they call updateAccessory()
 *     right after Matter.begin() to push the sketch's cached state at the
 *     hardware.
 *
 * The window is one AT round trip on the no-change path. Buffering across
 * it would need a queue whose depth is a new failure mode of its own.
 */
static void test_urcs_arriving_before_reconcile_are_dropped(void) {
  MockStream s;
  MatterOnOffLight light;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  light.begin(false);
  int changeSeen = 0;
  light.onChange([&](bool v) {
    changeSeen++;
    (void)v;
    return true;
  });

  /* The C6 reports a state change before the host has adopted any endpoint
   * id, i.e. while Matter.begin() is still in flight. */
  s.injectURC("+MTATTR:1,6,0,1");
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\nOK\r\n");
  Matter.begin();

  check("reconcile still succeeded", light.getEndPointId() == 1);
  check("the pre-reconcile URC fired no callback", changeSeen == 0);
  check("and did not touch the cached state", light.getOnOff() == false);
  check("it was consumed, not left in the buffer", s.scriptDrained());

  /* The very next one, now that ids are adopted, is delivered normally: the
   * drop is scoped to the pre-reconcile window and nothing is wedged. */
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("a change after reconcile is delivered", changeSeen == 1);
  check("and updates the cache", light.getOnOff() == true);
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

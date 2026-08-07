/*
 * test_modeselect.cpp - Task C7: MatterModeSelect. No +MTCMD consumer at
 * all (ChangeToMode is handled entirely inside the SDK; see the header
 * comment); the interesting surface is AT+MTMODES's grammar and its
 * reconcile resend, plus CurrentMode as a plain AT+MTATTR attribute.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterModeSelect.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterModeSelect &ms) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  ms.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0027\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0027_variant0(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  check("declared as the mode_select device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0027);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", ms.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached mode is 0", ms.getCurrentMode() == 0);
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  check("a second begin() after Matter.begin() is refused", !ms.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== setSupportedModes(): exact wire pin, one and several pairs ===== */

static void test_setsupportedmodes_one_pair_exact_wire_pin(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "Quiet" };
  s.expect("AT+MTMODES=1,0,\"Quiet\"", "OK\r\n");
  check("setSupportedModes(one pair) sends the exact wire pin", ms.setSupportedModes(modes, labels, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_several_pairs_replaces_list(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes1[] = { 0 };
  const char *labels1[] = { "Quiet" };
  s.expect("AT+MTMODES=1,0,\"Quiet\"", "OK\r\n");
  check("first call succeeds", ms.setSupportedModes(modes1, labels1, 1));

  uint8_t modes2[] = { 0, 1, 2 };
  const char *labels2[] = { "Quiet", "Normal", "Boost" };
  s.expect("AT+MTMODES=1,0,\"Quiet\",1,\"Normal\",2,\"Boost\"", "OK\r\n");
  check("second call replaces the list, not appends", ms.setSupportedModes(modes2, labels2, 3));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.20: a comma INSIDE a quoted label is legal and part of
 * the label's own text. */
static void test_setsupportedmodes_comma_in_label(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "Eco, low" };
  s.expect("AT+MTMODES=1,0,\"Eco, low\"", "OK\r\n");
  check("a comma inside a quoted label is sent verbatim, not split", ms.setSupportedModes(modes, labels, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_zero_pairs(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  check("0 pairs is refused host-side", !ms.setSupportedModes(nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_too_many_pairs(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[9];
  const char *labels[9];
  char buf[9][8];
  for (int i = 0; i < 9; i++) {
    modes[i] = (uint8_t)i;
    snprintf(buf[i], sizeof(buf[i]), "M%d", i);
    labels[i] = buf[i];
  }
  check("9 pairs exceeds the 8-pair cap and is refused", !ms.setSupportedModes(modes, labels, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.20's own worked example: "AT+MTMODES=7,0,"Quiet",0,
 * "Silent" -> +MTERR:1 (mode 0 repeated)". This class catches it host-side
 * (see the header comment), so the pin here is "no traffic reached the
 * wire", not a +MTERR:1 reply. */
static void test_setsupportedmodes_rejects_repeated_mode(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0, 0 };
  const char *labels[] = { "Quiet", "Silent" };
  check("a repeated mode value is refused host-side", !ms.setSupportedModes(modes, labels, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_missing_label(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "" }; /* empty label: 0 bytes, below the 1-byte minimum */
  check("an empty label is refused host-side", !ms.setSupportedModes(modes, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_oversized_label(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "123456789012345678901234567890123" }; /* 33 bytes, one over the 32-byte cap */
  check("label length 33 exceeds the 32-byte cap and is refused", strlen(labels[0]) == 33 && !ms.setSupportedModes(modes, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_quote_in_label(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "Bad\"Label" };
  check("a label containing a double quote is refused", !ms.setSupportedModes(modes, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_rejects_nonprintable_in_label(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "Bad\x01Label" };
  check("a label containing a non-printable byte is refused", !ms.setSupportedModes(modes, labels, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setsupportedmodes_failed_write_leaves_cache_unresent(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0 };
  const char *labels[] = { "Quiet" };
  s.expect("AT+MTMODES=1,0,\"Quiet\"", "ERROR\r\n");
  check("a rejected write returns false", !ms.setSupportedModes(modes, labels, 1));

  /* A later reconcile resends nothing: the failed write never populated the
   * cache in the first place, so this is confirmatory, not a dedicated
   * reconcile scenario. */
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0027\r\nOK\r\n");
  Matter.begin();
  check("no supported-modes list was resent (there is nothing cached)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile resend (B120 norm): not persisted, resent every boot ===== */

static void test_supportedmodes_resent_on_next_reconcile(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  uint8_t modes[] = { 0, 1 };
  const char *labels[] = { "Quiet", "Normal" };
  s.expect("AT+MTMODES=1,0,\"Quiet\",1,\"Normal\"", "OK\r\n");
  check("setSupportedModes succeeds", ms.setSupportedModes(modes, labels, 2));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0027\r\nOK\r\n");
  s.expect("AT+MTMODES=1,0,\"Quiet\",1,\"Normal\"", "OK\r\n"); /* resent verbatim */
  Matter.begin(); /* a second reconcile, e.g. a sketch's repeated loop() call */

  check("the list was resent verbatim on the second reconcile", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Nothing set yet: reconcile must not send an empty/garbage AT+MTMODES. */
static void test_reconcile_with_nothing_set_sends_nothing(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms); /* bringUp() itself already exercised one reconcile with nothing set */
  check("no AT+MTMODES traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== CurrentMode: plain AT+MTATTR attribute ===== */

static void test_setcurrentmode_exact_wire_pin_and_cache_update(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  s.expect("AT+MTATTR=1,80,3,2,1", "+MTATTR:1,80,3,2\r\nOK\r\n");
  check("setCurrentMode(2) sends the exact wire pin", ms.setCurrentMode(2));
  check("cache updated to 2", ms.getCurrentMode() == 2);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setcurrentmode_noop_on_unchanged_value(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms); /* cache starts 0 */
  check("setCurrentMode(0), already the cache, is a no-op", ms.setCurrentMode(0));
  check("no AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setcurrentmode_failed_write_leaves_cache(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  s.expect("AT+MTATTR=1,80,3,2,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !ms.setCurrentMode(2));
  check("cache untouched (still 0)", ms.getCurrentMode() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

/* Controller's ChangeToMode command, handled entirely inside the SDK
 * (header comment): the host only ever sees the generic +MTATTR URC. */
static void test_controller_changetomode_urc_updates_cache_and_fires_callback(void) {
  MockStream s; MatterModeSelect ms;
  bringUp(s, ms);
  int calls = 0;
  uint8_t seenMode = 255;
  ms.onChangeMode([&](uint8_t m) { calls++; seenMode = m; });
  s.injectURC("+MTATTR:1,80,3,2");
  Hearth.poll();
  check("CurrentMode cache updated from the controller URC", ms.getCurrentMode() == 2);
  check("onChangeMode fired exactly once with the new mode", calls == 1 && seenMode == 2);
  check("no echo written back to the fabric", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_attr_type_is_uint8(void) {
  MatterModeSelect ms;
  check("CurrentMode is uint8", ms.hearthAttrTypeFor(80, 3) == ESP_MATTER_VAL_TYPE_UINT8);
  check("an unrelated cluster falls through to the base default", ms.hearthAttrTypeFor(6, 0) == ESP_MATTER_VAL_TYPE_INTEGER);
}

int main(void) {
  printf("\n===== MatterModeSelect tests =====\n");
  test_begin_declares_0x0027_variant0();
  test_rebegin_after_reconcile_refused();
  test_setsupportedmodes_one_pair_exact_wire_pin();
  test_setsupportedmodes_several_pairs_replaces_list();
  test_setsupportedmodes_comma_in_label();
  test_setsupportedmodes_rejects_zero_pairs();
  test_setsupportedmodes_rejects_too_many_pairs();
  test_setsupportedmodes_rejects_repeated_mode();
  test_setsupportedmodes_rejects_missing_label();
  test_setsupportedmodes_rejects_oversized_label();
  test_setsupportedmodes_rejects_quote_in_label();
  test_setsupportedmodes_rejects_nonprintable_in_label();
  test_setsupportedmodes_failed_write_leaves_cache_unresent();
  test_supportedmodes_resent_on_next_reconcile();
  test_reconcile_with_nothing_set_sends_nothing();
  test_setcurrentmode_exact_wire_pin_and_cache_update();
  test_setcurrentmode_noop_on_unchanged_value();
  test_setcurrentmode_failed_write_leaves_cache();
  test_controller_changetomode_urc_updates_cache_and_fires_callback();
  test_attr_type_is_uint8();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

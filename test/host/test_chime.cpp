/*
 * test_chime.cpp - Task C7: MatterChime, the first real consumer of
 * AT_MT_SPEC.md S3.17's reserved fifth `+MTCMD` field (PlayChimeSound's
 * chimeID) and therefore the home for the Hearth core's widened dispatch
 * tests: the five-field adjudicated form, and (as a generic core-mechanism
 * pin, not a real chime scenario -- see that test's own comment) the
 * notify-only seq-0 form the same widening round introduced.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterChime.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void bringUp(MockStream &s, MatterChime &chime) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  chime.begin();
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0146\r\nOK\r\n");
  Matter.begin();
}

static void test_begin_declares_0x0146_variant0(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  check("declared as the chime device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x0146);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", chime.getEndPointId() == 1);
  check("begin() itself issued no AT traffic beyond the declaration", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  check("initial cached SelectedChime is 0", chime.getSelectedChime() == 0);
  check("initial cached Enabled is false", !chime.getEnabled());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  check("a second begin() after Matter.begin() is refused", !chime.begin());
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

/* ===== +MTCMD: the five-field adjudicated form, PlayChimeSound ===== */

/*
 * AT_MT_SPEC.md S3.17/S3.24, pinned exactly: "+MTCMD:<seq>,<ep>,<cluster>,
 * <command>,<payload>". Cluster 1366 (0x0556), command 0 (PlayChimeSound),
 * payload the requested chimeID -- this test is the reason the Hearth core
 * widening (MatterEndPoint.h's hearthOnForwardedCommand(), Hearth.cpp's
 * hearthDispatchCmd()) exists at all.
 */
static void test_forwarded_playchimesound_five_field_form_allow(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t seenChimeID = 255;
  chime.onPlayChime([&](uint8_t chimeID) { seenChimeID = chimeID; return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,1366,0,3"); /* PlayChimeSound, chimeID 3 */
  Hearth.poll();
  check("the reserved fifth field reached the callback as chimeID", seenChimeID == 3);
  check("an allowing onPlayChime() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * S3.17: "unlike the water valve..., the host's verdict reaches the
 * controller exactly as given, Status::Success on allow or
 * Status::Failure on deny". This is a REAL wire verdict, unlike
 * MatterWaterValve's; the pin below is the same AT+MTCMDRESP shape either
 * way (this library's dispatcher always sends the callback's verdict
 * unchanged), so what distinguishes chime from the valve is documented in
 * MatterChime.h's header comment, not observable from a host-side
 * MockStream script -- there is no different wire behaviour on THIS side
 * of the link to pin.
 */
static void test_forwarded_playchimesound_five_field_form_deny(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  chime.onPlayChime([](uint8_t) { return false; });
  s.expect("AT+MTCMDRESP=8,0", "OK\r\n");
  s.injectURC("+MTCMD:8,1,1366,0,5");
  Hearth.poll();
  check("a denying onPlayChime() answers exactly AT+MTCMDRESP=8,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_no_callback_denies(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  /* no onPlayChime() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,1366,0,1");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_wrong_cluster_denies(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  chime.onPlayChime([](uint8_t) { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0,1"); /* DoorLock's cluster, not Chime's */
  Hearth.poll();
  check("the wrong cluster id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_unrecognised_id_denies(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  chime.onPlayChime([](uint8_t) { return true; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,1366,9,1"); /* command id 9: not PlayChimeSound */
  Hearth.poll();
  check("an unrecognised command id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Hearth core regression: the FOUR-field form (no fifth field at all) must
 * still parse and dispatch exactly as it did before this task's widening --
 * every consumer before chime (the door lock) sends only this form, and
 * test_doorlock.cpp/test_reconcile.cpp already pin that behaviour
 * end-to-end. This test pins the same fact from chime's own callback,
 * confirming hasPayload/payload default to false/0 when the field is
 * absent, not garbage.
 */
static void test_forwarded_playchimesound_four_field_form_still_dispatches(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  bool called = false;
  uint8_t seenChimeID = 255;
  chime.onPlayChime([&](uint8_t chimeID) { called = true; seenChimeID = chimeID; return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,1366,0"); /* no fifth field */
  Hearth.poll();
  check("the callback still runs on the four-field form", called);
  check("with chimeID defaulting to 0 (no payload present)", seenChimeID == 0);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Hearth core regression, generic mechanism pin, NOT a real chime scenario:
 * AT_MT_SPEC.md S3.17's notify-only form ("+MTCMD:0,<ep>,<cluster>,
 * <command>", seq 0 reserved) is introduced by this same widening round but
 * has no real consumer among C7's three classes -- PlayChimeSound is
 * adjudicated (the test above), not notify-only; the real first consumer
 * (Smoke/CO Alarm's SelfTestRequest) is a later task. This test exercises
 * the core's seq-0 branch (hearthDispatchCmd() in Hearth.cpp) through a
 * MatterChime object anyway, because leaving new core branching
 * (Task C7's own change) completely uncovered by any test would violate
 * this project's TDD discipline: the callback still runs (dispatch without
 * reply is not "no dispatch"), but no AT+MTCMDRESP is ever sent for seq 0
 * -- sending one would itself earn +MTERR:1 from the firmware (S3.17: "the
 * firmware opens no mailbox slot... there is structurally nothing pending
 * under seq 0 to answer").
 */
static void test_notifyonly_seq_zero_dispatches_without_a_reply(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  int calls = 0;
  chime.onPlayChime([&](uint8_t) { calls++; return true; });
  s.injectURC("+MTCMD:0,1,1366,0"); /* seq 0: notify-only, no fifth field */
  Hearth.poll();
  check("the callback still ran under seq 0", calls == 1);
  check("no AT+MTCMDRESP was ever sent for seq 0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== setInstalledChimeSounds(): exact wire pin, one and several pairs ===== */

static void test_setinstalledsounds_one_pair_exact_wire_pin(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1 };
  const char *names[] = { "Doorbell" };
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\"", "OK\r\n");
  check("setInstalledChimeSounds(one pair) sends the exact wire pin", chime.setInstalledChimeSounds(ids, names, 1));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinstalledsounds_several_pairs_replaces_list(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids1[] = { 1 };
  const char *names1[] = { "Doorbell" };
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\"", "OK\r\n");
  check("first call succeeds", chime.setInstalledChimeSounds(ids1, names1, 1));

  uint8_t ids2[] = { 1, 2 };
  const char *names2[] = { "Doorbell", "Alert" };
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\",2,\"Alert\"", "OK\r\n");
  check("second call replaces the list, not appends", chime.setInstalledChimeSounds(ids2, names2, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.23's own worked example: a comma inside a quoted name is
 * legal and part of the name's own text. */
static void test_setinstalledsounds_comma_in_name(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1, 2 };
  const char *names[] = { "Doorbell", "Alert, urgent" };
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\",2,\"Alert, urgent\"", "OK\r\n");
  check("a comma inside a quoted name is sent verbatim, not split", chime.setInstalledChimeSounds(ids, names, 2));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinstalledsounds_rejects_zero_pairs(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  check("0 pairs is refused host-side", !chime.setInstalledChimeSounds(nullptr, nullptr, 0));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinstalledsounds_rejects_too_many_pairs(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[9];
  const char *names[9];
  char buf[9][8];
  for (int i = 0; i < 9; i++) {
    ids[i] = (uint8_t)i;
    snprintf(buf[i], sizeof(buf[i]), "S%d", i);
    names[i] = buf[i];
  }
  check("9 pairs exceeds the 8-pair cap and is refused", !chime.setInstalledChimeSounds(ids, names, 9));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* AT_MT_SPEC.md S3.23's own worked example: "AT+MTCHIMESOUNDS=10,1,
 * "Doorbell",1,"Chime" -> +MTERR:1 (id 1 repeated)", caught host-side. */
static void test_setinstalledsounds_rejects_repeated_id(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1, 1 };
  const char *names[] = { "Doorbell", "Chime" };
  check("a repeated id is refused host-side", !chime.setInstalledChimeSounds(ids, names, 2));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinstalledsounds_rejects_quote_in_name(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1 };
  const char *names[] = { "Bad\"Name" };
  check("a name containing a double quote is refused", !chime.setInstalledChimeSounds(ids, names, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setinstalledsounds_rejects_nonprintable_in_name(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1 };
  const char *names[] = { "Bad\x01Name" };
  check("a name containing a non-printable byte is refused", !chime.setInstalledChimeSounds(ids, names, 1));
  check("reports the grammar-violation code", Hearth.lastError() == 1);
  check("no AT traffic was issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== reconcile resend (B120 norm): not persisted, resent every boot ===== */

static void test_installedsounds_resent_on_next_reconcile(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  uint8_t ids[] = { 1, 2 };
  const char *names[] = { "Doorbell", "Alert" };
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\",2,\"Alert\"", "OK\r\n");
  check("setInstalledChimeSounds succeeds", chime.setInstalledChimeSounds(ids, names, 2));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0146\r\nOK\r\n");
  s.expect("AT+MTCHIMESOUNDS=1,1,\"Doorbell\",2,\"Alert\"", "OK\r\n"); /* resent verbatim */
  Matter.begin(); /* a second reconcile, e.g. a sketch's repeated loop() call */

  check("the list was resent verbatim on the second reconcile", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_reconcile_with_nothing_set_sends_nothing(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime); /* bringUp() itself already exercised one reconcile with nothing set */
  check("no AT+MTCHIMESOUNDS traffic with nothing set", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * SelectedChime/Enabled persist on the firmware side across AT+MTRESET
 * (AT_MT_SPEC.md S3.24), unlike InstalledChimeSounds: this class must NOT
 * push either at reconcile the way it pushes the sounds list. Confirms the
 * negative directly, on a chime that has actually set both, so a
 * regression that started pushing them would show up here as an
 * unexpected command.
 */
static void test_selectedchime_and_enabled_not_resent_on_reconcile(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,0,1", "OK\r\n");
  check("setSelectedChime(1) succeeds", chime.setSelectedChime(1));
  s.expect("AT+MTCHIME=1,1,1", "OK\r\n");
  check("setEnabled(true) succeeds", chime.setEnabled(true));

  s.expect("AT+MTEP?", "+MTEP:0,1,0x0146\r\nOK\r\n");
  Matter.begin();
  check("neither SelectedChime nor Enabled was resent (they persist firmware-side)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== SelectedChime / Enabled: cache-only in both directions ===== */

static void test_setselectedchime_exact_wire_pin_and_cache_update(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,0,3", "OK\r\n");
  check("setSelectedChime(3) sends the exact wire pin", chime.setSelectedChime(3));
  check("cache updated to 3", chime.getSelectedChime() == 3);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Final-review fix wave, Finding 2: SelectedChime/Enabled persist
 * firmware-side across AT+MTRESET (S3.24), but this class's cache always
 * re-initializes to 0/false in begin(), every boot. A host reboot is
 * exactly such a reset, so the firmware may be holding a genuinely
 * different value than the cache's fresh 0/false starting point, and the
 * OLD == guard silently swallowed the FIRST write of each field whenever
 * it happened to equal that starting point, the one case this host has no
 * way to tell "the wire already matches" from "nothing was ever sent".
 * The first post-begin() write of each field must therefore always reach
 * the wire, matching completeSelfTest()'s always-writes shape
 * (MatterSmokeCOAlarm.cpp), even when the value given equals the cache's
 * starting value. This test is RED against the old == guard.
 */
static void test_setselectedchime_first_write_reaches_wire_even_if_matching_cache(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime); /* cache starts 0 */
  s.expect("AT+MTCHIME=1,0,0", "OK\r\n");
  check("the first setSelectedChime(0) still reaches the wire", chime.setSelectedChime(0));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Once a value has actually reached the wire, a repeat of that SAME value
 * is still a no-op: the == guard's original purpose (skip redundant wire
 * traffic) still holds for every write after the first. */
static void test_setselectedchime_second_write_noop_on_unchanged_value(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,0,0", "OK\r\n");
  check("first write reaches the wire", chime.setSelectedChime(0));
  check("a second setSelectedChime(0), now matching an ACTUAL write, is a no-op", chime.setSelectedChime(0));
  check("no further AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setselectedchime_failed_write_leaves_cache(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,0,3", "+MTERR:1\r\nERROR\r\n"); /* id not installed */
  check("a rejected write returns false", !chime.setSelectedChime(3));
  check("cache untouched (still 0)", chime.getSelectedChime() == 0);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setenabled_exact_wire_pin_and_cache_update(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,1,1", "OK\r\n");
  check("setEnabled(true) sends the exact wire pin", chime.setEnabled(true));
  check("cache updated to true", chime.getEnabled());
  s.expect("AT+MTCHIME=1,1,0", "OK\r\n");
  check("setEnabled(false) sends the exact wire pin", chime.setEnabled(false));
  check("cache updated to false", !chime.getEnabled());
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Same Finding 2 fix, Enabled's side: RED against the old == guard. */
static void test_setenabled_first_write_reaches_wire_even_if_matching_cache(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime); /* cache starts false */
  s.expect("AT+MTCHIME=1,1,0", "OK\r\n");
  check("the first setEnabled(false) still reaches the wire", chime.setEnabled(false));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setenabled_second_write_noop_on_unchanged_value(void) {
  MockStream s; MatterChime chime;
  bringUp(s, chime);
  s.expect("AT+MTCHIME=1,1,0", "OK\r\n");
  check("first write reaches the wire", chime.setEnabled(false));
  check("a second setEnabled(false), now matching an ACTUAL write, is a no-op", chime.setEnabled(false));
  check("no further AT traffic issued", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoint 0 (not yet reconciled): every custom write path must fail
 * without ever reaching the wire, the same "endpoint 0 is unaddressable"
 * reasoning every sibling class's custom write path uses. */
static void test_setselectedchime_before_reconcile_fails_without_traffic(void) {
  MockStream s; MatterChime chime;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", chime.begin());
  check("setSelectedChime() before reconcile fails", !chime.setSelectedChime(1));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
  check("and nothing unscripted was sent either", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterChime tests =====\n");
  test_begin_declares_0x0146_variant0();
  test_rebegin_after_reconcile_refused();
  test_forwarded_playchimesound_five_field_form_allow();
  test_forwarded_playchimesound_five_field_form_deny();
  test_forwarded_command_no_callback_denies();
  test_forwarded_command_wrong_cluster_denies();
  test_forwarded_command_unrecognised_id_denies();
  test_forwarded_playchimesound_four_field_form_still_dispatches();
  test_notifyonly_seq_zero_dispatches_without_a_reply();
  test_setinstalledsounds_one_pair_exact_wire_pin();
  test_setinstalledsounds_several_pairs_replaces_list();
  test_setinstalledsounds_comma_in_name();
  test_setinstalledsounds_rejects_zero_pairs();
  test_setinstalledsounds_rejects_too_many_pairs();
  test_setinstalledsounds_rejects_repeated_id();
  test_setinstalledsounds_rejects_quote_in_name();
  test_setinstalledsounds_rejects_nonprintable_in_name();
  test_installedsounds_resent_on_next_reconcile();
  test_reconcile_with_nothing_set_sends_nothing();
  test_selectedchime_and_enabled_not_resent_on_reconcile();
  test_setselectedchime_exact_wire_pin_and_cache_update();
  test_setselectedchime_first_write_reaches_wire_even_if_matching_cache();
  test_setselectedchime_second_write_noop_on_unchanged_value();
  test_setselectedchime_failed_write_leaves_cache();
  test_setenabled_exact_wire_pin_and_cache_update();
  test_setenabled_first_write_reaches_wire_even_if_matching_cache();
  test_setenabled_second_write_noop_on_unchanged_value();
  test_setselectedchime_before_reconcile_fails_without_traffic();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

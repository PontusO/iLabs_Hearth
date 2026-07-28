/*
 * Host unit tests for the Preferences compatibility shim. Build with
 * `make -C test/host run`.
 *
 * The store is faked (PrefsStoreFake.cpp), so what is under test is the
 * record codec and the API semantics that have to match arduino-esp32:
 * defaults for missing keys, type width checks, namespace isolation,
 * read-only handles, and what happens when the region fills up.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "PrefsStoreFake.h"
#include "Preferences.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* ---- the two upstream examples' actual usage --------------------------- */

static void test_onofflight_usage(void) {
  hearthPrefsFakeReset();
  Preferences p;
  check("begin on a virgin store succeeds", p.begin("MatterPrefs", false));
  /* MatterOnOffLight.ino: getBool with a default of true before anything has
   * ever been written. This is the call that has to work on a board out of
   * the factory. */
  check("missing key returns the caller's default", p.getBool("OnOff", true) == true);
  check("putBool reports its width", p.putBool("OnOff", false) == 1);
  check("and reads back", p.getBool("OnOff", true) == false);
  p.end();

  /* A reboot: a fresh object over the same store. */
  Preferences q;
  q.begin("MatterPrefs", false);
  check("the value survives a re-begin", q.getBool("OnOff", true) == false);
}

static void test_dimmable_usage(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("MatterPrefs", false);
  p.putBool("OnOff", true);
  p.putUChar("Brightness", 200);
  check("both keys coexist", p.getBool("OnOff", false) == true);
  check("and the second reads back", p.getUChar("Brightness", 15) == 200);
  check("an untouched third key still defaults", p.getUChar("Missing", 15) == 15);
}

/* ---- codec behaviour ---------------------------------------------------- */

static void test_overwrite_in_place(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putUChar("a", 1);
  p.putUChar("b", 2);
  size_t before = p.freeEntries();
  p.putUChar("a", 99);
  check("same-width overwrite consumes no extra space", p.freeEntries() == before);
  check("the overwritten value is current", p.getUChar("a", 0) == 99);
  check("the record after it is intact", p.getUChar("b", 0) == 2);
}

static void test_resize_moves_the_record(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putUChar("a", 1);
  p.putUChar("b", 2);
  /* Rewriting "a" as a wider type has to delete and re-append, which moves
   * "b" down. If the splice is wrong, "b" is what breaks, not "a". */
  p.putUInt("a", 0xDEADBEEF);
  check("the widened value is correct", p.getUInt("a", 0) == 0xDEADBEEF);
  check("the record that shifted is intact", p.getUChar("b", 0) == 2);
  check("the old narrow value is gone", p.getUChar("a", 77) == 77);
}

static void test_type_width_is_enforced(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putUChar("small", 0x42);
  /* Reading a 1-byte record as 4 bytes must not return three bytes of the
   * next record. Upstream fails the read; so does this. */
  check("a too-wide read returns the default", p.getUInt("small", 12345) == 12345);
  check("the correctly-typed read still works", p.getUChar("small", 0) == 0x42);
}

static void test_bool_and_uchar_interchange(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putBool("flag", true);
  /* Upstream stores a bool as a one-byte unsigned, so this cross-read works
   * on ESP32 and has to work here. */
  check("a bool reads back as a uchar", p.getUChar("flag", 0) == 1);
  p.putUChar("flag2", 1);
  check("and a uchar reads back as a bool", p.getBool("flag2", false) == true);
  p.putUChar("zero", 0);
  check("a zero uchar is false", p.getBool("zero", true) == false);
}

static void test_namespaces_are_isolated(void) {
  hearthPrefsFakeReset();
  Preferences a, b;
  a.begin("one", false);
  b.begin("two", false);
  a.putUChar("k", 10);
  b.putUChar("k", 20);
  check("same key in two namespaces holds two values", a.getUChar("k", 0) == 10);
  check("and the other one is unaffected", b.getUChar("k", 0) == 20);
  a.clear();
  check("clear empties its own namespace", a.getUChar("k", 77) == 77);
  check("and leaves the other alone", b.getUChar("k", 0) == 20);
}

static void test_remove_and_iskey(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putUChar("gone", 5);
  p.putUChar("stays", 6);
  check("isKey finds a written key", p.isKey("gone"));
  check("remove reports success", p.remove("gone"));
  check("isKey no longer finds it", !p.isKey("gone"));
  check("removing it again fails", !p.remove("gone"));
  check("the neighbour survived the splice", p.getUChar("stays", 0) == 6);
}

static void test_readonly_refuses_writes(void) {
  hearthPrefsFakeReset();
  Preferences w;
  w.begin("ns", false);
  w.putUChar("k", 7);
  w.end();

  Preferences r;
  r.begin("ns", true);
  check("a read-only handle still reads", r.getUChar("k", 0) == 7);
  check("put on a read-only handle fails", r.putUChar("k", 8) == 0);
  check("and changes nothing", r.getUChar("k", 0) == 7);
  check("remove on a read-only handle fails", !r.remove("k"));
  check("clear on a read-only handle fails", !r.clear());
}

static void test_unstarted_handle(void) {
  hearthPrefsFakeReset();
  Preferences p;  /* no begin() */
  check("put without begin fails", p.putUChar("k", 1) == 0);
  check("get without begin returns the default", p.getUChar("k", 42) == 42);
  check("isKey without begin is false", !p.isKey("k"));
}

static void test_name_length_limit(void) {
  hearthPrefsFakeReset();
  Preferences p;
  check("a 16-character namespace is refused", !p.begin("0123456789abcdef", false));
  check("a 15-character namespace is accepted", p.begin("0123456789abcde", false));
  check("an empty key is refused", p.putUChar("", 1) == 0);
  check("a 16-character key is refused", p.putUChar("0123456789abcdef", 1) == 0);
  check("a 15-character key is accepted", p.putUChar("0123456789abcde", 1) == 1);
}

static void test_strings_and_bytes(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  check("putString reports the stored length", p.putString("s", "hello") == 6);  /* NUL included */
  check("getStringLength excludes the NUL", p.getStringLength("s") == 5);
  char buf[16];
  memset(buf, 0, sizeof(buf));
  check("getString fills the buffer", p.getString("s", buf, sizeof(buf)) == 6);
  check("and the contents are right", strcmp(buf, "hello") == 0);

  const uint8_t blob[4] = {1, 2, 3, 4};
  check("putBytes reports its length", p.putBytes("b", blob, sizeof(blob)) == 4);
  check("getBytesLength agrees", p.getBytesLength("b") == 4);
  uint8_t out[4] = {0, 0, 0, 0};
  check("getBytes round-trips", p.getBytes("b", out, sizeof(out)) == 4
        && memcmp(out, blob, sizeof(blob)) == 0);
  check("getStringLength on a blob is 0", p.getStringLength("b") == 0);
  check("getBytesLength on a string is 0", p.getBytesLength("s") == 0);
}

static void test_types_are_reported(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  p.putUChar("u8", 1);
  p.putInt("i32", 1);
  p.putString("str", "x");
  check("getType reports U8", p.getType("u8") == PT_U8);
  check("getType reports I32", p.getType("i32") == PT_I32);
  check("getType reports STR", p.getType("str") == PT_STR);
  check("getType on a missing key is INVALID", p.getType("nope") == PT_INVALID);
}

static void test_region_full(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  /* Fill the 256-byte fake until a put is refused. Each record is 5 header
   * bytes + 2 name + 3 key + payload. */
  int written = 0;
  char key[8];
  for (int i = 0; i < 100; i++) {
    snprintf(key, sizeof(key), "k%02d", i);
    if (p.putUInt(key, (uint32_t)i) == 0) {
      break;
    }
    written++;
  }
  check("the store fills up rather than running off the end", written > 0 && written < 100);
  check("freeEntries reports too little for another record", p.freeEntries() < 5 + 2 + 3 + 4);
  /* Everything written before the wall is still readable: a refused put must
   * not corrupt what came before it. */
  bool all_intact = true;
  for (int i = 0; i < written; i++) {
    snprintf(key, sizeof(key), "k%02d", i);
    if (p.getUInt(key, 0xFFFFFFFF) != (uint32_t)i) {
      all_intact = false;
      break;
    }
  }
  check("a refused put leaves every earlier record readable", all_intact);
}

static void test_unformatted_store_is_detected(void) {
  hearthPrefsFakeReset();
  /* Erased flash is 0xFF everywhere, which is not our magic. If the codec
   * trusted it, the first byte would read as a 255-byte namespace length and
   * the walk would immediately run off the region. */
  Preferences p;
  p.begin("ns", false);
  check("a virgin store reads as empty", !p.isKey("anything"));
  check("and is writable straight away", p.putUChar("k", 3) == 1);
  check("and reads back", p.getUChar("k", 0) == 3);
}

static void test_foreign_data_is_reformatted(void) {
  hearthPrefsFakeReset();
  uint8_t *raw = hearthPrefsFakeBytes();
  memset(raw, 0x5A, HEARTH_PREFS_FAKE_SIZE);  /* somebody else's EEPROM use */
  Preferences p;
  p.begin("ns", false);
  check("a foreign store is not walked as records", !p.isKey("anything"));
  check("and is usable after reformatting", p.putUChar("k", 9) == 1 && p.getUChar("k", 0) == 9);
}

static void test_put_commits_immediately(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  int before = hearthPrefsFakeCommits();
  p.putUChar("k", 1);
  check("put reaches storage before it returns", hearthPrefsFakeCommits() > before);
}

static void test_failed_commit_is_reported(void) {
  hearthPrefsFakeReset();
  Preferences p;
  p.begin("ns", false);
  hearthPrefsFakeFailCommits(true);
  check("a put whose commit fails reports 0", p.putUChar("k", 1) == 0);
  hearthPrefsFakeFailCommits(false);
}

int main(void) {
  printf("\n===== Preferences compatibility shim tests =====\n");
  test_onofflight_usage();
  test_dimmable_usage();
  test_overwrite_in_place();
  test_resize_moves_the_record();
  test_type_width_is_enforced();
  test_bool_and_uchar_interchange();
  test_namespaces_are_isolated();
  test_remove_and_iskey();
  test_readonly_refuses_writes();
  test_unstarted_handle();
  test_name_length_limit();
  test_strings_and_bytes();
  test_types_are_reported();
  test_region_full();
  test_unformatted_store_is_detected();
  test_foreign_data_is_reformatted();
  test_put_commits_immediately();
  test_failed_commit_is_reported();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

#include <stdio.h>
#include "ArduinoShim.h"
#include "HearthCompat.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void test_bool_roundtrip(void) {
  long out = -1;
  check("bool true flattens to 1", hearthAttrValToLong(esp_matter_bool(true), &out) && out == 1);
  check("bool false flattens to 0", hearthAttrValToLong(esp_matter_bool(false), &out) && out == 0);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_BOOLEAN, 1);
  check("1 rebuilds as bool true", v.type == ESP_MATTER_VAL_TYPE_BOOLEAN && v.val.b);
}

static void test_unsigned_roundtrip(void) {
  long out = 0;
  check("uint8 255 flattens", hearthAttrValToLong(esp_matter_uint8(255), &out) && out == 255);
  check("uint16 65535 flattens", hearthAttrValToLong(esp_matter_uint16(65535), &out) && out == 65535);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_UINT8, 64);
  check("64 rebuilds as uint8", v.type == ESP_MATTER_VAL_TYPE_UINT8 && v.val.u == 64);
}

static void test_signed_negative(void) {
  long out = 0;
  /* -12.34 C is the TemperatureMeasurement MeasuredValue -1234. Signedness is
   * the one thing a naive unsigned-only codec gets wrong, so it is asserted. */
  check("int16 -1234 keeps its sign",
        hearthAttrValToLong(esp_matter_int16((int16_t)-1234), &out) && out == -1234);
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INT16, -1234);
  check("-1234 rebuilds as int16", v.type == ESP_MATTER_VAL_TYPE_INT16 && v.val.i == -1234);
}

/*
 * Parity gap closed post-Task-7: upstream's own endpoint implementations
 * read val->val.u8 for a UINT8 attribute and val->val.i16 for an INT16 one
 * (not .u/.i), because an unmodified sketch's own attributeChangeCB override
 * carries that upstream body verbatim. This asserts the narrower members
 * actually see the value hearthAttrValFromLong wrote via .u/.i, not just
 * that the union compiles.
 */
static void test_narrow_union_members_read_back(void) {
  esp_matter_attr_val_t u8v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_UINT8, 200);
  check("uint8 200 rebuilds and reads back through .u8", u8v.type == ESP_MATTER_VAL_TYPE_UINT8 && u8v.val.u8 == 200);

  esp_matter_attr_val_t i16v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INT16, -1234);
  check("int16 -1234 rebuilds and reads back through .i16", i16v.type == ESP_MATTER_VAL_TYPE_INT16 && i16v.val.i16 == -1234);
}

/*
 * The boolean branch is the one case that used to write only the single
 * .val.b byte instead of the full 32-bit width, unlike every other branch
 * which writes through .i or .u. Upstream's own attributeChangeCB
 * implementations read val->val.u32 unconditionally near the top of the
 * function (e.g. MatterColorTemperatureLight.cpp's opening log_d call),
 * before ever branching on the attribute's real type, so a boolean write
 * that leaves .u16/.u32's upper bytes indeterminate is a real parity break,
 * not a theoretical one. Asserted via .val.u16 and .val.u32 specifically,
 * not .val.u8, since a single-byte write already happens to satisfy .u8.
 */
static void test_bool_write_is_full_width(void) {
  esp_matter_attr_val_t v1 = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_BOOLEAN, 1);
  check("bool true rebuilds and reads back through .u16", v1.val.u16 == 1);
  check("bool true rebuilds and reads back through .u32", v1.val.u32 == 1);

  esp_matter_attr_val_t v0 = esp_matter_bool(false);
  check("esp_matter_bool(false) reads back through .u16 too", v0.val.u16 == 0);
  check("esp_matter_bool(false) reads back through .u32 too", v0.val.u32 == 0);
}

/*
 * FINAL REVIEW, MINOR 2. The remaining esp_matter_* value constructors.
 * None of the four implemented endpoint classes uses them, but a sketch
 * calling setAttributeVal()/getAttributeVal() on an attribute of one of
 * these types needs them to exist at all to compile. Each is asserted three
 * ways: the type tag it sets, that hearthAttrValToLong() flattens it out of
 * the right union member (signed via .i, unsigned/enum/bitmap via .u, which
 * is what a naive one-member codec gets wrong for negatives), and that the
 * documented write-widest invariant holds, i.e. the value is readable back
 * through a narrower member than the one written.
 */
static void test_remaining_value_constructors(void) {
  long out = 0;

  esp_matter_attr_val_t i8v = esp_matter_int8(-42);
  check("esp_matter_int8 sets the INT8 type", i8v.type == ESP_MATTER_VAL_TYPE_INT8);
  check("esp_matter_int8 flattens with its sign", hearthAttrValToLong(i8v, &out) && out == -42);
  check("esp_matter_int8 wrote full width, readable through .i8", i8v.val.i8 == -42);

  esp_matter_attr_val_t i32v = esp_matter_int32(-70000);
  check("esp_matter_int32 sets the INT32 type", i32v.type == ESP_MATTER_VAL_TYPE_INT32);
  check("esp_matter_int32 flattens with its sign", hearthAttrValToLong(i32v, &out) && out == -70000);
  check("esp_matter_int32 reads back through .i", i32v.val.i == -70000);

  esp_matter_attr_val_t u32v = esp_matter_uint32(4000000000u);
  check("esp_matter_uint32 sets the UINT32 type", u32v.type == ESP_MATTER_VAL_TYPE_UINT32);
  check("esp_matter_uint32 flattens unsigned", hearthAttrValToLong(u32v, &out) && (unsigned long)out == 4000000000ul);
  check("esp_matter_uint32 reads back through .u32", u32v.val.u32 == 4000000000u);

  esp_matter_attr_val_t e8v = esp_matter_enum8(200);
  check("esp_matter_enum8 sets the ENUM8 type", e8v.type == ESP_MATTER_VAL_TYPE_ENUM8);
  check("esp_matter_enum8 flattens unsigned", hearthAttrValToLong(e8v, &out) && out == 200);
  /* .u16 and .u32, not .u8: a single-byte write would satisfy .u8 by
   * accident, so only the wider members prove the invariant. */
  check("esp_matter_enum8 wrote full width", e8v.val.u16 == 200 && e8v.val.u32 == 200);

  esp_matter_attr_val_t b8v = esp_matter_bitmap8(0x81);
  check("esp_matter_bitmap8 sets the BITMAP8 type", b8v.type == ESP_MATTER_VAL_TYPE_BITMAP8);
  check("esp_matter_bitmap8 flattens unsigned", hearthAttrValToLong(b8v, &out) && out == 0x81);
  check("esp_matter_bitmap8 wrote full width", b8v.val.u16 == 0x81 && b8v.val.u32 == 0x81);
}

static void test_uncarryable(void) {
  long out = 0;
  esp_matter_attr_val_t v; v.type = ESP_MATTER_VAL_TYPE_INVALID; v.val.i = 0;
  check("an uncarryable type is refused", !hearthAttrValToLong(v, &out));
}

static void test_rebuild_rejects_unknown_type(void) {
  /* A type outside the enum (reachable via an unchecked cast, e.g. a
   * corrupted or never-initialised attrVal->type) must not be handed back
   * as though it were valid: hearthAttrValFromLong's default branch forces
   * .type to ESP_MATTER_VAL_TYPE_INVALID so a caller inspecting the result
   * can tell the rebuild did not know the type. */
  esp_matter_attr_val_t v = hearthAttrValFromLong((esp_matter_val_type_t)99, 42);
  check("an out-of-enum type rebuilds as INVALID", v.type == ESP_MATTER_VAL_TYPE_INVALID);
}

int main(void) {
  printf("\n===== attribute value codec tests =====\n");
  test_bool_roundtrip();
  test_unsigned_roundtrip();
  test_signed_negative();
  test_narrow_union_members_read_back();
  test_bool_write_is_full_width();
  test_remaining_value_constructors();
  test_uncarryable();
  test_rebuild_rejects_unknown_type();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

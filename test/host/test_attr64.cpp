/*
 * Task 6 (energy round A): 64-bit attribute plumbing.
 *
 * The firmware side (AT_MT_SPEC.md S3.8 as of 0.8.0) emits and accepts
 * full-width 64-bit decimal in AT+MTATTR, signed per the attribute's own
 * type: a signed attribute carries INT64_MIN..INT64_MAX with a leading
 * minus when negative, an unsigned one carries 0..UINT64_MAX and the
 * firmware rejects a leading minus on it outright. This suite pins the
 * host mirror of that contract at its three layers:
 *
 *   1. the esp_matter_attr_val_t codec (HearthCompat.h): the INT64/UINT64
 *      types, their constructors, and full-width flatten/rebuild;
 *   2. the wire serializer (MatterEndPoint::hearthWriteAttr): emitted
 *      AT+MTATTR= lines captured verbatim through MockStream, including a
 *      byte-identity pin of the pre-change emission for 32-bit values;
 *   3. the reply and URC parse (getAttributeVal's line handler and
 *      Hearth.cpp's hearthDispatchAttr): scripted 64-bit read replies and
 *      injected URCs at the extremes.
 *
 * The extremes and the 2^31/2^32 seams are the interesting values: every
 * one of them is exactly what a 32-bit strtol/%ld pipeline silently
 * truncates or saturates, which is the defect this task removes.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/*
 * Concrete stand-in with a 64-bit-aware type table: cluster 0x0090 attr 8
 * (ElectricalPowerMeasurement ActivePower, the Task 7 consumer) is INT64,
 * cluster 0x0091 attr 1 (ElectricalEnergyMeasurement's energy mWh) is
 * UINT64. Everything else falls through to the base INTEGER default, same
 * as any endpoint type that never opted in.
 */
class TestEndPoint64 : public MatterEndPoint {
public:
  int changes = 0;
  esp_matter_attr_val_t last = {};
  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *val) override {
    changes++;
    last = *val;
    return true;
  }
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override {
    if (cluster_id == 0x0090 && attribute_id == 8) {
      return ESP_MATTER_VAL_TYPE_INT64;
    }
    if (cluster_id == 0x0091 && attribute_id == 1) {
      return ESP_MATTER_VAL_TYPE_UINT64;
    }
    return MatterEndPoint::hearthAttrTypeFor(cluster_id, attribute_id);
  }
};

/* ---------- layer 1: the codec ---------- */

static void test_int64_constructor_and_flatten(void) {
  int64_t out = 0;
  esp_matter_attr_val_t v = esp_matter_int64(INT64_MIN);
  check("esp_matter_int64 sets the INT64 type", v.type == ESP_MATTER_VAL_TYPE_INT64);
  check("INT64_MIN lives in .i64", v.val.i64 == INT64_MIN);
  check("INT64_MIN flattens intact", hearthAttrValToLong(v, &out) && out == INT64_MIN);
  v = esp_matter_int64(INT64_MAX);
  check("INT64_MAX flattens intact", hearthAttrValToLong(v, &out) && out == INT64_MAX);
}

static void test_uint64_constructor_and_flatten(void) {
  int64_t out = 0;
  esp_matter_attr_val_t v = esp_matter_uint64(UINT64_MAX);
  check("esp_matter_uint64 sets the UINT64 type", v.type == ESP_MATTER_VAL_TYPE_UINT64);
  check("UINT64_MAX lives in .u64", v.val.u64 == UINT64_MAX);
  /* The flatten carries the raw two's-complement bit pattern; the
   * signedness is the type tag's job, exactly as on the firmware side. */
  check("UINT64_MAX flattens as its bit pattern",
        hearthAttrValToLong(v, &out) && (uint64_t)out == UINT64_MAX);
}

static void test_seam_values_flatten(void) {
  int64_t out = 0;
  /* 2^31 and 2^32: the first value a 32-bit signed pipeline flips negative,
   * and the first a 32-bit unsigned one drops entirely. */
  check("2^31 keeps its sign through INT64",
        hearthAttrValToLong(esp_matter_int64((int64_t)2147483648LL), &out) && out == 2147483648LL);
  check("2^32 survives through INT64",
        hearthAttrValToLong(esp_matter_int64((int64_t)4294967296LL), &out) && out == 4294967296LL);
  check("2^32 survives through UINT64",
        hearthAttrValToLong(esp_matter_uint64(4294967296ULL), &out) && (uint64_t)out == 4294967296ULL);
}

static void test_rebuild_64(void) {
  esp_matter_attr_val_t v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INT64, INT64_MIN);
  check("INT64_MIN rebuilds into .i64", v.type == ESP_MATTER_VAL_TYPE_INT64 && v.val.i64 == INT64_MIN);
  v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_INT64, INT64_MAX);
  check("INT64_MAX rebuilds into .i64", v.val.i64 == INT64_MAX);
  v = hearthAttrValFromLong(ESP_MATTER_VAL_TYPE_UINT64, (int64_t)UINT64_MAX);
  check("UINT64_MAX rebuilds into .u64", v.type == ESP_MATTER_VAL_TYPE_UINT64 && v.val.u64 == UINT64_MAX);
}

/*
 * The write-widest invariant (HearthCompat.h's union comment, pinned for
 * the 32-bit members by test_attrval.cpp): with .i64/.u64 in the union the
 * widest member is now 64 bits, so every constructor writes that full
 * width and any narrower member reads back its little-endian prefix.
 * Asserted through the NEW wider members for OLD narrow constructors,
 * which is the direction that is indeterminate if a constructor still
 * writes only 32 bits.
 */
static void test_write_widest_covers_64(void) {
  check("esp_matter_uint32 wrote full 64-bit width",
        esp_matter_uint32(4000000000u).val.u64 == 4000000000ULL);
  check("esp_matter_int16 sign-extends through .i64",
        esp_matter_int16((int16_t)-1234).val.i64 == -1234);
  check("esp_matter_bool wrote full 64-bit width",
        esp_matter_bool(true).val.u64 == 1);
  /* And the 64-bit constructors stay readable through the narrow members,
   * the direction the invariant always guaranteed. */
  check("esp_matter_int64 of a small value reads back through .i",
        esp_matter_int64(-5).val.i == -5);
  check("esp_matter_uint64 of a small value reads back through .u",
        esp_matter_uint64(7).val.u == 7);
}

/* ---------- layer 2: the wire serializer ---------- */

static void test_write_extremes_on_the_wire(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTATTR=1,144,8,-9223372036854775808,1", "+MTATTR:1,144,8,-9223372036854775808\r\nOK\r\n");
  s.expect("AT+MTATTR=1,144,8,9223372036854775807,1", "+MTATTR:1,144,8,9223372036854775807\r\nOK\r\n");
  s.expect("AT+MTATTR=1,145,1,18446744073709551615,1", "+MTATTR:1,145,1,18446744073709551615\r\nOK\r\n");
  Hearth.begin(s);
  TestEndPoint64 ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_int64(INT64_MIN);
  check("INT64_MIN write emits full width", ep.updateAttributeVal(0x0090, 8, &v));
  v = esp_matter_int64(INT64_MAX);
  check("INT64_MAX write emits full width", ep.updateAttributeVal(0x0090, 8, &v));
  /* UINT64_MAX must render unsigned: the firmware rejects a leading minus
   * on an unsigned attribute, so "-1" here is not a smaller bug, it is a
   * refused write. */
  v = esp_matter_uint64(UINT64_MAX);
  check("UINT64_MAX write emits unsigned decimal", ep.updateAttributeVal(0x0091, 1, &v));
  check("all three lines matched verbatim", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_write_seams_on_the_wire(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTATTR=1,144,8,2147483648,1", "OK\r\n");
  s.expect("AT+MTATTR=1,144,8,-2147483649,1", "OK\r\n");
  s.expect("AT+MTATTR=1,145,1,4294967296,1", "OK\r\n");
  Hearth.begin(s);
  TestEndPoint64 ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_int64(2147483648LL);
  check("2^31 stays positive on the wire", ep.updateAttributeVal(0x0090, 8, &v));
  v = esp_matter_int64(-2147483649LL);
  check("-2^31-1 keeps its magnitude", ep.updateAttributeVal(0x0090, 8, &v));
  v = esp_matter_uint64(4294967296ULL);
  check("2^32 survives on the wire", ep.updateAttributeVal(0x0091, 1, &v));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/*
 * Byte-identity pin for 32-bit values. The three expected strings below are
 * a VERBATIM pre-change capture, recorded on unmodified main (commit
 * 2c80c47) by driving the same three writes through MockStream and printing
 * what arrived. If the 64-bit rework changes any byte of the 32-bit
 * emission, the strings stop matching and this fails; that is the whole
 * point, so never regenerate them from the code under test.
 */
static void test_small_values_emit_byte_identically(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTATTR=1,6,0,1,1", "OK\r\n");
  s.expect("AT+MTATTR=1,1026,0,-1234,1", "OK\r\n");
  s.expect("AT+MTATTR=1,1029,0,3000000000,0", "OK\r\n");
  Hearth.begin(s);
  TestEndPoint64 ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t b = esp_matter_bool(true);
  check("bool write is byte-identical", ep.updateAttributeVal(0x0006, 0x0000, &b));
  esp_matter_attr_val_t t = esp_matter_int16((int16_t)-1234);
  check("int16 write is byte-identical", ep.updateAttributeVal(0x0402, 0x0000, &t));
  esp_matter_attr_val_t u = esp_matter_uint32(3000000000u);
  check("uint32 write is byte-identical", ep.setAttributeVal(0x0405, 0x0000, &u));
  check("all three matched the pre-change capture", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* ---------- layer 3: reply and URC parse ---------- */

static void test_read_64bit_replies(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  s.expect("AT+MTATTR=1,144,8", "+MTATTR:1,144,8,-9223372036854775808\r\nOK\r\n");
  s.expect("AT+MTATTR=1,144,8", "+MTATTR:1,144,8,9223372036854775807\r\nOK\r\n");
  s.expect("AT+MTATTR=1,145,1", "+MTATTR:1,145,1,18446744073709551615\r\nOK\r\n");
  Hearth.begin(s);
  TestEndPoint64 ep;
  ep.setEndPointId(1);
  esp_matter_attr_val_t v = esp_matter_int64(0);
  check("INT64_MIN read succeeds", ep.getAttributeVal(0x0090, 8, &v));
  check("and lands intact in .i64", v.type == ESP_MATTER_VAL_TYPE_INT64 && v.val.i64 == INT64_MIN);
  v = esp_matter_int64(0);
  check("INT64_MAX read succeeds", ep.getAttributeVal(0x0090, 8, &v));
  check("and lands intact", v.val.i64 == INT64_MAX);
  v = esp_matter_uint64(0);
  check("UINT64_MAX read succeeds", ep.getAttributeVal(0x0091, 1, &v));
  check("and lands intact in .u64", v.type == ESP_MATTER_VAL_TYPE_UINT64 && v.val.u64 == UINT64_MAX);
  check("script drained", s.scriptDrained());
}

static void test_urc_dispatch_64bit(void) {
  MatterEndPoint::hearthClearDeclarations();
  MockStream s;
  Hearth.begin(s);
  TestEndPoint64 ep;
  MatterEndPoint::hearthDeclare(&ep, 0x0510);
  ep.setEndPointId(1);

  s.injectURC("+MTATTR:1,144,8,-9223372036854775808");
  Hearth.poll();
  check("INT64_MIN URC delivered", ep.changes == 1);
  check("typed INT64 with the full value",
        ep.last.type == ESP_MATTER_VAL_TYPE_INT64 && ep.last.val.i64 == INT64_MIN);

  s.injectURC("+MTATTR:1,145,1,18446744073709551615");
  Hearth.poll();
  check("UINT64_MAX URC delivered", ep.changes == 2);
  check("typed UINT64 with the full value",
        ep.last.type == ESP_MATTER_VAL_TYPE_UINT64 && ep.last.val.u64 == UINT64_MAX);

  /* Seam: 2^32 through the URC path, the value a 32-bit strtol saturates
   * (glibc clamps to LONG_MAX only on ERANGE; a 32-bit target truncates).
   * Either wrong answer differs from the true one. */
  s.injectURC("+MTATTR:1,144,8,4294967296");
  Hearth.poll();
  check("2^32 URC delivered", ep.changes == 3);
  check("with its full magnitude", ep.last.val.i64 == 4294967296LL);

  /* A small value through an endpoint with no 64-bit table entry keeps the
   * pre-change INTEGER default: same type tag, same .i landing spot. */
  s.injectURC("+MTATTR:1,6,0,1");
  Hearth.poll();
  check("a 32-bit URC still lands as INTEGER",
        ep.changes == 4 && ep.last.type == ESP_MATTER_VAL_TYPE_INTEGER && ep.last.val.i == 1);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== 64-bit attribute plumbing tests =====\n");
  test_int64_constructor_and_flatten();
  test_uint64_constructor_and_flatten();
  test_seam_values_flatten();
  test_rebuild_64();
  test_write_widest_covers_64();
  test_write_extremes_on_the_wire();
  test_write_seams_on_the_wire();
  test_small_values_emit_byte_identically();
  test_read_64bit_replies();
  test_urc_dispatch_64bit();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

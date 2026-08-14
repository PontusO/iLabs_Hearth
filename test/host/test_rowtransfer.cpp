/*
 * test_rowtransfer.cpp - Task 10 (energy round C2): HearthRowTransfer, the
 * shared AT+MTROW client. No concrete owner class exists yet (Task 11
 * builds MatterEvse), so this suite exercises the helper through
 * RowProbeEndpoint, a small MatterEndPoint that embeds one
 * HearthRowTransfer configured for kind 1 (EVSE charging target, nfields
 * 4), the test_demcontrol.cpp/test_cmdfields.cpp probe shape.
 *
 * The staged wire lines for kind 1's two-optional-field shape are taken
 * directly from design spec 2.2's own worked examples
 * ("AT+MTROW=5,1,0,64,480,80," / "AT+MTROW=5,1,1,64,1080,,15000") so the
 * absent-field convention is pinned against the spec's own text, not a
 * value this suite invented.
 */
#include <stdio.h>
#include <string.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndPoint.h"
#include "HearthRowTransfer.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

namespace {
const uint8_t kEvseTargetKind = 1;
const uint8_t kEvseTargetFields = 4;  // day, time, soc, energy (mt_rows.c)

HearthRowTransfer::Row evseRow(int64_t day, int64_t minutes, bool hasSoc, int64_t soc, bool hasEnergy, int64_t energy) {
  HearthRowTransfer::Row r;
  for (uint8_t i = 0; i < HearthRowTransfer::kMaxFields; i++) {
    r.present[i] = false;
    r.value[i] = 0;
  }
  r.present[0] = true;
  r.value[0] = day;
  r.present[1] = true;
  r.value[1] = minutes;
  r.present[2] = hasSoc;
  r.value[2] = soc;
  r.present[3] = hasEnergy;
  r.value[3] = energy;
  return r;
}
}  // namespace

class RowProbeEndpoint : public MatterEndPoint {
public:
  HearthRowTransfer rows;

  RowProbeEndpoint() : rows(this, kEvseTargetKind, kEvseTargetFields) {}

  bool attributeChangeCB(uint16_t, uint32_t, uint32_t, esp_matter_attr_val_t *) override {
    return true;
  }
};

static void bringUp(MockStream &s, RowProbeEndpoint &ep, uint16_t id = 5) {
  MatterEndPoint::hearthClearDeclarations();
  MatterEndPoint::hearthDeclare(&ep, 0x050C);
  ep.setEndPointId(id);
  Hearth.begin(s);
}

/* ===== stage(): the absent-field convention, outbound direction ===== */

static void test_stage_pins_design_spec_worked_example_soc_only(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROW=5,1,0,64,480,80,", "OK\r\n");
  check("stage(0, day=64 time=480 soc=80 no energy) matches design spec 2.2's own example",
        ep.rows.stage(0, evseRow(64, 480, true, 80, false, 0)));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_stage_pins_design_spec_worked_example_energy_only(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROW=5,1,1,64,1080,,15000", "OK\r\n");
  check("stage(1, day=64 time=1080 no soc energy=15000) matches design spec 2.2's own example",
        ep.rows.stage(1, evseRow(64, 1080, false, 0, true, 15000)));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_stage_both_optionals_present(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROW=5,1,2,32,600,90,5000", "OK\r\n");
  check("stage with both optionals present sends both, no absent tokens",
        ep.rows.stage(2, evseRow(32, 600, true, 90, true, 5000)));
  check("script drained", s.scriptDrained());
}

static void test_stage_refused_by_wire_returns_false(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROW=5,1,70,64,480,80,", "+MTERR:1\r\nERROR\r\n");
  check("an idx past the kind's ceiling is refused", !ep.rows.stage(70, evseRow(64, 480, true, 80, false, 0)));
  check("no unexpected commands", s.unexpected().empty());
}

/* ===== clearStaged() / apply(), including the count-0 clear ===== */

static void test_clear_staged(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWCLEAR=5,1", "OK\r\n");
  check("clearStaged() sends AT+MTROWCLEAR", ep.rows.clearStaged());
  check("script drained", s.scriptDrained());
}

static void test_apply_with_nonzero_count(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWAPPLY=5,1,2", "OK\r\n");
  check("apply(2) commits the staged set", ep.rows.apply(2));
  check("script drained", s.scriptDrained());
}

/*
 * The count-0 clear (design spec 2.6, mt_rows.c's own comment on the two
 * firmware defects that shipped from getting this direction wrong): apply()
 * always sends the literal count with no special casing, so count==0 must
 * reach the wire exactly like any other value, never suppressed or turned
 * into a different command.
 */
static void test_apply_zero_sends_the_clear_request(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWAPPLY=5,1,0", "OK\r\n");
  check("apply(0) sends AT+MTROWAPPLY=5,1,0 unconditionally, needing nothing staged first", ep.rows.apply(0));
  check("script drained", s.scriptDrained());
}

/* ===== getRow(): single-index reads, the absent-field convention inbound ===== */

static void test_get_row_present_with_absent_trailing_field(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,0", "+MTROW:0,2,64,480,80,\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 999;
  bool gotRow = false;
  check("getRow(0) succeeds", ep.rows.getRow(0, row, total, gotRow));
  check("gotRow true", gotRow);
  check("total is 2", total == 2);
  check("day field present, 64", row.present[0] && row.value[0] == 64);
  check("time field present, 480", row.present[1] && row.value[1] == 480);
  check("soc field present, 80", row.present[2] && row.value[2] == 80);
  check("energy field absent: the trailing empty token parsed as absent", !row.present[3]);
  check("script drained", s.scriptDrained());
}

static void test_get_row_present_with_absent_middle_field(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,1", "+MTROW:1,2,64,1080,,15000\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 0;
  bool gotRow = false;
  check("getRow(1) succeeds", ep.rows.getRow(1, row, total, gotRow));
  check("gotRow true", gotRow);
  check("day field present, 64", row.present[0] && row.value[0] == 64);
  check("time field present, 1080", row.present[1] && row.value[1] == 1080);
  check("soc field absent: the middle empty token parsed as absent", !row.present[2]);
  check("energy field present, 15000", row.present[3] && row.value[3] == 15000);
}

/* Both optionals absent in the SAME row (a shape the firmware itself would
 * never emit for kind 1, since it enforces "at least one" at apply, but
 * this class is generic across kinds and must parse it correctly anyway:
 * two adjacent empty fields, the last one ending the line at NUL rather
 * than a comma). */
static void test_get_row_both_trailing_fields_absent(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,0", "+MTROW:0,1,64,480,,\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 0;
  bool gotRow = false;
  check("getRow(0) succeeds", ep.rows.getRow(0, row, total, gotRow));
  check("gotRow true", gotRow);
  check("soc absent", !row.present[2]);
  check("energy absent, and the line's own trailing comma-then-nothing parsed cleanly", !row.present[3]);
}

/*
 * design spec 2.3: "An <idx> at or beyond <total> answers OK with no
 * +MTROW line, which is how a host detects an empty payload." idx == 0
 * getting no line is the documented signal that the store is empty.
 */
static void test_get_row_idx_zero_no_line_means_empty_store(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,0", "OK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 999;
  bool gotRow = true;
  check("getRow(0) still succeeds (OK, just no line)", ep.rows.getRow(0, row, total, gotRow));
  check("gotRow false: nothing at index 0", !gotRow);
  check("total reads 0: the documented empty-store signal", total == 0);
}

static void test_get_row_past_total_on_nonempty_store(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,5", "OK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 999;
  bool gotRow = true;
  check("getRow(5) past total still succeeds", ep.rows.getRow(5, row, total, gotRow));
  check("gotRow false", !gotRow);
  check("script drained", s.scriptDrained());
}

/* ===== getAll(): the bulk unqualified read ===== */

static void test_get_all_bulk_two_rows(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1", "+MTROW:0,2,64,480,80,\r\n+MTROW:1,2,64,1080,,15000\r\nOK\r\n");
  HearthRowTransfer::Row rows[4];
  uint16_t total = 0, returned = 0;
  check("getAll succeeds", ep.rows.getAll(rows, 4, total, returned));
  check("total is 2", total == 2);
  check("returned is 2", returned == 2);
  check("row 0 soc present 80, energy absent", rows[0].present[2] && rows[0].value[2] == 80 && !rows[0].present[3]);
  check("row 1 soc absent, energy present 15000", !rows[1].present[2] && rows[1].present[3] && rows[1].value[3] == 15000);
  check("script drained", s.scriptDrained());
}

static void test_get_all_bulk_empty_store_is_authoritative(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1", "OK\r\n");
  HearthRowTransfer::Row rows[4];
  uint16_t total = 999, returned = 999;
  check("getAll on an empty store still succeeds", ep.rows.getAll(rows, 4, total, returned));
  check("total is authoritatively 0 (no lines arrived, OK)", total == 0);
  check("returned is 0", returned == 0);
}

static void test_get_all_truncates_when_capacity_is_too_small(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1", "+MTROW:0,2,1,1,1,\r\n+MTROW:1,2,2,2,2,\r\nOK\r\n");
  HearthRowTransfer::Row rows[1];
  uint16_t total = 0, returned = 0;
  check("getAll succeeds even when rows[] is smaller than total", ep.rows.getAll(rows, 1, total, returned));
  check("total reports the real count, 2", total == 2);
  check("returned is capped at capacity, 1", returned == 1);
  check("returned < total signals truncation to the caller", returned < total);
}

/* ===== the seq-qualified proposal read, and the unqualified/qualified split ===== */

static void test_get_proposed_row(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,3,42", "+MTROW:3,5,64,600,,20000\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 0;
  bool gotRow = false;
  check("getProposedRow(seq=42, idx=3) sends the seq-qualified single form",
        ep.rows.getProposedRow(42, 3, row, total, gotRow));
  check("gotRow true", gotRow);
  check("total is 5 (the proposed set's own total)", total == 5);
  check("energy present, 20000", row.present[3] && row.value[3] == 20000);
}

static void test_get_all_proposed_uses_the_empty_idx_token(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,,42", "+MTROW:0,1,64,480,80,\r\nOK\r\n");
  HearthRowTransfer::Row rows[4];
  uint16_t total = 0, returned = 0;
  check("getAllProposed(seq=42) sends the double-comma bulk form", ep.rows.getAllProposed(42, rows, 4, total, returned));
  check("total is 1", total == 1);
  check("returned is 1", returned == 1);
}

/*
 * The unqualified and seq-qualified reads must be genuinely different wire
 * lines: this pins that an unqualified getRow() never accidentally carries
 * a seq, and a seq-qualified read never drops it.
 */
static void test_unqualified_and_qualified_reads_are_distinct_wire_lines(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,0", "OK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 0;
  bool gotRow = false;
  check("unqualified single read carries no seq", ep.rows.getRow(0, row, total, gotRow));

  s.expect("AT+MTROWGET=5,1,0,7", "OK\r\n");
  check("qualified single read carries exactly the given seq", ep.rows.getProposedRow(7, 0, row, total, gotRow));
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* seq == 0 is never issued by the firmware (cmd_mtrowget()'s own comment):
 * refused host-side, zero wire traffic, the HearthDemControl n>4 precedent. */
static void test_seq_zero_refused_host_side(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  Hearth.hearthSetError(0);
  HearthRowTransfer::Row row;
  uint16_t total = 0;
  bool gotRow = false;
  check("getProposedRow(seq=0) refused", !ep.rows.getProposedRow(0, 0, row, total, gotRow));
  check("error 1", Hearth.lastError() == 1);

  Hearth.hearthSetError(0);
  HearthRowTransfer::Row rows[4];
  uint16_t returned = 0;
  check("getAllProposed(seq=0) refused", !ep.rows.getAllProposed(0, rows, 4, total, returned));
  check("error 1", Hearth.lastError() == 1);
  check("zero wire traffic for both", s.scriptDrained() && s.unexpected().empty());
}

/* ===== a malformed +MTROW line is dropped, not trusted partially ===== */

static void test_malformed_row_line_is_dropped(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  /* "64,480,80" (three fields) is one short of kind 1's four-field arity:
   * the device never sends this, but the parser must not crash or invent
   * a fourth field, and must not report a row that was never validly
   * parsed. */
  s.expect("AT+MTROWGET=5,1,0", "+MTROW:0,1,64,480,80\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 999;
  bool gotRow = true;
  check("the call itself still succeeds (OK at the wire level)", ep.rows.getRow(0, row, total, gotRow));
  check("the malformed line was dropped: gotRow false", !gotRow);
}

/*
 * Defence in depth: a single-row read must only accept a line naming the
 * exact idx it asked for. Real firmware never sends a mismatched idx on a
 * single-row AT+MTROWGET, but this pins that the class itself enforces the
 * match rather than trusting "a +MTROW line arrived at all".
 */
static void test_get_row_ignores_mismatched_idx_line(void) {
  MockStream s;
  RowProbeEndpoint ep;
  bringUp(s, ep);

  s.expect("AT+MTROWGET=5,1,0", "+MTROW:9,10,64,480,80,\r\nOK\r\n");
  HearthRowTransfer::Row row;
  uint16_t total = 999;
  bool gotRow = true;
  check("the call still succeeds at the wire level", ep.rows.getRow(0, row, total, gotRow));
  check("gotRow false: the line named idx 9, not the requested idx 0", !gotRow);
}

/* ===== owner not yet declared: every method refuses, zero wire traffic ===== */

static void test_undeclared_endpoint_refuses_every_call(void) {
  MockStream s;
  RowProbeEndpoint ep;  // never bringUp(): getEndPointId() stays 0
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);

  Hearth.hearthSetError(0);
  check("stage refused", !ep.rows.stage(0, evseRow(1, 1, true, 1, false, 0)));
  check("error 2", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  check("clearStaged refused", !ep.rows.clearStaged());
  check("error 2", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  check("apply refused", !ep.rows.apply(1));
  check("error 2", Hearth.lastError() == 2);

  Hearth.hearthSetError(0);
  HearthRowTransfer::Row row;
  uint16_t total = 0, returned = 0;
  bool gotRow = false;
  check("getRow refused", !ep.rows.getRow(0, row, total, gotRow));
  check("error 2", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  HearthRowTransfer::Row rows[4];
  check("getAll refused", !ep.rows.getAll(rows, 4, total, returned));
  check("error 2", Hearth.lastError() == 2);

  /*
   * Fix round 1, finding 1: getProposedRow()/getAllProposed() carry the
   * identical getEndPointId() == 0 guard as the five methods above, but it
   * went untested here (every other call site in this suite uses a
   * declared endpoint). This class's own mutation history establishes that
   * a missing wire-level guard here manifests as an INDEFINITE HANG, not a
   * clean failure (HearthLink's blocking read loop against the host test's
   * fake millis(), which only advances via yield()), so the untested path
   * was exactly the one whose failure mode is worst. `seq` is a valid
   * nonzero value (7) so the seq == 0 guard is not what refuses these
   * calls: the endpoint guard, checked second in both methods, must be
   * what fires.
   */
  Hearth.hearthSetError(0);
  check("getProposedRow refused (endpoint guard, not the seq guard)", !ep.rows.getProposedRow(7, 0, row, total, gotRow));
  check("error 2, not error 1: the endpoint guard fired, not the seq guard", Hearth.lastError() == 2);
  Hearth.hearthSetError(0);
  check("getAllProposed refused (endpoint guard, not the seq guard)", !ep.rows.getAllProposed(7, rows, 4, total, returned));
  check("error 2, not error 1: the endpoint guard fired, not the seq guard", Hearth.lastError() == 2);

  check("zero wire traffic for the whole batch", s.scriptDrained() && s.unexpected().empty());
}

int main(void) {
  printf("\n===== HearthRowTransfer tests =====\n");
  test_stage_pins_design_spec_worked_example_soc_only();
  test_stage_pins_design_spec_worked_example_energy_only();
  test_stage_both_optionals_present();
  test_stage_refused_by_wire_returns_false();
  test_clear_staged();
  test_apply_with_nonzero_count();
  test_apply_zero_sends_the_clear_request();
  test_get_row_present_with_absent_trailing_field();
  test_get_row_present_with_absent_middle_field();
  test_get_row_both_trailing_fields_absent();
  test_get_row_idx_zero_no_line_means_empty_store();
  test_get_row_past_total_on_nonempty_store();
  test_get_all_bulk_two_rows();
  test_get_all_bulk_empty_store_is_authoritative();
  test_get_all_truncates_when_capacity_is_too_small();
  test_get_proposed_row();
  test_get_all_proposed_uses_the_empty_idx_token();
  test_unqualified_and_qualified_reads_are_distinct_wire_lines();
  test_get_row_ignores_mismatched_idx_line();
  test_seq_zero_refused_host_side();
  test_malformed_row_line_is_dropped();
  test_undeclared_endpoint_refuses_every_call();

  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

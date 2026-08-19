/*
 * HearthRowTransfer.h - Task 10 (energy round C2, design spec section 7.1):
 * the shared `AT+MTROW` client. Owns staging, apply-with-count, clear,
 * indexed and bulk reads and the seq-qualified proposal read for ANY row
 * kind (`main/include/mt_rows.h`'s `<kind>` byte), not only kind 1 (the
 * EVSE charging target). Extracted as its own unit from the outset, the
 * same reason `HearthMeasurementPush` was pulled out of
 * `MatterElectricalSensor` in round B: 0.13.0's DEM forecast is this
 * class's SECOND consumer (design spec section 9, "DEM forecast backfill"),
 * and `MatterEvse` (Task 11) is its first.
 *
 * ONE INSTANCE SERVES ONE (endpoint, kind) FOR ITS WHOLE LIFETIME, the
 * `HearthDemControl`/`HearthMeasurementPush` shape: the owner passes `this`
 * from its own constructor's init list (the endpoint id is read per wire
 * call, since it is 0 until the owner's declaration reconciles), and `kind`
 * plus `nfields` are fixed at construction. `nfields` must equal the
 * firmware's own `mt_rows_field_count(kind)` (main/mt_rows.c); this class
 * trusts its caller and does not itself carry a copy of the per-kind field
 * table, the same division `HearthMeasurementPush`/`HearthDemControl` make
 * between "this class knows the wire shape" and "the firmware knows the
 * field ranges".
 *
 * A `Row` is a flat array of up to `kMaxFields` (the firmware's own
 * `MT_ROW_MAX_FIELDS`) optional int64 fields, generic across every kind
 * that will ever exist; only the first `nfields` positions are meaningful
 * for a transfer configured for a narrower kind (kind 1 uses 4 of the 8).
 * `present[i] == false` is the absent-optional convention design spec 2.2
 * introduces ("an EMPTY FIELD means an absent optional, but the TOKEN must
 * still be present, so the field count always equals the kind's arity
 * exactly"): this class renders an absent field as an empty token when
 * staging and parses an empty token back into `present[i] == false` when
 * reading, in both directions, so a round trip through this class alone
 * never invents or drops a field.
 *
 * READS ARE STATELESS ON THE WIRE (design spec 2.3): every read re-derives
 * `<total>` from the answer, this class keeps no cursor of its own, and a
 * lost line is simply re-fetched. `getRow()`'s `total` output is only ever
 * what THAT call's own response told us: an `<idx>` at or past `<total>`
 * answers `OK` with no `+MTROW` line at all (design spec 2.3), so a miss at
 * `idx == 0` is the documented way to detect an EMPTY payload (`total`
 * reported as 0 is trustworthy there), but a miss at `idx > 0` tells this
 * class nothing about the real `<total>` beyond "past it": do not read
 * `total` as authoritative in that case, use `getAll()` or re-query index 0
 * instead. This is a wire fact, not a limitation of this class.
 *
 * THE SEQ-QUALIFIED READ (design spec section 2.3's `AT+MTROWGET`
 * `[,<seq>]` tail, mirrored in `main/mt_at.c`'s `cmd_mtrowget()` header
 * comment): with a matching `seq` the read serves the PROPOSED, uncommitted
 * rows of an outstanding adjudicated `+MTCMD` forward; with no `seq` it
 * always serves the LIVE store. This class holds no memory of a `seq` of
 * its own and never invents, caches or reuses one: the caller (the owner's
 * `hearthOnForwardedCommandFields()` override, Task 11) must pass through
 * the exact value the `+MTCMD` line carried, and only that value, for the
 * lifetime of that one verdict. `seq == 0` is refused host-side before any
 * wire traffic (Hearth error 1): the firmware never issues it and
 * `cmd_mtrowget()` itself documents it as "the idle marker", so naming it
 * can never be a legitimate request, the `HearthDemControl` n>4 precedent
 * for a host-side refusal whose answer is already known.
 */
#pragma once

#include <stdint.h>

class MatterEndPoint;

class HearthRowTransfer {
public:
  // main/include/mt_rows.h's MT_ROW_MAX_FIELDS: the widest row shape any
  // kind may ever use. A transfer configured for a narrower kind (kind 1's
  // 4 fields) only ever touches its own first `nfields` positions.
  static const uint8_t kMaxFields = 8;

  struct Row {
    bool present[kMaxFields];
    int64_t value[kMaxFields];
  };

  // Stores the pointer only, the HearthMeasurementPush/HearthDemControl
  // shape. `kind` and `nfields` are fixed for this object's lifetime.
  HearthRowTransfer(MatterEndPoint *owner, uint8_t kind, uint8_t nfields);

  // "AT+MTROW=<ep>,<kind>,<idx>,<field>[,...]" (design spec 2.1/2.2):
  // stages one row. Only `row.present[0..nfields-1]`/`value[0..nfields-1]`
  // are sent; positions at or past `nfields` are never looked at. Returns
  // false on any wire refusal (bare ERROR or a coded +MTERR); the caller
  // reads Hearth.lastError() for which.
  bool stage(uint16_t idx, const Row &row);

  // "AT+MTROWCLEAR=<ep>,<kind>" (design spec 2.6): discards the PENDING
  // staged set only, never the stored/live payload. Refused (+MTERR:1) by
  // the firmware itself if nothing is currently staged for this (ep, kind)
  // -- this class does not shadow that with its own guess.
  bool clearStaged();

  // "AT+MTROWAPPLY=<ep>,<kind>,<count>" (design spec 2.6): validates the
  // whole staged set and commits it, merging by day rather than replacing
  // wholesale (design spec 2.6's own emphasis; this class has no opinion on
  // that, it only carries the count). `count == 0` is the documented clear
  // request: it empties the ENTIRE stored payload unconditionally and needs
  // nothing staged first (mt_rows.c's own comment on the two firmware
  // defects that shipped from getting this direction wrong) -- this method
  // always sends the literal `count` given, with no special-casing on
  // either side of zero.
  bool apply(uint16_t count);

  // "AT+MTROWGET=<ep>,<kind>,<idx>" (design spec 2.3): one row from the
  // LIVE store. Returns false only on a wire failure (link down, timeout,
  // a coded error); on a true return, `total` is this call's own reported
  // total (see this header's own comment on when that is trustworthy) and
  // `gotRow` says whether `row` was actually filled -- `idx` at or past
  // `total` is not an error, it is the documented "nothing there" answer.
  bool getRow(uint16_t idx, Row &row, uint16_t &total, bool &gotRow);

  // "AT+MTROWGET=<ep>,<kind>" (design spec 2.3): every row in the LIVE
  // store, up to `maxRows` capacity. `total` is the full row count the
  // device reported (authoritative: a bulk read walks every row that
  // exists, so either at least one line arrived and carried the true
  // total, or none did and the store is genuinely empty, i.e. total == 0).
  // `returned` is how many were actually copied into `rows`; `returned <
  // total` means `rows` was too small and the tail was not copied, an
  // event the caller should size `maxRows` from the kind's own known
  // ceiling to avoid (e.g. HearthChargingSchedule::kMaxEntries for kind 1).
  bool getAll(Row *rows, uint16_t maxRows, uint16_t &total, uint16_t &returned);

  // "AT+MTROWGET=<ep>,<kind>,<idx>,<seq>": one row from the PROPOSED set
  // of the outstanding adjudicated forward named by `seq` (this header's
  // own comment above). Same return shape as getRow(). `seq` must be the
  // exact value the owner's own `+MTCMD` dispatch received; `seq == 0` is
  // refused host-side (Hearth error 1, zero wire traffic).
  bool getProposedRow(uint32_t seq, uint16_t idx, Row &row, uint16_t &total, bool &gotRow);

  // "AT+MTROWGET=<ep>,<kind>,,<seq>" (the empty-idx-token bulk spelling,
  // design spec's own "one spelling per shape"): every row of the PROPOSED
  // set. Same return shape as getAll(). `seq == 0` is refused host-side.
  bool getAllProposed(uint32_t seq, Row *rows, uint16_t maxRows, uint16_t &total, uint16_t &returned);

private:
  struct ReadCtx;

  static void hearthOnRowLine(const char *line, void *arg);
  bool hearthSendGet(const char *cmd, ReadCtx &ctx);

  MatterEndPoint *owner;
  uint8_t kind;
  uint8_t nfields;
};

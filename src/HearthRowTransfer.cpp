/*
 * HearthRowTransfer.cpp - implementation. See HearthRowTransfer.h for the
 * owner/kind/nfields shape, the absent-field convention in both
 * directions, the stateless-read semantics and the seq-qualified proposal
 * read.
 */
#include "HearthRowTransfer.h"
#include "MatterEndPoint.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. HearthCompat.h
 * (hearthParseWireValue()) arrives transitively through MatterEndPoint.h. */
#include "HearthGlobal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * `nfields` is clamped to kMaxFields here (fix round 1, finding 3): it is
 * the invariant that makes stage()'s 224-byte buffer math (that method's
 * own comment) actually hold. A caller is documented to pass
 * `mt_rows_field_count(kind)`, which can never exceed the firmware's own
 * MT_ROW_MAX_FIELDS == kMaxFields, so this is not expected to ever clamp
 * anything in practice; it exists so a future misuse (a wrong constant, a
 * copy-paste from a table row that does not belong to this kind) is turned
 * into an under-count -- rows silently missing their tail fields, source-
 * visible in the wire's own field count -- rather than a class whose
 * buffer math is silently no longer true.
 */
HearthRowTransfer::HearthRowTransfer(MatterEndPoint *owner, uint8_t kind, uint8_t nfields)
  : owner(owner), kind(kind), nfields(nfields > kMaxFields ? kMaxFields : nfields) {}

/*
 * Shared state for one AT+MTROWGET exchange, filled by hearthOnRowLine()
 * as +MTROW: lines arrive and drained back into the caller's out-params by
 * whichever public read method built it. `single`/`wantIdx`/`singleRow`
 * matter only for the single-row forms; `rows`/`maxRows`/`returned` only
 * for the bulk forms. See HearthRowTransfer.h's own comment on why `total`
 * is only ever what THIS call's response told us.
 */
struct HearthRowTransfer::ReadCtx {
  uint8_t nfields;
  bool single;
  uint16_t wantIdx;
  Row *singleRow;
  bool singleGot;
  Row *rows;
  uint16_t maxRows;
  uint16_t returned;
  uint16_t total;
  bool gotAnyLine;
};

/*
 * Parses one "+MTROW:<idx>,<total>,<field>[,<field>...]" line (design spec
 * 2.3) into ctx, or drops it silently if it is not a +MTROW line at all, or
 * is one but malformed. A malformed line from this device's own firmware
 * should never happen (mt_at.c's emit_row_line() is the only thing that
 * ever writes one), but a dropped/garbled line is treated the same way
 * hearthDispatchCmd() (Hearth.cpp) treats a malformed +MTCMD tail: ignored
 * rather than trusted partially, so a corrupted line can never fabricate a
 * field value.
 *
 * The field walk requires EXACTLY ctx->nfields fields, matching design spec
 * 2.1's "the field count always equals the kind's arity exactly": unlike
 * +MTCMD's open-ended optional tail, there is no legal shorter or longer
 * +MTROW line for a given kind. Each field's terminator is ',' for every
 * position except the last, which is '\0' (this line's own end, there is
 * no trailing tail after the last row field). An empty field (the
 * terminator appearing immediately, with nothing parsed before it) is the
 * absent-optional convention (design spec 2.2) and leaves present[i] false
 * with value[i] left at 0; a non-empty field is parsed with
 * hearthParseWireValue() (the +MTATTR/+MTCMD full-width int64 parser, not
 * strtoul(), since a row field can carry the full pipeline width, e.g. the
 * EVSE target's added-energy field).
 */
void HearthRowTransfer::hearthOnRowLine(const char *line, void *arg) {
  ReadCtx *ctx = (ReadCtx *)arg;
  if (strncmp(line, "+MTROW:", 7) != 0) {
    return;
  }
  const char *p = line + 7;

  char *end;
  unsigned long idx = strtoul(p, &end, 10);
  if (end == p || *end != ',') {
    return;  // malformed header: drop the line
  }
  p = end + 1;
  unsigned long total = strtoul(p, &end, 10);
  if (end == p || *end != ',') {
    return;
  }
  const char *q = end + 1;

  Row row;
  for (uint8_t i = 0; i < kMaxFields; i++) {
    row.present[i] = false;
    row.value[i] = 0;
  }

  bool ok = true;
  for (uint8_t i = 0; i < ctx->nfields; i++) {
    bool isLast = (uint8_t)(i + 1) == ctx->nfields;
    char expectedTerm = isLast ? '\0' : ',';

    if (*q == expectedTerm) {
      // empty field: absent, terminator found immediately
      row.present[i] = false;
      row.value[i] = 0;
      if (!isLast) {
        q++;  // consume the comma, move to the next field's start
      }
      continue;
    }
    if (*q == '\0') {
      ok = false;  // ran out of fields early: too few for this kind's arity
      break;
    }
    char *fend;
    int64_t v = hearthParseWireValue(q, &fend);
    if (fend == q || *fend != expectedTerm) {
      ok = false;  // zero digits consumed, or trailing junk before the
                    // expected terminator: malformed either way
      break;
    }
    row.present[i] = true;
    row.value[i] = v;
    q = isLast ? fend : fend + 1;
  }
  if (!ok || *q != '\0') {
    return;  // malformed, or trailing data past the kind's own arity
  }

  ctx->gotAnyLine = true;
  ctx->total = (uint16_t)total;
  if (ctx->single) {
    if ((uint16_t)idx == ctx->wantIdx) {
      *ctx->singleRow = row;
      ctx->singleGot = true;
    }
  } else if (ctx->returned < ctx->maxRows) {
    ctx->rows[ctx->returned] = row;
    ctx->returned++;
  }
}

bool HearthRowTransfer::hearthSendGet(const char *cmd, ReadCtx &ctx) {
  return Hearth.hearthCommand(cmd, hearthOnRowLine, &ctx) == 0;
}

/*
 * "AT+MTROW=<ep>,<kind>,<idx>,<field>[,...]" (design spec 2.1/2.2).
 * Buffer: "AT+MTROW=" (9) + ep (up to 5) + "," (1) + kind (up to 3, uint8)
 * + "," (1) + idx (up to 5, uint16) = 24, plus up to kMaxFields (8) fields
 * each "," (1) + a full-width negative int64 ("-9223372036854775808", 20
 * chars) = 8 * 21 = 168. 24 + 168 = 192, plus NUL = 193; 224 leaves
 * headroom for the generic kMaxFields shape any future kind might use
 * (kind 1 itself, nfields 4, is far under this).
 */
bool HearthRowTransfer::stage(uint16_t idx, const Row &row) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[224];
  int n = snprintf(cmd, sizeof(cmd), "AT+MTROW=%u,%u,%u", (unsigned)owner->getEndPointId(), (unsigned)kind, (unsigned)idx);
  for (uint8_t i = 0; i < nfields; i++) {
    /*
     * Fix round 1, finding 3: guard against a size_t underflow in
     * `sizeof(cmd) - (size_t)n` below. snprintf() returns the length it
     * WOULD have written even when truncated, so if a prior append already
     * filled or overflowed the buffer, `n` can be >= sizeof(cmd); computing
     * `sizeof(cmd) - (size_t)n` as unsigned arithmetic would then wrap to a
     * huge value and hand the next snprintf() call a corrupted size (and
     * `cmd + n` a pointer past the buffer), a wild write rather than a
     * clean truncation. Unreachable today: the constructor clamps
     * `nfields` to kMaxFields, and this method's own header comment proves
     * 224 bytes covers the worst case at kMaxFields fields, so `n` cannot
     * reach sizeof(cmd) before this loop even starts its last iteration.
     * The check costs one comparison per field and turns any future
     * violation of that invariant into a clean refusal instead of
     * undefined behaviour.
     */
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
      return false;
    }
    if (row.present[i]) {
      n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",%lld", (long long)row.value[i]);
    } else {
      n += snprintf(cmd + n, sizeof(cmd) - (size_t)n, ",");
    }
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/* "AT+MTROWCLEAR=<ep>,<kind>" (design spec 2.6). */
bool HearthRowTransfer::clearStaged() {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "AT+MTROWCLEAR=%u,%u", (unsigned)owner->getEndPointId(), (unsigned)kind);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * "AT+MTROWAPPLY=<ep>,<kind>,<count>" (design spec 2.6). `count` is always
 * sent literally, including 0 (the documented whole-payload clear): this
 * class adds no special-casing on either side of zero, matching
 * mt_rows.c's own emphasis that count==0 must reach the firmware
 * unconditionally rather than being gated on "is anything staged".
 */
bool HearthRowTransfer::apply(uint16_t count) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTROWAPPLY=%u,%u,%u", (unsigned)owner->getEndPointId(), (unsigned)kind, (unsigned)count);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * "AT+MTROWGET=<ep>,<kind>,<idx>" (design spec 2.3), the live store, one
 * row. See HearthRowTransfer.h for why `total` is only trustworthy here
 * when `gotRow` is true, or when `gotRow` is false and idx == 0 (the
 * documented empty-payload signal).
 */
bool HearthRowTransfer::getRow(uint16_t idx, Row &row, uint16_t &total, bool &gotRow) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+MTROWGET=%u,%u,%u", (unsigned)owner->getEndPointId(), (unsigned)kind, (unsigned)idx);

  ReadCtx ctx;
  ctx.nfields = nfields;
  ctx.single = true;
  ctx.wantIdx = idx;
  ctx.singleRow = &row;
  ctx.singleGot = false;
  ctx.rows = nullptr;
  ctx.maxRows = 0;
  ctx.returned = 0;
  ctx.total = 0;
  ctx.gotAnyLine = false;
  if (!hearthSendGet(cmd, ctx)) {
    return false;
  }
  total = ctx.total;
  gotRow = ctx.singleGot;
  return true;
}

/*
 * "AT+MTROWGET=<ep>,<kind>" (design spec 2.3), the live store, every row.
 * `total` is authoritative here (this header's own comment): either at
 * least one line arrived and reported it, or none did and the store is
 * genuinely empty.
 */
bool HearthRowTransfer::getAll(Row *rows, uint16_t maxRows, uint16_t &total, uint16_t &returned) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+MTROWGET=%u,%u", (unsigned)owner->getEndPointId(), (unsigned)kind);

  ReadCtx ctx;
  ctx.nfields = nfields;
  ctx.single = false;
  ctx.wantIdx = 0;
  ctx.singleRow = nullptr;
  ctx.singleGot = false;
  ctx.rows = rows;
  ctx.maxRows = maxRows;
  ctx.returned = 0;
  ctx.total = 0;
  ctx.gotAnyLine = false;
  if (!hearthSendGet(cmd, ctx)) {
    return false;
  }
  total = ctx.total;
  returned = ctx.returned;
  return true;
}

/*
 * "AT+MTROWGET=<ep>,<kind>,<idx>,<seq>": the PROPOSED set of the
 * outstanding forward named by `seq`, one row (HearthRowTransfer.h's own
 * comment on why this class never invents or caches a seq of its own).
 * `seq == 0` is refused host-side, zero wire traffic: cmd_mtrowget()
 * (main/mt_at.c) documents 0 as never issued and refuses it with
 * +MTERR:1, so the answer is already known without spending the round
 * trip, the HearthDemControl n>4 precedent.
 */
bool HearthRowTransfer::getProposedRow(uint32_t seq, uint16_t idx, Row &row, uint16_t &total, bool &gotRow) {
  if (seq == 0) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[48];
  snprintf(
    cmd, sizeof(cmd), "AT+MTROWGET=%u,%u,%u,%lu", (unsigned)owner->getEndPointId(), (unsigned)kind, (unsigned)idx, (unsigned long)seq
  );

  ReadCtx ctx;
  ctx.nfields = nfields;
  ctx.single = true;
  ctx.wantIdx = idx;
  ctx.singleRow = &row;
  ctx.singleGot = false;
  ctx.rows = nullptr;
  ctx.maxRows = 0;
  ctx.returned = 0;
  ctx.total = 0;
  ctx.gotAnyLine = false;
  if (!hearthSendGet(cmd, ctx)) {
    return false;
  }
  total = ctx.total;
  gotRow = ctx.singleGot;
  return true;
}

/*
 * "AT+MTROWGET=<ep>,<kind>,,<seq>", the empty-idx-token bulk spelling
 * (design spec's own "one spelling per shape"): the PROPOSED set, every
 * row. `seq == 0` refused host-side, the same reason as getProposedRow().
 */
bool HearthRowTransfer::getAllProposed(uint32_t seq, Row *rows, uint16_t maxRows, uint16_t &total, uint16_t &returned) {
  if (seq == 0) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+MTROWGET=%u,%u,,%lu", (unsigned)owner->getEndPointId(), (unsigned)kind, (unsigned long)seq);

  ReadCtx ctx;
  ctx.nfields = nfields;
  ctx.single = false;
  ctx.wantIdx = 0;
  ctx.singleRow = nullptr;
  ctx.singleGot = false;
  ctx.rows = rows;
  ctx.maxRows = maxRows;
  ctx.returned = 0;
  ctx.total = 0;
  ctx.gotAnyLine = false;
  if (!hearthSendGet(cmd, ctx)) {
    return false;
  }
  total = ctx.total;
  returned = ctx.returned;
  return true;
}

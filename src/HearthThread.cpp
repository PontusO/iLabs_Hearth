/*
 * HearthThread.cpp - Task 4 (Thread role API, 0.11.0): the implementation
 * behind HearthThread.h's declarations. See that header for the design
 * rationale; this file is the wire parse, the role-token table, and the
 * three HearthClass member bodies (declared in Hearth.h, defined here so
 * Hearth.cpp itself stays a thin dispatch hook, the same file split the
 * plan's task-4 brief asks for).
 */
#include "HearthGlobal.h"
#include "HearthThread.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

namespace {

struct HearthThreadRoleEntry {
  const char *name;
  HearthThreadRole role;
};

/* AT_MT_SPEC.md S3.27's own table, Matter's RoutingRoleEnum tokens. Order
 * does not matter here (linear scan, seven entries); it mirrors the enum's
 * declaration order in HearthThread.h for readability only. */
const HearthThreadRoleEntry kThreadRoleTable[] = {
  {"UNSPECIFIED", HEARTH_THREAD_UNSPECIFIED},
  {"UNASSIGNED", HEARTH_THREAD_UNASSIGNED},
  {"SLEEPY_END_DEVICE", HEARTH_THREAD_SLEEPY_END_DEVICE},
  {"END_DEVICE", HEARTH_THREAD_END_DEVICE},
  {"REED", HEARTH_THREAD_REED},
  {"ROUTER", HEARTH_THREAD_ROUTER},
  {"LEADER", HEARTH_THREAD_LEADER},
};

/*
 * Wire role token -> HearthThreadRole. A real token ("ROUTER") maps to its
 * named enum value; S3.27's decimal fallback for a value outside Matter's
 * current seven-entry enum ("42", a future SDK addition) matches nothing
 * in the table and degrades to HEARTH_THREAD_UNKNOWN (review round,
 * HearthThread.h's own comment on the enum), never to
 * HEARTH_THREAD_UNSPECIFIED: that value is the wire's own honest "the
 * Thread interface is down", a real and different fact from "this library
 * does not recognise the token it was sent", and conflating the two would
 * turn the firmware's own degrade-not-lie design back into a lie. `len`
 * rather than a NUL-terminated token because the caller may be pointing
 * into the middle of a longer wire line (the query reply's first field,
 * ahead of five more).
 */
HearthThreadRole hearthThreadRoleFromToken(const char *s, size_t len) {
  for (size_t i = 0; i < sizeof(kThreadRoleTable) / sizeof(kThreadRoleTable[0]); i++) {
    size_t nameLen = strlen(kThreadRoleTable[i].name);
    if (len == nameLen && strncmp(s, kThreadRoleTable[i].name, len) == 0) {
      return kThreadRoleTable[i].role;
    }
  }
  return HEARTH_THREAD_UNKNOWN;
}

/*
 * Parse "+MTTHREAD:"'s payload (the text after the prefix), AT_MT_SPEC.md
 * S3.27: "<role>,<attached>,<channel>,<panid>,<extpanid>,<partitionid>,<name>".
 *
 * <channel> is decimal; <panid>/<extpanid>/<partitionid> are hex with a
 * "0x" prefix (strtoul/strtoull's base-16 mode accepts that prefix
 * directly, same as hearthOnEpLine() in Hearth.cpp already relies on for
 * AT+MTEP?'s device-type field). Each of those four is independently
 * nullable: an empty field (the wire's own "," immediately followed by
 * another delimiter) sets its has* flag false and leaves the numeric field
 * whatever memset() below already put there (0) -- the has* flag is the
 * only signal a caller may trust, never the value, per this file's own
 * table-comment above and HearthThread.h's struct comment.
 *
 * <name> is the last field, always double-quoted, with '"' escaped as
 * "\\\"" and '\\' escaped as "\\\\" (S3.27's normative quoting rule for
 * this and any future free-form field). Un-escaped into a fixed 17-byte
 * buffer (16 + NUL, Thread's own network-name limit); a name longer than
 * that is truncated defensively even though the firmware promises never to
 * send one, the same belt-and-braces the fixed +MTCODES buffers in
 * Hearth.cpp already apply to wire-supplied lengths.
 *
 * Returns false on any malformed field (missing delimiter, unparsable
 * digits, an unterminated quoted name): the same silent-drop-the-whole-
 * line policy Hearth.cpp's hearthDispatchAttr()/hearthDispatchIdent()/
 * hearthDispatchCmd() already use for a line that does not parse cleanly,
 * rather than accepting a partially-decoded, partially-fabricated result.
 */
bool hearthParseThreadLine(const char *rest, HearthThreadInfo *out) {
  memset(out, 0, sizeof(*out));
  out->role = HEARTH_THREAD_UNSPECIFIED;

  const char *p = rest;
  const char *comma = strchr(p, ',');
  if (!comma) {
    return false;
  }
  out->role = hearthThreadRoleFromToken(p, (size_t)(comma - p));
  p = comma + 1;

  /* <attached>: never null (S3.27), a plain 0/1. */
  char *end;
  long attached = strtol(p, &end, 10);
  if (end == p || *end != ',') {
    return false;
  }
  out->attached = (attached != 0);
  p = end + 1;

  /* <channel>: decimal, nullable. */
  if (*p == ',') {
    out->hasChannel = false;
  } else {
    unsigned long v = strtoul(p, &end, 10);
    if (end == p) {
      return false;
    }
    out->hasChannel = true;
    out->channel = (uint16_t)v;
    p = end;
  }
  if (*p != ',') {
    return false;
  }
  p++;

  /* <panid>: hex, nullable. */
  if (*p == ',') {
    out->hasPanId = false;
  } else {
    unsigned long v = strtoul(p, &end, 16);
    if (end == p) {
      return false;
    }
    out->hasPanId = true;
    out->panId = (uint16_t)v;
    p = end;
  }
  if (*p != ',') {
    return false;
  }
  p++;

  /* <extpanid>: hex, up to 64 bits, nullable. strtoull, not strtoul: an
   * ExtendedPanId is 16 hex digits and does not fit in a 32-bit unsigned
   * long on every target this library builds for. */
  if (*p == ',') {
    out->hasExtPanId = false;
  } else {
    unsigned long long v = strtoull(p, &end, 16);
    if (end == p) {
      return false;
    }
    out->hasExtPanId = true;
    out->extPanId = (uint64_t)v;
    p = end;
  }
  if (*p != ',') {
    return false;
  }
  p++;

  /* <partitionid>: hex, nullable. */
  if (*p == ',') {
    out->hasPartitionId = false;
  } else {
    unsigned long v = strtoul(p, &end, 16);
    if (end == p) {
      return false;
    }
    out->hasPartitionId = true;
    out->partitionId = (uint32_t)v;
    p = end;
  }
  if (*p != ',') {
    return false;
  }
  p++;

  /* <name>: quoted and escaped on the wire; unescape into a plain C
   * string. A malformed line with no opening quote, or one whose closing
   * quote never arrives, is dropped like any other malformed field above. */
  if (*p != '"') {
    return false;
  }
  p++;
  size_t ni = 0;
  bool closed = false;
  while (*p != '\0') {
    if (*p == '"') {
      closed = true;
      p++;
      break;
    }
    char c = *p;
    if (c == '\\' && (p[1] == '"' || p[1] == '\\')) {
      p++;
      c = *p;
    }
    if (ni < sizeof(out->name) - 1) {
      out->name[ni++] = c;
    }
    p++;
  }
  out->name[ni] = '\0';
  return closed;
}

struct HearthThreadCtx {
  HearthThreadInfo info;
  bool got;
};

void hearthOnThreadLine(const char *line, void *arg) {
  HearthThreadCtx *ctx = (HearthThreadCtx *)arg;
  if (strncmp(line, "+MTTHREAD:", 10) != 0) {
    return;
  }
  if (hearthParseThreadLine(line + 10, &ctx->info)) {
    ctx->got = true;
  }
}

/* AT_MT_SPEC.md S3.11's event table, bit 28 (MT_EVT_THREAD_ROLE_CHANGED).
 * The one bit this whole file's event-mask subscription logic ever touches;
 * every other bit in the mask is the sketch's (or a prior round's) own
 * business and must survive a read-modify-write here untouched. */
const uint32_t kThreadRoleEvtBit = 1UL << 28;

/* "+MTEVTMASK:<hex32>" (the text after "+MTEVT?"'s reply prefix,
 * AT_MT_SPEC.md S3.11). No "0x" stripping needed: strtoul's base-16 mode
 * accepts the prefix directly, the same convention every other hex field
 * in this library already relies on (hearthOnEpLine()'s device-type field,
 * Hearth.cpp). */
struct HearthEvtMaskCtx {
  uint32_t mask;
  bool got;
};

void hearthOnEvtMaskLine(const char *line, void *arg) {
  HearthEvtMaskCtx *ctx = (HearthEvtMaskCtx *)arg;
  if (strncmp(line, "+MTEVTMASK:", 11) != 0) {
    return;
  }
  ctx->mask = (uint32_t)strtoul(line + 11, nullptr, 16);
  ctx->got = true;
}

}  // namespace

const char *hearthThreadRoleName(HearthThreadRole role) {
  switch (role) {
    case HEARTH_THREAD_UNSPECIFIED: return "UNSPECIFIED";
    case HEARTH_THREAD_UNASSIGNED: return "UNASSIGNED";
    case HEARTH_THREAD_SLEEPY_END_DEVICE: return "SLEEPY_END_DEVICE";
    case HEARTH_THREAD_END_DEVICE: return "END_DEVICE";
    case HEARTH_THREAD_REED: return "REED";
    case HEARTH_THREAD_ROUTER: return "ROUTER";
    case HEARTH_THREAD_LEADER: return "LEADER";
    case HEARTH_THREAD_UNKNOWN: return "UNKNOWN";
    default: return "UNKNOWN";  // an out-of-range cast; same fallback as an unrecognised wire token
  }
}

/*
 * AT+MTTHREAD? (S3.27), a full round trip. On success, also syncs the cache
 * threadRole() reads: a sketch that "queries once at startup and then
 * trusts the event thereafter" (S3.27's own words for +MTEVT:28) must see
 * that startup role immediately, not only after the first actual
 * transition. On failure (a bare ERROR, a +MTERR, a timeout, or a reply
 * that parses as OK but whose +MTTHREAD: line itself does not decode
 * cleanly) the cache is left untouched and this returns false;
 * Hearth.lastError() carries the wire code as usual (0.11.0, AT_MT_SPEC.md
 * S3.27: a WiFi image, or the combined image booted in WiFi mode, answers
 * +MTERR:8 here).
 */
bool HearthClass::threadInfo(HearthThreadInfo &out) {
  HearthThreadCtx ctx;
  ctx.got = false;
  if (hearthCommand("AT+MTTHREAD?", hearthOnThreadLine, &ctx) != 0 || !ctx.got) {
    return false;
  }
  out = ctx.info;
  _threadRole = ctx.info.role;
  return true;
}

/*
 * +MTEVT:28's payload (Hearth.cpp's hearthDispatchEvt(), which intercepts
 * bit 28 before the generic numeric <detail> parse every other bit uses:
 * this bit's payload is the decoded role TOKEN, not a number). Updates the
 * cache threadRole() reads and, if a sketch has registered one, calls its
 * callback synchronously, inside URC dispatch. No special re-entrancy
 * guard is needed here: this function never itself reaches the wire, so
 * whatever the sketch's callback does is subject to the ordinary
 * HearthLink busy-gate refusal (HEARTH_CMD_REENTRANT) like every other
 * callback this library dispatches from inside a command()/poll() read
 * loop.
 */
void HearthClass::hearthDispatchThreadRoleEvt(const char *rest, HearthClass *self) {
  HearthThreadRole role = hearthThreadRoleFromToken(rest, strlen(rest));
  self->_threadRole = role;
  if (self->_onThreadRoleChangeCB) {
    self->_onThreadRoleChangeCB(role);
  }
}

/*
 * The event-mask read-modify-write behind onThreadRoleChange()'s
 * subscription (bench review round: the callback never fired on real
 * hardware, because nothing ever asked the device to emit +MTEVT:28 in
 * the first place -- bit 28 is opt-in, AT_MT_SPEC.md S3.11's default mask
 * is 0x0800003F, and registering a callback used to just store a function
 * pointer with no wire effect at all).
 *
 * Called from Hearth.cpp's poll() and hearthCommand(), the same two call
 * sites and ordering as hearthDrainCmdRespQueue()/hearthDrainDeferredWork():
 * after the outer _link call has already returned, so _link.busy() reads
 * false and a nested call from inside a dispatched callback bails out
 * cleanly instead of corrupting the link's single reader. Reaches the wire
 * through _link.command() directly, never through Hearth's own
 * hearthCommand(), for the identical reason those two siblings do: this
 * runs *from inside* hearthCommand()'s own tail, and routing back through
 * it would recurse into this same drain and, worse, let this background
 * exchange's own outcome (or lack of one) clobber the caller's lastError().
 *
 * ALWAYS READ BEFORE WRITE, NEVER A BLIND WRITE: AT+MTEVT? first, so
 * whatever bits a sketch (or a prior round of this library) already
 * subscribed to are known before this touches anything, then exactly one
 * bit is OR'd in (a callback is registered) or AND'd out (it is not) and
 * the result is written back whole. A blind write of "default mask plus
 * bit 28" would silently clobber any other subscription the sketch or a
 * future round ever makes -- precisely the class of bug this whole review
 * round has been finding elsewhere in this file.
 *
 * The flag is cleared BEFORE the exchange runs (the same re-entrancy guard
 * hearthDrainDeferredWork() uses for its own flag): if either the read or
 * the write fails -- the link is down, a timeout, a coded error -- it is
 * set again so the next poll()/hearthCommand() retries, rather than the
 * subscription silently never being established or corrected. _lastError
 * is saved and restored around the whole exchange, so this background sync
 * can never clobber what the CALLER's own command just recorded, the same
 * discipline hearthDrainDeferredWork() applies for its own background
 * pushes.
 */
void HearthClass::hearthDrainEvtResubscribe() {
  if (!_evtResubscribeNeeded || _link.busy()) {
    return;
  }
  _evtResubscribeNeeded = false;
  int savedError = _lastError;

  HearthEvtMaskCtx ctx;
  ctx.mask = 0;
  ctx.got = false;
  int rc = _link.command("AT+MTEVT?", hearthOnEvtMaskLine, &ctx);
  if (rc != 0 || !ctx.got) {
    _evtResubscribeNeeded = true;  // retry: the read itself did not succeed
    _lastError = savedError;
    return;
  }

  uint32_t newMask = _onThreadRoleChangeCB ? (ctx.mask | kThreadRoleEvtBit) : (ctx.mask & ~kThreadRoleEvtBit);
  char cmd[24];  // "AT+MTEVT=0x" + 8 hex digits + NUL, with headroom
  snprintf(cmd, sizeof(cmd), "AT+MTEVT=0x%08lX", (unsigned long)newMask);
  rc = _link.command(cmd);
  if (rc != 0) {
    _evtResubscribeNeeded = true;  // retry: the write did not succeed
  }
  _lastError = savedError;
}

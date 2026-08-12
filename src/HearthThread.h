/*
 * HearthThread.h - Task 4 (Thread role API, 0.11.0, design spec
 * 2026-08-12): the types behind Hearth.threadInfo(), Hearth.threadRole()
 * and Hearth.onThreadRoleChange(). AT_MT_SPEC.md S3.27 (AT+MTTHREAD?) and
 * the +MTEVT bit 28 table entry are the wire contract this header and
 * HearthThread.cpp implement; read that section, not this comment, for the
 * grammar itself.
 *
 * This is a Hearth global surface, not a device type: no Matter-named class
 * carries a Thread role API on any SDK this library tracks, so per the
 * README's split rule (interop symbols are arduino-esp32's closed set;
 * anything this library invents lives on Hearth/hearth-prefixed names) the
 * whole surface lands on HearthClass. The enum and struct below are taken
 * from the plan's task-4 brief verbatim (its own "spec section 3" quote),
 * so their field order and names are not this file's to renegotiate.
 *
 * Two read paths, deliberately different costs:
 *
 *   - threadInfo() (HearthThread.cpp): a full AT+MTTHREAD? round trip,
 *     filling every field including the has* flags for the wire's nullable
 *     ones (S3.27: null renders as an empty field, never as 0).
 *   - threadRole() (inline below): a cached read with NO wire traffic at
 *     all, seeded to HEARTH_THREAD_UNSPECIFIED and refreshed by threadInfo()
 *     and by every +MTEVT:28 (Hearth.cpp's hearthDispatchEvt(), which
 *     intercepts bit 28 before the generic numeric <detail> parse every
 *     other bit uses -- this bit's payload is the decoded role TOKEN, not a
 *     number). A sketch polling its role every loop() iteration must not
 *     pay a UART round trip for it; this is the method that lets it not.
 *
 * onThreadRoleChange()'s callback is a PLAIN function pointer, the same
 * convention HearthDemControl's onPowerAdjust()/onCancelPowerAdjust() use
 * (see that header's own comment), not the std::function every adjudicated
 * +MTCMD callback in this library uses -- there is no verdict to return
 * here, just a notification. It runs INSIDE URC dispatch, with the link's
 * busy gate held by whatever poll()/hearthCommand() call is delivering the
 * +MTEVT:28 line: a wire write from inside it (including calling
 * threadInfo() itself) is refused HEARTH_CMD_REENTRANT, exactly the
 * reentrancy rule the README's "Your onChange fires from inside your own
 * setter" section and HearthDemControl's own comment both document. Set a
 * flag in the callback and act on it from loop(), never from inside the
 * callback.
 *
 * The name arrives already unquoted and unescaped as a plain C string:
 * the wire's quoting rule (S3.27: '"' -> "\\\"", '\\' -> "\\\\", the first
 * free-form string field in the AT+MT family and the rule normative for any
 * that follow) is HearthThread.cpp's problem, not the sketch's.
 */
#pragma once

#include <stdint.h>

/*
 * Matter's RoutingRoleEnum, decoded (S3.27's own table, cited to
 * thread-network-diagnostics-provider.cpp, not OpenThread's otDeviceRole,
 * which is a different, smaller vocabulary). Values and order are fixed by
 * the plan's task-4 brief (spec section 3, verbatim); do not renumber.
 *
 * An unrecognized wire token -- S3.27's own decimal fallback for a future
 * SDK addition -- degrades to HEARTH_THREAD_UNSPECIFIED rather than being
 * rejected or mis-mapped onto an unrelated real role: this enum has no
 * eighth "unknown" value of its own, and UNSPECIFIED is already the wire's
 * own "nothing meaningful to report" answer (S3.27: "the Thread interface
 * is down"), so reusing it here degrades honestly to the same shape of
 * answer rather than fabricating a value the wire never actually sent. See
 * HearthThread.cpp's hearthThreadRoleFromToken().
 */
enum HearthThreadRole {
  HEARTH_THREAD_UNSPECIFIED = 0,
  HEARTH_THREAD_UNASSIGNED,
  HEARTH_THREAD_SLEEPY_END_DEVICE,
  HEARTH_THREAD_END_DEVICE,
  HEARTH_THREAD_REED,
  HEARTH_THREAD_ROUTER,
  HEARTH_THREAD_LEADER,
};

/*
 * One AT+MTTHREAD? answer, decoded (S3.27). channel/panId/extPanId/
 * partitionId are all Nullable on the wire (empty field, never 0): the
 * has* flag is the ONLY honest signal of "unknown" -- a real PAN ID of
 * 0xFFFF, an all-ones extended PAN ID, or a partition ID of 0xFFFFFFFF
 * renders identically to null on the wire (S3.27's own caveat), so a
 * caller must gate on the flag, never infer "unknown" from the value
 * looking implausible. role and attached carry no such gate; the wire
 * never sends either of them null.
 *
 * name is fixed at 17 bytes (16 + NUL), Thread's own network-name limit
 * (S3.27); a detached device with no dataset reports it as the empty
 * string, not as an absent field.
 */
struct HearthThreadInfo {
  HearthThreadRole role;
  bool attached;
  bool hasChannel;
  uint16_t channel;
  bool hasPanId;
  uint16_t panId;
  bool hasExtPanId;
  uint64_t extPanId;
  bool hasPartitionId;
  uint32_t partitionId;
  char name[17];
};

/* Display name for a role, e.g. for a status line. Every value
 * HearthThreadRole defines has one; an out-of-range value (a raw cast a
 * caller should not be doing, but this must not crash on) falls back to
 * "UNSPECIFIED", the same degrade hearthThreadRoleFromToken() applies on
 * the wire-parsing side. See HearthThread.cpp. */
const char *hearthThreadRoleName(HearthThreadRole role);

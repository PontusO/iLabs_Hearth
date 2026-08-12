/*
 * HearthDemControl.cpp - implementation. See HearthDemControl.h for the
 * owner/helper division of labour, the wire surface, the accept-pushes-no-
 * state-change / endAdjustment-does semantic, and the reconcile split.
 */
#include "HearthDemControl.h"
#include "MatterEndPoint.h"
/* HearthGlobal.h, not Hearth.h: this is a library .cpp, and the Hearth
 * global must stay declared even in a build that set NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_HEARTH build-wide. See that header's own comment. */
#include "HearthGlobal.h"
#include <stdio.h>

HearthDemControl::HearthDemControl(MatterEndPoint *owner) : owner(owner) {}

void HearthDemControl::reset() {
  esaType = 0;
  esaCanGenerate = false;
  esaState = 0;
  absMinPower = 0;
  absMaxPower = 0;
  optOutState = 0;
  hasESAType = hasESACanGenerate = hasESAState = hasAbsMinPower = hasAbsMaxPower = hasOptOutState = false;
  hasCapability = false;
  capCause = 0;
  capCount = 0;
  /* enabled and the two callback pointers are deliberately left alone: the
   * owner assigns `enabled` right after, from the variant it validated
   * (this header's own comment), and no existing endpoint class clears a
   * callback registration on begin() either. */
}

/*
 * "AT+MTMEAS=<ep>,152,<field>,<value>" (AT_MT_SPEC.md S3.25's 0x0098
 * table). getEndPointId() == 0 is checked directly here, not through a
 * private MatterEndPoint guard: AT+MTMEAS does not ride the AT+MTATTR guard
 * path, the HearthMeasurementPush/WaterHeater precedent. %lld always: the
 * unsigned fields (ESAType, ESACanGenerate, ESAState, OptOutState) never
 * carry a negative value here, since every public setter takes an unsigned
 * parameter type.
 */
bool HearthDemControl::hearthSendDemPair(uint8_t field, int64_t value) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+MTMEAS=%u,%lu,%u,%lld", (unsigned)owner->getEndPointId(), (unsigned long)kDemClusterId, (unsigned)field, (long long)value);
  return Hearth.hearthCommand(cmd) == 0;
}

/*
 * "AT+MTDEMCAP=<ep>,<cause>,<n>[,<minPower>,<maxPower>,<minDuration>,
 * <maxDuration>]{n}" (AT_MT_SPEC.md S3.26). `entries` may be nullptr when
 * `n` is 0 (the loop below never dereferences it in that case). Buffer:
 * "AT+MTDEMCAP=" (12) + ep (5) + "," + cause (3) + "," + n (1) = ~24, plus
 * up to 4 entries at ",-9223372036854775808,-9223372036854775808,
 * 4294967295,4294967295" (~64 chars) each = ~256; 400 leaves headroom.
 */
bool HearthDemControl::hearthSendCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n) {
  if (owner->getEndPointId() == 0) {
    Hearth.hearthSetError(2);
    return false;
  }
  char cmd[400];
  int len = snprintf(cmd, sizeof(cmd), "AT+MTDEMCAP=%u,%u,%u", (unsigned)owner->getEndPointId(), (unsigned)cause, (unsigned)n);
  for (uint8_t i = 0; i < n; i++) {
    len += snprintf(
      cmd + len, sizeof(cmd) - (size_t)len, ",%lld,%lld,%lu,%lu", (long long)entries[i].minPowerMw, (long long)entries[i].maxPowerMw,
      (unsigned long)entries[i].minDurationS, (unsigned long)entries[i].maxDurationS
    );
  }
  return Hearth.hearthCommand(cmd) == 0;
}

/* The scalar fields' shared shape: no-op only when the field has been
 * pushed before AND the value is unchanged; cache and has-flag commit only
 * on a successful write. Field 6 does NOT use this helper (this header's
 * own comment: it keeps no cache and always writes). */
bool HearthDemControl::hearthSetDemField(uint8_t field, int64_t value, int64_t *cache, bool *hasFlag) {
  if (*hasFlag && *cache == value) {
    return true;
  }
  if (!hearthSendDemPair(field, value)) {
    return false;  // the cache is left untouched: the device's idea of the
                    // state and the host's idea of it must not diverge
  }
  *cache = value;
  *hasFlag = true;
  return true;
}

bool HearthDemControl::setESAType(uint8_t t) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  int64_t cache = esaType;
  bool ok = hearthSetDemField(kFieldESAType, t, &cache, &hasESAType);
  if (ok) {
    esaType = (uint8_t)cache;
  }
  return ok;
}

bool HearthDemControl::setESACanGenerate(bool g) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  int64_t cache = esaCanGenerate ? 1 : 0;
  bool ok = hearthSetDemField(kFieldESACanGenerate, g ? 1 : 0, &cache, &hasESACanGenerate);
  if (ok) {
    esaCanGenerate = (cache != 0);
  }
  return ok;
}

/*
 * The one setter whose cache is also read by the accept/cancel handlers
 * below to avoid a wrongly-suppressed endAdjustment() push (this header's
 * own comment: "an accept genuinely arms the internal cache").
 */
bool HearthDemControl::setESAState(uint8_t s) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  int64_t cache = esaState;
  bool ok = hearthSetDemField(kFieldESAState, s, &cache, &hasESAState);
  if (ok) {
    esaState = (uint8_t)cache;
  }
  return ok;
}

bool HearthDemControl::setAbsMinPower(int64_t mw) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  return hearthSetDemField(kFieldAbsMinPower, mw, &absMinPower, &hasAbsMinPower);
}

bool HearthDemControl::setAbsMaxPower(int64_t mw) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  return hearthSetDemField(kFieldAbsMaxPower, mw, &absMaxPower, &hasAbsMaxPower);
}

bool HearthDemControl::setOptOutState(uint8_t s) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  int64_t cache = optOutState;
  bool ok = hearthSetDemField(kFieldOptOutState, s, &cache, &hasOptOutState);
  if (ok) {
    optOutState = (uint8_t)cache;
  }
  return ok;
}

/* Field 6: event carrier, not an attribute. No cache, no no-op: every legal
 * call is a real wire line (this header's own comment). */
bool HearthDemControl::pushAdjustmentEnergyUse(int64_t mwh) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  return hearthSendDemPair(kFieldAdjEnergyUse, mwh);
}

/*
 * AT+MTDEMCAP: n > kCapMaxEntries is refused HOST-SIDE (the answer is
 * already known without spending the round trip, the POWER_ONLY
 * precedent); a legal n always writes (S3.26: "full replacement per
 * call", no null-until-pushed discipline here), and the cache this class
 * keeps afterwards is for the reconcile re-push only.
 */
bool HearthDemControl::setPowerAdjustmentCapability(uint8_t cause, const PowerAdjustEntry *entries, uint8_t n) {
  if (!enabled) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (n > kCapMaxEntries) {
    Hearth.hearthSetError(1);
    return false;
  }
  if (!hearthSendCapability(cause, entries, n)) {
    return false;
  }
  capCause = cause;
  capCount = n;
  for (uint8_t i = 0; i < n; i++) {
    capEntries[i] = entries[i];
  }
  hasCapability = true;
  return true;
}

void HearthDemControl::onPowerAdjust(bool (*cb)(int64_t powerMw, uint32_t durationS, uint8_t cause)) {
  _onPowerAdjustCB = cb;
}

void HearthDemControl::onCancelPowerAdjust(bool (*cb)()) {
  _onCancelPowerAdjustCB = cb;
}

/*
 * The owner's own hearthOnForwardedCommandFields() override calls this
 * after parsing cluster 152 command 0's raw fields itself (this class
 * never touches HearthCmdFields, this header's own comment). No push on
 * accept (design spec 4.1, the round's headline semantic): the firmware
 * already set ESAState PowerAdjustActive and emitted PowerAdjustStart
 * itself. The cache IS updated on accept, silently (no wire traffic),
 * purely so a later endAdjustment()/setESAState() call detects a real
 * change instead of no-opping against a stale belief.
 */
bool HearthDemControl::hearthOnPowerAdjustRequest(int64_t powerMw, uint32_t durationS, uint8_t cause) {
  if (!enabled) {
    return false;
  }
  bool allow = _onPowerAdjustCB ? _onPowerAdjustCB(powerMw, durationS, cause) : false;
  if (allow) {
    esaState = kESAStatePowerAdjustActive;
    hasESAState = true;
  }
  return allow;
}

/*
 * Same shape for CancelPowerAdjustRequest (payload-less, cluster 152
 * command 1): no push on accept, the firmware resets ESAState to Online
 * itself (AT_MT_SPEC.md S3.17). The cache mirrors that silently too.
 */
bool HearthDemControl::hearthOnCancelPowerAdjustRequest() {
  if (!enabled) {
    return false;
  }
  bool allow = _onCancelPowerAdjustCB ? _onCancelPowerAdjustCB() : false;
  if (allow) {
    esaState = kESAStateOnline;
    hasESAState = true;
  }
  return allow;
}

bool HearthDemControl::endAdjustment() {
  return setESAState(kESAStateOnline);
}

/*
 * The reconcile split (design spec 4.1, this header's own comment): the
 * configuration fields (ESAType, ESACanGenerate, AbsMinPower, AbsMaxPower,
 * the capability list) are RE-pushed if they were ever successfully set;
 * the volatile fields (ESAState, OptOutState) have their wire-pushed
 * has-flags cleared WITHOUT a resend (B229). Field 6 keeps no cache at all
 * and never appears here either way. Re-push results are deliberately
 * unchecked, the MatterWaterHeater::hearthOnReconciled() shape: a failed
 * resend surfaces on the next ordinary setter call, not here.
 */
void HearthDemControl::onReconciled() {
  if (hasESAType) {
    hearthSendDemPair(kFieldESAType, esaType);
  }
  if (hasESACanGenerate) {
    hearthSendDemPair(kFieldESACanGenerate, esaCanGenerate ? 1 : 0);
  }
  if (hasAbsMinPower) {
    hearthSendDemPair(kFieldAbsMinPower, absMinPower);
  }
  if (hasAbsMaxPower) {
    hearthSendDemPair(kFieldAbsMaxPower, absMaxPower);
  }
  if (hasCapability) {
    hearthSendCapability(capCause, capEntries, capCount);
  }
  hasESAState = false;
  hasOptOutState = false;
}

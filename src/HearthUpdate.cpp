/*
 * HearthUpdate.cpp: the link route, begin(), the URC parsing and the
 * pull-and-acknowledge download loop (plan Task 4, through the first half
 * of 4b). The verification, the verdict, the consent and the apply are the
 * second half (Task 4b2): on +MTOTA:DOWNLOADED hearthDrain() ends the
 * staged write and stops.
 */
/*
 * HearthGlobal.h, not Hearth.h (see HearthGlobal.h's own comment):
 * Hearth.h declares `extern HearthClass Hearth;` behind
 * NO_GLOBAL_INSTANCES / NO_GLOBAL_HEARTH, and those macros travel as
 * build-wide -D flags, so the library's own translation units would be
 * compiled with the declaration suppressed in a build that opts out.
 * HearthGlobal.h carries the same declaration unguarded, so every library
 * file that can reach the global Hearth sees it no matter how the build
 * was configured.
 */
#include "HearthGlobal.h"
#include "HearthUpdate.h"
#include "HearthFlasher.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

HearthUpdate::HearthUpdate()
  : _owner(0),
    _fs(0),
    _flasher(0),
    _hostHooks(),
    _haveManifest(false),
    _pendingSeq(0),
    _pendingLen(0),
    _havePendingBlock(false),
    _lastState(HEARTH_UPDATE_DISABLED),
    _downloadComplete(false),
    _stagedWriteOpen(false),
    _transferEnded(false),
    _draining(false),
    _applyRequestCB(0),
    _statusCB(0),
    _baudChangerCB(0),
    _fwPart(0xFF),
    _hostPart(0xFF),
    _consentPending(false),
    _consentRefused(false),
    _consentRefusalMs(0),
    _commissioned(false),
    _commissionedMs(0),
    _baud(HEARTH_LINK_BAUD),
    _baudWantedDownload(false),
    _applyPending(false),
    _requestorRetry(false),
    _requestorRetryMs(0),
    _requestorRetryNow(false),
    _beginComplete(false),
    _coprocReadySeen(false) {
  _declaredVersion = 0;
  _declaredVersionString[0] = 0;
  /* Final review I8: the ARDUINO default host hooks (PicoOTA, the linker
   * symbols, rp2040.reboot, hearthCoprocReset) are installed by the
   * inline begin() wrapper instead of here, and the drain work is named
   * by the same path (_drainFn null until then), so a sketch that never
   * calls begin() links neither. On the host build both stay null: the
   * hooks are "not available here" (the host apply fails, the
   * co-processor reset is skipped with a log line) and tests install
   * their own. */
  _drainFn = 0;
  _hostHooksInstalled = false;
  _status.state = HEARTH_UPDATE_DISABLED;
  _status.percent = 0;
  _status.offeredVersion = 0;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  _status.effectiveVersion = 0;
  _status.hearthVersion[0] = 0;
  _status.detail[0] = 0;
  _status.deferredSeconds = 0;
  _verdictSent = false;
  _model[0] = 0;
  _variant[0] = 0;
  _hearthVersion[0] = 0;
  _preApplyHearthVersion[0] = 0;
  _lastAttemptFlasherError = false;
  _effectiveVersion = 0;
}

HearthUpdateStage &HearthUpdate::stage() { return _stage; }

/* Call the owner's hearthCommand, or fail if there is no owner yet (the
 * constructor sets it before any command can be sent). */
int HearthUpdate::hearthCmd(const char *cmd, HearthLink::LineCb onLine, void *arg) {
  if (!_owner) return -2;
  return ((HearthClass *)_owner)->hearthCommand(cmd, onLine, arg);
}

/* The per-command timeout form: the block pull's own 1500 ms deadline. 0
 * keeps the link's default, so this is a plain forward. */
int HearthUpdate::hearthCmd(const char *cmd, HearthLink::LineCb onLine, void *arg, uint32_t timeout_ms) {
  if (!_owner) return -2;
  return ((HearthClass *)_owner)->hearthCommand(cmd, onLine, arg, timeout_ms);
}

void HearthUpdate::hearthAttach(HearthFs &fs) { _fs = &fs; }

void HearthUpdate::hearthSetOwner(void *owner) { _owner = owner; }

void HearthUpdate::hearthSetFlasher(HearthFlasher *f) { _flasher = f; }

void HearthUpdate::onApplyRequest(bool (*cb)()) { _applyRequestCB = cb; }

void HearthUpdate::onStatus(void (*cb)(const HearthUpdateStatus &)) { _statusCB = cb; }

void HearthUpdate::hearthSetBaudChanger(void (*cb)(uint32_t)) { _baudChangerCB = cb; }

void HearthUpdate::hearthSetHostHooks(const HearthHostHooks &h) {
  _hostHooks = h;
  _hostHooksInstalled = true;
}

HearthUpdateStatus HearthUpdate::status() const { return _status; }

bool HearthUpdate::available() const {
  return _status.state != HEARTH_UPDATE_DISABLED && _status.state != HEARTH_UPDATE_UNAVAILABLE;
}

/*
 * The URC route. `rest` is the line after "+MTOTA:", so a state line is
 * <STATE>[,<detail>] and the pending-block announcement is BLOCK,<seq>,<len>
 * (spec 3.31, URC table). This only parses and sets fields; it never calls
 * the link, so it is safe from inside a URC dispatch where the link's busy
 * gate is held.
 *
 * While disabled (begin() never ran) every URC is ignored: the co-processor
 * starts in mode 0 and only a begin() turns it on, so a +MTOTA: line before
 * that is a stray the host has not subscribed to (plan Task 4 case 9).
 */
void HearthUpdate::hearthOnOtaLine(const char *rest) {
  if (!rest || _status.state == HEARTH_UPDATE_DISABLED) {
    return;
  }
  /* A new transfer starting clears the error the last one left readable:
   * the case 6b contract is that the FAILED error and detail stay in
   * status() until the next transfer starts (a BLOCK line), not forever. */
  if (strncmp(rest, "BLOCK,", 6) == 0) {
    _status.error = HEARTH_UPDATE_OK;
    _status.reason = 0;
    _status.detail[0] = 0;
  }
  const char *comma = strchr(rest, ',');
  char state[24];
  size_t n = comma ? (size_t)(comma - rest) : strlen(rest);
  if (n >= sizeof(state)) {
    n = sizeof(state) - 1;
  }
  memcpy(state, rest, n);
  state[n] = 0;

  if (strcmp(state, "BLOCK") == 0) {
    /* The pending-block announcement: +MTOTA:BLOCK,<seq>,<len>. A BLOCK
     * line always means a transfer is in progress, so the state is set to
     * DOWNLOADING (the co-processor may send the BLOCK before the
     * DOWNLOADING state line, or the state may still be IDLE from a
     * previous transfer). A new transfer also resets the download-complete
     * flag the last one set, so a later DOWNLOADED ends the write again.
     * Final review M8: the co-processor sends both fields, but a line
     * without the comma (no seq at all) or without the length after the
     * seq must be ignored, not parsed: with no comma, comma + 1 is
     * NULL + 1, and with no length, strtoul reads past the terminator. */
    if (!comma) {
      return;
    }
    char *end;
    uint32_t seq = (uint32_t)strtoul(comma + 1, &end, 10);
    if (end == comma + 1 || *end != ',') {
      return;  /* no length after the seq */
    }
    uint32_t len = (uint32_t)strtoul(end + 1, 0, 10);
    _pendingSeq = seq;
    _pendingLen = len;
    _havePendingBlock = true;
    _downloadComplete = false;
    /* I2: a BLOCK with seq 0 while a write is open is the next transfer's
     * first block (the requestor restarts its sequence at 0). The old
     * transfer's partial .tmp is discarded on the next drain (the flag,
     * acted on before the pull), and the pull then opens a fresh write,
     * so this block appends to a new file, not the old partial one. */
    if (_stagedWriteOpen && seq == 0) {
      _transferEnded = true;
    }
    _status.state = HEARTH_UPDATE_DOWNLOADING;
    return;
  }

  /* A state line. _lastState remembers the state before this line so the
   * drain (4b) can tell a change from a repeat; the detail is parsed per
   * state. */
  _lastState = _status.state;
  if (strcmp(state, "IDLE") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    _status.percent = 0;
    _havePendingBlock = false;
    /* Final review I3: the download is over, so the wanted rate goes back
     * to the default here too, not only on DOWNLOADED, ERROR and the
     * abort. */
    _baudWantedDownload = false;
    /* I2, second half: an IDLE the co-processor sends after an ERROR
     * (or instead of it, a provider abort) ends the transfer the same
     * way; the same drain-side flag acts on it. */
    _transferEnded = true;
  } else if (strcmp(state, "QUERYING") == 0) {
    /* Leaves the state where it is (IDLE); the callback still fires (case
     * 6b). No link call: this is a parse-only URC route. */
  } else if (strcmp(state, "AVAILABLE") == 0) {
    /* The provider is offering an image: the download is starting, so the
     * link goes to the download baud (case 10). This is a parse-only URC
     * route, so the AT+MTBAUD itself goes out on the next drain, not here. */
    _status.state = HEARTH_UPDATE_IDLE;
    if (comma) {
      _status.offeredVersion = (uint32_t)strtoul(comma + 1, 0, 10);
    }
    _baudWantedDownload = true;
  } else if (strcmp(state, "DOWNLOADING") == 0) {
    _status.state = HEARTH_UPDATE_DOWNLOADING;
    if (comma) {
      _status.percent = (uint8_t)atoi(comma + 1);
    }
  } else if (strcmp(state, "DOWNLOADED") == 0) {
    _status.state = HEARTH_UPDATE_VERIFYING;
    _status.percent = 100;
    _havePendingBlock = false;
    /* The drain ends the staged write, restores the link baud and runs the
     * verification and the verdict on this state; it runs outside the link's
     * dispatch, where this callback cannot (the hearthDrain* pattern, this
     * header's own comment). */
  } else if (strcmp(state, "APPLY") == 0) {
    /* The requestor asked to apply (spec 7.3): set the pending flag only.
     * This is a URC route and may not call the link, so hearthDrain() is
     * where the apply runs, once, in hearthApply(). The re-review M5 gate:
     * the apply is acted on only when _verdictSent, a verdict (the
     * AT+MTOTASTAGED=1 for the staged bundle) went out in this boot; an
     * APPLY with no verdict in this boot (the co-processor survived a
     * host reboot, so no verdict went out here) is dropped with the flag
     * and the state left where it was. A DEFERRED between the verdict
     * and the apply (the provider's AwaitNextAction) keeps the flag, so
     * the APPLY that follows the delay still applies. */
    if (_verdictSent) {
      _status.state = HEARTH_UPDATE_APPLYING_FW;
      _applyPending = true;
    }
  } else if (strcmp(state, "DEFERRED") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    /* Final review I3: the offer is not going to run now, so the link
     * goes back to the default rate too. */
    _baudWantedDownload = false;
    _status.deferredSeconds = comma ? (uint32_t)strtoul(comma + 1, 0, 10) : 0;
  } else if (strcmp(state, "DISCONTINUED") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    /* Final review I3: the offer is gone, so the link goes back to the
     * default rate too. */
    _baudWantedDownload = false;
    _status.percent = 0;
  } else if (strcmp(state, "ERROR") == 0) {
    /* The co-processor ended the transfer (case 6b): the state is FAILED
     * with HEARTH_UPDATE_ERR_COPROC and the detail, and the link baud goes
     * back to the default. The error and detail stay readable in status()
     * until the next transfer starts (a BLOCK line). Final review I2: a
     * transfer the co-processor ends between blocks used to leave the
     * staged write open, and the next offer's first block would have
     * appended to the old partial .tmp (a good bundle then refused as
     * malformed); the flag below tells the next drain to end and discard
     * the write there, where the file work is allowed to run. */
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_COPROC;
    _baudWantedDownload = false;
    _transferEnded = true;
    if (comma) {
      const char *d = comma + 1;
      size_t dn = strlen(d);
      if (dn >= sizeof(_status.detail)) {
        dn = sizeof(_status.detail) - 1;
      }
      memcpy(_status.detail, d, dn);
      _status.detail[dn] = 0;
    } else {
      _status.detail[0] = 0;
    }
  } else {
    /* An unknown token: nothing parsed, nothing changed, no callback. */
    return;
  }
  if (_statusCB) {
    _statusCB(_status);
  }
}

/*
 * Called from HearthClass::poll() after the link's own drains, and again
 * at the end of every hearthCommand() this object sends, which is why the
 * _draining guard below exists. The pull-and-acknowledge loop (Task 4b1):
 * a pending block pulls and acknowledges itself, and +MTOTA:DOWNLOADED
 * ends the staged write and sets _downloadComplete, which Task 4b2 turns
 * into the verification, the verdict, the consent and the
 * AT+MTOTASTAGED.
 *
 * Final review I8: the work below is hearthDrainImpl(), reached only
 * through _drainFn, which begin() sets. A constructed object (a sketch
 * that never calls begin()) costs one null check and nothing else, and
 * the whole update path is out of its link.
 */
void HearthUpdate::hearthDrain() {
  if (_drainFn) {
    (this->*_drainFn)();
  }
}

void HearthUpdate::hearthDrainImpl() {
  /* Final review I4: every other drain in HearthClass returns when
   * _link.busy() is set (the C3 lesson recorded above
   * hearthDrainCmdRespQueue), and this one was missing the check. A
   * sketch callback dispatched from inside a URC (an onChange that sets
   * another endpoint, the pattern the README documents) calls
   * hearthCommand(), and that call's tail runs this drain while the outer
   * exchange still holds the busy latch: without the guard, a pending
   * block's pull would get HEARTH_CMD_REENTRANT and abort a healthy
   * transfer, and a pending APPLY would run with every hearthCmd
   * refused. The pending flags survive the return, and the outer call's
   * own tail drain runs the work once the latch is released.
   *
   * The _draining guard still has to come first, though: _draining is
   * what stops a drain NESTED INSIDE a running drain (the pull's own
   * hearthCommand()s each end in a drain, the apply's AT+MTVER? waits do)
   * from running the pull a second time, and it is not a re-entrancy
   * guard: every nested caller is already inside this same drain. A
   * nested caller with the link busy is exactly that inner call, and it
   * must return on _draining, not on the busy flag: with the busy check
   * first, a drain that started while the link was busy (a nested
   * sketch call from inside the outer command, the I4 scenario itself)
   * would never see the _draining guard, and its nested drain would
   * re-enter the pull with the flags still set. */
  if (_draining) {
    return;
  }
  if (_owner && ((HearthClass *)_owner)->link().busy()) {
    return;
  }
  DrainGuard guard(_draining);
  /* B671, bench 2026-09-30: the co-processor's OTA requestor mode is
   * not persisted (spec 5.2, every boot starts disabled), so after ANY
   * co-processor reboot the requestor is off until this host re-runs
   * the declaration and the requestor switch, and a +MTERR:8 that
   * settled begin() on the boot race (F669) is never re-checked.
   * hearthNoteCoprocReady() armed the flag on the +MTREADY (it runs
   * inside a URC callback, no link calls), and the re-probe runs here,
   * on the drain, exactly as begin() runs it: the declaration first,
   * then the requestor switch. Only a begin() that completed its
   * requestor switch gets re-probed (a begin() that never reached it
   * has sent nothing this can send), and only while the state is
   * IDLE, UNAVAILABLE or FAILED: a download, a verdict or an apply is
   * in flight and handles its own reboots, so any other state does
   * nothing. Final review I6: FAILED used to be a resting state the
   * re-probe skipped, and the co-processor's OTA state is already IDLE
   * after a failed apply (no URC moves it), so a co-processor reboot
   * while FAILED left the requestor off for the rest of the boot. The
   * gate admits FAILED now, and hearthDeclareAndRequestor() moves it to
   * IDLE with error OK when the re-probe succeeds (the co-processor is
   * up and FOTA is live again) or leaves it as it is when the re-probe
   * fails. */
  if (_coprocReadySeen) {
    _coprocReadySeen = false;
    if (_beginComplete
        && (_status.state == HEARTH_UPDATE_IDLE
            || _status.state == HEARTH_UPDATE_UNAVAILABLE
            || _status.state == HEARTH_UPDATE_FAILED)) {
      hearthDeclareAndRequestor();
    }
  }
  if (_status.state == HEARTH_UPDATE_DISABLED || _status.state == HEARTH_UPDATE_UNAVAILABLE) {
    /* B667, bench 2026-09-30 (nRF54L15 on its CPico carrier): an
     * uncommissioned nRF answers AT+MTOTA=1 with +MTERR:8 while its
     * AT+MTSWVER answered OK, because its firmware wires the requestor
     * only once the device is commissioned (the ESP32-C6 wires it before
     * commissioning, so the C6 bench did not show it). begin() settled
     * UNAVAILABLE, and hearthDrain() used to return here forever: a
     * product powered up and then commissioned never got FOTA until its
     * host rebooted. When begin()'s AT+MTOTA=1 met that 8 it armed
     * _requestorRetry, and now the retry goes out once the commissioning
     * is noted (hearthNoteCommissioned set _requestorRetryNow) or every
     * 30 s while it lasts (one AT command per 30 s). It stays UNAVAILABLE
     * while it lasts: a firmware without FOTA never arms it, so its
     * UNAVAILABLE is settled for good. */
    if (_requestorRetry
        && (_requestorRetryNow
            || millis() - _requestorRetryMs >= 30000)) {
      _requestorRetryNow = false;
      _requestorRetryMs = (uint32_t)millis();
      int rc = hearthCmd("AT+MTOTA=1", 0, 0);
      if (rc == 0) {
        _requestorRetry = false;
        _status.state = HEARTH_UPDATE_IDLE;
        _status.error = HEARTH_UPDATE_OK;  /* the state and the error move together */
        if (_statusCB) {
          _statusCB(_status);
        }
      }
      /* 8 (or any other failure): stay UNAVAILABLE (and its error) and
       * retry later; no state change, so no error change either. */
    }
    return;
  }
  /* The download-time baud switch (case 10). A state line only records the
   * rate it needs (_baudWantedDownload); the AT+MTBAUD itself goes out here,
   * because a URC callback may not call the link. The switch is sent only
   * when the needed rate differs from the one last set, so a repeat state
   * line is a no-op and a switch back is seen.
   *
   * The download rate is wanted only when the switch can be FOLLOWED: on
   * a target build a test hook is installed, or the link's own port is
   * HEARTH_SERIAL_PORT and hearthRebaudLink() can bring it to the rate.
   * B666, bench 2026-09-30: on the device nothing re-clocked the host's
   * UART, the co-processor switched to 921600 after its OK and the link
   * went deaf. The host build keeps today's behaviour (the command goes
   * out, nothing to re-clock) so the tests that script AT+MTBAUD without
   * installing a hook stay valid; the ARDUINO-only condition above is the
   * gate the device needs and changes nothing there. */
#ifdef ARDUINO
  bool downloadFollowable =
      _baudChangerCB != 0 || ((HearthClass *)_owner)->hearthCanRebaudLink();
#else
  bool downloadFollowable = true;
#endif
  uint32_t wantBaud =
      (_baudWantedDownload && downloadFollowable) ? _cfg.downloadBaud : HEARTH_LINK_BAUD;
  if (wantBaud != _baud) {
    hearthSetBaud(wantBaud);
  }
  /* I2: the co-processor ended the transfer (an ERROR or IDLE state line,
   * or a BLOCK with seq 0 while the write was still open), and the staged
   * write is still open. End and discard it here, on the drain, where the
   * file work may run (the URC callback only set the flag): the next
   * transfer's first block then opens a FRESH write instead of appending
   * to the old partial .tmp. The pending block, when the flag came from a
   * BLOCK line, survives to the pull below, which begins the fresh write. */
  if (_transferEnded) {
    _transferEnded = false;
    if (_stagedWriteOpen) {
      _stage.stagedEndWrite();
      _stage.stagedRemove();
      _stagedWriteOpen = false;
      /* _havePendingBlock is left as the URC route left it: an ERROR or
       * IDLE cleared it (nothing to pull), a BLOCK with seq 0 set it with
       * the new transfer's first block in hand and the pull below runs. */
    }
  }
  if (_havePendingBlock) {
    hearthPullBlock();
    return;
  }
  if (_status.state == HEARTH_UPDATE_VERIFYING && !_downloadComplete) {
    /* +MTOTA:DOWNLOADED was parsed (the state is VERIFYING) but the
     * staged write has not ended yet: end it now, restore the link baud,
     * set the flag, and run the verification and the verdict. */
    _stage.stagedEndWrite();
    _stagedWriteOpen = false;
    _downloadComplete = true;
    _baudWantedDownload = false;
    hearthSetBaud(HEARTH_LINK_BAUD);  /* the download is over, back to the default */
    hearthVerifyAndVerdict();
    return;
  }
  /* The consent hook refused and the verdict is still pending: if the
   * refusal has outlived the consent window the attempt is abandoned
   * (AT+MTOTA=0 then AT+MTOTA=1, the staged bundle removed, the state
   * IDLE). A refusal inside the window re-runs the hook on this drain, and
   * the verdict goes out when the hook returns true. */
  if (_status.state == HEARTH_UPDATE_VERIFYING && _consentPending) {
    /* _consentRefused, not a sentinel in the timestamp: a refusal recorded
     * when millis() is 0 lapses like any other (0 was "no refusal" before
     * the flag existed, and it could never lapse). */
    if (_consentRefused
        && (millis() - _consentRefusalMs) >= _cfg.consentWindowMs) {
      hearthAbandon();
      return;
    }
    bool consent = _applyRequestCB ? _applyRequestCB() : true;
    if (!consent) {
      return;  /* still refused, wait for the next drain */
    }
    _consentPending = false;
    _consentRefused = false;
    _consentRefusalMs = 0;
    char cmd[HEARTH_LINE_MAX];
    snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=1");
    hearthCmd(cmd, 0, 0);
    /* The verdict for the staged bundle is out (re-review M5): the apply
     * that follows it is the one that may apply. */
    _verdictSent = true;
    _status.state = HEARTH_UPDATE_WAIT_APPLY;
    if (_statusCB) {
      _statusCB(_status);
    }
  }
  /* 6a: the requestor's apply request. It was parsed on the URC route
   * (hearthOnOtaLine set _applyPending), and this is where the flasher
   * runs: the whole apply is blocking by design, so it runs here, on the
   * drain, and the loop is blocked for its duration. The re-review M5
   * gate: the apply is acted on only when _verdictSent (the URC route
   * checked it when it set _applyPending, and nothing in between clears
   * it); an APPLY with no verdict in this boot is dropped with the flag.
   * hearthApply() clears the flag, so one verdict gives one apply. */
  if (_applyPending) {
    _applyPending = false;
    hearthApply();
  }
}

/* The block-pull collector, for AT+MTOTAGET=<seq>. The answer is one
 * +MTOTABLK:<seq>,<off>,<hex> line per at most 96 bytes (ceil(len/96)
 * lines, the last shorter for a short block, the hex upper-case) and then
 * the terminal OK. Each line is validated before it is copied: the prefix,
 * the block's own seq, the offset exactly where the previous line ended,
 * and the hex length exactly the announced span of the block at that
 * offset. The first failure marks the pull failed, and every later line,
 * the included line, is ignored: the partial data is discarded by the
 * caller and nothing is acknowledged. */
struct BlkPull {
  uint8_t buf[1024];
  uint32_t seq;
  uint32_t len;
  uint32_t filled;
  bool failed;
};
void onBlkLine(const char *line, void *arg) {
  BlkPull *p = (BlkPull *)arg;
  if (strncmp(line, "+MTOTABLK:", 10) != 0) {
    /* +MTERR: lines never reach this callback: HearthLink::command
     * intercepts them and returns the code (12) to the caller, which is
     * where the mid-pull drop is handled. */
    return;
  }
  if (p->failed) {
    return;
  }
  const char *cs = line + 10;
  const char *c1 = strchr(cs, ',');
  if (!c1) {
    p->failed = true;
    return;
  }
  char *end;
  unsigned long s = strtoul(cs, &end, 10);
  if (end != c1 || s != p->seq) {
    p->failed = true;
    return;
  }
  const char *c2 = strchr(c1 + 1, ',');
  if (!c2) {
    p->failed = true;
    return;
  }
  unsigned long off = strtoul(c1 + 1, &end, 10);
  if (end != c2 || off != p->filled) {
    p->failed = true;
    return;
  }
  const char *hex = c2 + 1;
  size_t hn = strlen(hex);
  uint32_t want = p->len - (uint32_t)off;
  if (want > 96) {
    want = 96;
  }
  if (hn != (size_t)want * 2) {
    p->failed = true;
    return;
  }
  for (size_t i = 0; i < want; i++) {
    unsigned int v;
    if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
      p->failed = true;
      return;
    }
    p->buf[off + i] = (uint8_t)v;
  }
  p->filled = (uint32_t)off + want;
}

/*
 * Pull the pending block and acknowledge it. The stage gets its .tmp file
 * before the first block of a transfer: on a parse error or a timeout the
 * transfer aborts (AT+MTOTA=0) and the partial staged file is removed, so
 * a dead transfer never leaves a file a later transfer could mistake for
 * its own.
 *
 * The retry rules (plan Task 4b):
 *  - a parse error re-pulls once: the pull is sent again, and the answer
 *    that parses is used;
 *  - the second parse failure aborts the transfer (AT+MTOTA=0), the
 *    state is FAILED with HEARTH_UPDATE_ERR_LINK;
 *  - a TIMEOUT is not re-pulled. The block must be acknowledged within 5 s
 *    and a second full pull does not fit that at 115200, so a timed-out
 *    pull aborts at once;
 *  - a +MTERR:12 after partial lines (the co-processor dropped the
 *    transfer mid-pull) discards the partial block and nothing is
 *    acknowledged: the state is left for the ERROR,<detail> and IDLE
 *    lines that follow.
 *
 * A successful pull ends in the staged append and the AT+MTOTAACK, which
 * the co-processor answers before the next +MTOTA:BLOCK.
 */
void HearthUpdate::hearthPullBlock() {
  uint32_t seq = _pendingSeq;
  uint32_t len = _pendingLen;
  if (len == 0 || len > 1024) {
    /* A block the buffer cannot hold: nothing to pull, abort the
     * transfer. (The co-processor announces 1024-byte blocks at most.) */
    hearthAbortPull();
    return;
  }
  /* Before the first block of a transfer the stage opens its write. A new
   * transfer is a new bundle: the verdict the last one sent (if any) no
   * longer describes what is staged, so the re-review M5 flag clears here
   * and the next apply waits for the next verdict. */
  if (!_stagedWriteOpen) {
    if (!_stage.stagedBeginWrite()) {
      hearthAbortPull();
      return;
    }
    _stagedWriteOpen = true;
    _verdictSent = false;
  }
  BlkPull pull;
  memset(&pull, 0, sizeof(pull));
  pull.seq = seq;
  pull.len = len;
  char cmd[HEARTH_LINE_MAX];
  int rc;
  int attempt;
  for (attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0) {
      memset(&pull, 0, sizeof(pull));
      pull.seq = seq;
      pull.len = len;
    }
    snprintf(cmd, sizeof(cmd), "AT+MTOTAGET=%lu", (unsigned long)seq);
    rc = hearthCmd(cmd, onBlkLine, &pull, kPullTimeoutMs);
    if (rc == 12) {
      /* +MTERR:12 after partial lines: the co-processor dropped the
       * transfer mid-pull. Nothing is acknowledged and the state is left
       * for the ERROR,<detail> and IDLE lines that follow. The partial
       * staged write is ended (the .tmp is removed, not renamed) so a
       * dead transfer never leaves a file a later transfer could append
       * to. */
      _havePendingBlock = false;
      /* End the staged write (closes the .tmp) and remove the staged
       * file if it was renamed by a previous endWrite. The .tmp itself
       * is removed by stagedEndWrite since no append succeeded. */
      _stage.stagedEndWrite();
      _stage.stagedRemove();
      _stagedWriteOpen = false;
      return;
    }
    if (rc == 0 && !pull.failed && pull.filled == len) {
      /* The whole block landed in order: append it and acknowledge. */
      break;
    }
    if (rc != 0) {
      /* A timeout or a link failure: never re-pulled, abort at once. */
      hearthAbortPull();
      return;
    }
    /* pull.failed, or a short answer without an error: a garbled line, an
     * out-of-sequence offset, a hex length that does not match the
     * announced block. The loop re-pulls once; the second failure aborts
     * below. */
  }
  _havePendingBlock = false;
  if (attempt >= 2) {
    /* Two parse failures in a row: the transfer is aborting. */
    hearthAbortPull();
    return;
  }
  if (!_stage.stagedAppend(pull.buf, pull.filled)) {
    hearthAbortPull();
    return;
  }
  snprintf(cmd, sizeof(cmd), "AT+MTOTAACK=%lu", (unsigned long)seq);
  if (hearthCmd(cmd, 0, 0) != 0) {
    hearthAbortPull();
    return;
  }
  /* Acknowledged. The co-processor answers the ACK and then sends either
   * the next +MTOTA:BLOCK or +MTOTA:DOWNLOADED; the next drain handles it. */
}

/*
 * End a failed transfer: the AT+MTOTA=0 that tells the co-processor to
 * abort, the partial staged file removed (a dead transfer must not leave a
 * file a later transfer could append to), and the state FAILED with
 * HEARTH_UPDATE_ERR_LINK. This is one of the places a transfer ends FAILED
 * while the download baud is active (a pull that timed out or failed to
 * parse twice, mid-download at downloadBaud), so the link goes back to the
 * default here, exactly as the DOWNLOADED path does: when the co-processor
 * next reboots it comes up at the default rate and the two ends must still
 * understand each other (plan case 10). Final review I1: the AT+MTOTA=0
 * turns the requestor OFF for the rest of the co-processor's boot (mode 0
 * in the firmware's cmd_mtota), and no URC can move the state out of
 * FAILED, so without a follow-up AT+MTOTA=1 the provider's next offer would
 * never be heard: the requestor is turned back on right after the =0, as
 * hearthAbandon() does. Final review M6: the version in force is re-declared
 * before the requestor switch, the same first-wire call every other
 * failure path makes (the abort is one of them, this comment above
 * promises it): the aborted transfer applied nothing, so the declaration
 * must not keep claiming the new product version to the controller.
 */
void HearthUpdate::hearthAbortPull() {
  _havePendingBlock = false;
  _downloadComplete = false;
  _stagedWriteOpen = false;
  _baudWantedDownload = false;
  hearthSetBaud(HEARTH_LINK_BAUD);  /* the download is over, back to the default */
  /* No re-declaration here: an abort ends a DOWNLOAD, before any apply,
   * so no new version was ever declared and the one in force is still the
   * one begin() declared (M6 is about a host part failing after a Hearth
   * part was applied, not about the transfer). */
  hearthCmd("AT+MTOTA=0", 0, 0);
  hearthCmd("AT+MTOTA=1", 0, 0);  /* I1: the requestor on again for the next offer */
  _stage.stagedEndWrite();
  _stage.stagedRemove();
  _status.state = HEARTH_UPDATE_FAILED;
  _status.error = HEARTH_UPDATE_ERR_LINK;
  if (_statusCB) {
    _statusCB(_status);
  }
}

bool HearthUpdate::hearthDownloadComplete() const {
  return _downloadComplete;
}

/*
 * The download-time baud switch (case 10). The link goes to the download
 * baud on AVAILABLE and back to the default on DOWNLOADED and on FAILED.
 * B666 (bench 2026-09-30): the co-processor (core/mt/mt_at.c, cmd_mtbaud)
 * answers OK at the CURRENT rate and only then switches, so the host
 * re-clocks its own side only when the answer is OK. On the device the
 * re-clock is hearthRebaudLink() (the link's own port, when it is
 * HEARTH_SERIAL_PORT); in the host build a test hook installed through
 * hearthSetBaudChanger() records the rate the sketch's own port would
 * use, and with neither available the command simply goes out (nothing
 * to re-clock there). When the answer is not OK and the switch was UP
 * (to the download baud), the host must NOT re-clock: both ends stay at
 * the old rate, the transfer runs there, and _baudWantedDownload is
 * cleared so this drain does not retry the switch on the next poll.
 *
 * Final review I3: when the switch was BACK (to HEARTH_LINK_BAUD) and the
 * answer is not OK, the co-processor is usually gone (it rebooted at the
 * download baud and comes up at its default, HEARTH_LINK_BAUD, so the
 * host at the download baud is deaf). The host re-clocks to
 * HEARTH_LINK_BAUD anyway and records it in _baud, so the next drain sees
 * the rates equal and does not send another AT+MTBAUD into the deaf link
 * and wait the 1.5 s command timeout again. The co-processor is reset
 * through the owner's hearthResetCoprocessor() when a reset line is known
 * (a no-op without one), so both ends are at a known rate: its +MTREADY
 * then triggers the B671 re-probe.
 */
void HearthUpdate::hearthSetBaud(uint32_t baud) {
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTBAUD=%lu", (unsigned long)baud);
  int rc = hearthCmd(cmd, 0, 0);
  if (rc != 0) {
    if (baud == HEARTH_LINK_BAUD) {
      /* I3: the co-processor may have rebooted to its default rate
       * already: re-clock the host to the default anyway and say so
       * (naming the rate, the 7c-fix1 review's one log line). */
#ifdef ARDUINO
      Serial.printf("Hearth.update: the co-processor did not answer AT+MTBAUD=%lu, the host clocks to %lu anyway\n",
                    (unsigned long)baud, (unsigned long)baud);
#endif
      _baud = baud;
      if (_baudChangerCB) {
        _baudChangerCB(baud);
      } else if (_owner) {
        ((HearthClass *)_owner)->hearthRebaudLink(baud);
      }
      /* The reset line is known when it is not -1 (begin() resolved the
       * config pins from the owner). hearthResetCoprocessor() is a no-op
       * without one; its +MTREADY (on the default rate, now readable)
       * triggers the B671 re-probe. The B632 settle runs first, as at
       * every other reset the update drives: the MG24's key store saves
       * its key map 2 s after a write, so a reset inside that window
       * after a commissioning loses the new fabric. */
      if (_owner && _cfg.resetPin != -1) {
        hearthSettleAfterCommissioning();
        ((HearthClass *)_owner)->hearthResetCoprocessor();
      }
      _baudWantedDownload = false;
      return;
    }
    _baudWantedDownload = false;  /* B666: the co-processor stayed at the old rate */
    return;
  }
  _baud = baud;
  if (_baudChangerCB) {
    _baudChangerCB(baud);
  } else if (_owner) {
    ((HearthClass *)_owner)->hearthRebaudLink(baud);
  }
}

/*
 * Abandon the attempt (case 8, past the consent window). The co-processor's
 * block buffer is held for the whole download and is freed only when the
 * attempt ends, so the host sends AT+MTOTA=0 (the abort that frees the
 * buffer at once, not after the driver's six-hour watchdog) and then
 * AT+MTOTA=1 to re-arm the requestor for the next offer. The staged bundle
 * is removed and the state goes IDLE.
 */
void HearthUpdate::hearthAbandon() {
  _havePendingBlock = false;
  _downloadComplete = false;
  _stagedWriteOpen = false;
  _consentPending = false;
  _consentRefused = false;
  _consentRefusalMs = 0;
  /* The bundle is removed: no verdict stands for it any more (re-review
   * M5), so an apply that arrives now has nothing to apply. */
  _verdictSent = false;
  /* End the staged write only while it is open: on DOWNLOADED the write
   * already ended, so the second endWrite here would be a no-op at best and
   * could remove a staged file the verdict is still using at worst. */
  if (_stagedWriteOpen) {
    _stage.stagedEndWrite();
  }
  _stage.stagedRemove();
  hearthCmd("AT+MTOTA=0", 0, 0);
  hearthCmd("AT+MTOTA=1", 0, 0);
  _status.state = HEARTH_UPDATE_IDLE;
  _status.percent = 0;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  if (_statusCB) {
    _statusCB(_status);
  }
}

/*
 * Final review I5: the effective product version and the declaration it
 * backs, set together. Every place that changes the effective version
 * (begin(), the fw-only success tail, the first-boot host confirm, the
 * 6c resume) calls this, so the re-probe and the rollback always declare
 * the version actually in force with its matching string (before, the
 * success tails updated the effective version and left the declared pair
 * at the pre-update values, and a later re-probe or failure would have
 * re-declared the old one).
 */
void HearthUpdate::hearthSetEffectiveVersion(uint32_t version, const char *versionString) {
  _effectiveVersion = version;
  _status.effectiveVersion = version;
  _declaredVersion = version;
  snprintf(_declaredVersionString, sizeof(_declaredVersionString), "%s", versionString);
}

/* Final review M6: the re-declaration itself, AT+MTSWVER with the version
 * in force and its string. The apply's failure paths call this as their
 * first wire call (the host part's failures, the abort, the failure
 * tail's no-rollback branch) so the controller is not told the product
 * updated when it did not. */
void HearthUpdate::hearthReDeclareInForce() {
  char rcmd[HEARTH_LINE_MAX];
  snprintf(rcmd, sizeof(rcmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)_declaredVersion,
           _declaredVersionString);
  hearthCmd(rcmd, 0, 0);
}

/*
 * B671: the declaration and the requestor switch, re-run after a
 * co-processor reboot, from hearthDrain() on the flag
 * hearthNoteCoprocReady() set. A separate function, not begin()'s own
 * tail (begin() sends the declaration before its AT+CGMM/MTVER/MTOTA?
 * queries). Same two commands, same order and same answers as begin():
 * the declaration first (AT+MTSWVER with the effective version and its
 * string), then AT+MTOTA=1. Final review I6: the drain's gate also runs
 * this while the state is FAILED (a failed apply leaves the
 * co-processor's OTA state IDLE, so no URC moves it, and the re-probe is
 * what re-arms the requestor): a successful re-probe moves FAILED to
 * IDLE with error OK, because the co-processor is up and FOTA is live
 * again, and a failed re-probe leaves FAILED and its error as they are
 * (no state change, so no error change either).
 */
void HearthUpdate::hearthDeclareAndRequestor() {
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)_effectiveVersion, _declaredVersionString);
  int r0 = hearthCmd(cmd, 0, 0);
  if (r0 == 8) {
    /* The firmware without FOTA (begin()'s own branch): settled for
     * good, no further command and no B667 retry (that 8 is not the
     * requestor refusing, it is the command itself unknown). */
    _status.state = HEARTH_UPDATE_UNAVAILABLE;
    _status.effectiveVersion = _effectiveVersion;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }
  if (r0 != 0) {
    /* A timeout (or a link failure): leave the state as it is. The
     * requestor stays where the last good boot left it, and the next
     * reboot re-probes again. */
    return;
  }
  int rc = hearthCmd("AT+MTOTA=1", 0, 0);
  if (rc == 8) {
    /* The requestor is not wired yet (begin()'s own branch, B667):
     * UNAVAILABLE with the retry armed on the commissioning and every
     * 30 s until it goes. */
    _status.state = HEARTH_UPDATE_UNAVAILABLE;
    _status.effectiveVersion = _effectiveVersion;
    _requestorRetry = true;
    _requestorRetryMs = (uint32_t)millis();
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }
  if (rc != 0) {
    /* A timeout: leave the state as it is. */
    return;
  }
  _status.state = HEARTH_UPDATE_IDLE;
  _status.error = HEARTH_UPDATE_OK;  /* I6: a successful re-probe clears FAILED's error */
  _requestorRetry = false;  /* the re-probe made the requestor go: no retry */
  if (_statusCB) {
    _statusCB(_status);
  }
}

/* B671: a +MTREADY was dispatched (hearthOnURCLine, every +MTREADY,
 * expected or not). Recording a flag, no link call, so it is safe from
 * the URC callback it runs in; the re-probe itself goes out on the next
 * hearthDrain(). */
void HearthUpdate::hearthNoteCoprocReady() {
  _coprocReadySeen = true;
}

/*
 * The query-result collectors: the lines after the prefix, kept in plain
 * members (no heap, no link calls from inside a command callback).
 */
namespace {
struct OtaQuery {
  bool got;
  int mode;
  char state[24];
  int percent;
  char variant[16];
};
void onOtaQueryLine(const char *line, void *arg) {
  OtaQuery *q = (OtaQuery *)arg;
  if (strncmp(line, "+MTOTA:", 7) != 0) {
    return;
  }
  /* The answer is +MTOTA:<mode>,<state>,<percent>,<variant>: the first
   * field is the mode digit, which is what keeps it out of the URC set. */
  char *end;
  int mode = (int)strtol(line + 7, &end, 10);
  if (end == line + 7 || *end != ',') {
    return;
  }
  q->mode = mode;
  const char *p = end + 1;
  const char *c1 = strchr(p, ',');
  if (!c1) {
    return;
  }
  size_t n = (size_t)(c1 - p);
  if (n >= sizeof(q->state)) {
    n = sizeof(q->state) - 1;
  }
  memcpy(q->state, p, n);
  q->state[n] = 0;
  const char *c2 = strchr(c1 + 1, ',');
  q->percent = c2 ? atoi(c1 + 1) : 0;
  if (c2) {
    const char *v = c2 + 1;
    size_t vn = strlen(v);
    if (vn >= sizeof(q->variant)) {
      vn = sizeof(q->variant) - 1;
    }
    memcpy(q->variant, v, vn);
    q->variant[vn] = 0;
  }
  q->got = true;
}

struct ModelQuery {
  bool got;
  char model[33];
};
void onModelLine(const char *line, void *arg) {
  ModelQuery *q = (ModelQuery *)arg;
  /* AT+CGMM answers with the plain model line, no prefix: anything but a
   * terminal or a + line is the answer. */
  if (line[0] == '+' || strcmp(line, "OK") == 0 || strcmp(line, "ERROR") == 0) {
    return;
  }
  size_t n = strlen(line);
  if (n >= sizeof(q->model)) {
    n = sizeof(q->model) - 1;
  }
  memcpy(q->model, line, n);
  q->model[n] = 0;
  q->got = true;
}

struct VerQuery {
  bool got;
  char version[33];
};
void onVerLine2(const char *line, void *arg) {
  VerQuery *q = (VerQuery *)arg;
  if (strncmp(line, "+MTVER:", 7) != 0) {
    return;
  }
  size_t n = strlen(line + 7);
  if (n >= sizeof(q->version)) {
    n = sizeof(q->version) - 1;
  }
  memcpy(q->version, line + 7, n);
  q->version[n] = 0;
  q->got = true;
}

/* The model's filesystem need in bytes (DE625, the per-port figures the
 * plan's Global Constraints name). 0: a model the list does not know. */
uint32_t modelFsNeed(const char *model) {
  if (strcmp(model, "ESP32-C6 Hearth") == 0) {
    return 6815744;  /* 6.5 MiB: a combined image, an 8 MB FS board */
  }
  if (strcmp(model, "nRF54L15 Hearth") == 0 || strcmp(model, "nRF54LM20A Hearth") == 0
      || strcmp(model, "MGM240P Hearth") == 0) {
    return 3670016;  /* 3.5 MiB: a 4 MB FS board */
  }
  return 0;
}

}  // namespace

/*
 * The apply of the Hearth (co-processor) part (plan Task 6a, spec 7.3 and
 * 7.5). hearthDrain() runs it once, for the +MTOTA:APPLY the URC route
 * parsed. It works from the staged file and the state record, not from
 * download-time memory alone: Task 6c's resume after a power loss reopens
 * the same stage and calls hearthApplyFw() with the state it loaded.
 *
 * The order (the test pins every command):
 *  1. AT+MTSWVER=<the bundle's product version>,"<its string>" FIRST,
 *     before any flash (spec 7.5): every reset from here on is one the
 *     requestor must come back from already knowing the new version.
 *  2. hearthApplyFw(): up to three flash attempts, each
 *     flash -> re-clock the link -> wait for +MTREADY -> AT+MTVER?.
 *  3. Success: retain the new part, AT+MTOTA=1 (the requestor's mode is
 *     not persisted, spec 5.2, so it is off after the co-processor's
 *     reboot), the manifest, and then the host part (6b).
 *  4. Three failures: re-declare the old version, the retained image if
 *     one fits this model, AT+MTOTA=1, FAILED with HEARTH_UPDATE_ERR_FLASH
 *     and the staged bundle kept for a manual retry.
 */
void HearthUpdate::hearthApply() {
  /* The verdict is consumed by this apply (re-review M5: one verdict
   * gives one apply). Cleared before any early return, so a failed
   * re-open or a bad bundle does not leave a stale verdict standing. */
  _verdictSent = false;
  HearthFile *staged = _stage.stagedOpenRead();
  if (!staged) {
    /* The staged bundle is gone (a power loss in the consent window):
     * nothing to flash. The stagedRemove() below is a no-op. */
    hearthCmd("AT+MTOTA=1", 0, 0);
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_FLASH;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }
  HearthFileSource src(*staged);
  HearthBundleInfo info;
  HearthBundleError e = HearthBundle::open(src, _cfg.publicKey, info);
  if (e != HEARTH_BUNDLE_OK) {
    delete staged;
    hearthCmd("AT+MTOTA=1", 0, 0);
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_BUNDLE;
    _status.reason = (int)e;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }

  /* The part to flash: the one the verify selected, and only that one.
   * 0xFF (or a part that is not a type-2 Hearth part) means there is no
   * Hearth part: the apply goes straight to the host part. The verify
   * leaves _fwPart 0xFF for a host-only selection (the Hearth part's
   * version equals the running one, so it skipped it), and falling back to
   * the bundle's first Hearth part here would flash a part the verify
   * skipped. */
  int part = _fwPart;
  if (part < 0 || part >= (int)info.partCount || info.parts[part].type != 2) {
    part = 0xFF;
  }
  if (part == 0xFF) {
    /* A host-only selection: the host applies alone, and the declaration
    * it needs is the one it sends itself (it was never declared here). */
    delete staged;
    hearthApplyHost();
    return;
  }

  HearthUpdateState st;
  memset(&st, 0, sizeof(st));
  st.phase = HEARTH_PHASE_FW;
  st.targetVersion = info.productVersion;
  snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s", info.productVersionString);
  st.fwPart = (uint8_t)part;
  st.hostPart = (uint8_t)_hostPart;

  /* Final review I7: keep the pre-apply running Hearth version: the
   * success tail overwrites _hearthVersion with the new part's, and the
   * rollback check in hearthFwFailed() compares against this instead
   * (AT+MTVER? answering it means nothing was written and the old
   * application is intact). It is kept BEFORE the apply runs, because the
   * failure tail (which reads it) runs inside hearthApplyFw's failure
   * path, before hearthApply's own post-apply code. */
  snprintf(_preApplyHearthVersion, sizeof(_preApplyHearthVersion), "%s", _hearthVersion);
  /* The declaration goes out before any flash (spec 7.5's hard rule). */
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)info.productVersion,
           info.productVersionString);
  hearthCmd(cmd, 0, 0);

  if (hearthApplyFw(st) == 0) {
    hearthFwSucceeded(info, part, *staged, st);
  } else {
    hearthFwFailed();
  }
  /* Final review M1: hearthApply() owns the staged file and deletes it on
   * every path: before this, the failure branch (three flash attempts, the
   * failure tail keeping the staged bundle for a manual retry) left the
   * heap object and its open LittleFS handle alive, whose file is later
   * removed or renamed while still open. hearthFwSucceeded() takes a
   * reference and does not delete (its callers, including the 6c resume,
   * own and delete their own handle). */
  delete staged;
}

/*
 * The success tail of the Hearth-part apply (6a's, moved out of
 * hearthApply() by 6c so the resume shares it, ruling 6). The new image is
 * confirmed running (AT+MTVER? matched the part's version): retain it for a
 * future rollback (the rotation deletes the old retained image first,
 * DE625), turn the requestor back on (its mode is not persisted, spec 5.2,
 * so it is off after the co-processor's reboot) and cache the new running
 * version, then finish fw-only (the manifest, the staged bundle and the
 * state gone, IDLE with the bundle's product version) or go on to the host
 * part (spec 7: the host applies second). Final review M1: the staged
 * file is owned by the caller (hearthApply() deletes it on every path,
 * the 6c resume its own handle); this function reads it through the
 * reference and does not delete it.
 */
void HearthUpdate::hearthFwSucceeded(const HearthBundleInfo &info, int part, HearthFile &staged,
                                     const HearthUpdateState &st) {
  const HearthBundlePart &p = info.parts[part];
  _stage.retainFwPart(staged, info.containerOffset + p.offset, p.length, p.target, p.version);
  hearthCmd("AT+MTOTA=1", 0, 0);
  snprintf(_hearthVersion, sizeof(_hearthVersion), "%s", p.version);
  snprintf(_status.hearthVersion, sizeof(_status.hearthVersion), "%s", p.version);

  if (st.hostPart == 0xFF) {
    /* No host part selected: the fw-only finish. The manifest takes the
     * bundle's product version and string; the host version is kept
     * from the old manifest, "" when there was none. */
    HearthManifest m;
    if (_haveManifest) {
      m = _manifest;
    } else {
      memset(&m, 0, sizeof(m));
    }
    m.productVersion = info.productVersion;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", info.productVersionString);
    _stage.saveManifest(m);
    _manifest = m;
    _haveManifest = true;
    _stage.stagedRemove();
    _stage.clearState();
    /* I5: the effective version and the declaration move together. */
    hearthSetEffectiveVersion(info.productVersion, info.productVersionString);
    _status.state = HEARTH_UPDATE_IDLE;
    _status.error = HEARTH_UPDATE_OK;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
  } else {
    /* A host part is selected: the host applies second (spec 7). */
    hearthApplyHost();
  }
}

/*
 * The failure tail of the Hearth-part apply (6a's, moved out of
 * hearthApply() by 6c so the resume shares it, ruling 6). Three failed
 * attempts (spec 7.3): the retained image, if one fits this model, is one
 * more flash.
 *
 * Final re-review I7: the last attempt that failed in the flasher
 * (rc != HEARTH_FLASH_OK, the B672 class) exited with its own reset
 * (B668), so the co-processor is still booting when this tail runs. The
 * wait for that boot's +MTREADY runs FIRST, before the retained branch's
 * AT+MTVER? (asked during the boot it gets no answer, and the retained
 * image would be flashed over a healthy application): the arm the attempt
 * left in place (M4's, live until its wait consumed the marker) is the
 * wait's, so the boot is not reported as an unexpected one, and it is
 * re-armed for the wait's own timeout path. The later wait before the
 * declaration then goes, so there is one wait. _lastAttemptFlasherError
 * is the only failure mode with a pending exit reset: after a no-+MTREADY
 * or a verify mismatch nothing further reboots, so the wait is skipped
 * for those (the flash's 30 s is not spent on a marker that cannot come).
 *
 * Final review I7: before flashing the retained image the co-processor is
 * asked AT+MTVER?. It answering the pre-apply version (_preApplyHearthVersion,
 * kept before the first attempt: _hearthVersion is overwritten later)
 * means the old application is intact and running (every attempt failed
 * at entry, B672's class), so the rollback flash is skipped and only the
 * old version is re-declared: a retained image is the last image applied
 * by FOTA and can be OLDER than what runs now (a USB reflash), and
 * flashing it would downgrade a healthy co-processor.
 */
void HearthUpdate::hearthFwFailed() {
  char rtarget[33], rversion[33];
  uint32_t rlen = 0;
  bool haveRetained = _stage.retainedFwInfo(rtarget, rversion, rlen);
  bool didRollback = false;
  HearthClass *owner = (HearthClass *)_owner;
  /* Final re-review I7, first: the last attempt's exit reset, when the
   * flasher itself failed (B672's class). Arm for the wait (the M4 arm
   * the attempt left in place is the wait's; on a no-+MTREADY or verify
   * mismatch nothing is armed) so the boot's +MTREADY is consumed as an
   * expected reboot and never reaches the sketch as
   * HEARTH_COPROCESSOR_REBOOTED, and the retained branch's AT+MTVER?
   * below finds the co-processor up. The later wait before the
   * declaration then goes, so there is one wait. */
  if (_lastAttemptFlasherError) {
    owner->hearthArmExpectedReboot();
    if (!owner->link().waitReady(HEARTH_FLASH_READY_TIMEOUT_MS)) {
#ifdef ARDUINO
      Serial.printf("Hearth.update: no +MTREADY in %lu ms after the last attempt\n",
                    (unsigned long)HEARTH_FLASH_READY_TIMEOUT_MS);
#endif
    }
    owner->hearthDisarmExpectedReboot();
    /* B671: the wait dispatches the +MTREADY through the URC route,
     * setting the re-probe flag. Clear it: this reboot was driven by the
     * update (the flasher's exit) and the requestor is re-run below. */
    _coprocReadySeen = false;
  }
  if (haveRetained && strcmp(rtarget, _model) == 0) {
    /* I7: is the co-processor still running the pre-apply version? */
    VerQuery vq;
    vq.got = false;
    vq.version[0] = 0;
    hearthCmd("AT+MTVER?", onVerLine2, &vq);
    if (vq.got && strcmp(vq.version, _preApplyHearthVersion) == 0) {
      /* The pre-apply version answers: nothing was written (the attempts
       * failed before it, B672's class) and the old application is
       * intact, so the rollback flash is skipped. Only re-declare; the
       * requestor switch below comes after. */
      hearthReDeclareInForce();
      delete _stage.retainedFwOpen();  /* the branch only checked for it */
      snprintf(_hearthVersion, sizeof(_hearthVersion), "%s", vq.version);
      snprintf(_status.hearthVersion, sizeof(_status.hearthVersion), "%s", vq.version);
#ifdef ARDUINO
      Serial.printf("Hearth.update: the co-processor still runs %s, the rollback flash is skipped\n",
                    vq.version);
#endif
      didRollback = true;
    } else if (HearthFile *ret = _stage.retainedFwOpen()) {
      /* The old version is re-declared BEFORE the rollback flash (the
       * ruling): between the declaration and a successful boot the
       * co-processor claims a version it is not yet running. */
      hearthReDeclareInForce();
      HearthFlasher *fl = _flasher ? _flasher : HearthFlasher::forModel(_model);
      if (fl) {
        HearthFileSource rsrc(*ret);
        HearthCoprocPins pins;
        pins.reset = _cfg.resetPin;
        pins.resetActiveLow = _cfg.resetActiveLow;
        pins.strap = _cfg.strapPin;
        pins.strapActiveLow = _cfg.strapActiveLow;
        /* The retained image carries no digest (it is the bytes of an
         * image that already ran, not a bundle part): a zero digest, the
         * same convention the flashers apply to a range with no bundle
         * part behind it. */
        uint8_t zeros[32];
        memset(zeros, 0, sizeof(zeros));
        hearthSettleAfterCommissioning();
        /* The flasher's own exit (success or failure) resets the
         * co-processor: arm it so that boot is not reported as an
         * unexpected one (final review M4). */
        owner->hearthArmExpectedReboot();
        int rc = fl->flash(*owner->link().stream(), pins, rsrc, 0, rlen, zeros);
        (void)rc;
#if defined(ARDUINO) && defined(HEARTH_SERIAL_PORT)
        /* The flasher left the port at its own rate; re-clock it. */
        /* B674: drain the TX first; begin() ends the running UART. */
        HEARTH_SERIAL_PORT.flush();
        HEARTH_SERIAL_PORT.begin(HEARTH_LINK_BAUD);
#endif
#ifdef ARDUINO
        if (rc != HEARTH_FLASH_OK) {
          Serial.printf("Hearth.update: the rollback flash of the retained image failed: flasher error %d\n", rc);
        }
#endif
        /* The flash's own 30 s (bench MG24). */
        if (owner->link().waitReady(HEARTH_FLASH_READY_TIMEOUT_MS)) {
          /* B671: waitReady dispatched the +MTREADY through the URC route,
           * setting the re-probe flag. Clear it before the AT+MTVER?
           * below (whose own drain would otherwise see the flag set and
           * fire the re-probe): this reboot was driven by the update (the
           * rollback flash) and the requestor is re-run below. */
          _coprocReadySeen = false;
          VerQuery rvq;
          rvq.got = false;
          rvq.version[0] = 0;
          hearthCmd("AT+MTVER?", onVerLine2, &rvq);
          if (rvq.got) {
            /* Cache whatever it answers as the running version. */
            snprintf(_hearthVersion, sizeof(_hearthVersion), "%s", rvq.version);
            snprintf(_status.hearthVersion, sizeof(_status.hearthVersion), "%s", rvq.version);
          }
          didRollback = true;
        } else {
#ifdef ARDUINO
          Serial.printf("Hearth.update: no +MTREADY in %lu ms after the rollback flash\n",
                        (unsigned long)HEARTH_FLASH_READY_TIMEOUT_MS);
#endif
        }
        owner->hearthDisarmExpectedReboot();
        _coprocReadySeen = false;
      }
      delete ret;
    }
  }
  if (!didRollback) {
    /* No retained image to restore: the old version is re-declared so
     * the requestor comes back knowing what it is running. */
    hearthReDeclareInForce();
  }

  /* Either way the requestor is back on, the state file is gone (a
   * failed apply must not resume) and the staged bundle is KEPT, for a
   * manual retry. */
  hearthCmd("AT+MTOTA=1", 0, 0);
  _stage.clearState();
  _status.state = HEARTH_UPDATE_FAILED;
  _status.error = HEARTH_UPDATE_ERR_FLASH;
  _status.reason = 0;
  if (_statusCB) {
    _statusCB(_status);
  }
}

/*
 * The flash attempts of the part named by st (6a): the state is saved with
 * the attempt number, the flasher runs on the link's stream, the port is
 * re-clocked to the link baud (the flashers leave it at their own rate),
 * the expected reboot is armed and the link waits for +MTREADY, and
 * AT+MTVER? must answer the part's version. The return: 0 on success, 1
 * when the attempt failed (the caller tries again).
 *
 * The attempt count starts at firstAttempt (6c's resume parameter): a
 * power loss in the middle of an attempt counts the interrupted attempt,
 * so the resume enters with st.attempts + 1 and the total across any
 * number of power cycles stays bounded at three. A caller that enters
 * with firstAttempt greater than 3 does no flash at all and fails at
 * once (the failure tail is the caller's).
 */
int HearthUpdate::hearthApplyFw(HearthUpdateState &st, int firstAttempt) {
  /* The last-attempt record is this apply's to make (final re-review
   * Minor): an early return below (no flasher, the bundle will not
   * re-open) must not inherit the flag the previous apply's last attempt
   * left, so it starts false. */
  _lastAttemptFlasherError = false;
  HearthClass *owner = (HearthClass *)_owner;
  HearthFlasher *fl = _flasher ? _flasher : HearthFlasher::forModel(_model);
  /* Reopen the staged bundle on every call: the flasher reads the image
   * bytes through the source, and a resume (6c) enters this function with
   * no file open yet. */
  HearthFile *staged = _stage.stagedOpenRead();
  if (!staged || !fl) {
    delete staged;
    if (!fl) {
      /* No flasher knows this model: the declaration is rolled back
       * (below) and the failure reported. */
      char rcmd[HEARTH_LINE_MAX];
      snprintf(rcmd, sizeof(rcmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)_declaredVersion,
               _declaredVersionString);
      hearthCmd(rcmd, 0, 0);
      _status.state = HEARTH_UPDATE_FAILED;
      _status.error = HEARTH_UPDATE_ERR_FLASH;
      _status.reason = 0;
      if (_statusCB) {
        _statusCB(_status);
      }
    }
    return 1;
  }
  HearthFileSource src(*staged);
  HearthBundleInfo info;
  if (HearthBundle::open(src, _cfg.publicKey, info) != HEARTH_BUNDLE_OK) {
    delete staged;
    return 1;
  }
  const HearthBundlePart &p = info.parts[st.fwPart];
  uint32_t off = info.containerOffset + p.offset;
  HearthCoprocPins pins;
  pins.reset = _cfg.resetPin;
  pins.resetActiveLow = _cfg.resetActiveLow;
  pins.strap = _cfg.strapPin;
  pins.strapActiveLow = _cfg.strapActiveLow;

  for (int attempt = firstAttempt; attempt <= 3; attempt++) {
    st.attempts = (uint8_t)attempt;
    _stage.saveState(st);
    hearthSettleAfterCommissioning();
    /* Final review M4: arm BEFORE the flash, not after it. A flasher
     * failure after its entry reset exits with its own reset (B668), and
     * an arm in place keeps that boot from reaching the sketch as
     * HEARTH_COPROCESSOR_REBOOTED. The wait below consumes the marker on
     * a good flash, and every other exit disarms again so the next
     * spontaneous reboot is still reported. */
    owner->hearthArmExpectedReboot();
    int rc = fl->flash(*owner->link().stream(), pins, src, off, p.length, p.sha256);
#if defined(ARDUINO) && defined(HEARTH_SERIAL_PORT)
    /* The flasher left the port at its own rate (921600 on the ESP ROM
     * loader): back to the link baud before the next line on this stream. */
    /* B674: drain the TX first; begin() ends the running UART. */
    HEARTH_SERIAL_PORT.flush();
    HEARTH_SERIAL_PORT.begin(HEARTH_LINK_BAUD);
#endif
    if (rc != HEARTH_FLASH_OK) {
      /* The flasher failed: disarm and the next attempt, with no wait and
       * no AT+MTVER?. The flasher exits with its own reset (B668) after
       * this one, so its +MTREADY is still landing when the next
       * attempt's flash (or the failure tail's wait) runs; the failure
       * tail's wait for it is armed before it runs (final re-review I7). */
      _lastAttemptFlasherError = true;
      owner->hearthDisarmExpectedReboot();
#ifdef ARDUINO
      Serial.printf("Hearth.update: flash attempt %d of 3 failed: flasher error %d\n", attempt, rc);
#endif
      continue;
    }
    /* The flash landed: the co-processor is rebooting into the new image.
     * The arm is already in place (above) and the wait consumes its
     * +MTREADY. The flash's own 30 s, not the link's 10 s (bench MG24):
     * a freshly flashed co-processor first-boots its key store and its
     * Matter stack, and on the MGM240P that ran past the 10 s. */
    if (!owner->link().waitReady(HEARTH_FLASH_READY_TIMEOUT_MS)) {
      /* No +MTREADY in time: the attempt failed, disarm so the next
       * spontaneous reboot is still reported. The flasher already
       * succeeded, so no further reset is pending (final re-review
       * Minor): the failure tail does not wait for a marker that cannot
       * come. */
      _lastAttemptFlasherError = false;
      owner->hearthDisarmExpectedReboot();
#ifdef ARDUINO
      Serial.printf("Hearth.update: flash attempt %d of 3 failed: no +MTREADY in %lu ms\n",
                    attempt, (unsigned long)HEARTH_FLASH_READY_TIMEOUT_MS);
#endif
      continue;
    }
    /* B671: waitReady dispatched the +MTREADY through the URC route,
     * setting the re-probe flag; clear it because this reboot was driven
     * by the update (the flash attempt) and the requestor is re-run on
     * success. */
    _coprocReadySeen = false;
    VerQuery vq;
    vq.got = false;
    vq.version[0] = 0;
    hearthCmd("AT+MTVER?", onVerLine2, &vq);
    if (vq.got && strcmp(vq.version, p.version) == 0) {
      _lastAttemptFlasherError = false;  /* the new image runs, no exit reset pending */
      delete staged;
      return 0;  /* the new image is confirmed running */
    }
    /* A boot that reports another version: the attempt failed. The
     * flasher succeeded and the co-processor is up, so no further reset
     * is pending (final re-review Minor): the failure tail does not wait
     * for a marker that cannot come. */
    _lastAttemptFlasherError = false;
#ifdef ARDUINO
    Serial.printf("Hearth.update: flash attempt %d of 3 failed: the co-processor runs %s, the bundle says %s\n",
                  attempt, vq.got ? vq.version : "?", p.version);
#endif
  }
  delete staged;
  return 1;
}

/*
 * The host part of the apply (spec 7: the host applies second, plan Task
 * 6b). Reached two ways: after a successful Hearth part (the caller has
 * already declared the bundle's product version, and it must not be
 * declared twice), or directly for a host-only selection (the declaration
 * is sent here, before the reboot it precedes, per spec 7.5's hard rule).
 *
 * The order:
 *  1. the declaration, host-only only;
 *  2. the state APPLYING_HOST;
 *  3. the hooks must all be present (imageRange, stageImage, reboot):
 *     a null one is a FAILED with HEARTH_UPDATE_ERR_HOST, the staged
 *     bundle kept;
 *  4. save the running sketch as host-prev.bin (its XIP range), stage the
 *     part's range of the staged bundle through PicoOTA (addFile, no
 *     copy), and on either failure the same FAILED with the bundle kept;
 *  5. the state record: phase HOST, attempts 0, the target and the part
 *     indexes the first-boot confirm and the resume (6c) need;
 *  6. the reboot: on the device it does not return, on the host the
 *     test's hook does, and the function returns with it.
 *
 * Final review M6: every failure path below re-declares _declaredVersion
 * (the version in force before this update) before it reports FAILED.
 * After a successful Hearth part the declaration is the NEW product
 * version and the co-processor's NotifyUpdateApplied already went out at
 * its boot, but the manifest is untouched: without the re-declaration the
 * controller would believe the product updated. The next offer is then
 * judged against the old version and selects the host part alone.
 */
void HearthUpdate::hearthApplyHost() {
  HearthUpdateState st;
  memset(&st, 0, sizeof(st));
  if (_fwPart == 0xFF) {
    /* The host-only path: the declaration goes out before the reboot that
     * boots the new image (spec 7.5). The both-parts path already sent it
     * in hearthApply() and must not send it twice. */
    HearthFile *staged = _stage.stagedOpenRead();
    if (staged) {
      HearthFileSource src(*staged);
      HearthBundleInfo info;
      if (HearthBundle::open(src, _cfg.publicKey, info) == HEARTH_BUNDLE_OK) {
        st.targetVersion = info.productVersion;
        snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s",
                 info.productVersionString);
        char cmd[HEARTH_LINE_MAX];
        snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"",
                 (unsigned long)info.productVersion, info.productVersionString);
        hearthCmd(cmd, 0, 0);
      }
      delete staged;
    }
  } else {
    /* Both parts: hearthApply() declared the bundle's product version
     * before the flash; the state record takes the same value. */
    st.targetVersion = _declaredVersion;
    snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s",
             _declaredVersionString);
  }

  _status.state = HEARTH_UPDATE_APPLYING_HOST;
  if (_statusCB) {
    _statusCB(_status);
  }
  if (!_hostHooks.imageRange || !_hostHooks.stageImage || !_hostHooks.reboot) {
    /* A hook is not available here: the apply fails, the state file is
     * gone (there is nothing to resume to) and the staged bundle is kept
     * for a manual retry. M6: the version in force is re-declared first. */
    hearthReDeclareInForce();
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_HOST;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }

  const uint8_t *xipStart = 0;
  uint32_t xipLen = 0;
  if (!_hostHooks.imageRange(&xipStart, &xipLen)
      || !_stage.saveHostPrev(xipStart, xipLen)
      || !stagedHostPart(_hostPart, st)) {
    /* M6: the version in force is re-declared first. */
    hearthReDeclareInForce();
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_HOST;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }
  st.phase = HEARTH_PHASE_HOST;
  st.attempts = 0;
  st.fwPart = (uint8_t)_fwPart;
  st.hostPart = (uint8_t)_hostPart;
  if (!_stage.saveState(st)) {
    /* M6: the version in force is re-declared first. */
    hearthReDeclareInForce();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_HOST;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }
  _hostHooks.reboot();
  /* On the device this does not return. On the host the test's hook does,
   * and so does this function: the state record and the staged bundle are
   * what the next boot's begin() acts on. */
}

/*
 * Stage the host part through the stageImage hook: the part's range inside
 * the staged bundle (its offset plus the container's, the same offset a
 * flasher is handed), from the staged file's path. The state record is
 * filled with the target's version and the part's index, which the
 * first-boot confirm and the resume (6c) need.
 */
bool HearthUpdate::stagedHostPart(int hostPart, HearthUpdateState &st) {
  HearthFile *staged = _stage.stagedOpenRead();
  if (!staged) {
    return false;
  }
  HearthFileSource src(*staged);
  HearthBundleInfo info;
  if (HearthBundle::open(src, _cfg.publicKey, info) != HEARTH_BUNDLE_OK) {
    delete staged;
    return false;
  }
  if (hostPart < 0 || hostPart >= (int)info.partCount) {
    delete staged;
    return false;
  }
  const HearthBundlePart &p = info.parts[hostPart];
  bool ok = _hostHooks.stageImage(_stage.stagedPath(),
                                  info.containerOffset + p.offset, p.length);
  delete staged;
  if (!ok) {
    return false;
  }
  st.targetVersion = info.productVersion;
  snprintf(st.targetVersionString, sizeof(st.targetVersionString), "%s",
           info.productVersionString);
  st.hostPart = (uint8_t)hostPart;
  return true;
}

/*
 * The verification and the verdict (Task 4b2), run from hearthDrain() on
 * +MTOTA:DOWNLOADED once the staged write is ended.
 *
 * The order (plan Task 4, step 2) is: open the staged bundle and verify its
 * signature (a parse or signature failure is a hard refusal with the bundle's
 * own error as the reason); then the apply decision per spec 4, which
 * produces a refusal reason or the selected parts; then verifyPart for each
 * selected part (a digest failure is a hard refusal, reason 2); then the
 * product-version downgrade check (reason 4 unless allowDowngrade); then
 * consent, then AT+MTOTASTAGED.
 *
 * The refusal reasons 1 to 6 are the HearthBundleError values (spec 5.4).
 * A target that does not match the running co-processor is a refusal of the
 * whole bundle with reason 3, the plan's hard refusal, not a skip. A cached
 * variant of unknown selects no Hearth part at all. A bundle where nothing
 * is selected answers =0,4 (the versions all match, nothing to do) and
 * updates the manifest's product version to the bundle's so the provider
 * stops offering it.
 */
void HearthUpdate::hearthVerifyAndVerdict() {
  HearthFile *staged = _stage.stagedOpenRead();
  if (!staged) {
    /* The staged file is gone: a storage failure staging the bundle. */
    hearthRefuse(HEARTH_BUNDLE_ERR_STORAGE);
    return;
  }
  HearthFileSource src(*staged);
  HearthBundleInfo info;
  HearthBundleError e = HearthBundle::open(src, _cfg.publicKey, info);
  if (e != HEARTH_BUNDLE_OK) {
    delete staged;
    hearthRefuse(e);
    return;
  }

  /* The apply decision. _fwPart and _hostPart are indexes into info.parts,
   * 0xFF for the part not selected. */
  int fwPart = 0xFF;
  int hostPart = 0xFF;
  bool unknownVariant = (strcmp(_variant, "unknown") == 0);
  for (int i = 0; i < (int)info.partCount; i++) {
    const HearthBundlePart &p = info.parts[i];
    if (p.type == 2) {
      /* A Hearth part: it refuses the whole bundle when its target does not
       * match the running co-processor (case 6d, the nRF part on a C6). */
      if (strcmp(p.target, _model) != 0) {
        delete staged;
        hearthRefuse(HEARTH_BUNDLE_ERR_TARGET);
        return;
      }
      if (!unknownVariant) {
        int pv = 0;
        if (strcmp(_variant, "wifi") == 0) pv = 1;
        else if (strcmp(_variant, "thread") == 0) pv = 2;
        else if (strcmp(_variant, "combined") == 0) pv = 3;
        if (p.variant != 0 && p.variant != (uint8_t)pv) {
          delete staged;
          hearthRefuse(HEARTH_BUNDLE_ERR_TARGET);
          return;
        }
        if (fwPart == 0xFF && strcmp(p.version, _hearthVersion) != 0) {
          fwPart = i;
        }
      }
    } else if (p.type == 1) {
      /* A host part: it applies when its version differs from the manifest's
       * host version. The manifest is the applied product version's record. */
      if (hostPart == 0xFF) {
        const char *hv = _haveManifest ? _manifest.hostVersion : "";
        if (strcmp(p.version, hv) != 0) {
          hostPart = i;
        }
      }
    }
  }

  /* A gzip host part cannot be applied: the OTA bootloader inflates only a
   * file that begins with the gzip magic at byte 0, but the host part is
   * staged in place inside the bundle, whose byte 0 is the Matter OTA
   * header. Flashed as staged, the image would stay compressed and the
   * host would not boot, so a selected gzip part refuses the bundle. A
   * host part that is not selected (its version equals the manifest's)
   * does not refuse. */
  if (hostPart != 0xFF && (info.parts[hostPart].flags & 1)) {
    delete staged;
    hearthRefuse(HEARTH_BUNDLE_ERR_FORMAT);
    return;
  }

  /* Verify each selected part's digest. */
  if (fwPart != 0xFF && HearthBundle::verifyPart(src, info, fwPart) != HEARTH_BUNDLE_OK) {
    delete staged;
    hearthRefuse(HEARTH_BUNDLE_ERR_DIGEST);
    return;
  }
  if (hostPart != 0xFF && HearthBundle::verifyPart(src, info, hostPart) != HEARTH_BUNDLE_OK) {
    delete staged;
    hearthRefuse(HEARTH_BUNDLE_ERR_DIGEST);
    return;
  }
  delete staged;

  /* The product-version downgrade check: a lower product version is refused
   * with reason 4 unless allowDowngrade (case 7). */
  if (info.productVersion < _effectiveVersion && !_cfg.allowDowngrade) {
    hearthRefuse(HEARTH_BUNDLE_ERR_VERSION);
    return;
  }

  /* Nothing selected: the versions all match, nothing to do. The =0,4
   * answer is the plan's chosen answer for "nothing to do" (the plan's Task
   * 4 rule names 4 as its reason), not a failure of this bundle: record the
   * product version so the provider stops offering it, and leave the state
   * IDLE with no error. */
  if (fwPart == 0xFF && hostPart == 0xFF) {
    HearthManifest m;
    if (_haveManifest) {
      m = _manifest;
    } else {
      memset(&m, 0, sizeof(m));
    }
    m.productVersion = info.productVersion;
    snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", info.productVersionString);
    if (hostPart != 0xFF) {
      snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", info.parts[hostPart].version);
    }
    _stage.saveManifest(m);
    _manifest = m;
    _haveManifest = true;
    _status.state = HEARTH_UPDATE_IDLE;
    _status.error = HEARTH_UPDATE_OK;
    _status.reason = 0;
    _stage.stagedRemove();
    char cmd[HEARTH_LINE_MAX];
    snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=0,4");
    hearthCmd(cmd, 0, 0);
    if (_statusCB) {
      _statusCB(_status);
    }
    return;
  }

  _fwPart = fwPart;
  _hostPart = hostPart;

  /* The consent hook. A refusal leaves the verdict pending (the staged
   * bundle stays) and re-runs the hook on the next drain until it is
   * accepted or the consent window lapses (hearthDrain abandons it). */
  bool consent = true;
  if (_applyRequestCB) {
    consent = _applyRequestCB();
  }
  if (!consent) {
    _consentPending = true;
    _consentRefused = true;
    _consentRefusalMs = (uint32_t)millis();
    return;
  }
  _consentPending = false;
  _consentRefused = false;
  _consentRefusalMs = 0;

  /* The verdict is in: AT+MTOTASTAGED=1 and the state WAIT_APPLY. The apply
   * (the flasher) is Task 6, which answers the +MTOTA:APPLY that follows. */
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=1");
  hearthCmd(cmd, 0, 0);
  /* The verdict for the staged bundle is out (re-review M5): the apply
   * that follows it is the one that may apply. */
  _verdictSent = true;
  _status.state = HEARTH_UPDATE_WAIT_APPLY;
  _status.offeredVersion = info.productVersion;
  if (_statusCB) {
    _statusCB(_status);
  }
}

/*
 * A refusal of the downloaded bundle: the verdict goes out as
 * AT+MTOTASTAGED=0,<reason> with the reason the HearthBundleError value
 * (1 to 6, spec 5.4), the staged bundle is removed and the state is FAILED
 * with HEARTH_UPDATE_ERR_BUNDLE and .reason the same value.
 */
void HearthUpdate::hearthRefuse(HearthBundleError reason) {
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=0,%d", (int)reason);
  hearthCmd(cmd, 0, 0);
  _stage.stagedRemove();
  _status.state = HEARTH_UPDATE_FAILED;
  _status.error = HEARTH_UPDATE_ERR_BUNDLE;
  _status.reason = (int)reason;
  if (_statusCB) {
    _statusCB(_status);
  }
  /* The bundle is refused: the verdict that was pending does not stand,
   * so the re-review M5 flag clears (an apply for a removed bundle would
   * find nothing to flash). */
  _verdictSent = false;
}

void HearthUpdate::end() {
  _status.state = HEARTH_UPDATE_DISABLED;
  _status.percent = 0;
  _havePendingBlock = false;
  _downloadComplete = false;
  _stagedWriteOpen = false;
  _applyPending = false;
  /* end() takes FOTA down: a verdict sent before it cannot apply after
   * it (re-review M5). */
  _verdictSent = false;
  _requestorRetry = false;
  _requestorRetryMs = 0;
  _requestorRetryNow = false;
  /* B671: after end() begin() has not completed, so the re-probe after a
   * co-processor reboot stays off until a begin() completes again. */
  _beginComplete = false;
  _coprocReadySeen = false;
}



/*
 * begin(): bring FOTA up. The order (plan Task 4, step 2) is:
 *   1. the filesystem (or ERR_NO_FS) and the stage (or ERR_NO_FS);
 *   2. load the manifest, effective version = max(baseline, manifest);
 *   3. AT+MTSWVER=<effective>,"<string>";
 *   4. AT+CGMM (cache the model), then the DE625 check: the filesystem's
 *      total size against the model's need, or ERR_NO_SPACE before any OTA
 *      command; an unknown model skips the check and logs;
 *   5. AT+MTVER? (cache the running Hearth version);
 *   6. AT+MTOTA? (cache the variant from the answer);
 *   7. AT+MTOTA=1 (the requestor on; +MTERR:8 means the image has no
 *      requestor: UNAVAILABLE, not an error, no further command).
 * Then, when a state file says a phase was in progress, the resume path
 * (Task 6) takes over: a state with phase FW (6c) resumes the Hearth-part
 * flash, and the state's phase HOST and HOST_CONFIRM run their first-boot
 * branches after the normal sequence reaches IDLE.
 */
bool HearthUpdate::hearthBeginImpl(uint32_t productVersion, const char *versionString, const HearthUpdateConfig &cfg) {
  if (!_fs) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_NO_FS;
    return false;
  }
  _cfg = cfg;
  /* Task 7c-fix4 (F669, B670): when the config carries no pins (the
   * default on a board whose variant defines none, the CPico 2350
   * carriers), the flasher takes them from the owner's
   * hearthCoprocPins() (coprocessorPins() stored them, or the variant
   * macros supply them). Explicit cfg pins still win. Final review M10:
   * the merge is PER PIN: a config pin of -1 takes the owner's value for
   * that pin only, so a config that sets only resetPin still gets the
   * owner's strap (before, the merge ran only when both were -1, and
   * such a config failed ERR_ENTER three times). */
  {
    HearthCoprocPins p = ((HearthClass *)_owner)->hearthCoprocPins();
    if (_cfg.resetPin == -1) {
      _cfg.resetPin = p.reset;
      _cfg.resetActiveLow = p.resetActiveLow;
    }
    if (_cfg.strapPin == -1) {
      _cfg.strapPin = p.strap;
      _cfg.strapActiveLow = p.strapActiveLow;
    }
  }
  /* B671: this begin() has not completed yet; the re-probe after a
   * co-processor reboot runs only once its requestor switch has. */
  _beginComplete = false;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  _status.offeredVersion = 0;
  _status.percent = 0;
  _status.deferredSeconds = 0;
  _status.detail[0] = 0;
  _haveManifest = false;
  _havePendingBlock = false;
  /* B667: a retry armed by an earlier begin() (or an end() in between)
   * does not survive a new begin(): this begin() either reaches IDLE or
   * arms the retry again on its own AT+MTOTA=1. */
  _requestorRetry = false;
  _requestorRetryMs = 0;
  _requestorRetryNow = false;
  /* A new begin() is a new FOTA session: no verdict stands yet (re-review
   * M5), so an apply that arrives before this session's verdict is
   * ignored. */
  _verdictSent = false;
  /* Final review I8: this is the one body that names the heavy drain
   * path, on every build: the drain work becomes reachable (a
   * constructed object's hearthDrain() is one null check and nothing
   * else). The ARDUINO default host hooks and the LittleFS-backed fs go
   * in through the inline begin() wrapper, unless an earlier
   * hearthAttach() or hearthSetHostHooks() already installed their own
   * (the flag keeps that precedence). */
  _drainFn = &HearthUpdate::hearthDrainImpl;

  if (!_fs->begin() || !_stage.begin(*_fs, _cfg.dir)) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_NO_FS;
#ifdef ARDUINO
    Serial.println("Hearth.update: no filesystem, FOTA off");
#endif
    return false;
  }
  _stage.loadManifest(_manifest);
  _haveManifest = _stage.haveManifest();

  /* A state file loaded right after the manifest, before the link
   * commands (plan Task 6b): the host part was staged and the sketch
   * rebooted into it, and this boot confirms it (HOST) or gives up
   * (HOST_CONFIRM, the previous sketch is running again after a failed
   * first boot). Phase FW is 6c's resume, handled after the manifest is
   * loaded and the state read and BEFORE the declaration and the link
   * commands (ruling 1): the co-processor may be in its recovery
   * bootloader with a half-written application, and a resume that waited
   * for the link first would never run then. */
  HearthUpdateState st;
  bool haveState = _stage.loadState(st);

  /* The 6c resume: the host lost power in the middle of flashing the
   * co-processor. It is not an apply request: the consent hook is not
   * called (ruling 8). The effective version is computed here (the
   * manifest was loaded above), because the failure tail re-declares it
   * and the success path recomputes it after the new manifest is written.
   * The result is picked up by the recompute below. */
  uint32_t eff = productVersion;
  const char *effStr = versionString ? versionString : "";
  if (_haveManifest && _manifest.productVersion > eff) {
    eff = _manifest.productVersion;
    effStr = _manifest.productVersionString;
  }
  bool fwResumedFailed = false;

  if (haveState && st.phase == HEARTH_PHASE_FW) {
    if (!_stage.stagedExists()) {
      /* The staged bundle is gone (ruling 2): nothing to resume. Clear
       * the state and go on as if there had been no state file. */
      _stage.clearState();
      haveState = false;
    } else {
      /* The model comes from the staged bundle: AT+CGMM has not run yet,
       * and the verify already required the part's target to equal the
       * co-processor's model, so HearthFlasher::forModel(_model) and the
       * retained-image target check work unchanged (ruling 3). */
      HearthFile *staged = _stage.stagedOpenRead();
      HearthFileSource src(*staged);
      HearthBundleInfo info;
      HearthBundleError e = staged ? HearthBundle::open(src, _cfg.publicKey, info)
                                   : HEARTH_BUNDLE_ERR_STORAGE;
      if (!staged || e != HEARTH_BUNDLE_OK || st.fwPart >= (int)info.partCount) {
        /* The bundle cannot be read (or the state names no part in it):
         * there is nothing to flash. Treat it as the missing staged
         * bundle: the state is cleared and begin() goes on as normal. */
        delete staged;
        _stage.clearState();
        haveState = false;
      } else {
        snprintf(_model, sizeof(_model), "%s", info.parts[st.fwPart].target);
        delete staged;
        /* The version to re-declare on failure (ruling 3): the effective
         * version begin() computed from the baseline and the manifest. */
        _declaredVersion = eff;
        snprintf(_declaredVersionString, sizeof(_declaredVersionString), "%s", effStr);
        /* The declaration before the flash: one try, its answer ignored
         * (a co-processor in its bootloader does not answer; spec 7.5's
         * rule still holds whenever it can hear, ruling 5). It is skipped
         * when going straight to the failure tail (ruling 4). */
        char rcmd[HEARTH_LINE_MAX];
        if (st.attempts >= 3) {
          /* Three attempts already spent (ruling 4): no flash at all,
           * straight to the failure tail. */
          hearthFwFailed();
          /* The failure tail ran (FAILED with
           * HEARTH_UPDATE_ERR_FLASH, the state cleared, the staged bundle
           * kept). begin() still runs its normal sequence (declaring the
           * OLD effective version) so the link and the requestor come up,
           * but it must not overwrite the FAILED state and error with
           * IDLE at its end (ruling 7). */
          haveState = false;
          fwResumedFailed = true;
        } else {
          snprintf(rcmd, sizeof(rcmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)st.targetVersion,
                   st.targetVersionString);
          hearthCmd(rcmd, 0, 0);
          /* The interrupted attempt counts: the resume starts where the
           * state file left off (ruling 4). */
          int rc = hearthApplyFw(st, (int)st.attempts + 1);
          if (rc == 0) {
            /* Success: the new image is confirmed running. The success tail
             * retains it, turns the requestor back on, writes the manifest
             * (fw-only) or stages the host part (6b), and deletes the
             * staged file. The _fwPart and _hostPart members must be set
             * first: hearthApplyHost() reads them to decide its path. */
            _fwPart = st.fwPart;
            _hostPart = st.hostPart;
            HearthFile *staged2 = _stage.stagedOpenRead();
            if (staged2) {
              /* M1: hearthFwSucceeded() does not delete; this resume owns
               * its own handle and deletes it after. */
              hearthFwSucceeded(info, st.fwPart, *staged2, st);
              delete staged2;
            }
            if (st.hostPart == 0xFF) {
              /* Success, fw-only: the success tail has written the new
               * manifest. Recompute the effective version from the updated
               * manifest before the declaration, so the normal sequence
               * declares the NEW version (ruling 7). */
              uint32_t eff2 = productVersion;
              const char *effStr2 = versionString ? versionString : "";
              if (_haveManifest && _manifest.productVersion > eff2) {
                eff2 = _manifest.productVersion;
                effStr2 = _manifest.productVersionString;
              }
              eff = eff2;
              effStr = effStr2;
              /* I5: the effective version and the declaration move
               * together (before, this recompute updated the effective
               * version and the declared pair and only that, and the
               * status field stayed behind). */
              hearthSetEffectiveVersion(eff, effStr);
            } else {
              /* Success with a host part selected: the success tail called
               * hearthApplyHost(), which reboots the host (ruling 7). On
               * the device it does not return; on the host test the
               * reboot hook does, and begin() returns at once, with the
               * state the host apply left (phase HOST saved). */
              return true;
            }
          } else {
            /* Failure: the failure tail re-declares the old version,
             * flashes the retained image if one fits, turns the requestor
             * back on, clears the state, and reports FAILED with
             * HEARTH_UPDATE_ERR_FLASH (the staged bundle is kept).
             * begin() still runs its normal sequence (declaring the OLD
             * effective version) so the link and the requestor come up,
             * but it must not overwrite the FAILED state and error with
             * IDLE at its end (ruling 7). */
            hearthFwFailed();
            haveState = false;
            fwResumedFailed = true;
          }
        }
      }
    }
  }

  _effectiveVersion = eff;
  /* The version begin() is about to declare, kept for the rollback path
   * (spec 7.5 re-declares it before a failed apply gives up). I5: the
   * declaration pair moves with the effective version through this
   * helper, so a resumed apply that fails re-declares what is in force
   * (before it, the recompute below updated the effective version and
   * left the declared pair at the pre-update values). */
  _declaredVersion = eff;
  snprintf(_declaredVersionString, sizeof(_declaredVersionString), "%s", effStr);

  /* The declaration: for the HOST phase it is the state's target (the new
   * product version, even if the sketch's baseline is lower), tried up to
   * three times until one answers OK. For the HOST_CONFIRM phase it is the
   * normal declaration (the effective version), also tried up to three
   * times. For the normal path it is the effective version, tried once. */
  char cmd[HEARTH_LINE_MAX];
  int r0;
  if (haveState && st.phase == HEARTH_PHASE_HOST) {
    /* The declaration is the state's target (the new product version, even
     * if the sketch's baseline is lower), tried up to three times until one
     * answers OK. Three failures: the first-boot failure (re-stage
     * host-prev.bin and reboot, the phase becomes HOST_CONFIRM). The
     * requestor switch below still runs: the first-boot confirm resets the
     * co-processor and re-runs it itself, so the switch here only proves
     * the link is up. */
    r0 = -1;
    for (int i = 0; i < 3; i++) {
      snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"",
               (unsigned long)st.targetVersion, st.targetVersionString);
      r0 = hearthCmd(cmd, 0, 0);
      if (r0 == 0) break;
    }
    if (r0 != 0) {
      return hearthFirstBootHostFail(st);
    }
    /* I5: the declaration is the state's target (the NEW product
     * version), and it went through: that version is in force now, so
     * the re-probe and the rollback declare it, not the pre-update value
     * the recompute left the declared pair at. */
    hearthSetEffectiveVersion(st.targetVersion, st.targetVersionString);
  } else if (haveState && st.phase == HEARTH_PHASE_HOST_CONFIRM) {
    /* The normal declaration (the effective version), tried up to three
     * times. No answer: clearState, FAILED with HEARTH_UPDATE_ERR_LINK,
     * begin() returns false, no stageImage, no reboot (it must not loop).
     * The requestor switch below still runs: begin() returns after it
     * either way, the same commands on the wire as before.
     *
     * The declaration is the effective version, not the state's target
     * (the HOST phase above declares the target): this boot ran the
     * PREVIOUS sketch (the host update did not take, which is why the
     * phase is HOST_CONFIRM), so the version in force is the one this
     * sketch's baseline and the manifest agree on, and that is what the
     * co-processor must be told it claims. Declaring the target here
     * would tell the controller the product updated when it did not. */
    r0 = -1;
    for (int i = 0; i < 3; i++) {
      snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"",
               (unsigned long)_effectiveVersion, _declaredVersionString);
      r0 = hearthCmd(cmd, 0, 0);
      if (r0 == 0) break;
    }
    if (r0 != 0) {
      _stage.clearState();
      _status.state = HEARTH_UPDATE_FAILED;
      _status.error = HEARTH_UPDATE_ERR_LINK;
      _status.reason = 0;
      if (_statusCB) {
        _statusCB(_status);
      }
      return false;
    }
  } else {
    /* The normal declaration: the effective version, tried once. */
    snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)eff, effStr);
    r0 = hearthCmd(cmd, 0, 0);
    if (r0 == 8) {
      /* Firmware 1.2.0 and earlier has no AT+MTSWVER: the parser answers
       * a command it does not know with +MTERR:8 then ERROR (the ordinary
       * unknown command path, AT_MT_SPEC.md). It has no AT+MTOTA either,
       * so FOTA is unavailable, not a broken link: exactly the AT+MTOTA=1
       * -> 8 branch below, UNAVAILABLE, not an error, no further command.
       *
       * B671: the declaration was attempted (this is the boot race, F669:
       * the nRF answered the first command with nothing and AT+MTSWVER
       * with +MTERR:8 while still starting), so begin() has completed
       * enough for the re-probe to re-check it after a reboot. */
      _beginComplete = true;
      _status.state = HEARTH_UPDATE_UNAVAILABLE;
      _status.effectiveVersion = eff;
      if (_statusCB) {
        _statusCB(_status);
      }
      return true;
    }
    if (r0 != 0) {
      _status.state = HEARTH_UPDATE_DISABLED;
      _status.error = HEARTH_UPDATE_ERR_LINK;
      return false;
    }
  }

  ModelQuery mq;
  mq.got = false;
  mq.model[0] = 0;
  int r1 = hearthCmd("AT+CGMM", onModelLine, &mq);
  if (r1 != 0 || !mq.got) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
  }
  snprintf(_model, sizeof(_model), "%s", mq.model);
  uint32_t need = modelFsNeed(_model);
  if (need != 0 && _fs->totalBytes() < need) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_NO_SPACE;
#ifdef ARDUINO
    Serial.printf("Hearth.update: filesystem %lu B smaller than the %s need of %lu B, FOTA off\n",
                  (unsigned long)_fs->totalBytes(), _model, (unsigned long)need);
#endif
    return false;
  }
  if (need == 0) {
#ifdef ARDUINO
    Serial.printf("Hearth.update: model %s not in the DE625 need table, skipping the size check\n", _model);
#endif
  }

  VerQuery vq;
  vq.got = false;
  vq.version[0] = 0;
  int r2 = hearthCmd("AT+MTVER?", onVerLine2, &vq);
  if (r2 != 0 || !vq.got) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
  }
  snprintf(_hearthVersion, sizeof(_hearthVersion), "%s", vq.version);
  snprintf(_status.hearthVersion, sizeof(_status.hearthVersion), "%s", vq.version);

  OtaQuery q;
  q.got = false;
  q.mode = -1;
  q.state[0] = 0;
  q.percent = 0;
  q.variant[0] = 0;
  int r3 = hearthCmd("AT+MTOTA?", onOtaQueryLine, &q);
  if (r3 != 0 || !q.got) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
  }
  snprintf(_variant, sizeof(_variant), "%s", q.variant);

  int rc = hearthCmd("AT+MTOTA=1", 0, 0);
  if (rc == 8) {
    /* The image has no requestor: FOTA unavailable, not an error. No
     * further command (spec 3.31).
     *
     * B667: an AT+MTSWVER that answered OK proves the firmware has FOTA,
     * so this 8 is the requestor not being wired yet (the nRF54L15 wires
     * its requestor only once the device is commissioned), not the
     * firmware without FOTA that the AT+MTSWVER -> 8 branch above settled
     * for good. hearthDrain() retries this once the commissioning is
     * noted or every 30 s until it goes. */
    _status.state = HEARTH_UPDATE_UNAVAILABLE;
    _status.effectiveVersion = eff;
    _requestorRetry = true;
    _requestorRetryMs = (uint32_t)millis();
    if (_statusCB) {
      _statusCB(_status);
    }
    _beginComplete = true;
    return true;
  }
  if (rc != 0) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
  }

  if (!fwResumedFailed) {
    /* The 6c resume's failure tail leaves the state FAILED with
     * HEARTH_UPDATE_ERR_FLASH (ruling 7): the normal sequence came up so
     * the link and the requestor do, but the FAILED outcome must stay
     * readable, so the IDLE it would set here is skipped. */
    _status.state = HEARTH_UPDATE_IDLE;
    _status.effectiveVersion = eff;
  }
  /* Final review I7: the running Hearth version begin() just read, kept
   * for the rollback check in hearthFwFailed() (its own AT+MTVER? answer
   * is compared against it: the same version means nothing was written). */
  snprintf(_preApplyHearthVersion, sizeof(_preApplyHearthVersion), "%s", _hearthVersion);
  /* B671: the requestor switch has run (it is on, or it is refused and
   * the retry is armed), so begin() has completed and the re-probe after
   * a co-processor reboot may run. */
  _beginComplete = true;
  if (_statusCB) {
    _statusCB(_status);
  }
  /* The first-boot confirm (plan Task 6b): the declaration already ran
   * (above), the normal sequence just reached IDLE, and now the phase's
   * own work runs. */
  if (haveState && st.phase == HEARTH_PHASE_HOST) {
    return hearthFirstBootHost(st);
  }
  if (haveState && st.phase == HEARTH_PHASE_HOST_CONFIRM) {
    return hearthFirstBootHostConfirm(st);
  }
  return true;
}

/*
 * The first-boot failure (plan Task 6b): the new sketch cannot reach
 * Hearth (the declaration timed out three times in begin()). host-prev.bin
 * is re-staged whole and the sketch reboots back to it, with the phase set
 * to HOST_CONFIRM so the second boot does not loop. No host-prev.bin, or a
 * null or failing stageImage hook: clearState, FAILED with
 * HEARTH_UPDATE_ERR_HOST, return false, no reboot.
 */
bool HearthUpdate::hearthFirstBootHostFail(const HearthUpdateState &stIn) {
  HearthUpdateState st = stIn;
  HearthFile *prev = _stage.hostPrevOpen();
  uint32_t size = prev ? prev->size() : 0;
  delete prev;
  if (!size || !_hostHooks.stageImage) {
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_HOST;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
    return false;  /* no host-prev.bin, or no hook: no reboot, no loop */
  }
  if (!_hostHooks.stageImage(_stage.hostPrevPath(), 0, size)) {
    _stage.clearState();
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_HOST;
    _status.reason = 0;
    if (_statusCB) {
      _statusCB(_status);
    }
    return false;
  }
  st.phase = HEARTH_PHASE_HOST_CONFIRM;
  _stage.saveState(st);
  _hostHooks.reboot();
  _status.state = HEARTH_UPDATE_FAILED;
  _status.error = HEARTH_UPDATE_ERR_HOST;
  _status.reason = 0;
  if (_statusCB) {
    _statusCB(_status);
  }
  return false;
}

/*
 * The first-boot confirm (plan Task 6b, spec 7.5): the new sketch is
 * running, the declaration already answered OK (in begin()), and the
 * normal sequence just reached IDLE. The manifest is written with the
 * state's target and the host part's version read from the staged bundle
 * (the manifest after a host apply is written here, not before the
 * reboot, so a failed host boot leaves the old one), the state and the
 * staged bundle are gone, and the co-processor is reset on its reset line
 * (never AT+MTEPAPPLY, which the firmware answers with an error when no
 * AT+MTEP session is open and would rewrite the composition) so its
 * requestor re-initialises with the declared version and sends its
 * NotifyUpdateApplied.
 */
bool HearthUpdate::hearthFirstBootHost(const HearthUpdateState &stIn) {
  HearthUpdateState st = stIn;
  /* Confirmed: the manifest takes the state's target and the host part's
   * version from the staged bundle, "" when it cannot be read. */
  char hostVer[33];
  hostVer[0] = 0;
  {
    HearthFile *staged = _stage.stagedOpenRead();
    if (staged) {
      HearthFileSource src(*staged);
      HearthBundleInfo info;
      if (HearthBundle::open(src, _cfg.publicKey, info) == HEARTH_BUNDLE_OK
          && st.hostPart < (int)info.partCount) {
        snprintf(hostVer, sizeof(hostVer), "%s", info.parts[st.hostPart].version);
      }
      delete staged;
    }
  }
  HearthManifest m;
  if (_haveManifest) {
    m = _manifest;
  } else {
    memset(&m, 0, sizeof(m));
  }
  m.productVersion = st.targetVersion;
  snprintf(m.productVersionString, sizeof(m.productVersionString), "%s", st.targetVersionString);
  snprintf(m.hostVersion, sizeof(m.hostVersion), "%s", hostVer);
  _stage.saveManifest(m);
  _manifest = m;
  _haveManifest = true;
  _stage.clearState();
  _stage.stagedRemove();
  /* I5: the confirmed product version is in force now, with its string:
   * the re-probe and any later rollback declare it, not the pre-update
   * value the recompute left the declared pair at. */
  hearthSetEffectiveVersion(st.targetVersion, st.targetVersionString);
  /* The co-processor reset: the reset line, not AT+MTEPAPPLY. A null or
   * false hook skips it with a log line (the requestor's NotifyUpdateApplied
   * then waits for the co-processor's next boot). */
  HearthCoprocPins pins;
  pins.reset = _cfg.resetPin;
  pins.resetActiveLow = _cfg.resetActiveLow;
  pins.strap = _cfg.strapPin;
  pins.strapActiveLow = _cfg.strapActiveLow;
  if (_hostHooks.coprocReset) {
    hearthSettleAfterCommissioning();
    ((HearthClass *)_owner)->hearthArmExpectedReboot();
    if (_hostHooks.coprocReset(pins)) {
      if (((HearthClass *)_owner)->link().waitReady(HEARTH_READY_TIMEOUT_MS)) {
        /* B671: waitReady dispatched the +MTREADY through the URC route,
         * setting the re-probe flag. Clear it before the AT+MTOTA=1 below
         * (whose own drain would otherwise see the flag set and fire the
         * re-probe): this reboot was driven by the update (the first-boot
         * confirm) and the requestor is re-run just below. */
        _coprocReadySeen = false;
        hearthCmd("AT+MTOTA=1", 0, 0);
      } else {
        _coprocReadySeen = false;
        ((HearthClass *)_owner)->hearthDisarmExpectedReboot();
      }
    } else {
      ((HearthClass *)_owner)->hearthDisarmExpectedReboot();
#ifdef ARDUINO
      Serial.println("Hearth.update: no co-processor reset line, the reset is skipped");
#endif
    }
  } else {
#ifdef ARDUINO
    Serial.println("Hearth.update: no co-processor reset hook, the reset is skipped");
#endif
  }
  _status.state = HEARTH_UPDATE_IDLE;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  if (_statusCB) {
    _statusCB(_status);
  }
  return true;
}

/*
 * The boot after a failed first boot (plan Task 6b): the previous sketch
 * is running again (the host update did not take). The normal declaration
 * (the effective version) is tried up to three times. It answers: begin()
 * completes, the state is cleared and the update reports FAILED with
 * HEARTH_UPDATE_ERR_HOST (the host update did not take); the manifest is
 * untouched and the staged bundle kept. No answer: FAILED with
 * HEARTH_UPDATE_ERR_LINK and begin() returns false, with no stageImage and
 * no reboot (it must not loop).
 */
bool HearthUpdate::hearthFirstBootHostConfirm(const HearthUpdateState &st) {
  /* The declaration already ran in begin() (the normal declaration, the
   * effective version, up to three times) and the normal sequence reached
   * IDLE: this function only reports the outcome. The state is cleared and
   * the update is FAILED with HEARTH_UPDATE_ERR_HOST (the host update did
   * not take); the manifest is untouched and the staged bundle kept. No
   * stageImage, no reboot: the loop must stop.
   *
   * Final review M6 does not add a wire call here, though its rule reads
   * as if it did: begin() has already sent the same declaration, the
   * effective version (the old one, from the manifest: the recompute of
   * the effective version in begin() is the same value this re-declaration
   * would carry), so a second AT+MTSWVER here is a duplicate of a command
   * the link just answered OK, not a correction of a wrong one (the
   * hearthApplyHost() failures are the case M6 fixes: their declaration is
   * the NEW product version, sent in hearthApply() before the host part's
   * own failure is known). */
  (void)st;
  _stage.clearState();
  _status.state = HEARTH_UPDATE_FAILED;
  _status.error = HEARTH_UPDATE_ERR_HOST;
  _status.reason = 0;
  if (_statusCB) {
    _statusCB(_status);
  }
  return true;
}

/* B632: a commissioning complete (+MTEVT:3) was dispatched. Recording a
 * time and a flag, no link call, so it is safe from the URC callback
 * hearthDispatchEvt() runs in. */
void HearthUpdate::hearthNoteCommissioned() {
  _commissioned = true;
  _commissionedMs = (uint32_t)millis();
  /* B667: the requestor retry may have been armed by begin() (the
   * nRF54L15 wires its requestor on commissioning): record that it may
   * go now. Recording only, the AT+MTOTA=1 goes out on the next
   * hearthDrain(). */
  if (_requestorRetry) {
    _requestorRetryNow = true;
  }
}

/* B632: the MG24's key store saves its key map two seconds after a write,
 * so a reset inside that window after a commissioning loses the new
 * fabric. The plan's rule: the library waits at least 2 s after a
 * commissioning completes before any reset it drives. Called immediately
 * before every reset the update drives: each flash attempt in
 * hearthApplyFw(), the retained rollback flash in hearthFwFailed() and
 * the first-boot co-processor reset in hearthFirstBootHost(). The apply
 * blocks anyway, so the remainder is a plain delay(). Once the 2000 ms
 * have run the flag clears: an old commissioning never delays a reset
 * again. */
void HearthUpdate::hearthSettleAfterCommissioning() {
  if (!_commissioned) {
    return;
  }
  uint32_t elapsed = millis() - _commissionedMs;
  if (elapsed >= 2000) {
    _commissioned = false;
    return;
  }
  delay(2000 - elapsed);
  _commissioned = false;
}

bool HearthUpdate::checkNow() {
  if (_status.state == HEARTH_UPDATE_DISABLED || _status.state == HEARTH_UPDATE_UNAVAILABLE) {
    return false;
  }
  /* AT+MTOTA=2: query the providers now. +MTERR:12 means disabled, busy or
   * no provider: false, and nothing changes. */
  return hearthCmd("AT+MTOTA=2", 0, 0) == 0;
}

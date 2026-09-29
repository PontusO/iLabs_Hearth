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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

HearthUpdate::HearthUpdate()
  : _owner(0),
    _fs(0),
    _flasher(0),
    _haveManifest(false),
    _pendingSeq(0),
    _pendingLen(0),
    _havePendingBlock(false),
    _lastState(HEARTH_UPDATE_DISABLED),
    _downloadComplete(false),
    _stagedWriteOpen(false),
    _draining(false),
    _applyRequestCB(0),
    _statusCB(0),
    _baudChangerCB(0),
    _fwPart(0xFF),
    _hostPart(0xFF),
    _consentPending(false),
    _consentRefusalMs(0),
    _baud(HEARTH_LINK_BAUD),
    _baudWantedDownload(false) {
  _status.state = HEARTH_UPDATE_DISABLED;
  _status.percent = 0;
  _status.offeredVersion = 0;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  _status.effectiveVersion = 0;
  _status.hearthVersion[0] = 0;
  _status.detail[0] = 0;
  _status.deferredSeconds = 0;
  _model[0] = 0;
  _variant[0] = 0;
  _hearthVersion[0] = 0;
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
     * flag the last one set, so a later DOWNLOADED ends the write again. */
    char *end;
    uint32_t seq = (uint32_t)strtoul(comma + 1, &end, 10);
    uint32_t len = (uint32_t)strtoul(end + 1, 0, 10);
    _pendingSeq = seq;
    _pendingLen = len;
    _havePendingBlock = true;
    _downloadComplete = false;
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
    /* The requestor asked to apply; 4b/6 answers it with the flasher. */
    _status.state = HEARTH_UPDATE_APPLYING_FW;
  } else if (strcmp(state, "DEFERRED") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    _status.deferredSeconds = comma ? (uint32_t)strtoul(comma + 1, 0, 10) : 0;
  } else if (strcmp(state, "DISCONTINUED") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    _status.percent = 0;
  } else if (strcmp(state, "ERROR") == 0) {
    /* The co-processor ended the transfer (case 6b): the state is FAILED
     * with HEARTH_UPDATE_ERR_COPROC and the detail, and the link baud goes
     * back to the default. The error and detail stay readable in status()
     * until the next transfer starts (a BLOCK line). */
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_COPROC;
    _baudWantedDownload = false;
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
 */
void HearthUpdate::hearthDrain() {
  if (_draining) {
    return;
  }
  DrainGuard guard(_draining);
  if (_status.state == HEARTH_UPDATE_DISABLED || _status.state == HEARTH_UPDATE_UNAVAILABLE) {
    return;
  }
  /* The download-time baud switch (case 10). A state line only records the
   * rate it needs (_baudWantedDownload); the AT+MTBAUD itself goes out here,
   * because a URC callback may not call the link. The switch is sent only
   * when the needed rate differs from the one last set, so a repeat state
   * line is a no-op and a switch back is seen. */
  uint32_t wantBaud = _baudWantedDownload ? _cfg.downloadBaud : HEARTH_LINK_BAUD;
  if (wantBaud != _baud) {
    hearthSetBaud(wantBaud);
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
    if (_consentRefusalMs != 0
        && (millis() - _consentRefusalMs) >= _cfg.consentWindowMs) {
      hearthAbandon();
      return;
    }
    bool consent = _applyRequestCB ? _applyRequestCB() : true;
    if (!consent) {
      return;  /* still refused, wait for the next drain */
    }
    _consentPending = false;
    _consentRefusalMs = 0;
    char cmd[HEARTH_LINE_MAX];
    snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=1");
    hearthCmd(cmd, 0, 0);
    _status.state = HEARTH_UPDATE_WAIT_APPLY;
    if (_statusCB) {
      _statusCB(_status);
    }
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
  /* Before the first block of a transfer the stage opens its write. */
  if (!_stagedWriteOpen) {
    if (!_stage.stagedBeginWrite()) {
      hearthAbortPull();
      return;
    }
    _stagedWriteOpen = true;
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
 * HEARTH_UPDATE_ERR_LINK.
 */
void HearthUpdate::hearthAbortPull() {
  _havePendingBlock = false;
  _downloadComplete = false;
  _stagedWriteOpen = false;
  hearthCmd("AT+MTOTA=0", 0, 0);
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
 * On the device the link's own port brings the UART to the rate; in the host
 * build (HEARTH_SERIAL_PORT undefined) there is no UART to re-clock, so the
 * AT+MTBAUD goes out and the test hook (hearthSetBaudChanger) records the
 * rate the sketch's own port would use.
 */
void HearthUpdate::hearthSetBaud(uint32_t baud) {
  _baud = baud;
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTBAUD=%lu", (unsigned long)baud);
  hearthCmd(cmd, 0, 0);
  if (_baudChangerCB) {
    _baudChangerCB(baud);
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
  _consentRefusalMs = 0;
  _stage.stagedEndWrite();
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

  /* Nothing selected: the versions all match, nothing to do. Answer =0,4
   * and record the product version so the provider stops offering it. */
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
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_BUNDLE;
    _status.reason = HEARTH_BUNDLE_ERR_VERSION;
    _stage.stagedRemove();
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
    _consentRefusalMs = (uint32_t)millis();
    return;
  }
  _consentPending = false;
  _consentRefusalMs = 0;

  /* The verdict is in: AT+MTOTASTAGED=1 and the state WAIT_APPLY. The apply
   * (the flasher) is Task 6, which answers the +MTOTA:APPLY that follows. */
  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTOTASTAGED=1");
  hearthCmd(cmd, 0, 0);
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
}

void HearthUpdate::end() {
  _status.state = HEARTH_UPDATE_DISABLED;
  _status.percent = 0;
  _havePendingBlock = false;
  _downloadComplete = false;
  _stagedWriteOpen = false;
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
 * (Task 6) takes over; for this half that hand-over is not written yet.
 */
bool HearthUpdate::begin(uint32_t productVersion, const char *versionString, const HearthUpdateConfig &cfg) {
  /* The tests inject their fs with hearthAttach(); on the device the
   * LittleFS-backed fs is the default (hearthLittleFs() is ARDUINO-only). */
#ifdef ARDUINO
  if (!_fs) {
    _fs = &hearthLittleFs();
  }
#endif
  if (!_fs) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_NO_FS;
    return false;
  }
  _cfg = cfg;
  _status.error = HEARTH_UPDATE_OK;
  _status.reason = 0;
  _status.offeredVersion = 0;
  _status.percent = 0;
  _status.deferredSeconds = 0;
  _status.detail[0] = 0;
  _haveManifest = false;
  _havePendingBlock = false;

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

  uint32_t eff = productVersion;
  const char *effStr = versionString ? versionString : "";
  if (_haveManifest && _manifest.productVersion > eff) {
    eff = _manifest.productVersion;
    effStr = _manifest.productVersionString;
  }
  _effectiveVersion = eff;

  char cmd[HEARTH_LINE_MAX];
  snprintf(cmd, sizeof(cmd), "AT+MTSWVER=%lu,\"%s\"", (unsigned long)eff, effStr);
  int r0 = hearthCmd(cmd, 0, 0);
  if (r0 != 0) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
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
     * further command (spec 3.31). */
    _status.state = HEARTH_UPDATE_UNAVAILABLE;
    _status.effectiveVersion = eff;
    if (_statusCB) {
      _statusCB(_status);
    }
    return true;
  }
  if (rc != 0) {
    _status.state = HEARTH_UPDATE_DISABLED;
    _status.error = HEARTH_UPDATE_ERR_LINK;
    return false;
  }

  _status.state = HEARTH_UPDATE_IDLE;
  _status.effectiveVersion = eff;
  /* The resume path (a state file naming an in-progress phase) is Task 6. */
  if (_statusCB) {
    _statusCB(_status);
  }
  return true;
}

bool HearthUpdate::checkNow() {
  if (_status.state == HEARTH_UPDATE_DISABLED || _status.state == HEARTH_UPDATE_UNAVAILABLE) {
    return false;
  }
  /* AT+MTOTA=2: query the providers now. +MTERR:12 means disabled, busy or
   * no provider: false, and nothing changes. */
  return hearthCmd("AT+MTOTA=2", 0, 0) == 0;
}

/*
 * HearthUpdate.cpp: the link route, begin() and the URC parsing (plan
 * Task 4, first half). The download loop, the states' actions, the verdict,
 * the consent and the baud switch are the second half (Task 4b);
 * hearthDrain() exists and is called from HearthClass::poll() but returns
 * for now, and the bundle verdict is the part 4b fills in.
 */
/*
 * HearthGlobal.h, not Hearth.h (see Hearth.cpp's own comment there): this
 * file calls through the Hearth object, so it must see the declaration the
 * library's own translation units agree on, the one with the update member.
 * Hearth.h and HearthGlobal.h declare the same class two different ways
 * (with and without the member) only until the include paths line up;
 * compiling this file from Hearth.h would change its notion of the class's
 * size and layout from Hearth.cpp's, the ODR violation that misroutes a
 * call through the wrong member offset.
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
    _applyRequestCB(0),
    _statusCB(0) {
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

void HearthUpdate::hearthAttach(HearthFs &fs) { _fs = &fs; }

void HearthUpdate::hearthSetOwner(void *owner) { _owner = owner; }

void HearthUpdate::hearthSetFlasher(HearthFlasher *f) { _flasher = f; }

void HearthUpdate::onApplyRequest(bool (*cb)()) { _applyRequestCB = cb; }

void HearthUpdate::onStatus(void (*cb)(const HearthUpdateStatus &)) { _statusCB = cb; }

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
  const char *comma = strchr(rest, ',');
  char state[24];
  size_t n = comma ? (size_t)(comma - rest) : strlen(rest);
  if (n >= sizeof(state)) {
    n = sizeof(state) - 1;
  }
  memcpy(state, rest, n);
  state[n] = 0;

  if (strcmp(state, "BLOCK") == 0) {
    /* The pending-block announcement: +MTOTA:BLOCK,<seq>,<len>. */
    char *end;
    uint32_t seq = (uint32_t)strtoul(comma + 1, &end, 10);
    uint32_t len = (uint32_t)strtoul(end + 1, 0, 10);
    _pendingSeq = seq;
    _pendingLen = len;
    _havePendingBlock = true;
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
    /* Leaves the state where it is (IDLE); the callback still fires. */
  } else if (strcmp(state, "AVAILABLE") == 0) {
    _status.state = HEARTH_UPDATE_IDLE;
    if (comma) {
      _status.offeredVersion = (uint32_t)strtoul(comma + 1, 0, 10);
    }
  } else if (strcmp(state, "DOWNLOADING") == 0) {
    _status.state = HEARTH_UPDATE_DOWNLOADING;
    if (comma) {
      _status.percent = (uint8_t)atoi(comma + 1);
    }
  } else if (strcmp(state, "DOWNLOADED") == 0) {
    _status.state = HEARTH_UPDATE_VERIFYING;
    _status.percent = 100;
    _havePendingBlock = false;
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
    _status.state = HEARTH_UPDATE_FAILED;
    _status.error = HEARTH_UPDATE_ERR_COPROC;
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
 * Called from HearthClass::poll() after the link's own drains. The second
 * half (Task 4b) fills in the pull loop, the verdict and the apply; for
 * this half it returns.
 */
void HearthUpdate::hearthDrain() {
  /* Task 4b: pull the pending block (AT+MTOTAGET/ACK), verify and give the
   * verdict on DOWNLOADED, run the apply on APPLY. */
}

void HearthUpdate::end() {
  _status.state = HEARTH_UPDATE_DISABLED;
  _status.percent = 0;
  _havePendingBlock = false;
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

/*
 * HearthUpdate: the host side of the co-processor's FOTA (plan Task 4,
 * spec 2026-09-07-fota-design section 9.1). It stands on the Hearth link
 * (AT+MT, AT_MT_SPEC.md S3.31-S3.33), on a HearthFs for its files (the
 * update stage, HearthUpdateStage.h) and, from Task 5 on, on a flasher for
 * the apply.
 *
 * FOTA is off unless begin() is called: a constructed object is disabled,
 * sends nothing and answers no URC (the URC route in Hearth.cpp is
 * constructed with it disabled, so a sketch that never calls begin()
 * changes nothing). begin() brings the filesystem up, declares the
 * effective product version, reads the running co-processor's model and
 * version, turns the requestor on and then runs on the +MTOTA: URCs: a
 * URC is a line whose first field after "+MTOTA:" is an upper-case state
 * token, while the AT+MTOTA? answer's first field is the mode digit (0, 1
 * or 2); HearthLink::isAsyncURC() makes the distinction, the colon keeping
 * the +MTOTABLK: command responses out of the URC set too (spec 3.31).
 *
 * hearthOnOtaLine() only parses and sets fields; it never touches the link
 * (the hearthDrain* pattern in Hearth::poll() is the only place the update
 * reaches the wire).
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "HearthFs.h"
#include "HearthUpdateStage.h"
#include "HearthBundle.h"
#include "HearthDevKey.h"

/* The co-processor's reset and strap pins, from the board variant where it
 * defines them (the Challenger 2350 does: PIN_ESP_RST and PIN_ESP_MODE) and
 * -1 everywhere else. Each is overridable with a -D before this header is
 * first included; a flasher handed a pin of -1 refuses with
 * HEARTH_FLASH_ERR_ENTER (Task 5). */
#if defined(PIN_ESP_RST)
#define HEARTH_DEFAULT_RESET_PIN PIN_ESP_RST
#else
#define HEARTH_DEFAULT_RESET_PIN (-1)
#endif
#if defined(PIN_ESP_MODE)
#define HEARTH_DEFAULT_STRAP_PIN PIN_ESP_MODE
#else
#define HEARTH_DEFAULT_STRAP_PIN (-1)
#endif

enum HearthUpdateStateEnum {
  HEARTH_UPDATE_UNAVAILABLE,  /* the image has no requestor: +MTERR:8 to AT+MTOTA=1 */
  HEARTH_UPDATE_DISABLED,     /* begin() was never called (or end() was called) */
  HEARTH_UPDATE_IDLE,         /* requestor on, nothing in flight */
  HEARTH_UPDATE_DOWNLOADING,  /* pulling blocks */
  HEARTH_UPDATE_VERIFYING,    /* the bundle is checked and judged */
  HEARTH_UPDATE_WAIT_APPLY,   /* staged; waiting for the apply request */
  HEARTH_UPDATE_APPLYING_FW,  /* flashing the Hearth image */
  HEARTH_UPDATE_APPLYING_HOST,/* flashing the host image */
  HEARTH_UPDATE_FAILED        /* the last attempt ended in error */
};

enum HearthUpdateErr {
  HEARTH_UPDATE_OK = 0,
  HEARTH_UPDATE_ERR_NO_FS,      /* the filesystem would not begin */
  HEARTH_UPDATE_ERR_UNSUPPORTED,
  HEARTH_UPDATE_ERR_LINK,
  HEARTH_UPDATE_ERR_BUNDLE,     /* + the HearthBundleError in .reason */
  HEARTH_UPDATE_ERR_FLASH,
  HEARTH_UPDATE_ERR_HOST,
  HEARTH_UPDATE_ERR_NO_SPACE,   /* DE625: the filesystem is smaller than the port's need */
  HEARTH_UPDATE_ERR_COPROC      /* the co-processor ended the transfer: +MTOTA:ERROR,<detail> */
};

struct HearthUpdateStatus {
  HearthUpdateStateEnum state;
  uint8_t percent;
  uint32_t offeredVersion;
  HearthUpdateErr error;
  int reason;
  uint32_t effectiveVersion;
  char hearthVersion[33];
  char detail[16];          /* the co-processor's ERROR detail: abort, cancelled, session, failed, noprovider */
  uint32_t deferredSeconds; /* the provider's AwaitNextAction delay, 0 otherwise */
};

struct HearthUpdateConfig {
  const uint8_t *publicKey = HEARTH_DEV_PUBKEY;
  bool allowDowngrade = false;
  uint32_t downloadBaud = 921600;
  const char *dir = "/hearth";
  int resetPin = HEARTH_DEFAULT_RESET_PIN;
  bool resetActiveLow = true;   /* PIN_ESP_RST or -1 */
  int strapPin = HEARTH_DEFAULT_STRAP_PIN;
  bool strapActiveLow = true;   /* PIN_ESP_MODE or -1 */
  uint32_t consentWindowMs = 600000; /* a refused apply is retried for this long, then abandoned */
};

class HearthFlasher;  /* Task 5; forward declared so Task 4 links without it */

class HearthUpdate {
public:
  HearthUpdate();

  /* Bring FOTA up: the filesystem (or HEARTH_UPDATE_ERR_NO_SPACE /
   * HEARTH_UPDATE_ERR_NO_FS), the stage, the effective version, the
   * declaration and the requestor. See begin()'s own comment in the
   * implementation for the order. */
  bool begin(uint32_t productVersion, const char *versionString, const HearthUpdateConfig &cfg = HearthUpdateConfig());
  void end();
  bool checkNow();
  bool available() const;
  HearthUpdateStatus status() const;

  /* The apply hook: the verdict is in, the bundle is staged, flash it? */
  void onApplyRequest(bool (*cb)());
  /* Fired on every state change (and on the QUERYING state, which does not
   * change the state). */
  void onStatus(void (*cb)(const HearthUpdateStatus &));

  /* library-internal */
  void hearthOnOtaLine(const char *rest);    /* from HearthClass::hearthOnURCLine; no link calls */
  void hearthDrain();                        /* from HearthClass::poll() */
  void hearthAttach(HearthFs &fs);           /* tests inject the fake fs; target uses hearthLittleFs() */
  void hearthSetFlasher(HearthFlasher *f);   /* Task 5/6 */
  HearthUpdateStage &stage();                /* the stage, over the attached fs */
  /* The HearthClass this object is a member of; set by HearthClass's
   * constructor. The update's wire commands go through this object's
   * hearthCommand(), not the global Hearth, so a test that creates a
   * local HearthClass and begins its own update talks to the link the
   * test scripted, not the global's unstarted one. */
  void hearthSetOwner(void *owner);

private:
  int hearthCmd(const char *cmd, HearthLink::LineCb onLine, void *arg);
  void *_owner;
  HearthFs *_fs;
  HearthUpdateConfig _cfg;
  HearthFlasher *_flasher;

  HearthUpdateStatus _status;

  HearthUpdateStage _stage;
  HearthManifest _manifest;
  bool _haveManifest;

  char _model[33];
  char _variant[16];
  char _hearthVersion[33];
  uint32_t _effectiveVersion;

  /* Parsed by hearthOnOtaLine(), acted on by hearthDrain(): the pending
   * block, the last state line, the offered version and progress. */
  uint32_t _pendingSeq;
  uint32_t _pendingLen;
  bool _havePendingBlock;
  HearthUpdateStateEnum _lastState;

  bool (*_applyRequestCB)();
  void (*_statusCB)(const HearthUpdateStatus &);
};

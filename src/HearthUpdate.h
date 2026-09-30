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
#ifdef ARDUINO
/* The device defaults below are inline in this header, and this is where
 * their includes and externs live too (final review I8): every reference
 * to LittleFS and PicoOTA sits in header-inline code that only a sketch
 * calling Hearth.update.begin() instantiates, so a sketch that never
 * begins the update links none of either archive. XIP_BASE comes with the
 * pico headers the core pulls in, as it does in PicoOTA.h's own
 * addFile(). */
#include "HearthFsLittle.h"
#include <PicoOTA.h>
extern "C" uint8_t __flash_binary_start, __flash_binary_end;
#endif
#include "HearthUpdateStage.h"
#include "HearthBundle.h"
#include "HearthDevKey.h"
#include "HearthFlasher.h"  /* HearthCoprocPins, for the host hooks */

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
  /* The pins come from the board variant where it defines them (the
   * Challenger 2350: PIN_ESP_RST and PIN_ESP_MODE), else from
   * Hearth.coprocessorPins(), with these two fields as an override: a
   * pin of -1 here takes the pin in force on the board (begin() merges
   * per pin, final review M10). */
  int resetPin = HEARTH_DEFAULT_RESET_PIN;
  bool resetActiveLow = true;   /* PIN_ESP_RST, or -1 and the board's */
  int strapPin = HEARTH_DEFAULT_STRAP_PIN;
  bool strapActiveLow = true;   /* PIN_ESP_MODE, or -1 and the board's */
  uint32_t consentWindowMs = 600000; /* a refused apply is retried for this long, then abandoned */
};

class HearthFlasher;  /* Task 5; forward declared so Task 4 links without it */

/* The host-side hooks for the host part of the apply (spec 7, plan Task 6b).
 * Each is a function pointer that takes no context: the library-internal
 * hearthSetHostHooks() installs a set of them, and the inline begin()
 * wrapper (final review I8) installs the defaults unless an install
 * already went through: a sketch that never calls begin() links none of
 * the PicoOTA and linker symbols the defaults name, and a test that
 * installed its own first is left alone (the wrapper installs the
 * defaults through hearthSetHostHooks() only when _hostHooksInstalled
 * is false).
 * On the device (ARDUINO) the defaults are the real ones:
 * imageRange the running sketch's XIP range (from the linker's
 * __flash_binary_start / __flash_binary_end), stageImage the PicoOTA
 * addFile of the named range plus its commit, reboot rp2040.reboot() and
 * coprocReset the co-processor's reset line pulsed for 100 ms. On the host
 * every default is null: a null hook means "not available here", the host
 * apply then fails with HEARTH_UPDATE_ERR_HOST, and a null or false
 * coprocReset skips the reset with a log line (the co-processor's next boot
 * answers the NotifyUpdateApplied on its own). */
struct HearthHostHooks {
  bool (*imageRange)(const uint8_t **start, uint32_t *len);   /* the running sketch in XIP */
  bool (*stageImage)(const char *path, uint32_t off, uint32_t len); /* PicoOTA: addFile + commit */
  void (*reboot)();                                           /* rp2040.reboot() */
  bool (*coprocReset)(const HearthCoprocPins &pins);          /* pulse the reset line; false when there is none */
};

class HearthUpdate {
public:
  HearthUpdate();

  /* Bring FOTA up: the filesystem (or HEARTH_UPDATE_ERR_NO_SPACE /
   * HEARTH_UPDATE_ERR_NO_FS), the stage, the effective version, the
   * declaration and the requestor. See hearthBeginImpl's own comment in
   * the implementation for the order.
   *
   * The wrapper is inline in this header (final review I8): it is the
   * only place that names hearthLittleFs() and the ARDUINO default host
   * hooks, so a sketch that never calls begin() links none of LittleFS,
   * PicoOTA or the linker symbols the defaults name, while the host build
   * compiles the wrapper without its ARDUINO block and stays a plain
   * forward to hearthBeginImpl(). The fs and the hooks attach once, at
   * the first begin(): a test that attached its own fs or installed its
   * own hooks earlier (hearthAttach(), hearthSetHostHooks()) is left
   * alone, and the default hooks go in through hearthSetHostHooks(),
   * which sets _hostHooksInstalled, so a begin() that follows an explicit
   * install never lays the defaults over it. */
  bool begin(uint32_t productVersion, const char *versionString, const HearthUpdateConfig &cfg = HearthUpdateConfig()) {
#ifdef ARDUINO
    if (!_fs) {
      hearthAttach(hearthLittleFs());
    }
    if (!_hostHooksInstalled) {
      HearthHostHooks hk;
      hk.imageRange = [](const uint8_t **start, uint32_t *len) {
        *start = &__flash_binary_start;
        *len = (uint32_t)(&__flash_binary_end - &__flash_binary_start);
        return true;
      };
      hk.stageImage = [](const char *path, uint32_t off, uint32_t len) {
        picoOTA.begin();
        picoOTA.addFile(path, off, XIP_BASE, len);
        return picoOTA.commit();
      };
      hk.reboot = []() {
        rp2040.reboot();
      };
      hk.coprocReset = [](const HearthCoprocPins &pins) {
        return hearthCoprocReset(pins, 100);
      };
      hearthSetHostHooks(hk);
    }
#endif
    return hearthBeginImpl(productVersion, versionString, cfg);
  }
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
  /* B632: from HearthClass::hearthDispatchEvt() on +MTEVT:3 (the Matter
   * commissioning complete); records the time, no link call, and the
   * settle wait runs before every reset the update drives. */
  void hearthNoteCommissioned();
  /* B671: from HearthClass::hearthOnURCLine() on every +MTREADY (the
   * requestor mode is not persisted, spec 5.2: every boot starts
   * disabled). Recording only, no link call; hearthDrain() re-probes the
   * declaration and the requestor switch on it. */
  void hearthNoteCoprocReady();
  /* From HearthClass::poll() and every hearthCommand(). The whole update
   * path (the pull, the verify, the verdict, the apply) runs behind
   * _drainFn, null until begin() sets it (final review I8: a sketch that
   * never begins the update links none of it). The _draining and the
   * link-busy guards run inside hearthDrainImpl(), so a never-begun
   * object's drain costs one null check. */
  void hearthDrain();
  void hearthAttach(HearthFs &fs);           /* tests inject the fake fs; target uses hearthLittleFs() */
  void hearthSetFlasher(HearthFlasher *f);   /* Task 5/6 */
  HearthUpdateStage &stage();                /* the stage, over the attached fs */
  /* The HearthClass this object is a member of; set by HearthClass's
   * constructor. The update's wire commands go through this object's
   * hearthCommand(), not the global Hearth, so a test that creates a
   * local HearthClass and begins its own update talks to the link the
   * test scripted, not the global's unstarted one. */
  void hearthSetOwner(void *owner);
  bool hearthDownloadComplete() const;  /* 4b1: the staged write was ended on +MTOTA:DOWNLOADED */
  /* The test hook for the download baud switch (plan Task 4b2 case 10). On
   * the device the link's own port brings the UART to the rate; the tests
   * install a callback that records it instead. 0 disables the switch. */
  void hearthSetBaudChanger(void (*cb)(uint32_t));
  /* The test hook for the host part (plan Task 6b). The inline begin()
   * wrapper installs the ARDUINO defaults (PicoOTA, the linker symbols,
   * rp2040.reboot, hearthCoprocReset; final review I8), and all-null
   * hooks stand in on the host; tests install their own over file-static
   * records instead. It sets _hostHooksInstalled, so an install through
   * here (before or after a begin()) is what keeps the wrapper from
   * laying its defaults over it. A null hook means "not available
   * here". */
  void hearthSetHostHooks(const HearthHostHooks &h);

private:
  /* The body begin() forwards to (its comment block stays with this in
   * the implementation): the fs and the ARDUINO default hooks attach in
   * the wrapper, not here, so this names no LittleFS or PicoOTA symbol. */
  bool hearthBeginImpl(uint32_t productVersion, const char *versionString, const HearthUpdateConfig &cfg);
  /* The re-entry guard for hearthDrain(): set on construction, cleared by
   * the destructor on every exit path, including an early return from a
   * nested hearthCommand() call in the middle of the pull. */
  struct DrainGuard {
    bool &flag;
    explicit DrainGuard(bool &f) : flag(f) { flag = true; }
    ~DrainGuard() { flag = false; }
  };
  void hearthPullBlock();   /* AT+MTOTAGET/ACK for _pendingSeq, with the retry rules */
  /* AT+MTOTA=0 then AT+MTOTA=1 (final review I1: the =0 turns the
   * requestor off, so the =1 puts it on again for the next offer, as
   * hearthAbandon() does), remove the partial staged file, FAILED/ERR_LINK */
  void hearthAbortPull();
  int hearthCmd(const char *cmd, HearthLink::LineCb onLine, void *arg);
  int hearthCmd(const char *cmd, HearthLink::LineCb onLine, void *arg, uint32_t timeout_ms);
  void hearthVerifyAndVerdict();  /* 4b2: on DOWNLOADED, verify the staged bundle, consent, AT+MTOTASTAGED */
  void hearthRefuse(HearthBundleError reason);  /* 4b2: AT+MTOTASTAGED=0,<reason>, remove staged, FAILED/ERR_BUNDLE */
  void hearthSetBaud(uint32_t baud);  /* 4b2: AT+MTBAUD=<baud> plus the test hook */
  /* Final review I8: the work hearthDrain() used to run in its own body:
   * the re-probe, the retry, the baud switch, the pull, the verdict and
   * the apply. The body lives here, and begin() sets _drainFn to it, so
   * the heavy code is named only on that path. */
  void hearthDrainImpl();
  /* Final review I5: the four version fields that must move together,
   * set by every place that changes the effective version (begin(), the
   * fw-only success tail, the first-boot host confirm, the resume): the
   * re-probe and the rollback re-declare the version actually in force
   * with its matching string. */
  void hearthSetEffectiveVersion(uint32_t version, const char *versionString);
  /* Final review M6: re-declare the version in force (AT+MTSWVER with
   * _declaredVersion and its string), the apply's failure paths' first
   * wire call. */
  void hearthReDeclareInForce();
  void hearthAbandon();     /* 4b2: AT+MTOTA=0 then AT+MTOTA=1, remove the staged bundle, IDLE */
  /* B671: the declaration and the requestor switch, re-run after a
   * co-processor reboot (the requestor mode is not persisted, spec 5.2).
   * A separate function, not begin()'s own tail: begin() sends the
   * declaration before its AT+CGMM/MTVER/MTOTA? queries, so it keeps its
   * own inline sequence and does not call this. The re-probe runs the two
   * commands in begin()'s order and no others (a begin() that never
   * reached its requestor has sent nothing this can send, and a timed-out
   * declaration is re-probed on the next reboot). A 8 on the switch is
   * the requestor not wired (B667's retry arms), an 8 on the declaration
   * the firmware without FOTA (settled for good), any other failure
   * leaves the state as it is. */
  void hearthDeclareAndRequestor();
  /* 6a: the apply of the Hearth (co-processor) part, run by hearthDrain()
   * once and for all on the pending +MTOTA:APPLY. Task 6c's resume calls
   * hearthApplyFw() directly with the state it loaded. */
  void hearthApply();
  /* 6a, first-attempt parameter added by 6c: the flash attempts from
   * firstAttempt to 3 (a resume continues where the state file left off,
   * it does not restart the count). 0 success, 1 three failures. */
  int hearthApplyFw(HearthUpdateState &st, int firstAttempt = 1);
  void hearthApplyHost();  /* 6b: the host part; stage it through PicoOTA and reboot */
  /* 6c: the success and failure tails of the Hearth-part apply, shared by
   * hearthApply() (6a) and the resume after a power loss (6c). Final
   * review M1: hearthApply() owns the staged file and deletes it on
   * every path; this tail reads it through the reference and does not
   * delete it (the resume caller owns and deletes its own handle). */
  void hearthFwSucceeded(const HearthBundleInfo &info, int part, HearthFile &staged, const HearthUpdateState &st);
  void hearthFwFailed();
  /* 6b: the first-boot paths in begin(), after the state file is loaded.
   * The host part was staged and the sketch rebooted into it; now confirm
   * (HOST, the manifest is written here) or give up (HOST_CONFIRM, the
   * previous sketch is running again and the loop must stop). */
  bool hearthFirstBootHost(const HearthUpdateState &st);
  bool hearthFirstBootHostConfirm(const HearthUpdateState &st);
  /* B632: the 2 s settle wait before every reset the update drives (the
   * flash attempts, the retained rollback flash, the first-boot
   * co-processor reset). The MG24's key store saves its key map 2 s after
   * a write, so a reset inside that window after a commissioning loses the
   * new fabric. */
  void hearthSettleAfterCommissioning();
  /* 6b: the first-boot failure, called from begin() when the declaration
   * timed out three times: re-stage host-prev.bin and reboot. */
  bool hearthFirstBootHostFail(const HearthUpdateState &st);
  /* 6b: the host part's range of the staged bundle, through the stageImage
   * hook; fills the state record's version fields and hostPart. */
  bool stagedHostPart(int hostPart, HearthUpdateState &st);
  void *_owner;
  HearthFs *_fs;
  HearthUpdateConfig _cfg;
  HearthFlasher *_flasher;
  HearthHostHooks _hostHooks;  /* 6b: the host part's hooks, set by hearthSetHostHooks */

  HearthUpdateStatus _status;

  HearthUpdateStage _stage;
  HearthManifest _manifest;
  bool _haveManifest;

  char _model[33];
  char _variant[16];
  char _hearthVersion[33];
  /* Final review I7: the running Hearth version read in begin(), kept
   * because _hearthVersion is overwritten later (the success tail caches
   * the new part's version). The rollback check in hearthFwFailed()
   * compares AT+MTVER?'s answer against this, not against _hearthVersion. */
  char _preApplyHearthVersion[33];
  /* Final re-review Minor: set by hearthApplyFw() to true when the LAST
   * attempt's flasher itself returned rc != HEARTH_FLASH_OK (the flasher
   * exits with its own reset, B668, so its +MTREADY is still landing when
   * the failure tail runs and the tail's first wait consumes it). Cleared
   * on every other failure mode (no +MTREADY, verify mismatch: the
   * flasher succeeded, so nothing further reboots), on success, and at
   * the top of hearthApplyFw() so an early return (no flasher, the
   * bundle will not re-open) does not inherit a stale true from a
   * previous apply. */
  bool _lastAttemptFlasherError;
  uint32_t _effectiveVersion;

  /* Parsed by hearthOnOtaLine(), acted on by hearthDrain(): the pending
   * block, the last state line, the offered version and progress. */
  uint32_t _pendingSeq;
  uint32_t _pendingLen;
  bool _havePendingBlock;
  HearthUpdateStateEnum _lastState;

  bool _downloadComplete;
  /* True while the stage's staged.ota.tmp is open for the current
   * transfer. Set by the first pull (stagedBeginWrite), cleared by
   * stagedEndWrite on DOWNLOADED or by the abort path. */
  bool _stagedWriteOpen;
  /* Final review I2: the co-processor ended the current transfer while
   * the staged write was still open. hearthOnOtaLine() sets it on an
   * ERROR or IDLE state line and on a BLOCK with seq 0 while
   * _stagedWriteOpen (a new transfer's first block); the next
   * hearthDrain() ends and discards the write there (stagedEndWrite,
   * stagedRemove, _stagedWriteOpen false, _havePendingBlock false)
   * before it runs the pull the flag came in with. Set on the URC
   * route (record only, no link or file call), acted on on the drain. */
  bool _transferEnded;
  /* Set on entry to hearthDrain(), cleared by the DrainGuard on every exit
   * path. hearthCommand() runs hearthDrain() at the end of its own call, so
   * without this the pull loop (which sends hearthCommand()s) would drain
   * itself and recurse; a nested drain finds the flag set and returns at
   * once, having sent nothing. */
  bool _draining;
  /* The block pull's own deadline, the plan's fixed 1500 ms. A pull that
   * runs out of time is not re-pulled: the 5 s acknowledgement deadline
   * cannot absorb a second full pull at 115200, so the transfer aborts at
   * once instead. */
  static const uint32_t kPullTimeoutMs = 1500;
  /* One +MTOTABLK: answer carries at most this many bytes of the block. */
  static const uint32_t kBlkLineBytes = 96;

  bool (*_applyRequestCB)();
  void (*_statusCB)(const HearthUpdateStatus &);
  void (*_baudChangerCB)(uint32_t);  /* 4b2 test hook for AT+MTBAUD, 0 = no switch */
  /* Final review I8: the pointer hearthDrain() calls the work through,
   * null until begin() sets it (to hearthDrainImpl()). Null on the host
   * test build, where the tests run hearthDrain() on a constructed,
   * never-begun object; begin() sets it on every build. The member
   * pointer is what keeps the drain work out of a never-begun sketch's
   * link: the only name for hearthDrainImpl() lives in begin()'s body. */
  void (HearthUpdate::*_drainFn)();

  /* 4b2: the apply decision from the verify on DOWNLOADED. Set before the
   * consent hook runs; _fwPart and _hostPart are indexes into the bundle's
   * parts, 0xFF for the part not selected. */
  int _fwPart;
  int _hostPart;
  bool _consentPending;      /* true while the verdict waits on the consent hook */
  bool _consentRefused;      /* true while a refusal is on the clock */
  uint32_t _consentRefusalMs; /* millis() of the refusal, the time only */
  /* Final re-review M5: a verdict (the AT+MTOTASTAGED=1 for the staged
   * bundle) went out in this boot, so the +MTOTA:APPLY that follows it is
   * the one that may apply. Set when the verdict goes out (the consent
   * path in hearthDrainImpl() and the end of hearthVerifyAndVerdict()),
   * cleared when a new transfer starts (stagedBeginWrite), when the
   * bundle is abandoned or removed (hearthAbandon(), hearthRefuse()),
   * when an apply starts (hearthApply(), one verdict gives one apply),
   * and in the constructor, end() and begin's body. DEFERRED keeps it:
   * the provider's AwaitNextAction defers the apply, it does not undo
   * the verdict, so the APPLY that follows the delay still applies. An
   * APPLY with no verdict in this boot (the co-processor survived a host
   * reboot, so no verdict went out here) is dropped with the flag. */
  bool _verdictSent;
  /* B632: the last commissioning complete (+MTEVT:3). The flag, not a
   * timestamp sentinel, is what arms the wait: a commissioning recorded at
   * millis() 0 still settles. The settle wait clears the flag once the
   * 2000 ms have run, so an old commissioning never delays a reset again. */
  bool _commissioned;
  uint32_t _commissionedMs;
  /* The link rate as last set over AT+MTBAUD (HEARTH_LINK_BAUD at start).
   * The drain compares the rate a state needs against this and sends the
   * switch only when they differ, so a repeat state line is a no-op and a
   * switch back is seen. The URC callback only records which rate a state
   * needs (_baudWantedDownload); the AT+MTBAUD itself goes out on the next
   * drain, because a URC callback may not call the link. */
  uint32_t _baud;
  bool _baudWantedDownload;

  /* 6a: the pending apply, parsed by hearthOnOtaLine() (+MTOTA:APPLY sets
   * it, no link call there) and answered once by hearthDrain() in
   * hearthApply(). */
  bool _applyPending;
  /* The version begin() declared with its first AT+MTSWVER (the effective
   * version and its string). The rollback path re-declares it before it
   * gives up (spec 7.5): the co-processor claims a version it is not yet
   * running until the old image is back and booted. */
  uint32_t _declaredVersion;
  char _declaredVersionString[33];

  /* B667: begin() met a +MTERR:8 on AT+MTOTA=1 after a good AT+MTSWVER
   * (the firmware has FOTA, the requestor is not wired yet, e.g. an
   * nRF54L15 that is not commissioned): UNAVAILABLE, but hearthDrain()
   * retries AT+MTOTA=1 on the commissioning or every 30 s until it goes.
   * Set only in begin()'s AT+MTOTA=1 -> 8 branch, cleared when the retry
   * succeeds and by end() and begin(). */
  bool _requestorRetry;
  /* B667: millis() of the last retry attempt (begin()'s AT+MTOTA=1 sets
   * it when it arms _requestorRetry). */
  uint32_t _requestorRetryMs;
  /* B667: a commissioning (+MTEVT:3) was recorded while _requestorRetry
   * is set; hearthNoteCommissioned() only records it (it runs inside a
   * URC callback, no link calls), hearthDrain() acts on it and clears it. */
  bool _requestorRetryNow;
  /* B671: set by begin() when its requestor switch has run and by end()
   * back to false: the re-probe after a co-processor reboot runs only
   * once begin() has completed (a begin() that never reached its requestor
   * has sent nothing the re-probe can send). */
  bool _beginComplete;
  /* B671: a +MTREADY was seen (hearthNoteCoprocReady set this, recording
   * only); hearthDrain() clears it and, when _beginComplete and the state
   * is IDLE or UNAVAILABLE, runs hearthDeclareAndRequestor(). */
  bool _coprocReadySeen;
  /* Final review I8: true once the ARDUINO default host hooks (PicoOTA,
   * the linker symbols, rp2040.reboot, hearthCoprocReset) were installed
   * by the inline begin() wrapper, or by any explicit
   * hearthSetHostHooks() (which sets it too, so an explicit install keeps
   * its precedence over a later begin()). False in the constructor. */
  bool _hostHooksInstalled;
};

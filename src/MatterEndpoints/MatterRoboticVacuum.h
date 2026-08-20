/*
 * MatterRoboticVacuum.h - Task 7's (RVC + Microwave batch) Robotic Vacuum
 * Cleaner endpoint type.
 *
 * Like MatterDoorLock (C3) and every Hearth-original class since, this has
 * NO arduino-esp32 counterpart: upstream's Matter library ships no Robotic
 * Vacuum Cleaner class at all (see Hearth.h's umbrella comment). There is
 * nothing to mirror an API from, so the public surface below is this port's
 * own design, built directly against the firmware's wire contract
 * (AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.21) and the task brief's own
 * interface sketch, with one correction the firmware's own Task 3 (this
 * batch) established as binding: see "CurrentMode caching" below.
 *
 * Device type 0x0074 is robotic_vacuum_cleaner
 * (esp_matter_endpoint.h's ESP_MATTER_ROBOTIC_VACUUM_CLEANER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:114,
 * "#define ESP_MATTER_ROBOTIC_VACUUM_CLEANER_DEVICE_TYPE_ID 0x0074"), also
 * AT_MT_SPEC.md S3.9's own device type table ("0x0074 | Robotic Vacuum
 * Cleaner"). One endpoint carries three clusters at once (design spec
 * section 2.1):
 *
 * - RvcRunMode, cluster 0x0054 (84) (connectedhomeip's zap-generated
 *   zzz_generated/app-common/clusters/RvcRunMode/ClusterId.h, "inline
 *   constexpr ClusterId Id = 0x00000054;", the file's own header comment
 *   reading "cluster code: 84/0x54"); ChangeToMode is command 0x0000
 *   (.../RvcRunMode/CommandIds.h, "namespace ChangeToMode { inline
 *   constexpr CommandId Id = 0x00000000; }").
 * - RvcCleanMode, cluster 0x0055 (85) (.../RvcCleanMode/ClusterId.h, "inline
 *   constexpr ClusterId Id = 0x00000055;", "cluster code: 85/0x55");
 *   ChangeToMode is command 0x0000, identical shape to RvcRunMode's
 *   (.../RvcCleanMode/CommandIds.h, same "namespace ChangeToMode { inline
 *   constexpr CommandId Id = 0x00000000; }").
 * - RvcOperationalState, cluster 0x0061 (97) (.../RvcOperationalState/
 *   ClusterId.h, "inline constexpr ClusterId Id = 0x00000061;", "cluster
 *   code: 97/0x61"); Pause is command 0x0000, Resume is command 0x0003, and
 *   GoHome is command 0x0080 (.../RvcOperationalState/CommandIds.h,
 *   "namespace Pause { inline constexpr CommandId Id = 0x00000000; }",
 *   "namespace Resume { inline constexpr CommandId Id = 0x00000003; }",
 *   "namespace GoHome { inline constexpr CommandId Id = 0x00000080; }").
 *   Start/Stop are NOT supported on this derived cluster at all -- answered
 *   kUnknownEnumValue by the SDK's own base delegate before this firmware's
 *   delegate is ever reached (AT_MT_SPEC.md S3.21) -- so this class does not
 *   register them.
 *
 * All eight IDs verified against the pinned esp-matter checkout's own
 * generated headers, not transcribed from the brief that named them; given
 * as plain integers in the .cpp, this library's usual pattern, since there
 * is no connectedhomeip header on a host build.
 *
 * RvcState_t below transcribes the derived-cluster-number-space subset of
 * OperationalStateEnum from the same pinned checkout
 * (zzz_generated/app-common/clusters/RvcOperationalState/Enums.h):
 *
 *   enum class OperationalStateEnum : uint8_t
 *   {
 *       kStopped          = 0x00,
 *       kRunning          = 0x01,
 *       kPaused           = 0x02,
 *       kError            = 0x03,
 *       kSeekingCharger   = 0x40,
 *       kCharging         = 0x41,
 *       kDocked           = 0x42,
 *       kEmptyingDustBin  = 0x43,
 *       kCleaningMop      = 0x44,
 *       kFillingWaterTank = 0x45,
 *       kUpdatingMaps     = 0x46,
 *       // kUnknownEnumValue intentionally not defined [...]
 *   };
 *
 * AT_MT_SPEC.md S3.21 accepts 0/1/2 (shared with the base cluster) plus
 * 0x40/0x41/0x42 for AT+MTOPSTATE on an RvcOperationalState endpoint; 3
 * (Error) is rejected firmware-side for both clusters, and 0x43-0x46
 * (dust-bin/mop/water-tank/map states) are out of this task's scope and not
 * in the union the firmware publishes either. RvcState_t below therefore
 * exposes exactly the six values the wire actually accepts on this cluster,
 * nothing past them -- this class does not duplicate the firmware's own
 * range check, the same "the firmware never transcribes enum values; the
 * library transcribes with quoted header evidence" rule
 * MatterOperationalStateEndpoint.h's header comment states for its own
 * three-value enum.
 *
 * Design, following MatterOperationalStateEndpoint's own precedent (no
 * arduino-esp32 counterpart, no initial state to reconcile) plus
 * MatterModeSelect's for the two SupportedModes lists:
 *
 * - begin() declares only (hearthDeclare(), device type 0x0074, variant 0);
 *   there is no sketch-declared initial RvcOperationalState or mode list to
 *   reconcile against the C6's own cluster-creation defaults, so this class
 *   has no reconcile-time state PUSH, only the mode-list RESEND
 *   hearthOnReconciled() below performs (S3.20.1's own "not persisted, the
 *   store lives in RAM and starts empty every boot" policy). The
 *   operational-state cache starts at kStateStopped and stays there until
 *   the sketch itself calls setOperationalState(): "the firmware never
 *   calls AT+MTOPSTATE on its own" (S3.21).
 * - setSupportedRunModes()/setSupportedCleanModes() enforce S3.20.1's
 *   grammar host-side (1..8 triples, mode values unique within the call,
 *   labels 1..32 bytes of printable ASCII with no '"'; <tag>'s 0..0xFFFF
 *   range is already guaranteed by its uint16_t parameter type, so there is
 *   nothing extra to check there), the identical discipline
 *   MatterModeSelect::setSupportedModes() established for the ModeSelect
 *   form of the same grammar. Each call replaces exactly one cluster's list
 *   (S3.20.1: "the store is keyed by (<ep>, <cluster>), not by <ep> alone"),
 *   so an RVC endpoint carrying both clusters needs two separate calls, one
 *   per setter. Cache commit only after a successful wire write (house
 *   discipline: a failed write must not update it).
 * - hearthOnReconciled() re-sends BOTH cached lists on every reconcile, not
 *   only the first, exactly MatterModeSelect's own precedent doubled: a
 *   no-op for whichever list (or both) nothing has been set on yet.
 * - onChangeRunMode()/onChangeCleanMode() register the host's verdict for a
 *   firmware-forwarded ChangeToMode on RvcRunMode/RvcCleanMode
 *   respectively (AT_MT_SPEC.md S3.20.1/S3.17); hearthOnForwardedCommand-
 *   Fields() below is where the dispatch actually lands, carrying the
 *   requested mode as fields.value[0]. No callback registered denies by
 *   default (fail closed, this library's usual norm).
 * - onPause()/onResume()/onGoHome() register the host's verdict for a
 *   firmware-forwarded Pause/Resume/GoHome on RvcOperationalState
 *   (AT_MT_SPEC.md S3.17/S3.21); same dispatch method, same fail-closed
 *   default. Unlike the water valve, a deny here IS the wire response the
 *   controller observes (ErrorStateID 0x02, UnableToCompleteOperation), the
 *   same shape as the OperationalState trio (S3.21's own text).
 * - setOperationalState()/getOperationalState() report the vacuum's actual
 *   RvcOperationalState via AT+MTOPSTATE (S3.21), never AT+MTATTR: every
 *   RvcOperationalState attribute is managed internally by the cluster's
 *   own SDK Instance (identical AttributeAccessInterface interception
 *   S3.20.1 documents for CurrentMode, S3.21's own note says so
 *   explicitly), so attributeChangeCB() below is a documented no-op, the
 *   same shape MatterOperationalStateEndpoint's own header comment
 *   establishes. getOperationalState() is cache-only (no wire round trip).
 *
 * **CurrentMode caching: task-brief correction, binding.** The task brief's
 * own interface sketch suggested attributeChangeCB() as where
 * getCurrentRunMode()/getCurrentCleanMode()'s cache updates; this batch's
 * firmware Task 3 established, traced against the pinned SDK source, that
 * this is wrong for RvcRunMode/RvcCleanMode's CurrentMode (AT_MT_SPEC.md
 * S3.20.1): CurrentMode on these two clusters is AttributeAccessInterface-
 * served, not ember-backed, so a ChangeToMode never reaches
 * esp_matter::attribute::set_val() and **no +MTATTR URC ever fires for it,
 * from either the firmware's own clamp or a controller-driven change**. The
 * identical mechanism holds for RvcOperationalState's OperationalState
 * attribute (S3.21's own note, cross-referencing S3.20.1's finding). So:
 *
 * - attributeChangeCB() below is a documented no-op returning `started`,
 *   the MatterOperationalStateEndpoint precedent (this file's header
 *   comment above), NOT a CurrentMode cache handler -- there is nothing for
 *   it to ever be called with, on any of this class's three clusters.
 * - getCurrentRunMode()/getCurrentCleanMode()'s cache updates ONLY when
 *   hearthOnForwardedCommandFields() below allows a ChangeToMode: the
 *   requested mode is cached exactly when the registered onChangeRunMode()/
 *   onChangeCleanMode() callback returns true, mirroring the wire's own
 *   ChangeToModeResponse (kSuccess on allow). A denied or unadjudicated
 *   ChangeToMode (no callback registered, the SDK's own kUnsupportedMode
 *   short-circuit before this host is ever asked) leaves the cache
 *   untouched, matching what the device's own CurrentMode actually did (or
 *   did not do).
 * - hearthAttrTypeFor() below simply delegates to the base class
 *   (ESP_MATTER_VAL_TYPE_INTEGER): there is no (cluster, attribute) pair on
 *   any of this class's three clusters this override would ever need to
 *   type, for the identical reason attributeChangeCB() is a no-op.
 *
 * The same "adjudication verdict is the only signal, never an ember-level
 * one" reasoning applies to setOperationalState()/getOperationalState()
 * above: they are driven entirely by this host's own control loop, exactly
 * as MatterOperationalStateEndpoint's precedent already established, not by
 * anything this class watches for.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterRoboticVacuum : public MatterEndPoint {
public:
  // clang-format off
  // RvcOperationalState::OperationalStateEnum protocol values this wire
  // command accepts on an RvcOperationalState endpoint (AT_MT_SPEC.md
  // S3.21's <state>); see the header comment above for the quoted
  // pinned-header source. The first three are shared with the plain
  // OperationalState cluster (MatterOperationalStateEndpoint::
  // OperationalState_t); the 0x40-range three are exclusive to this
  // derived cluster.
  enum RvcState_t {
    kStateStopped        = 0,
    kStateRunning         = 1,
    kStatePaused          = 2,
    kStateSeekingCharger  = 0x40,
    kStateCharging        = 0x41,
    kStateDocked          = 0x42,
  };
  // clang-format on

  MatterRoboticVacuum();
  ~MatterRoboticVacuum();

  // declares only (device type 0x0074, variant 0); no initial state or mode
  // list to reconcile -- see the header comment.
  bool begin();
  // this will stop processing Robotic Vacuum Cleaner Matter events
  void end();

  // replace RvcRunMode's / RvcCleanMode's SupportedModes list (AT_MT_SPEC.md
  // S3.20.1): 1..8 mode/tag/label triples, each mode 0..255 unique within
  // the call, each tag a bare u16 (0 = this cluster's conformance-required
  // default -- kIdle-first/kCleaning for RvcRunMode, kVacuum for
  // RvcCleanMode, see S3.20.1's table), each label 1..32 bytes of printable
  // ASCII with no '"'. Re-sent automatically on every later reconcile
  // (hearthOnReconciled()). The two clusters are independent stores on the
  // same endpoint; setting one does not touch the other.
  bool setSupportedRunModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);
  bool setSupportedCleanModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // register the host's verdict for a controller-invoked ChangeToMode on
  // RvcRunMode / RvcCleanMode (AT_MT_SPEC.md S3.17/S3.20.1); the callback's
  // argument is the requested mode. No callback registered denies by
  // default (fail closed). The cache updates only on an allow -- see the
  // header comment's "CurrentMode caching" section.
  void onChangeRunMode(std::function<bool(uint8_t)> cb);
  void onChangeCleanMode(std::function<bool(uint8_t)> cb);

  // register the host's verdict for a controller-invoked Pause / Resume /
  // GoHome on RvcOperationalState (AT_MT_SPEC.md S3.17/S3.21). No callback
  // registered denies by default; a deny genuinely fails the command the
  // controller sees (ErrorStateID 0x02), the same shape as the
  // OperationalState trio, not the water valve's discarded verdict.
  void onPause(std::function<bool()> cb);
  void onResume(std::function<bool()> cb);
  void onGoHome(std::function<bool()> cb);

  // report the vacuum's actual RvcOperationalState once the host's own
  // control loop confirms it (AT+MTOPSTATE, AT_MT_SPEC.md S3.21); updates
  // the cache only on a successful write.
  bool setOperationalState(uint8_t state);
  // returns the cached operational state; no wire round trip.
  uint8_t getOperationalState();

  // cached CurrentMode for RvcRunMode / RvcCleanMode. NOT a wire read:
  // CurrentMode on these clusters is never reachable over AT+MTATTR and no
  // +MTATTR URC ever reports a change (see the header comment's
  // "CurrentMode caching" section). Updated only when onChangeRunMode()/
  // onChangeCleanMode() allows a forwarded ChangeToMode.
  uint8_t getCurrentRunMode();
  uint8_t getCurrentCleanMode();

  // Documented no-op returning `started`: no attribute on RvcRunMode,
  // RvcCleanMode or RvcOperationalState has an AT+MTATTR path, so
  // hearthDispatchAttr() never has a (cluster, attribute) pair from any of
  // the three to route here. Present only because MatterEndPoint declares
  // this pure virtual -- see the header comment's "CurrentMode caching"
  // section and MatterOperationalStateEndpoint's identical precedent.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Delegates to the base (ESP_MATTER_VAL_TYPE_INTEGER): there is no
  // (cluster, attribute) pair on any of this class's three clusters this
  // override would ever need to type, for the identical reason
  // attributeChangeCB() above is a no-op.
  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  // Hearth's own addition (MatterEndPoint.h, Task 6): the firmware forwards
  // a controller-invoked ChangeToMode (RvcRunMode/RvcCleanMode, one payload
  // field, the requested mode) or Pause/Resume/GoHome (RvcOperationalState,
  // no payload) here for a verdict; see the on*() registrars above.
  // Everything else -- the wrong cluster, an unrecognised command id, or a
  // call before begin() -- defers to the base class default
  // (MatterEndPoint::hearthOnForwardedCommandFields(), itself fail-closed),
  // rather than returning false directly.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  static const uint8_t kMaxModes = 8;      // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t kMaxLabelLen = 32;  // AT_MT_SPEC.md S3.20.1: 1..32 printable ASCII bytes

  bool started = false;
  uint8_t operationalState = kStateStopped;
  uint8_t currentRunMode = 0;
  uint8_t currentCleanMode = 0;

  uint8_t runModes[kMaxModes];
  uint16_t runTags[kMaxModes];
  char runLabels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t runModesCount = 0;

  uint8_t cleanModes[kMaxModes];
  uint16_t cleanTags[kMaxModes];
  char cleanLabels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t cleanModesCount = 0;

  std::function<bool(uint8_t)> _onChangeRunModeCB = nullptr;
  std::function<bool(uint8_t)> _onChangeCleanModeCB = nullptr;
  std::function<bool()> _onPauseCB = nullptr;
  std::function<bool()> _onResumeCB = nullptr;
  std::function<bool()> _onGoHomeCB = nullptr;

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: re-sends BOTH cached SupportedModes lists (RvcRunMode's and
  // RvcCleanMode's), which the firmware does not persist across a reboot
  // (S3.20.1). A no-op for a list nothing has been set on yet, doubling
  // MatterModeSelect's own precedent since this class carries two
  // independent lists on one endpoint.
  void hearthOnReconciled() override;

  // builds and sends
  // "AT+MTMODES=<ep>,<cluster>,<mode1>,<tag1>,\"<label1>\",..." for exactly
  // the triples given; wire-only, no cache update (house discipline: the
  // caller decides whether/what to commit to the cache afterwards).
  bool hearthSendModes(uint32_t cluster, const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // wire-only AT+MTOPSTATE write; the caller decides whether/what to commit
  // to the cache.
  bool hearthSendOperationalState(uint8_t state);

private:
  // Shared host-side grammar validation (S3.20.1: count bounds, mode
  // uniqueness, per-label length/printability/quote-exclusion) plus the
  // wire send and cache commit, common to setSupportedRunModes() and
  // setSupportedCleanModes(): the only difference between the two public
  // setters is which cluster id and which cache they target.
  bool hearthSetModeList(
    uint32_t cluster, const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count, uint8_t *cacheModes,
    uint16_t *cacheTags, char cacheLabels[][kMaxLabelLen + 1], uint8_t *cacheCount
  );
};

/*
 * MatterMicrowaveOven.h - Task 8's (RVC + Microwave batch) Microwave Oven
 * endpoint type, the last of this batch.
 *
 * Like MatterDoorLock (C3) and every Hearth-original class since, this has
 * NO arduino-esp32 counterpart: upstream's Matter library ships no Microwave
 * Oven class at all (see Hearth.h's umbrella comment). There is nothing to
 * mirror an API from, so the public surface below is this port's own
 * design, built directly against the firmware's wire contract
 * (AT_MT_SPEC.md S3.9/S3.17/S3.20.1/S3.21) and the task brief's own
 * interface sketch.
 *
 * Device type 0x0079 is microwave_oven
 * (esp_matter_endpoint.h's ESP_MATTER_MICROWAVE_OVEN_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:89,
 * "#define ESP_MATTER_MICROWAVE_OVEN_DEVICE_TYPE_ID 0x0079"), also
 * AT_MT_SPEC.md S3.9's own device type table ("0x0079 | Microwave Oven").
 * One endpoint carries three clusters at once
 * (esp_matter_endpoint.h's `microwave_oven` namespace: `config_t` embeds
 * `app_with_operational_state_config` -- the plain OperationalState cluster
 * -- plus `microwave_oven_mode`/`microwave_oven_control`):
 *
 * - MicrowaveOvenMode, cluster 0x005E (94) (connectedhomeip's zap-generated
 *   zzz_generated/app-common/clusters/MicrowaveOvenMode/ClusterId.h, "inline
 *   constexpr ClusterId Id = 0x0000005E;", the file's own header comment
 *   reading "cluster code: 94/0x5E"). Its own CommandIds.h declares
 *   "kAcceptedCommandsCount = 0": there is no ChangeToMode command on this
 *   cluster at all (AT_MT_SPEC.md S3.20.1: "MicrowaveOvenMode has no
 *   ChangeToMode command at all; its mode is selected through
 *   SetCookingParameters' cookMode field"). This class therefore declares no
 *   mode-change registrar and never registers a mode-change handler for
 *   this cluster -- there is nothing on the wire it could ever adjudicate.
 *   Its SupportedModes list is still declared over the cluster-aware
 *   AT+MTMODES form (S3.20.1), exactly like RvcRunMode/RvcCleanMode.
 * - MicrowaveOvenControl, cluster 0x005F (95) (.../MicrowaveOvenControl/
 *   ClusterId.h, "inline constexpr ClusterId Id = 0x0000005F;", "cluster
 *   code: 95/0x5F"); SetCookingParameters is command 0x0000 and AddMoreTime
 *   is command 0x0001 (.../MicrowaveOvenControl/CommandIds.h, "namespace
 *   SetCookingParameters { inline constexpr CommandId Id = 0x00000000; }",
 *   "namespace AddMoreTime { inline constexpr CommandId Id =
 *   0x00000001; }").
 * - OperationalState, cluster 0x0060 (96), the plain base cluster, not the
 *   RvcOperationalState derivation the Robotic Vacuum Cleaner uses
 *   (AT_MT_SPEC.md S3.21: "the washer rules in this section apply to it
 *   verbatim, since microwave_oven::add() wires the same plain
 *   OperationalState cluster, not a derived one"). This is exactly the
 *   shared MatterOperationalStateEndpoint core (Pause/Stop/Start/Resume,
 *   setOperationalState()/getOperationalState()), so this class subclasses
 *   it rather than reimplementing anything: see that header's own comment
 *   for the quoted pinned-header evidence of cluster/command ids and the
 *   design rationale (deny-is-the-wire-response, cache-only getter, no
 *   AT+MTATTR path for any OperationalState attribute).
 *
 * All four MicrowaveOvenMode/MicrowaveOvenControl ids above verified against
 * the pinned esp-matter checkout's own generated headers, not transcribed
 * from the brief that named them; given as plain integers in the .cpp, this
 * library's usual pattern, since there is no connectedhomeip header on a
 * host build. The three OperationalState ids are inherited unchanged from
 * MatterOperationalStateEndpoint and not repeated here.
 *
 * Design, following MatterRoboticVacuum's own precedent (a single endpoint
 * carrying more than one cluster, one of which is an inherited
 * OperationalState-family core) and MatterOperationalStateEndpoint's own
 * (no arduino-esp32 counterpart, no initial state to reconcile):
 *
 * - begin() = hearthBeginOperationalState(kMicrowaveOvenDeviceType); no
 *   initial OperationalState or mode list to reconcile against the C6's own
 *   cluster-creation defaults, so declaring is all it does past resetting
 *   this class's own mode-list cache to empty (the base's own
 *   hearthBeginOperationalState() already resets operationalState/started;
 *   there is nothing else of the base's for this class to touch).
 * - setSupportedModes() enforces S3.20.1's grammar host-side (1..8 triples,
 *   mode values unique within the call, labels 1..32 bytes of printable
 *   ASCII with no '"'; <tag>'s 0..0xFFFF range is already guaranteed by its
 *   uint16_t parameter type), the identical discipline
 *   MatterRoboticVacuum::setSupportedRunModes()/setSupportedCleanModes()
 *   established for the same cluster-aware form, narrowed to the one list
 *   this class actually has. Cache commit only after a successful wire
 *   write (house discipline: a failed write must not update it).
 * - hearthOnReconciled() re-sends the cached mode list on every reconcile,
 *   not only the first (S3.20.1: "not persisted... the store lives in RAM
 *   and starts empty every boot"), a no-op if nothing has been set yet.
 * - onCookingParameters() registers the host's verdict for a
 *   firmware-forwarded SetCookingParameters (MicrowaveOvenControl,
 *   AT_MT_SPEC.md S3.17); hearthOnForwardedCommandFields() below is where
 *   the dispatch actually lands, carrying all four payload fields packed
 *   into a HearthCookingParams. No callback registered denies by default
 *   (fail closed, this library's usual norm). Same verdict-is-wire-response
 *   shape as chime's PlayChimeSound, not the OperationalState family's
 *   GenericOperationalError indirection (S3.17: "HandleSetCookingParameters
 *   Callback... return[s] a Status directly... allow answers
 *   Status::Success, deny Status::Failure").
 * - onAddMoreTime() registers the host's verdict for a firmware-forwarded
 *   AddMoreTime, same cluster, same verdict shape. Its single payload field
 *   is the server-computed ABSOLUTE finalCookTimeSec, not a delta (S3.17:
 *   "AddMoreTime carries the same... gap... only SetCookingParameters is
 *   wired, so mt_devtypes.cpp's thunk hand-adds it"; the value itself is the
 *   new total the server already resolved, not an increment the host would
 *   need to add to anything).
 * - hearthOnForwardedCommandFields() handles cluster 0x5F
 *   (MicrowaveOvenControl) itself and defers everything else -- including
 *   cluster 0x0060 (OperationalState), which the base class's
 *   Pause/Stop/Start/Resume handling already covers -- to
 *   MatterOperationalStateEndpoint::hearthOnForwardedCommandFields().
 *   MatterOperationalStateEndpoint does not itself override this virtual
 *   (only the legacy four-argument hearthOnForwardedCommand()), so that
 *   qualified call statically binds to MatterEndPoint's own default body
 *   (MatterEndPoint.cpp: unwrap the first tail position and call
 *   hearthOnForwardedCommand() -- itself a virtual call on `this`, which
 *   this class does not override, so it dispatches to
 *   MatterOperationalStateEndpoint::hearthOnForwardedCommand()). The net
 *   effect is exactly the inherited OperationalState path, unchanged: see
 *   the test file for the regression that proves this chain reaches Pause/
 *   Stop/Start/Resume from THIS class's dispatch entry point.
 * - No getters for CookTime/PowerSetting, and no attributeChangeCB()
 *   override for them either (this class inherits
 *   MatterOperationalStateEndpoint's own no-op, itself inherited unchanged,
 *   good for all three clusters here): both attributes are Instance/
 *   delegate-owned, command-driven state, never ember-backed (S3.17:
 *   "Neither write ever reaches esp_matter::attribute::set_val(), so no
 *   +MTATTR URC ever fires for either attribute and AT+MTATTR does not
 *   serve them"). A host reads them back only through a commissioned
 *   controller, the identical CurrentMode-caching finding
 *   MatterRoboticVacuum's own header comment documents for its two ModeBase
 *   clusters and RvcOperationalState.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndpoints/MatterOperationalStateEndpoint.h"

/*
 * Task 8's own struct (task brief's exact shape): the four
 * SetCookingParameters fields, carried with has-flags rather than assumed
 * always-present. AT_MT_SPEC.md S3.17: on THIS firmware's actual wire
 * traffic all four always arrive resolved (the server defaults whichever
 * optional the invoke omitted before the delegate callback runs), so
 * hasCookMode/hasCookTime/hasPower are true on every real invoke this
 * firmware forwards today; the wire grammar itself still allows a sparse
 * line (AT_MT_SPEC.md S3.17's own general empty-field convention), so this
 * struct carries the has-flags honestly rather than assuming the
 * currently-observed shape is the only legal one. startAfterSetting is a
 * plain bool, not a has/value pair (task brief's own struct shape): it
 * defaults false when the field is absent, matching the server's own
 * default for the same field (S3.17: "startAfter defaults to false").
 */
struct HearthCookingParams {
  bool hasCookMode;
  uint8_t cookMode;
  bool hasCookTime;
  uint32_t cookTimeSec;
  bool hasPower;
  uint8_t powerPercent;
  bool startAfterSetting;
};

class MatterMicrowaveOven : public MatterOperationalStateEndpoint {
public:
  MatterMicrowaveOven();
  ~MatterMicrowaveOven();

  // declares only (device type 0x0079, variant 0); no initial state or mode
  // list to reconcile -- see the header comment.
  bool begin();

  // replace MicrowaveOvenMode's SupportedModes list (AT_MT_SPEC.md
  // S3.20.1): 1..8 mode/tag/label triples, each mode 0..255 unique within
  // the call, each tag a bare u16 (0 = kNormal, this cluster's
  // conformance-required default for every mode, first or not -- S3.20.1's
  // table), each label 1..32 bytes of printable ASCII with no '"'.
  // Re-sent automatically on every later reconcile (hearthOnReconciled()).
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // register the host's verdict for a controller-invoked SetCookingParameters
  // on MicrowaveOvenControl (AT_MT_SPEC.md S3.17); the callback's argument
  // carries all four fields, honestly has-flagged (see HearthCookingParams
  // above). No callback registered denies by default (fail closed). Mode
  // selection for this device rides this command's cookMode field, not a
  // ChangeToMode on MicrowaveOvenMode -- that cluster has no such command
  // (see the header comment).
  void onCookingParameters(std::function<bool(const HearthCookingParams &)> cb);

  // register the host's verdict for a controller-invoked AddMoreTime on
  // MicrowaveOvenControl (AT_MT_SPEC.md S3.17); the callback's argument is
  // the server-computed ABSOLUTE finalCookTimeSec, not a delta to add to
  // anything. No callback registered denies by default.
  void onAddMoreTime(std::function<bool(uint32_t finalCookTimeSec)> cb);

  // Hearth's own addition (MatterEndPoint.h, Task 6): the firmware forwards
  // a controller-invoked SetCookingParameters/AddMoreTime (MicrowaveOvenControl,
  // handled here directly) or Pause/Stop/Start/Resume (OperationalState,
  // deferred to the inherited base class implementation) here for a
  // verdict; see the on*() registrars above and the header comment's
  // deferral-chain explanation.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  static const uint8_t kMaxModes = 8;      // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t kMaxLabelLen = 32;  // AT_MT_SPEC.md S3.20.1: 1..32 printable ASCII bytes

  uint8_t modes[kMaxModes];
  uint16_t tags[kMaxModes];
  char labels[kMaxModes][kMaxLabelLen + 1];  // +1 for the terminating NUL
  uint8_t modesCount = 0;

  std::function<bool(const HearthCookingParams &)> _onCookingParametersCB = nullptr;
  std::function<bool(uint32_t)> _onAddMoreTimeCB = nullptr;

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: re-sends the cached MicrowaveOvenMode SupportedModes list, which
  // the firmware does not persist across a reboot (S3.20.1). A no-op if
  // nothing has been set yet.
  void hearthOnReconciled() override;

  // builds and sends "AT+MTMODES=<ep>,94,<mode1>,<tag1>,\"<label1>\",..."
  // for exactly the triples given; wire-only, no cache update (house
  // discipline: the caller decides whether/what to commit to the cache
  // afterwards).
  bool hearthSendModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);
};

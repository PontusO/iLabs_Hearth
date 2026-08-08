/*
 * MatterOperationalStateEndpoint.h - shared implementation core for the
 * OperationalState trio (Task C8): Laundry Washer, Dishwasher, Laundry
 * Dryer.
 *
 * AT_MT_SPEC.md S3.21: "This is the device type behind Laundry Washer
 * (0x0073), Dishwasher (0x0075) and Laundry Dryer (0x007C) (S3.9), all
 * three of which wire the identical base OperationalState cluster with no
 * device-specific extension." Every wire command, attribute id, and
 * command id below is therefore identical across all three device types;
 * the task brief calls out this sharing explicitly ("one implementation
 * shared by three thin classes, proven by a cross-class test"), so this
 * class holds the entire OperationalState wire contract, and
 * MatterLaundryWasher / MatterDishwasher / MatterLaundryDryer
 * (MatterLaundryWasher.h, MatterDishwasher.h, MatterLaundryDryer.h) are
 * thin public subclasses that add nothing but their own device type
 * constant, passed to hearthBeginOperationalState() below.
 *
 * Like MatterDoorLock (C3) and its later siblings, this has NO
 * arduino-esp32 counterpart: upstream's Matter library ships no
 * OperationalState-backed appliance class at all (see Hearth.h's umbrella
 * comment). There is nothing to mirror an API from, so the public surface
 * below is this port's own design, built directly against the firmware's
 * C4 wire contract (docs/AT_MT_SPEC.md S3.17/S3.21) and the task brief's
 * exact signatures.
 *
 * Cluster 0x0060 (96) is OperationalState (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/OperationalState/ClusterId.h, "inline
 * constexpr ClusterId Id = 0x00000060;", the file's own header comment
 * reading "cluster code: 96/0x60"); Pause is command 0x0000, Stop is
 * command 0x0001, Start is command 0x0002, and Resume is command 0x0003
 * (.../OperationalState/CommandIds.h, "namespace Pause { inline constexpr
 * CommandId Id = 0x00000000; }", "namespace Stop { inline constexpr
 * CommandId Id = 0x00000001; }", "namespace Start { inline constexpr
 * CommandId Id = 0x00000002; }", "namespace Resume { inline constexpr
 * CommandId Id = 0x00000003; }"). All five verified against the pinned
 * esp-matter checkout's own generated headers, not transcribed from the
 * brief that named them; given as plain integers in the .cpp, this
 * library's usual pattern, since there is no connectedhomeip header on a
 * host build.
 *
 * OperationalState_t below transcribes OperationalStateEnum from the same
 * pinned checkout (zzz_generated/app-common/clusters/OperationalState/
 * Enums.h):
 *
 *   enum class OperationalStateEnum : uint8_t
 *   {
 *       kStopped = 0x00,
 *       kRunning = 0x01,
 *       kPaused  = 0x02,
 *       kError   = 0x03,
 *       kUnknownEnumValue = 4,  // internal CHIP sentinel, never transmitted
 *   };
 *
 * AT_MT_SPEC.md S3.21 accepts only 0..2 for AT+MTOPSTATE's <state>: 3
 * (Error) is reserved for the device's own fault-detection path
 * (OnOperationalErrorDetected, not exposed on this AT surface) and is
 * rejected host-side by the firmware with +MTERR:1, never settable through
 * this command at all. This class does not duplicate that range check:
 * "the firmware never transcribes enum values (accessors or bridge-side
 * validation); the library transcribes with quoted header evidence" is the
 * governing rule, and no sibling class in this library duplicates a
 * single-integer enum range check either (MatterDoorLock::setLockState()
 * is the precedent). A rejected write surfaces as an ordinary failed
 * AT+MTOPSTATE: setOperationalState() returns false, cache untouched.
 *
 * **A deny IS the wire response here, unlike the water valve.** S3.21:
 * "the adjudication verdict IS the wire response: the SDK copies the
 * filled GenericOperationalError straight into the command's
 * OperationalCommandResponse... with ErrorStateID 0x02
 * (UnableToCompleteOperation)". onPause()/onResume()/onStart()/onStop()'s
 * verdict is therefore a real allow/deny the controller observes, the same
 * shape as MatterDoorLock and MatterChime, not MatterWaterValve's
 * discarded one.
 *
 * Design, following MatterWaterValve's own precedent (no arduino-esp32
 * counterpart, no initial state to reconcile):
 *
 * - Each subclass's public no-arg begin() takes no initial state: there is
 *   no sketch-declared initial OperationalState to reconcile against the
 *   C6's own cluster-creation default, so this class has no
 *   hearthOnReconciled() override and pushes nothing at reconcile time.
 *   The cache starts at kStateStopped and stays there until the sketch
 *   itself calls setOperationalState(), mirroring "the firmware never
 *   calls AT+MTOPSTATE on its own" (S3.21): actuation timing, and
 *   therefore the first real state report, belongs entirely to the host.
 * - onPause()/onResume()/onStart()/onStop() register the host's verdict
 *   for a firmware-forwarded Pause/Resume/Start/Stop invoke (AT_MT_SPEC.md
 *   S3.17); hearthOnForwardedCommand() below is where the dispatch
 *   actually lands. No callback registered denies by default, this
 *   library's usual fail-closed default.
 * - setOperationalState()/getOperationalState() report the appliance's
 *   actual state via AT+MTOPSTATE (S3.21), never AT+MTATTR: every
 *   OperationalState attribute is managed internally by the cluster's own
 *   SDK Instance (S3.21: "none of them is reachable over AT+MTATTR"), so
 *   attributeChangeCB() below is a documented no-op, the same shape
 *   MatterChime's header comment establishes for a cluster with no
 *   AT+MTATTR-reachable attribute at all. getOperationalState() is
 *   cache-only (no wire round trip), matching every sibling class's
 *   established getter convention.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterOperationalStateEndpoint : public MatterEndPoint {
public:
  // clang-format off
  // OperationalStateEnum protocol values (AT_MT_SPEC.md S3.21's <state>);
  // see the header comment above for the quoted pinned-header source.
  enum OperationalState_t {
    kStateStopped = 0,
    kStateRunning = 1,
    kStatePaused  = 2,
  };
  // clang-format on

  ~MatterOperationalStateEndpoint();

  // this will stop processing OperationalState Matter events
  void end();

  // register the host's verdict for a firmware-forwarded Pause / Resume /
  // Start / Stop command (AT_MT_SPEC.md S3.17). No callback registered
  // denies by default; see the header comment: unlike the water valve,
  // this verdict IS the real wire response the controller observes.
  void onPause(std::function<bool()> cb);
  void onResume(std::function<bool()> cb);
  void onStart(std::function<bool()> cb);
  void onStop(std::function<bool()> cb);

  // report the appliance's actual state once the host's own control loop
  // confirms it (AT+MTOPSTATE, AT_MT_SPEC.md S3.21); updates the cache
  // only on a successful write.
  bool setOperationalState(uint8_t state);
  // returns the cached operational state; no wire round trip.
  uint8_t getOperationalState();

  // this function is called by Matter internal event processor. It could be
  // overwritten by the application, if necessary. A documented no-op here:
  // see the header comment (no cluster attribute has an AT+MTATTR path).
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h): the firmware forwards a
  // controller-invoked Pause/Resume/Start/Stop here for a verdict; see
  // onPause()/onResume()/onStart()/onStop() above. Fails closed (false)
  // with no callback registered, for the wrong cluster, or for any command
  // id this class does not recognise.
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) override;

protected:
  MatterOperationalStateEndpoint();

  // Each thin subclass's begin() calls this with its own device type
  // constant; the wire contract past declaration is identical for all
  // three (AT_MT_SPEC.md S3.9/S3.21), so it lives here once. Declares
  // only; no initial state to reconcile (see the header comment).
  bool hearthBeginOperationalState(uint32_t deviceTypeId);

  bool started = false;
  uint8_t operationalState = kStateStopped;

  std::function<bool()> _onPauseCB = nullptr;
  std::function<bool()> _onResumeCB = nullptr;
  std::function<bool()> _onStartCB = nullptr;
  std::function<bool()> _onStopCB = nullptr;

  // wire-only AT+MTOPSTATE write; the caller decides whether/what to commit
  // to the cache (house discipline: a failed write must not update it).
  bool hearthSendOperationalState(uint8_t state);
};

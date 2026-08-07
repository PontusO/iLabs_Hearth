/*
 * MatterWaterValve.h - Task C7's water valve endpoint type.
 *
 * Like MatterDoorLock (Task C3), this has NO arduino-esp32 counterpart:
 * upstream's Matter library ships no water valve class at all (see Hearth.h's
 * umbrella comment). There is nothing to mirror an API from, so the public
 * surface below is this port's own design, built directly against the
 * firmware's C2 wire contract (docs/AT_MT_SPEC.md S3.17/S3.19) and the task
 * brief's exact signatures.
 *
 * Device type 0x0042 is water_valve
 * (esp_matter_endpoint.h's ESP_MATTER_WATER_VALVE_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:134,
 * "#define ESP_MATTER_WATER_VALVE_DEVICE_TYPE_ID 0x0042"). Cluster 0x0081
 * (129) is ValveConfigurationAndControl (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/ValveConfigurationAndControl/ClusterId.h,
 * "inline constexpr ClusterId Id = 0x00000081;", the file's own header
 * comment reading "cluster code: 129/0x81"); CurrentState is attribute
 * 0x0004 (.../ValveConfigurationAndControl/AttributeIds.h, "namespace
 * CurrentState { inline constexpr AttributeId Id = 0x00000004; }"); Open is
 * command 0x0000 and Close is command 0x0001
 * (.../ValveConfigurationAndControl/CommandIds.h, "namespace Open { inline
 * constexpr CommandId Id = 0x00000000; }" and "namespace Close { inline
 * constexpr CommandId Id = 0x00000001; }"). All four verified against the
 * pinned esp-matter checkout's own generated headers, not transcribed from
 * the brief that named them; given as plain integers in the .cpp, this
 * library's usual pattern, since there is no connectedhomeip header on a
 * host build.
 *
 * ValveState_t below transcribes ValveStateEnum from the same pinned
 * checkout (zzz_generated/app-common/clusters/ValveConfigurationAndControl/
 * Enums.h):
 *
 *   enum class ValveStateEnum : uint8_t   (Enums.h:43-53)
 *   {
 *       kClosed        = 0x00,
 *       kOpen          = 0x01,
 *       kTransitioning = 0x02,
 *       kUnknownEnumValue = 3,  // internal CHIP sentinel, never transmitted
 *   };
 *
 * AT_MT_SPEC.md S3.19 accepts only 0..2 for AT+MTVALVE's <state> (matching
 * ValveStateEnum's three real values exactly) and 0..100 for the optional
 * <level>, so ValveState_t below exposes exactly the state range, nothing
 * past it.
 *
 * **The verdict onOpen()/onClose() give cannot fail the command on the
 * wire.** Unlike the door lock, where a deny surfaces to the controller as
 * Status::Failure, ValveConfigurationAndControl's own server calls the
 * delegate's HandleOpenValve/HandleCloseValve synchronously and discards
 * what they return (S3.19: "TEMPORARY_RETURN_IGNORED at both delegate call
 * sites, valve-configuration-and-control-cluster.cpp" -- an SDK property,
 * not a firmware or library choice). The controller always sees
 * Status::Success once the command reaches the host at all. The verdict
 * this class's hearthOnForwardedCommand() still relays as the
 * AT+MTCMDRESP argument, and it is still sent -- the generic dispatcher
 * (Hearth.cpp) does not know per-endpoint-type that this particular reply
 * is discarded -- but it decides nothing the controller can observe. What
 * it DOES gate is purely host-side: whether the sketch's own onOpen()/
 * onClose() callback goes on to actually move the physical valve, entirely
 * outside this library's scope. A denying callback must still return false
 * for that reason (it is the only signal the sketch has that "no, don't
 * actuate"), it just does not mean what a denying onLock() means.
 *
 * Design, following MatterDoorLock's own precedent and this library's B120
 * norm where it applies:
 *
 * - begin() takes no initial state, unlike MatterDoorLock's begin(locked):
 *   there is no sketch-declared initial CurrentState to reconcile against
 *   the C6's own cluster-creation default, so this class has no
 *   hearthOnReconciled() override and pushes nothing at reconcile time.
 *   The cache starts at kStateClosed and stays there until the sketch
 *   itself calls setValveState(), exactly mirroring the "the firmware
 *   never calls AT+MTVALVE on its own" wire fact (S3.19): actuation timing,
 *   and therefore the first real state report, belongs entirely to the
 *   host.
 * - onOpen()/onClose() register the host's verdict for a firmware-forwarded
 *   Open/Close invoke (AT_MT_SPEC.md S3.17); hearthOnForwardedCommand()
 *   (MatterEndPoint.h) is where the dispatch actually lands. No callback
 *   registered denies by default, the same fail-closed default as every
 *   other +MTCMD consumer in this library, even though (see above) a deny
 *   here cannot change what the controller sees.
 * - setValveState()/getValveState() report the valve's actual state via
 *   AT+MTVALVE (S3.19), never AT+MTATTR: like the door lock's LockState,
 *   the firmware never decides that the valve moved on its own, so the
 *   host, and only the host, owns telling it. The cache updates only on a
 *   successful write, this library's usual failed-write discipline.
 *   getValveState() is cache-only (no wire round trip), matching every
 *   sibling class's established getter convention, and is the class's
 *   "URC-fed cache": attributeChangeCB() below is what a genuine
 *   CurrentState report (were one ever raised) would feed.
 * - <level> is accepted (the two-arg setValveState() overload) but never
 *   cached: S3.19 states plainly that this SDK revision's water_valve
 *   thunk fixes FeatureMap at 0, so CurrentLevel/TargetLevel are never
 *   created as attributes at all, and a level sent through AT+MTVALVE
 *   "neither errors nor does anything a controller can observe". There is
 *   therefore nothing for a getValveLevel() to read back (S3.19's own
 *   documented +MTERR:4), so this class does not add one; see the task
 *   brief's own signature list, which omits it too.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterWaterValve : public MatterEndPoint {
public:
  // clang-format off
  // ValveStateEnum protocol values (AT_MT_SPEC.md S3.19's <state>); see the
  // header comment above for the quoted pinned-header source.
  enum ValveState_t {
    kStateClosed        = 0,
    kStateOpen          = 1,
    kStateTransitioning = 2,
  };
  // clang-format on

  MatterWaterValve();
  ~MatterWaterValve();

  // declares only; no initial state to reconcile (see the header comment).
  bool begin();
  // this will stop processing Water Valve Matter events
  void end();

  // register the host's verdict for a firmware-forwarded Open / Close
  // command (AT_MT_SPEC.md S3.17). No callback registered denies by
  // default; see the header comment for what a deny actually gates here
  // (host-side actuation only, never the wire response).
  void onOpen(std::function<bool()> cb);
  void onClose(std::function<bool()> cb);

  // report the valve's actual state once the host's own mechanism confirms
  // it moved (AT+MTVALVE, AT_MT_SPEC.md S3.19); updates the cache only on a
  // successful write. The two-arg overload additionally reports a level
  // (0..100); see the header comment for why it is not cached or readable
  // back.
  bool setValveState(uint8_t state);
  bool setValveState(uint8_t state, uint8_t level);
  // returns the cached valve state; no wire round trip (this library's
  // usual getter convention). URC-fed: attributeChangeCB() below is what
  // would update it from a genuine CurrentState report.
  uint8_t getValveState();

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  // Hearth's own addition (MatterEndPoint.h): the firmware forwards a
  // controller-invoked Open/Close here for a verdict; see onOpen()/
  // onClose() above. Fails closed (false) with no callback registered, for
  // the wrong cluster, or for any command id this class does not
  // recognise -- same fail-closed shape as MatterDoorLock, even though
  // (header comment) a deny here cannot fail the command on the wire.
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) override;

protected:
  bool started = false;
  uint8_t valveState = kStateClosed;

  std::function<bool()> _onOpenCB = nullptr;
  std::function<bool()> _onCloseCB = nullptr;

  // wire-only AT+MTVALVE write; the caller decides whether/what to commit
  // to the cache (house discipline: a failed write must not update it).
  bool hearthSendValveState(uint8_t state, bool hasLevel, uint8_t level);
};

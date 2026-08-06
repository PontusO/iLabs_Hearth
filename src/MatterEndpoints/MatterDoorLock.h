/*
 * MatterDoorLock.h - Task C3's door lock endpoint type.
 *
 * Unlike every other class in this library, this one has NO arduino-esp32
 * counterpart: upstream's Matter library ships no door lock class at all
 * (see Hearth.h's umbrella comment). There is nothing to mirror an API
 * from, so the public surface below is this port's own design, built
 * directly against the firmware's C3 wire contract
 * (docs/AT_MT_SPEC.md S3.17-S3.18) and the task brief's exact signatures.
 *
 * Device type 0x000A is door_lock
 * (esp_matter_endpoint.h's ESP_MATTER_DOOR_LOCK_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:104,
 * "#define ESP_MATTER_DOOR_LOCK_DEVICE_TYPE_ID 0x000A"). Cluster 0x0101
 * (257) is DoorLock (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/DoorLock/ClusterId.h:14,
 * "inline constexpr ClusterId Id = 0x00000101;"); LockState is attribute
 * 0x0000 (.../DoorLock/AttributeIds.h, "namespace LockState { inline
 * constexpr AttributeId Id = 0x00000000; }"); LockDoor is command 0x0000
 * and UnlockDoor is command 0x0001 (.../DoorLock/CommandIds.h, "namespace
 * LockDoor { inline constexpr CommandId Id = 0x00000000; }" and "namespace
 * UnlockDoor { inline constexpr CommandId Id = 0x00000001; }"). All four
 * verified against the pinned esp-matter checkout's own generated headers,
 * not transcribed from the brief that named them; given as plain integers
 * in the .cpp, this library's usual pattern, since there is no
 * connectedhomeip header on a host build.
 *
 * LockState_t and OperationSource_t below transcribe DlLockState and
 * OperationSourceEnum from the same pinned checkout
 * (zzz_generated/app-common/clusters/DoorLock/Enums.h):
 *
 *   enum class DlLockState : uint8_t          (Enums.h:95-106)
 *   {
 *       kNotFullyLocked = 0x00,
 *       kLocked         = 0x01,
 *       kUnlocked       = 0x02,
 *       kUnlatched      = 0x03,
 *       kUnknownEnumValue = 4,  // internal CHIP sentinel, never transmitted
 *   };
 *
 *   enum class OperationSourceEnum : uint8_t  (Enums.h:318-337)
 *   {
 *       kUnspecified       = 0x00,
 *       kManual            = 0x01,
 *       kProprietaryRemote = 0x02,
 *       kKeypad            = 0x03,
 *       kAuto              = 0x04,
 *       kButton            = 0x05,
 *       kSchedule          = 0x06,
 *       kRemote            = 0x07,
 *       kRfid              = 0x08,
 *       kBiometric         = 0x09,
 *       kAliro             = 0x0A,
 *       kUnknownEnumValue = 11,  // internal CHIP sentinel, never transmitted
 *   };
 *
 * AT_MT_SPEC.md S3.18 accepts only 0..2 for AT+MTLOCK's <state> (kUnlatched
 * and the internal sentinel are not valid values to report over this
 * command) and only 0..10 for <source> (every defined OperationSourceEnum
 * value except the internal sentinel), so LockState_t/OperationSource_t
 * below expose exactly those ranges, nothing past them.
 *
 * Design, following the task brief's own notes and this library's B120
 * norm (MatterTemperatureControlledCabinet's fix round 2):
 *
 * - begin(locked) only declares (hearthDeclare(), device type 0x000A,
 *   variant 0) and seeds the cache; it issues no AT traffic itself. The
 *   declared state reaches the C6 from hearthOnReconciled() instead, once
 *   per reconcile, as a direct AT+MTLOCK write that bypasses
 *   setLockState()'s own skip-if-equal -- the same shape as the cabinet's
 *   TemperatureNumber push and for the same reason: the C6 creates the
 *   DoorLock cluster at its own default LockState, not the sketch's, so
 *   only an unconditional push can make the cache and the device agree in
 *   the first place. Only after that does setLockState()'s skip-if-equal
 *   become the sound optimisation it already is for every other class.
 * - onLock()/onUnlock() register the host's verdict for a firmware-forwarded
 *   LockDoor/UnlockDoor invoke (AT_MT_SPEC.md S3.17); hearthOnForwardedCommand()
 *   (MatterEndPoint.h) is where the dispatch actually lands. No callback
 *   registered denies by default: a lock fails closed, never open, on
 *   every path that is not an explicit allow (spec's own words, S3.17).
 * - setLockState()/lock()/unlock() report the door's actual state via
 *   AT+MTLOCK (S3.18), never AT+MTATTR: the firmware never calls AT+MTLOCK's
 *   effect on its own (spec F4 / S3.18's own note), so the host, and only
 *   the host, owns telling it the bolt actually moved, once its own
 *   mechanism confirms it. The cache updates only on a successful write,
 *   this library's usual failed-write discipline.
 * - getLockState() and attributeChangeCB() are cache-only / cache-updating,
 *   matching every sibling class's established convention (no wire round
 *   trip on a read).
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterDoorLock : public MatterEndPoint {
public:
  // clang-format off
  // DlLockState protocol values (AT_MT_SPEC.md S3.18's <state>); see the
  // header comment above for the quoted pinned-header source.
  enum LockState_t {
    kStateNotFullyLocked = 0,
    kStateLocked         = 1,
    kStateUnlocked       = 2,
  };

  // OperationSourceEnum protocol values (AT_MT_SPEC.md S3.18's <source>);
  // see the header comment above for the quoted pinned-header source.
  enum OperationSource_t {
    kSourceUnspecified       = 0,
    kSourceManual            = 1,
    kSourceProprietaryRemote = 2,
    kSourceKeypad            = 3,
    kSourceAuto              = 4,
    kSourceButton            = 5,
    kSourceSchedule          = 6,
    kSourceRemote            = 7,
    kSourceRfid              = 8,
    kSourceBiometric         = 9,
    kSourceAliro             = 10,
  };
  // clang-format on

  MatterDoorLock();
  ~MatterDoorLock();

  // begin with the door's initial lock state (true = Locked, false =
  // Unlocked). Declares only; the state itself reaches the C6 at the next
  // Matter.begin() reconcile (hearthOnReconciled()), not from begin() itself.
  bool begin(bool locked = true);
  // this will stop processing Door Lock Matter events
  void end();

  // register the host's verdict for a firmware-forwarded LockDoor /
  // UnlockDoor command (AT_MT_SPEC.md S3.17). No callback registered
  // denies by default (fail closed).
  void onLock(std::function<bool()> cb);
  void onUnlock(std::function<bool()> cb);

  // report the door's actual state once the host's own mechanism confirms
  // it moved (AT+MTLOCK, AT_MT_SPEC.md S3.18); updates the cache only on a
  // successful write.
  bool setLockState(uint8_t state, uint8_t source = kSourceManual);
  bool lock();    // setLockState(kStateLocked, kSourceManual)
  bool unlock();  // setLockState(kStateUnlocked, kSourceManual)
  // returns the cached lock state; no wire round trip (this library's usual
  // getter convention -- see e.g. MatterFan.cpp's header comment).
  uint8_t getLockState();

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

  // Hearth's own addition (MatterEndPoint.h): the firmware forwards a
  // controller-invoked LockDoor/UnlockDoor here for a verdict; see
  // onLock()/onUnlock() above. Fails closed (false) with no callback
  // registered, for the wrong cluster, or for any command id this class
  // does not recognise.
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id) override;

protected:
  bool started = false;
  uint8_t lockState = kStateNotFullyLocked;

  std::function<bool()> _onLockCB = nullptr;
  std::function<bool()> _onUnlockCB = nullptr;

  // wire-only AT+MTLOCK write; the caller decides whether/what to commit to
  // the cache (house discipline: a failed write must not update it).
  bool hearthSendLockState(uint8_t state, uint8_t source);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: pushes the cached lock state directly to the wire via
  // AT+MTLOCK, bypassing setLockState()'s own skip-if-equal. See this
  // file's header comment (B120 norm) for why the push, not the setter,
  // is what has to run here.
  void hearthOnReconciled() override;
};

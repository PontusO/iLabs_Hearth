/*
 * MatterSmokeCOAlarm.h - Task C8's Smoke/CO Alarm endpoint type.
 *
 * Like MatterDoorLock (C3) and its later siblings, this has NO
 * arduino-esp32 counterpart: upstream's Matter library ships no Smoke/CO
 * Alarm class at all (see Hearth.h's umbrella comment). There is nothing
 * to mirror an API from, so the public surface below is this port's own
 * design, built directly against the firmware's C5 wire contract
 * (docs/AT_MT_SPEC.md S3.17/S3.22) and the task brief's exact signatures.
 *
 * Device type 0x0076 is smoke_co_alarm
 * (esp_matter_endpoint.h's ESP_MATTER_SMOKE_CO_ALARM_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:91,
 * "#define ESP_MATTER_SMOKE_CO_ALARM_DEVICE_TYPE_ID 0x0076"). Cluster
 * 0x005C (92) is SmokeCoAlarm (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/SmokeCoAlarm/ClusterId.h, "inline
 * constexpr ClusterId Id = 0x0000005C;", the file's own header comment
 * reading "cluster code: 92/0x5C"). SelfTestRequest is command 0x0000
 * (.../SmokeCoAlarm/CommandIds.h, "namespace SelfTestRequest { inline
 * constexpr CommandId Id = 0x00000000; }"). ExpressedState is attribute
 * 0x0000 (.../SmokeCoAlarm/AttributeIds.h, "namespace ExpressedState {
 * inline constexpr AttributeId Id = 0x00000000; }"), an enum8
 * (.../SmokeCoAlarm/Enums.h's "enum class ExpressedStateEnum : uint8_t").
 * All four verified against the pinned esp-matter checkout's own generated
 * headers; given as plain integers in the .cpp, this library's usual
 * pattern, since there is no connectedhomeip header on a host build.
 *
 * AT_MT_SPEC.md S3.22's own field table maps AT+MTALARM=<ep>,<field>,<value>
 * to eleven of the cluster's own Set* methods (field 0/ExpressedState is
 * derived, never settable directly):
 *
 *   field  1  SmokeState              setSmokeState()
 *   field  2  COState                 setCOState()
 *   field  3  BatteryAlert            setBatteryAlert()
 *   field  4  DeviceMuted             setDeviceMuted()
 *   field  5  TestInProgress          completeSelfTest() (value 0 only)
 *   field  6  HardwareFaultAlert      setHardwareFaultAlert()
 *   field  7  EndOfServiceAlert       setEndOfServiceAlert()
 *   field  8  InterconnectSmokeAlarm  setInterconnectSmokeAlarm()
 *   field  9  InterconnectCOAlarm     setInterconnectCOAlarm()
 *   field 10  ContaminationState      setContaminationState()
 *   field 11  SmokeSensitivityLevel   setSmokeSensitivityLevel()
 *
 * None of the eleven setters (nor completeSelfTest()) validates its
 * <value> against its field's own enum range host-side: AT_MT_SPEC.md's
 * governing rule is "the firmware never transcribes enum values (accessors
 * or bridge-side validation); the library transcribes with quoted header
 * evidence", so the range check belongs to the firmware's own AT+MTALARM
 * handler (+MTERR:1), not duplicated here -- the same "no host-side enum
 * range check" precedent MatterDoorLock::setLockState() and
 * MatterOperationalStateEndpoint::setOperationalState() both establish for
 * a single-integer enum field.
 *
 * **The self-test lifecycle is notify-only, not adjudicated.** S3.22:
 * SmokeCoAlarmServer::HandleRemoteSelfTestRequest answers the controller
 * itself before the app-level hook ever runs, so "+MTCMD:0,<ep>,92,0"
 * always arrives under seq 0 (AT_MT_SPEC.md S3.17's notify-only form): the
 * sketch's onSelfTest() callback runs (this class's
 * hearthOnForwardedCommand()), but there is no verdict to send back and
 * this library's dispatcher never tries (Hearth.cpp's hearthDispatchCmd(),
 * seq 0 branch). completeSelfTest() is the other half: it reports
 * "AT+MTALARM=<ep>,5,0" once the host's own self-test actually finishes,
 * which the SDK's own SetTestInProgress recognises as the true-to-false
 * edge and fires SelfTestComplete on the fabric.
 *
 * **ExpressedState is the one attribute this class reads back over the
 * wire, not from a cache.** It is derived server-side from the ten states
 * above and never settable (field 0 in the table answers +MTERR:1), so
 * unlike every setter's own cached getter there is no host-side value to
 * cache in the first place: getExpressedState() is a genuine AT+MTATTR
 * read (getAttributeVal(), the base class's live wire round trip) -- the
 * first real consumer of that path in this library. Every sibling class
 * avoids calling it as a redundant read-before-write (see MatterFan.cpp's
 * own comment on why), but there is no write here for a read to be
 * redundant against.
 *
 * Design, following MatterChime's own precedent (no arduino-esp32
 * counterpart, no attribute reachable through the generic attribute
 * store's write path):
 *
 * - begin() takes no initial state: there is no sketch-declared initial
 *   value for any of the ten Set*-backed states to reconcile against the
 *   C6's own cluster-creation default, so this class has no
 *   hearthOnReconciled() override and pushes nothing at reconcile time.
 *   Every cache starts at 0 (Normal/NotMuted/High as appropriate) and
 *   stays there until the sketch itself calls the matching setter,
 *   mirroring "the firmware never calls AT+MTALARM on its own".
 * - onSelfTest() registers the host's SelfTestRequest handler; see the
 *   notify-only note above. hearthOnForwardedCommand() returns true when a
 *   callback is registered (there being no verdict to fail) and false
 *   otherwise: the dispatcher discards the return value for seq 0 either
 *   way (Hearth.cpp), so this is documentation of "did this class actually
 *   run something", not a wire-observable signal.
 * - The ten setters are wire-only writes through AT+MTALARM, never
 *   AT+MTATTR (S3.22: "AT+MTATTR is not the intended path to this
 *   cluster's state"): each updates its own cache only on a successful
 *   write, this library's usual failed-write discipline, and each is a
 *   no-op (no wire traffic) when the new value already matches the cache.
 * - attributeChangeCB() is a documented no-op, the same shape
 *   MatterChime's header comment establishes: nothing in this class's
 *   cache is fed by a +MTATTR URC, since every write goes through
 *   AT+MTALARM and the one read (ExpressedState) is a synchronous
 *   getAttributeVal() reply consumed directly by that call, never routed
 *   through the generic dispatcher; hearthAttrTypeFor() is likewise not
 *   overridden, for the same reason (getAttributeVal()'s caller pre-sets
 *   the target type itself, see MatterEndPoint.h's own comment).
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterSmokeCOAlarm : public MatterEndPoint {
public:
  MatterSmokeCOAlarm();
  ~MatterSmokeCOAlarm();

  // declares only; no initial state to reconcile (see the header comment).
  bool begin();
  // this will stop processing Smoke/CO Alarm Matter events
  void end();

  // register the host's SelfTestRequest handler (AT_MT_SPEC.md S3.17/S3.22
  // notify-only form). No callback registered is a documented no-op; see
  // the header comment for why there is no verdict here at all.
  void onSelfTest(std::function<void()> cb);
  // report self-test completion: AT+MTALARM=<ep>,5,0 (field 5/TestInProgress
  // false), which the SDK recognises as the true-to-false edge and fires
  // SelfTestComplete on the fabric. Always reaches the wire: there is no
  // cached TestInProgress value to compare against.
  bool completeSelfTest();

  // AT+MTALARM's fields 1-4 and 6-11 (AT_MT_SPEC.md S3.22's own table);
  // each reports state through the cluster's own Set* method, updating the
  // cache only on a successful write, and is a no-op when the value is
  // already cached.
  bool setSmokeState(uint8_t s);
  bool setCOState(uint8_t s);
  bool setBatteryAlert(uint8_t s);
  bool setDeviceMuted(uint8_t s);
  bool setHardwareFaultAlert(bool v);
  bool setEndOfServiceAlert(uint8_t v);
  bool setInterconnectSmokeAlarm(uint8_t s);
  bool setInterconnectCOAlarm(uint8_t s);
  bool setContaminationState(uint8_t s);
  bool setSmokeSensitivityLevel(uint8_t s);

  // cached getters; no wire round trip. Mirror the ten setters above.
  uint8_t getSmokeState();
  uint8_t getCOState();
  uint8_t getBatteryAlert();
  uint8_t getDeviceMuted();
  bool getHardwareFaultAlert();
  uint8_t getEndOfServiceAlert();
  uint8_t getInterconnectSmokeAlarm();
  uint8_t getInterconnectCOAlarm();
  uint8_t getContaminationState();
  uint8_t getSmokeSensitivityLevel();

  // ExpressedState: derived, read-only, never cached (see the header
  // comment); a genuine AT+MTATTR read on every call. Returns 0 (Normal)
  // if the read fails.
  uint8_t getExpressedState();

  // this function is called by Matter internal event processor. It could be
  // overwritten by the application, if necessary. A documented no-op here:
  // see the header comment.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h): the firmware forwards a
  // controller-invoked SelfTestRequest here, notify-only; see onSelfTest()
  // above.
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) override;

protected:
  bool started = false;

  uint8_t smokeState = 0;
  uint8_t coState = 0;
  uint8_t batteryAlert = 0;
  uint8_t deviceMuted = 0;
  bool hardwareFaultAlert = false;
  uint8_t endOfServiceAlert = 0;
  uint8_t interconnectSmokeAlarm = 0;
  uint8_t interconnectCOAlarm = 0;
  uint8_t contaminationState = 0;
  uint8_t smokeSensitivityLevel = 0;

  std::function<void()> _onSelfTestCB = nullptr;

  // wire-only AT+MTALARM write; the caller decides whether/what to commit
  // to the cache (house discipline: a failed write must not update it).
  bool hearthSendAlarmField(uint8_t field, uint8_t value);
};

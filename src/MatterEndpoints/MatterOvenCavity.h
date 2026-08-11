/*
 * MatterOvenCavity.h - Task 9 (composed-appliance round): the oven cavity,
 * this library's first TYPED owned child. A Temperature Controlled Cabinet
 * (device type 0x0071) composed under an Oven (0x007B), which is what gives
 * it the Heater conditional cluster set (AT_MT_SPEC.md S3.9's 0x0071
 * table): OvenMode (0x49, 73 decimal, AT+MTMODES's cluster-aware form,
 * S3.20.1) and OvenCavityOperationalState (0x48, 72 decimal, AT+MTOPSTATE
 * S3.21 and adjudicated +MTCMD forwards S3.17).
 *
 * Like MatterRefrigerator (Task 8) and every Hearth-original class, this
 * has NO arduino-esp32 counterpart (see Hearth.h's umbrella comment); the
 * public surface below is this port's own design against the firmware's
 * wire contract and the task brief's interface sketch.
 *
 * SUBCLASSES MatterTemperatureControlledCabinet rather than reimplementing
 * it: the cavity IS a cabinet on the wire (same device type, same variant
 * byte, same TemperatureControl cluster 0x56), so the temperature machinery
 * (both begin() argument shapes, the setters/getters, the reconcile push
 * that fix round 2's bench bug mandated, attributeChangeCB and
 * hearthAttrTypeFor) is inherited, not transcribed. What the subclass
 * changes:
 *
 * - OWNED ONLY, no public standalone begin path. The cabinet base class is
 *   standalone-legal by design; the cavity is not: its conditional clusters
 *   exist ONLY when the firmware derives them from an Oven parent, so a
 *   standalone cavity endpoint would answer nothing this class promises.
 *   Both begin() overloads below therefore refuse outright unless
 *   MatterOven::addCavity() marked this object owned, and never declare
 *   anything themselves (the owner pattern Task 8's cabinet established:
 *   the parent's declaration carries the parent index, and a self-declare
 *   would wipe it via the registry's in-place update).
 * - The modes API targets OvenMode (73), not the fridge-cabinet cluster
 *   (82) the base class serves for refrigerator-owned cabinets. The
 *   inherited cluster-82 members are hidden or skipped: setSupportedModes()
 *   below hides the base version (own storage, own cluster id on the wire),
 *   and hearthOnForwardedCommandFields() below defers unmatched forwards to
 *   MatterEndPoint's fail-closed default DIRECTLY, deliberately skipping
 *   the cabinet's owned-cabinet cluster-82 adjudication: a spurious
 *   cluster-82 forward on a cavity must be denied without consulting any
 *   callback. onChangeMode()/getCurrentMode() are inherited as-is; they
 *   only store the verdict callback and read the cached mode, and this
 *   class's own dispatch is the only thing that updates that cache.
 * - Tag 0 in a setSupportedModes() triple asks the firmware for OvenMode's
 *   own conformance default, kBake (0x4000) on every mode (S3.20.1's tag
 *   table), not the fridge cluster's kAuto.
 * - The operational-state surface is Stop/Start ONLY. OvenCavity-
 *   OperationalState marks Pause (0) and Resume (3) disallowConform
 *   (OperationalState_Oven.xml revision 2, S3.17/S3.21): the firmware
 *   registers neither command and never forwards them, and this class has
 *   NO onPause/onResume members at all, so the compiler enforces the legal
 *   surface on a typed reference. A spurious injected (72,0)/(72,3)
 *   forward is denied without reaching any callback.
 * - setOperationalState() enforces plain {0,1,2} membership HOST-side
 *   (Hearth.hearthSetError(1), no wire traffic), the task brief's explicit
 *   requirement for this typed child. This deliberately diverges from the
 *   OperationalState trio's "the firmware validates the enum" precedent:
 *   mt_at.c's own handler only checks the UNION of both clusters' legal
 *   sets (S3.21), so an RVC-only value like 0x40 would otherwise travel
 *   the wire just to be rejected by the bridge; the cavity's legal set is
 *   closed and known at compile time, so the host refuses first.
 *
 * CurrentMode and the operational state both follow the 0.6.0
 * Instance-served rule (S3.20.1/S3.21): no +MTATTR URC ever fires for
 * either, so getCurrentMode() updates only on an onChangeMode() allow
 * verdict (B196: a same-mode ChangeToMode short-circuits firmware-side and
 * never reaches this host), and getOperationalState() is this host's own
 * bookkeeping over its own successful AT+MTOPSTATE writes. The verdict a
 * Stop/Start callback returns IS the wire response the controller
 * observes (a deny answers ErrorStateID 0x02, the trio's S3.21 shape).
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"

class MatterOvenCavity : public MatterTemperatureControlledCabinet {
public:
  // Maps to the 0x0071 variant byte (AT_MT_SPEC.md S3.9), the same space
  // MatterRefrigerator::CabinetFlavour_t covers: NUMBER builds a
  // TemperatureNumber cavity, LEVELS a TemperatureLevel one. The owned
  // cavity's begin() overload must match the flavour addCavity() declared.
  enum CabinetFlavour_t {
    NUMBER = 0,
    LEVELS = 1
  };

  MatterOvenCavity();
  ~MatterOvenCavity();

  // begin with the TemperatureNumber feature: the cabinet base class's
  // exact argument shape and defaults (upstream's own), cache-only when
  // owned; an oven sketch will normally pass real values (the example uses
  // 180.0, 30.0, 300.0, 5.0). Refused on a cavity no oven owns: there is
  // no standalone begin path (see the header comment).
  bool begin(double tempSetpoint = 0.00, double minTemperature = -10.0, double maxTemperature = 32.0, double step = 0.50);
  // begin with the TemperatureLevel feature, same base-class shape; same
  // owned-only refusal.
  bool begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel = 0);

  // Replace OvenMode's (0x49) SupportedModes list on THIS cavity's own
  // endpoint (AT_MT_SPEC.md S3.20.1): 1..8 mode/tag/label triples, each
  // mode 0..255 unique within the call, each tag a bare u16 (0 = kBake,
  // this cluster's conformance default on every mode), each label 1..32
  // bytes of printable ASCII with no '"'. Re-sent automatically on every
  // later reconcile. Hides the base class's fridge-cabinet (0x52) version;
  // see the header comment.
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // onChangeMode()/getCurrentMode() are inherited from the cabinet base
  // class unchanged: they store the verdict callback and read the cached
  // mode, and this class's own (73,0) dispatch below is the only updater.

  // OvenCavityOperationalState (0x48): register the host's verdict for a
  // firmware-forwarded Stop (command 1) / Start (command 2) invoke
  // (AT_MT_SPEC.md S3.17). No callback registered denies by default; the
  // verdict IS the wire response the controller observes. There are
  // deliberately NO onPause/onResume members: the cluster disallows both
  // commands (see the header comment).
  void onStop(std::function<bool()> cb);
  void onStart(std::function<bool()> cb);

  // report the cavity's actual state once the host's own control loop
  // confirms it (AT+MTOPSTATE, AT_MT_SPEC.md S3.21): plain {0,1,2}
  // (Stopped/Running/Paused), anything else refused host-side with
  // Hearth.hearthSetError(1) and no wire traffic. Updates the cache only
  // on a successful write.
  bool setOperationalState(uint8_t state);
  // returns the cached operational state; no wire round trip (S3.21: the
  // attribute is Instance-served, AT+MTATTR cannot reach it).
  uint8_t getOperationalState();

  // Task 9: adjudicates ChangeToMode on this cavity's OvenMode cluster
  // (73,0) and Stop/Start on its OvenCavityOperationalState cluster
  // (72,1)/(72,2). Everything else, INCLUDING a spurious fridge-cabinet
  // cluster-82 forward, defers to MatterEndPoint's fail-closed default
  // directly, skipping the cabinet base class's owned-cabinet
  // adjudication (see the header comment).
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

protected:
  // clang-format off
  static const uint8_t kMaxOvenModes        = 8;  // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t kMaxOvenModeLabelLen = 32; // S3.20.1: 1..32 printable ASCII bytes
  // clang-format on

  // OvenMode list storage, separate from the base class's fridge-cabinet
  // arrays on purpose: the base reconcile hook resends ITS arrays on
  // cluster 82 whenever its own count is non-zero, and a cavity must never
  // populate those (this class's setSupportedModes() hides the base one).
  uint8_t ovenModes[kMaxOvenModes];
  uint16_t ovenTags[kMaxOvenModes];
  char ovenModeLabels[kMaxOvenModes][kMaxOvenModeLabelLen + 1];  // +1 for the terminating NUL
  uint8_t ovenModesCount = 0;

  uint8_t cavityOperationalState = 0;  // kStopped, the SDK's own boot state

  std::function<bool()> _onStopCB = nullptr;
  std::function<bool()> _onStartCB = nullptr;

  // builds and sends "AT+MTMODES=<ep>,73,<mode1>,<tag1>,\"<label1>\",..."
  // for exactly the triples given; wire-only, no cache update (house
  // discipline: the caller decides whether/what to commit afterwards).
  bool hearthSendOvenModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  // wire-only AT+MTOPSTATE write; the caller decides whether/what to
  // commit to the cache (a failed write must not update it).
  bool hearthSendOperationalState(uint8_t state);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile: the base
  // class pushes the cached temperature configuration (its cluster-82
  // resend is structurally dead here, see above), then this override
  // resends the cached OvenMode list, which the firmware does not persist
  // across a reboot (S3.20.1). No operational-state resend: the firmware
  // never calls AT+MTOPSTATE on its own and neither does this class
  // outside setOperationalState() (the trio's S3.21 convention).
  void hearthOnReconciled() override;

  // MatterOven sets the inherited ownership flags (hearthOwnedByFridge,
  // reused here to mean "owned by the composing appliance", plus
  // hearthOwnedInert/hearthOwnedLevels) from addCavity()/its constructor,
  // exactly the way MatterRefrigerator drives the cabinet base class.
  friend class MatterOven;
};

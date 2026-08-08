/*
 * MatterChime.h - Task C7's Chime endpoint type.
 *
 * Like MatterDoorLock (C3) and its C7 siblings MatterWaterValve and
 * MatterModeSelect, this has NO arduino-esp32 counterpart: upstream's
 * Matter library ships no Chime class at all (see Hearth.h's umbrella
 * comment). There is nothing to mirror an API from, so the public surface
 * below is this port's own design, built directly against the firmware's
 * C6 wire contract (docs/AT_MT_SPEC.md S3.17/S3.23/S3.24) and the task
 * brief's exact signatures.
 *
 * Device type 0x0146 is chime
 * (esp_matter_endpoint.h's ESP_MATTER_CHIME_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:162,
 * "#define ESP_MATTER_CHIME_DEVICE_TYPE_ID 0x0146"). Cluster 0x0556 (1366)
 * is Chime (connectedhomeip's zap-generated
 * zzz_generated/app-common/clusters/Chime/ClusterId.h,
 * "inline constexpr ClusterId Id = 0x00000556;", the file's own header
 * comment reading "cluster code: 1366/0x556"); InstalledChimeSounds is
 * attribute 0x0000, SelectedChime is 0x0001, Enabled is 0x0002
 * (.../Chime/AttributeIds.h, "namespace InstalledChimeSounds { ... Id =
 * 0x00000000; }", "namespace SelectedChime { ... Id = 0x00000001; }",
 * "namespace Enabled { ... Id = 0x00000002; }"); PlayChimeSound is command
 * 0x0000 (.../Chime/CommandIds.h, "namespace PlayChimeSound { inline
 * constexpr CommandId Id = 0x00000000; }"). All five verified against the
 * pinned esp-matter checkout's own generated headers, not transcribed from
 * the brief that named them; given as plain integers in the .cpp, this
 * library's usual pattern, since there is no connectedhomeip header on a
 * host build.
 *
 * **None of the three attributes above has an AT+MTATTR path.** S3.23/
 * S3.24: "this cluster is registered directly with esp-matter's data model
 * provider rather than through its generic attribute store, so
 * AT+MTATTR has no path to either attribute" -- and the same is true of
 * InstalledChimeSounds (S3.23, "served by the cluster's own ChimeDelegate
 * mechanism"). AT+MTCHIMESOUNDS (installed sounds) and AT+MTCHIME
 * (SelectedChime/Enabled) are therefore the ONLY wire paths to this
 * cluster's state in either direction: there is no read-back command
 * either, so setSelectedChime()/getSelectedChime() and setEnabled()/
 * getEnabled() below are cache-only in both directions, and
 * attributeChangeCB() is a documented no-op (present only because
 * MatterEndPoint declares it pure virtual, the same shape
 * MatterGenericSwitch's own header comment establishes for a class with no
 * AT+MTATTR-reachable attribute at all); hearthAttrTypeFor() is likewise
 * not overridden, for the same reason.
 *
 * **InstalledChimeSounds and SelectedChime/Enabled have opposite
 * persistence, unlike the door lock's single LockState.** S3.23:
 * InstalledChimeSounds "is not persisted. The store lives in RAM and
 * starts empty every boot", the same policy AT+MTMODES/AT+MTTEMPLEVELS
 * follow -- so setInstalledChimeSounds()'s cached list is re-sent verbatim
 * on every later reconcile (hearthOnReconciled() below), the established
 * B120 norm. S3.24 is the opposite: "SetSelectedChime()/SetEnabled()
 * persist their own value through CHIP's attribute persistence provider...
 * survive AT+MTRESET" -- so setSelectedChime()/setEnabled() need no
 * reconcile push at all, and this class has none for them; the task
 * brief's own signature list agrees, listing setInstalledChimeSounds() as
 * "resent per reconcile" and saying nothing of the kind for
 * setSelectedChime()/setEnabled().
 *
 * **PlayChimeSound is a REAL wire verdict, unlike the water valve's
 * discarded one.** S3.17: "unlike the water valve (S3.19) or the
 * OperationalState trio (S3.21), the host's verdict reaches the controller
 * exactly as given, Status::Success on allow or Status::Failure on deny,
 * with no SDK-side remapping". onPlayChime() below therefore behaves
 * exactly like MatterDoorLock's onLock()/onUnlock(): a genuine allow/deny
 * the controller actually observes, not merely a host-side hint the way
 * MatterWaterValve's onOpen()/onClose() are. It also carries
 * AT_MT_SPEC.md S3.17's reserved fifth `+MTCMD` field -- the requested
 * `chimeID` -- the first (and, at this task, only) consumer of that field
 * in this library; see MatterEndPoint.h's own comment on the widened
 * hearthOnForwardedCommand() signature.
 *
 * **"whenever the command actually reaches the host at all" is load-
 * bearing.** S3.24: the SDK short-circuits PlayChimeSound before it ever
 * forwards, answering the controller itself with no +MTCMD URC raised at
 * all, in two cases: Enabled is false (answers Status::Success), or the
 * chimeID names a sound not currently installed via AT+MTCHIMESOUNDS
 * (answers Status::NotFound). A host that has set AT+MTCHIME=<ep>,1,0 will
 * see PlayChimeSound invokes against that endpoint simply stop arriving as
 * +MTCMD URCs, not arrive and fail; onPlayChime()'s callback is never
 * consulted for either case, and there is nothing this class could do
 * about it even if it wanted to -- it is the SDK's own short-circuit, not
 * a firmware or library choice.
 */
#pragma once

#include <cstddef>
#include <stdint.h>
#include <string.h>
#include <functional>
#include "MatterEndPoint.h"

class MatterChime : public MatterEndPoint {
public:
  MatterChime();
  ~MatterChime();

  // declares only; no initial SelectedChime/Enabled/InstalledChimeSounds
  // to reconcile (SelectedChime/Enabled persist on the firmware side; see
  // the header comment for why this class pushes neither at reconcile).
  bool begin();
  // this will stop processing Chime Matter events
  void end();

  // register the host's verdict for a firmware-forwarded PlayChimeSound
  // invoke (AT_MT_SPEC.md S3.17). No callback registered denies by
  // default (fail closed), and -- unlike the water valve -- this verdict
  // is a real wire verdict the controller actually observes; see the
  // header comment.
  void onPlayChime(std::function<bool(uint8_t chimeID)> cb);

  // replace the InstalledChimeSounds list (AT_MT_SPEC.md S3.23): 1..8
  // id/name pairs, each id 0..255 unique within the list, each name 1..32
  // bytes of printable ASCII with no '"' (a comma INSIDE a name is legal
  // and part of its text). Re-sent automatically on every later reconcile;
  // see the header comment.
  bool setInstalledChimeSounds(const uint8_t *ids, const char *const *names, uint8_t count);

  // SelectedChime / Enabled (AT_MT_SPEC.md S3.24, AT+MTCHIME's <what> 0/1).
  // Cache-only in both directions: neither attribute has an AT+MTATTR path
  // (see the header comment), but both persist on the firmware side across
  // AT+MTRESET, so this class needs no reconcile push for either. The
  // FIRST call to each setter after begin() always reaches the wire, even
  // if the value given equals the cache's 0/false starting point: a host
  // reboot is itself an AT+MTRESET-equivalent reset, so the firmware may
  // already hold a different value than this fresh cache (Finding 2, the
  // final-review fix wave; see the protected flags below).
  bool setSelectedChime(uint8_t id);
  uint8_t getSelectedChime();
  bool setEnabled(bool on);
  bool getEnabled();

  // this function is called by Matter internal event processor. It could be
  // overwritten by the application, if necessary. A documented no-op here:
  // see the header comment (no cluster attribute has an AT+MTATTR path).
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

  // Hearth's own addition (MatterEndPoint.h): the firmware forwards a
  // controller-invoked PlayChimeSound here for a verdict; see
  // onPlayChime() above. Fails closed (false) with no callback registered,
  // for the wrong cluster, or for any command id this class does not
  // recognise.
  bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload, uint32_t payload) override;

protected:
  bool started = false;
  uint8_t selectedChime = 0;
  bool enabled = false;

  // Final-review fix wave, Finding 2: SelectedChime/Enabled persist
  // firmware-side across AT+MTRESET (S3.24), but this cache always
  // re-initializes to 0/false in begin(), every boot including a host
  // reboot -- exactly the kind of reset S3.24 means. The == guard in
  // setSelectedChime()/setEnabled() below would otherwise silently
  // swallow the FIRST write of a boot whenever it happened to equal the
  // cache's fresh 0/false starting point, the one case this host cannot
  // tell "the wire already matches" from "nothing was ever sent". These
  // flags force exactly that first write through regardless of value;
  // see the .cpp for the guard shape.
  bool writtenSelectedChime = false;
  bool writtenEnabled = false;

  // clang-format off
  static const uint8_t kMaxSounds  = 8;  // AT_MT_SPEC.md S3.23: 1..8 pairs
  static const uint8_t kMaxNameLen = 32; // AT_MT_SPEC.md S3.23: 1..32 printable ASCII bytes
  // clang-format on

  uint8_t installedIdsArray[kMaxSounds];
  char installedNames[kMaxSounds][kMaxNameLen + 1];  // +1 for the terminating NUL
  uint8_t installedCount = 0;

  std::function<bool(uint8_t)> _onPlayChimeCB = nullptr;

  // builds and sends "AT+MTCHIMESOUNDS=<ep>,<id1>,"<name1>",..." for
  // exactly the pairs given; wire-only, no cache update.
  bool hearthSendInstalledSounds(const uint8_t *ids, const char *const *names, uint8_t count);
  // builds and sends "AT+MTCHIME=<ep>,<what>,<value>"; wire-only, no cache
  // update.
  bool hearthSendChimeField(uint8_t what, uint8_t value);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile, not only the
  // first: resends the cached InstalledChimeSounds list, which the
  // firmware does not persist across a reboot (S3.23). A no-op if nothing
  // has been set yet.
  void hearthOnReconciled() override;
};

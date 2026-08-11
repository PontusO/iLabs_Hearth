/*
 * MatterTemperatureControlledCabinet.h - the twentieth and last concrete
 * Hearth endpoint type, completing the upstream class set.
 *
 * Mirrors arduino-esp32's Matter library MatterTemperatureControlledCabinet
 * (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterTemperatureControlledCabinet.h
 * and the paired .cpp): the public section below reproduces its API surface
 * (both begin() overloads, every setter/getter, attributeChangeCB), with one
 * addition (setSupportedTemperatureLevelLabels(), a Hearth-only extension,
 * documented below) and the internals rebuilt for this port rather than
 * transcribed, per docs/superpowers/specs/2026-08-05-cabinet-templevels-design.md
 * S2: upstream's bundled esp-matter differs structurally (its own
 * TemperatureControl config/attribute helpers are not what this firmware's
 * pinned esp-matter 21aa3d1 exposes), so only its API is a porting
 * reference, never its .cpp body.
 *
 * Device type 0x0071 is temperature_controlled_cabinet
 * (esp_matter_endpoint.h's ESP_MATTER_TEMPERATURE_CONTROLLED_CABINET_DEVICE_TYPE_ID,
 * AT_MT_SPEC.md's device type table). Cluster 0x0056 (86) is TemperatureControl;
 * TemperatureSetpoint 0x0000, MinTemperature 0x0001, MaxTemperature 0x0002,
 * Step 0x0003 (all int16, hundredths of a degree, exactly as upstream's own
 * comment on its raw fields states), SelectedTemperatureLevel 0x0004 (uint8).
 * IDs verified against connectedhomeip's zap-generated ids/Clusters.h and
 * ids/Attributes.h at the exact 3.3.8-bundled revision (the same source
 * every other class in this library cites for its own IDs; see
 * MatterThermostat.h), not transcribed from the brief that named them:
 * ids/Clusters.h:172-174 gives TemperatureControl::Id = 0x00000056, and
 * ids/Attributes.h:2774-2799 gives the five attribute IDs above in exactly
 * that order. There is no such header on a host build, so both are given as
 * plain integers in the .cpp, the library's usual pattern.
 *
 * SupportedTemperatureLevels (attribute 0x0005) is NOT an ordinary
 * AT+MTATTR-reachable attribute at all: the design doc's SDK survey (S2)
 * found it ATTRIBUTE_FLAG_MANAGED_INTERNALLY on the pinned esp-matter,
 * served by CHIP's own SupportedTemperatureLevelsIteratorDelegate rather
 * than the attribute store AT+MTATTR reads and writes. AT+MTTEMPLEVELS
 * (AT_MT_SPEC.md S3.16) is the only wire path to it, carrying full label
 * TEXT rather than upstream's bare numeric identifiers -- a deliberate
 * design choice (S1: "host-side strings can never reach a controller UI"
 * otherwise), not a parity gap. hearthOnReconciled() below is what makes
 * this automatic rather than something every sketch has to remember: labels
 * are not persisted on the C6 (S3.16, "the sketch re-sends them after every
 * boot"), so this class resends whatever is currently cached on every
 * reconcile, the same event MatterEndPoint.h's own comment on
 * hearthOnReconciled() documents as the one hook point that already runs on
 * every Matter.begin() call.
 *
 * Deviations from a literal transcription of upstream's .cpp, all following
 * the established pattern of prior classes in this library and documented
 * again in the .cpp:
 *
 * 1. Both begin() overloads validate their arguments and call hearthDeclare()
 *    BEFORE touching any member state, matching upstream's own
 *    already-created guard (getEndPointId() != 0, checked first in both of
 *    upstream's begin() bodies) and MatterThermostat's deviation 1: a
 *    rejected begin() -- bad arguments, or a re-begin after reconcile --
 *    consumes no registry slot and leaves every cached value untouched.
 * 2. begin() itself issues no AT traffic beyond the declaration: unlike
 *    upstream, which bakes tempSetpoint/minTemperature/maxTemperature/step
 *    (or the level array) into the esp_matter config struct at endpoint
 *    creation, AT+MTEP=<devtype>[,<variant>] (S3.9) carries no channel for
 *    initial attribute values -- the C6 creates the cluster with whatever
 *    its own data model default is (0/10/1 for min/max/step, observed on
 *    the bench). hearthOnReconciled() below is what actually establishes
 *    the sketch's chosen state on the device, for BOTH modes, symmetrically:
 *    it pushes the four cached TemperatureNumber values (min, max, step,
 *    setpoint, in that order) directly via updateAttributeVal(), bypassing
 *    the setters' own skip-if-equal, and (TemperatureLevel mode) the
 *    labels and selected level, exactly as it always did.
 *
 *    This replaces an earlier, incorrect contract ("call the setters
 *    explicitly after Matter.begin()") that shipped in this class's first
 *    round and was caught on the bench, not in the host suite: a sketch
 *    calling setMinTemperature() with the very value it had just passed to
 *    begin() hit the setters' cache-equality skip and sent nothing, because
 *    begin()'s cache is seeded from the sketch's OWN arguments while the C6
 *    boots the cluster at esp-matter's unrelated defaults -- the setter's
 *    skip-if-equal, which assumes cache mirrors device, was therefore wrong
 *    from the very first call, not just eventually. MatterThermostat's
 *    begin() (kDefaultMinHeatSetpointLimit-adjacent defaults 1600/2400/2000)
 *    looks like the same shape and is not: its cache seeds were chosen to
 *    deliberately MATCH the firmware thunk's own seeded thermostat defaults,
 *    so cache equals device from boot and its setters' skip-if-equal was
 *    sound from the start. The cabinet has no such matching firmware seed to
 *    lean on (its TN values are sketch-supplied, arbitrary), so the
 *    precedent does not transfer; the reconcile push is what makes cache and
 *    device agree at all, and only after that does skip-if-equal become
 *    correct for it too.
 * 3. Every getter (getTemperatureSetpoint(), getSelectedTemperatureLevel(),
 *    getSupportedTemperatureLevelsCount(), ...) returns the cached value
 *    directly, with no getAttributeVal() round trip. Matches this library's
 *    established convention (MatterFan.cpp's header comment, MatterThermostat's
 *    inline getters): on this stack getAttributeVal() is a real AT+MTATTR
 *    read, and every sibling class relies on the cache alone. Doubly true
 *    for getSupportedTemperatureLevelsCount(): SupportedTemperatureLevels has
 *    no AT+MTATTR path to read at all (see above), so a wire round trip is
 *    not merely redundant here, it is impossible.
 * 4. setSupportedTemperatureLevelLabels(const char *const *labels,
 *    uint16_t count) is a Hearth-only addition, not part of upstream's
 *    surface (per the naming rule, hence no Matter-name collision risk:
 *    it is not called setSupportedTemperatureLevels, which upstream already
 *    defines with different, numeric-identifier semantics). Sends the
 *    labels verbatim through AT+MTTEMPLEVELS (S3.16); the numeric
 *    identifiers set via begin()/setSupportedTemperatureLevels() remain the
 *    sketch-side handle either way, since SelectedTemperatureLevel is the
 *    list index, not the label text. Enforces S3.16's full grammar
 *    host-side (1..16 labels, 1..16 bytes each, every byte printable ASCII
 *    0x20..0x7E, never a '"') and returns false with
 *    Hearth.hearthSetError(1), the wire's own "grammar/label violation"
 *    code, on a violation, without ever reaching the wire. The quote check
 *    specifically is not merely cosmetic: an unescaped '"' inside a label
 *    would corrupt the AT+MTTEMPLEVELS line's own field boundary at the
 *    firmware parser rather than coming back as a clean rejection.
 *
 * Composed-appliance round, Task 8: a cabinet can now be OWNED by a
 * MatterRefrigerator (addCabinet()), which changes two things and nothing
 * else:
 *
 * - An owned cabinet's begin() declares NOTHING: the parent already declared
 *   it, with the parent's own composition index riding on the AT+MTEP line
 *   (AT_MT_SPEC.md S3.9's third field). A hearthDeclare() from the cabinet's
 *   own begin() would update its registry entry in place and silently wipe
 *   that parent index back to HEARTH_NO_PARENT (the in-place-update
 *   semantics test_composition_parent.cpp pins), so the owned path must not
 *   go anywhere near the registry. begin() still validates its arguments,
 *   still refuses a flavour that does not match what addCabinet() declared
 *   (the declared variant is the wire truth; a mismatched begin() would
 *   cache state for a cluster shape the endpoint does not have), and still
 *   caches the sketch's values for the reconcile push, exactly as before.
 * - An owned cabinet gains the fridge-cabinet modes API below
 *   (setSupportedModes()/onChangeMode()/getCurrentMode()):
 *   RefrigeratorAndTemperatureControlledCabinetMode (0x0052) is a
 *   conditional cluster the firmware derives from the parent (S3.9's 0x0071
 *   note: Cooler conformance under a Refrigerator), so the API refuses on
 *   an unowned cabinet without touching the wire: there is no such cluster
 *   on a standalone cabinet endpoint for it to reach. Same S3.20.1 grammar,
 *   caching and adjudication discipline as MatterRoboticVacuum's mode
 *   setters, including the 0.6.0 CurrentMode rule: the cluster's
 *   CurrentMode is Instance-served, never raises a +MTATTR URC, so
 *   getCurrentMode() updates only when onChangeMode() allows a forwarded
 *   ChangeToMode (and B196 means a same-mode request never even reaches
 *   this host).
 */
#pragma once

#include <cstddef>
#include <stdint.h>
#include <string.h>
#include "MatterEndPoint.h"

class MatterTemperatureControlledCabinet : public MatterEndPoint {
public:
  MatterTemperatureControlledCabinet();
  ~MatterTemperatureControlledCabinet();

  // begin with the TemperatureNumber feature (mutually exclusive with TemperatureLevel).
  // Declares only; begin() itself pushes nothing to the wire. The values given
  // here reach the device automatically during the next Matter.begin() reconcile
  // (hearthOnReconciled()), not from begin() itself; see deviation 2 above.
  bool begin(double tempSetpoint = 0.00, double minTemperature = -10.0, double maxTemperature = 32.0, double step = 0.50);

  // begin with the TemperatureLevel feature (mutually exclusive with TemperatureNumber).
  // supportedLevels is an array of level identifiers (0..255), 1..16 entries;
  // selectedLevel must be one of them. Generated labels ("Level <n>") are sent
  // automatically at reconcile; see setSupportedTemperatureLevelLabels() for real names.
  bool begin(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel = 0);

  // this will stop processing Temperature Controlled Cabinet Matter events
  void end();

  // set the temperature setpoint
  bool setTemperatureSetpoint(double temperature);
  // returns the temperature setpoint in Celsius
  double getTemperatureSetpoint();

  // set the minimum temperature
  bool setMinTemperature(double temperature);
  // returns the minimum temperature in Celsius
  double getMinTemperature();

  // set the maximum temperature
  bool setMaxTemperature(double temperature);
  // returns the maximum temperature in Celsius
  double getMaxTemperature();

  // set the temperature step
  bool setStep(double step);
  // returns the temperature step in Celsius
  double getStep();

  // set the selected temperature level (TemperatureLevel mode only)
  bool setSelectedTemperatureLevel(uint8_t level);
  // returns the selected temperature level
  uint8_t getSelectedTemperatureLevel();

  // set the supported temperature level identifiers (TemperatureLevel mode only);
  // regenerates and sends "Level <n>" labels for the new set (see deviation 2's
  // note on SupportedTemperatureLevels having no bare-numeric wire form here)
  bool setSupportedTemperatureLevels(uint8_t *levels, uint16_t count);
  // get supported temperature levels count
  uint16_t getSupportedTemperatureLevelsCount();

  // Hearth extension (not part of upstream's surface; see deviation 4 above):
  // replace the level labels with real display text, sent verbatim through
  // AT+MTTEMPLEVELS. Re-sent automatically on every later reconcile, exactly
  // like the generated defaults it replaces.
  bool setSupportedTemperatureLevelLabels(const char *const *labels, uint16_t count);

  // Fridge-cabinet modes API (Task 8, composed-appliance round): valid ONLY
  // on a cabinet owned by a MatterRefrigerator (see the header comment).
  // Replaces RefrigeratorAndTemperatureControlledCabinetMode's (0x0052)
  // SupportedModes list on THIS cabinet's own endpoint (AT_MT_SPEC.md
  // S3.20.1): 1..8 mode/tag/label triples, each mode 0..255 unique within
  // the call, each tag a bare u16 (0 = the cluster's conformance default,
  // kAuto on every mode for this cluster), each label 1..32 bytes of
  // printable ASCII with no '"'. Re-sent automatically on every later
  // reconcile. Refused without wire traffic on an unowned cabinet.
  bool setSupportedModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);
  // register the host's verdict for a controller-invoked ChangeToMode on
  // this cabinet's own 0x0052 cluster (S3.17/S3.20.1); the callback's
  // argument is the requested mode. No callback registered denies by
  // default (fail closed). Consulted only when owned; an unowned cabinet
  // denies through the base class default without ever asking.
  void onChangeMode(std::function<bool(uint8_t)> cb);
  // cached CurrentMode, the 0.6.0 rule: never a wire read, updated only
  // when onChangeMode() allows a forwarded ChangeToMode.
  uint8_t getCurrentMode();

  // Task 8: adjudicates ChangeToMode on this cabinet's own 0x0052 cluster
  // when owned by a refrigerator; everything else defers to the base class
  // default (fail closed), including the owned-only guard.
  bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields) override;

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

  esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const override;

protected:
  bool started = false;
  // Feature mode: true = TemperatureNumber, false = TemperatureLevel. Mutually exclusive.
  bool useTemperatureNumber = true;

  // temperature in 1/100th Celsius (stored as int16_t, upstream's own scale)
  int16_t rawTempSetpoint = 0;
  int16_t rawMinTemperature = 0;
  int16_t rawMaxTemperature = 0;
  int16_t rawStep = 0;
  uint8_t selectedTempLevel = 0;

  // clang-format off
  static const uint16_t kMaxSupportedLevels = 16; // temperature_control::k_max_temp_level_count, upstream's own cap
  static const uint16_t kMaxLevelLabelLen   = 16; // AT_MT_SPEC.md S3.16: 1..16 printable ASCII bytes per label
  // clang-format on

  uint8_t supportedLevelsArray[kMaxSupportedLevels];
  uint16_t supportedLevelsCount = 0;
  // Label text actually sent on the wire: generated defaults at begin(),
  // overwritten (and re-sent on later reconciles) by
  // setSupportedTemperatureLevelLabels(). +1 for the terminating NUL.
  char levelLabels[kMaxSupportedLevels][kMaxLevelLabelLen + 1];
  uint16_t levelLabelCount = 0;

  // internal functions to set the raw temperature values
  bool setRawTemperatureSetpoint(int16_t _rawTemperature);
  bool setRawMinTemperature(int16_t _rawTemperature);
  bool setRawMaxTemperature(int16_t _rawTemperature);
  bool setRawStep(int16_t _rawStep);
  bool begin(int16_t _rawTempSetpoint, int16_t _rawMinTemperature, int16_t _rawMaxTemperature, int16_t _rawStep);
  bool beginInternal(uint8_t *supportedLevels, uint16_t levelCount, uint8_t selectedLevel);

  // fills levelLabels[0..supportedLevelsCount) with "Level <n>" per entry of supportedLevelsArray
  void hearthGenerateDefaultLabels();
  // builds and sends "AT+MTTEMPLEVELS=<ep>,"..."..." for exactly the given labels; wire-only, no cache update
  bool hearthSendLevelLabels(const char *const *labels, uint16_t count);

  // Hearth's own hook (MatterEndPoint.h), on every reconcile: in
  // TemperatureNumber mode, pushes the four cached min/max/step/setpoint
  // values directly to the wire (fix round 2's bench bug); in
  // TemperatureLevel mode, resends labels then SelectedTemperatureLevel, as
  // before (see deviation 2). Task 8: an owned cabinet additionally resends
  // its cached 0x0052 mode list afterwards, which the firmware does not
  // persist across a reboot (S3.20.1), the same B120 shape as
  // MatterRoboticVacuum's own hook.
  void hearthOnReconciled() override;

  /*
   * Task 8 ownership state, set only by MatterRefrigerator (a friend, so
   * these need no public mutators a sketch could misuse):
   * - hearthOwnedByFridge: addCabinet() marks the cabinet owned; begin()
   *   then skips self-declaration and the modes API unlocks.
   * - hearthOwnedInert: the reference addCabinet() returns when it must
   *   refuse (capacity exhausted, or called after the fridge's begin()).
   *   Everything on an inert cabinet fails: it exists so addCabinet() can
   *   keep its reference-returning signature without handing back an alias
   *   of a real cabinet whose begin() would then succeed.
   * - hearthOwnedLevels: the flavour addCabinet() declared (false =
   *   TemperatureNumber, true = TemperatureLevel), which begin() checks its
   *   own overload against: the declared variant is the wire truth.
   */
  bool hearthOwnedByFridge = false;
  bool hearthOwnedInert = false;
  bool hearthOwnedLevels = false;

  // clang-format off
  static const uint8_t kMaxFridgeModes        = 8;  // AT_MT_SPEC.md S3.20.1: 1..8 triples
  static const uint8_t kMaxFridgeModeLabelLen = 32; // S3.20.1: 1..32 printable ASCII bytes
  // clang-format on

  uint8_t fridgeModes[kMaxFridgeModes];
  uint16_t fridgeTags[kMaxFridgeModes];
  char fridgeModeLabels[kMaxFridgeModes][kMaxFridgeModeLabelLen + 1];  // +1 for the terminating NUL
  uint8_t fridgeModesCount = 0;
  uint8_t currentFridgeMode = 0;
  std::function<bool(uint8_t)> _onChangeModeCB = nullptr;

  // builds and sends "AT+MTMODES=<ep>,82,<mode1>,<tag1>,\"<label1>\",..."
  // for exactly the triples given; wire-only, no cache update (house
  // discipline: the caller decides whether/what to commit afterwards).
  bool hearthSendFridgeModes(const uint8_t *modes, const uint16_t *tags, const char *const *labels, uint8_t count);

  friend class MatterRefrigerator;
};

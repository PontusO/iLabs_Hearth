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
 * 2. begin() issues no AT traffic beyond the declaration itself: unlike
 *    upstream, which bakes tempSetpoint/minTemperature/maxTemperature/step
 *    (or the level array) into the esp_matter config struct at endpoint
 *    creation, AT+MTEP=<devtype>[,<variant>] (S3.9) carries no channel for
 *    initial attribute values -- the C6 creates the cluster with whatever
 *    its own data model default is. A sketch that wants a non-default
 *    initial TemperatureNumber state must call the setters explicitly after
 *    Matter.begin(), exactly as every other write-capable class in this
 *    library already requires (see MatterThermostat.h's own begin()).
 *    TemperatureLevel mode is the one exception: hearthOnReconciled() below
 *    pushes the (generated or custom) labels and the selected level
 *    automatically, because there is no other point at which a sketch could
 *    reasonably intervene before a controller might read an empty list.
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
 *    list index, not the label text. Enforces this library's cap host-side
 *    (1..16 labels, 1..16 bytes each -- S3.16's own grammar) and returns
 *    false with Hearth.hearthSetError(1), the wire's own "grammar/label
 *    violation" code, on a violation, without ever reaching the wire.
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
  // Declares only; see deviation 2 above for why the values given here are not
  // pushed to the wire by begin() itself.
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

  // Hearth's own hook (MatterEndPoint.h): resends the TemperatureLevel state
  // (labels, then SelectedTemperatureLevel) on every reconcile. No-op in
  // TemperatureNumber mode; see deviation 2.
  void hearthOnReconciled() override;
};

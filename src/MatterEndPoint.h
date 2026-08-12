/*
 * MatterEndPoint.h - base class every Hearth endpoint type derives from.
 *
 * Mirrors arduino-esp32's Matter library MatterEndPoint (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndPoint.h),
 * with three differences:
 *
 * - createSecondaryNetworkInterface() and getSecondaryNetworkEndPointId()
 *   are not implemented. They exist upstream for devices with more than one
 *   network interface; the C6 image has one. The gap is recorded in the
 *   README (Task 9).
 * - getAttribute() is not implemented either. Upstream returns an
 *   esp_matter::attribute_t *, a handle into ESP-IDF's live data model; a
 *   host on RP2350 has no such data model to hand a handle into; there is no
 *   host-side type this could return. Concrete endpoint types must go
 *   through getAttributeVal/setAttributeVal/updateAttributeVal instead.
 * - the hearth* statics are Hearth's own addition, not part of the upstream
 *   surface, so per the naming rule they carry a Hearth prefix rather than a
 *   Matter one. They are an ordered registry of the endpoints a sketch
 *   declared: endpoint IDs are assigned from declaration order, and C++
 *   static initialization order across translation units is unspecified, so
 *   construction order cannot stand in for it.
 *
 * setAttributeVal writes AT+MTATTR's mode 0 (no report to the fabric);
 * updateAttributeVal writes mode 1 (reported to subscribers and bound
 * devices). A host reflecting a controller-driven change must use
 * setAttributeVal, or it echoes the change back to the fabric and loops.
 * See AT_MT_SPEC.md S3.8.
 *
 * getAttributeVal rebuilds its result using attrVal->type as the target
 * type: the wire only ever carries a bare integer, never a type tag, so the
 * caller must pre-set attrVal->type to the expected type (typically via
 * esp_matter_bool()/esp_matter_uint8()/etc.) before calling. A call with
 * attrVal->type left as ESP_MATTER_VAL_TYPE_INVALID, or set to anything
 * outside esp_matter_val_type_t, is rejected with +MTERR:5 rather than
 * risking a value silently misread out of the wrong union member.
 */
#pragma once

#include <stdint.h>
#include <functional>
#include "HearthCompat.h"

/* Registry capacity. Matches the firmware's MT_COMP_MAX_ENDPOINTS, raised
 * 16 -> 24 alongside it in the composed-appliance round (0.7.0). The cap
 * also bounds Hearth.cpp's hearthOnEpLine() parse of the AT+MTEP? reply, so
 * lagging behind the firmware would silently drop reply lines past the old
 * cap and make a large live composition compare unequal (or worse, falsely
 * equal against a truncated declared registry) on every boot. */
#ifndef HEARTH_MAX_ENDPOINTS
#define HEARTH_MAX_ENDPOINTS 24
#endif

/*
 * Task 6 (RVC + Microwave batch): the wire grammar's tail widened again,
 * from Task C7's single optional field to up to four (the
 * RoboticVacuumCleaner's GoHome/SelectAreas and the Microwave Oven's
 * SetCookingParameters are the first consumers that need more than one
 * value at once). Energy round B widened it once more, to five,
 * "+MTCMD:<seq>,<ep>,<cluster>,<cmd>[,<p1>[,<p2>[,<p3>[,<p4>[,<p5>]]]]]"
 * (AT_MT_SPEC.md S3.17: the water heater's Boost is the first five-field
 * consumer, duration + presence mask + up to three appended numeric
 * optionals). `count` is how many of the five tail positions the wire
 * line actually reached, counting an empty one; `present[i]` is false only
 * where that position was empty (",,"), not where the line ended before it.
 * A four-field line where the wire always sends every position but leaves
 * some empty (the ",,30,,1" shape Task 6's brief calls out, since the
 * firmware resolves optionals on its side today) parses to count 4 with a
 * mixed `present` array; a legacy line that never reaches a given position
 * at all leaves it both absent and, since the struct is zero-initialised by
 * hearthDispatchCmd() before parsing, value 0.
 *
 * Energy round C1 (Task 5, HearthDemControl's PowerAdjustRequest forward,
 * AT_MT_SPEC.md S3.17) widened `value[]` from uint32_t to int64_t, and
 * hearthDispatchCmd()'s tail parse from strtoul() to HearthCompat.h's
 * hearthParseWireValue() (the same 64-bit-bit-pattern parser already used
 * for +MTATTR's identical width problem, this file's own include below):
 * a command payload field can now genuinely carry Matter's full int64
 * range (PowerAdjustRequest's `power` field is int64 mW), where the
 * previous uint32_t silently truncated anything past 2^32 -- the exact
 * defect class this codebase's own AT+MTATTR/AT+MTMEAS pipelines were
 * widened against earlier (0.8.0). Every existing consumer narrows its own
 * read of `value[i]` down to a uint8_t/uint16_t/uint32_t/bool with an
 * explicit or implicit cast, so this widening changes nothing for any
 * LEGITIMATE existing payload: every shipped consumer's field is a mode
 * id, a presence mask, a duration, a cook time or a percentage, none
 * anywhere near 2^32.
 *
 * That claim is NOT "bit-for-bit identical" for a hypothetical value AT
 * or ABOVE 2^32, and review round 1 (F2) is right to insist on the precise
 * version rather than the sloppier one: `unsigned long` is 64-bit on the
 * host this library's suite builds for (x86-64), so strtoul() never
 * saturates below that width there and the host suite is STRUCTURALLY
 * UNABLE to observe any difference at all, old parser or new. On the
 * RP2350 target `unsigned long` is 32-bit, where strtoul() SATURATES to
 * ULONG_MAX with errno ERANGE on overflow rather than truncating (the C
 * standard's own contract for it, not a two's-complement wraparound), so
 * the old and new parses of a value at or above 2^32 genuinely differed
 * there: the wire text "5000000000" parsed to 0xFFFFFFFF (saturated)
 * through the old strtoul()-based parser, and parses to 0x2A05F200 (the
 * true value's own low 32 bits, through hearthParseWireValue()'s correct
 * 64-bit parse followed by MatterEndPoint.cpp's legacy narrowing cast for
 * any endpoint type still on the four-argument hearthOnForwardedCommand()
 * virtual) after this widening. The new figure is never a worse one than
 * the old, and per the paragraph above no shipped +MTCMD consumer can
 * legitimately land in this case regardless -- but "no legitimate legacy
 * payload can exceed 2^32, and where the two differ the new value is the
 * correct one" is the honest claim, not "identical either way". Only a
 * genuinely 64-bit-wide payload field, DEM's `power` being the first,
 * needs the extra range.
 */
#define HEARTH_CMD_FIELDS_MAX 5
struct HearthCmdFields {
  uint8_t count;
  bool present[HEARTH_CMD_FIELDS_MAX];
  int64_t value[HEARTH_CMD_FIELDS_MAX];
};

class MatterEndPoint {
public:
  /*
   * Deregisters this endpoint from the declaration registry. The registry
   * holds raw pointers and nothing else ever removes one, so without this a
   * destroyed device object (a scope-local, or one owned by something that
   * goes away) leaves a dangling pointer that hearthFindByEndPointId()
   * dereferences on the very next +MTATTR URC.
   *
   * Non-virtual, exactly as upstream's MatterEndPoint is: this gives the
   * destructor upstream already has implicitly a body, it does not add a
   * name to the Matter-named surface, and matching upstream's (non-)
   * virtuality keeps that surface identical. Nothing in this library or in
   * upstream's examples deletes a device object through a MatterEndPoint *;
   * they are file-scope or stack objects, destroyed as their concrete type.
   */
  ~MatterEndPoint();

  enum attrOperation_t {
    ATTR_SET = false,
    ATTR_UPDATE = true
  };

  using EndPointIdentifyCB = std::function<bool(bool)>;

  /* Invoked when a subscribed attribute changes. The base class has no data
   * model of its own, so this stays pure virtual; each concrete endpoint
   * type provides it. */
  virtual bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) = 0;

  /*
   * Hearth's own addition (Hearth-prefixed, not part of the upstream
   * surface, same precedent as the hearthDeclare family below): tells
   * hearthDispatchAttr() (Hearth.cpp) what esp_matter_val_type_t a given
   * (cluster_id, attribute_id) pair actually is, so a +MTATTR URC's value
   * lands in the matching esp_matter_attr_val_t union member before
   * attributeChangeCB() is called. The wire never carries a type tag (see
   * this file's header comment), and the generic URC dispatcher has no
   * per-attribute type table of its own, so without this it could only
   * guess -- which is exactly what it used to do, hardcoding
   * ESP_MATTER_VAL_TYPE_INTEGER for everything. That default is preserved
   * here for any endpoint type that does not override this, so the switch
   * only changes behaviour for endpoint types that opt in by overriding it.
   */
  virtual esp_matter_val_type_t hearthAttrTypeFor(uint32_t cluster_id, uint32_t attribute_id) const;

  uint16_t getEndPointId();
  void setEndPointId(uint16_t ep);

  bool getAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);
  bool setAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);
  bool updateAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);

  bool endpointIdentifyCB(uint16_t endpoint_id, bool identifyIsEnabled);
  void onIdentify(EndPointIdentifyCB onEndPointIdentifyCB);

  /* Hearth additions, not part of the upstream surface: the ordered
   * registry of endpoints a sketch declared.
   *
   * hearthDeclare() is idempotent for an endpoint already in the registry,
   * *before* reconcile: a repeat declaration of the same object updates its
   * entry in place rather than appending a second one. A sketch doing
   * begin(); end(); begin(); on one device object otherwise turned a
   * one-endpoint composition into a two-endpoint one, which reconcile then
   * "fixed" with a full clear/apply/co-processor-reboot cycle.
   *
   * After reconcile it refuses instead, with +MTERR:10, even for an exact
   * repeat. The registry would be unharmed either way, but a true return
   * licenses the calling endpoint's begin() to overwrite its cached state
   * from its initialState argument without writing anything to the wire,
   * which desynchronises the sketch from the C6 silently. Upstream refuses
   * the same call for the same reason (its begin() bails out once
   * getEndPointId() is non-zero). See the .cpp for the full case.
   *
   * hearthUndeclare() removes an endpoint, preserving the order of
   * everything after it: endpoint IDs are assigned from declaration order,
   * so compacting the array any other way would silently reshuffle the
   * composition. Called from the destructor; a sketch has no reason to call
   * it directly.
   *
   * hearthDeclare()'s three-arg form carries the per-device-type variant
   * AT_MT_SPEC.md S3.9 added (`AT+MTEP=<id>[,<variant>]`): a sub-selector
   * within one device type ID, today meaningful only for the Temperature
   * Controlled Cabinet (0 = TemperatureNumber, 1 = TemperatureLevel), and
   * `0` for every other type. The two-arg form is every existing class's
   * call site, unchanged; it forwards to the three-arg form with variant 0
   * rather than duplicating the declare/refuse logic above. Order matters
   * exactly as much for the variant as for the device type itself: it is
   * part of what makes two compositions identical (see hearthOnReconciled()
   * below and ArduinoMatter::begin()'s comparison in Hearth.cpp), so a
   * changed variant on an otherwise-identical declaration must trigger a
   * rebuild the same way a changed device type does.
   *
   * The four-arg form carries the composed-appliance parent index the wire
   * grammar's third field added (`AT+MTEP=<id>[,<variant>[,<parent_idx>]]`):
   * the composition index (declaration order, decimal on the wire) of an
   * EARLIER entry this endpoint sits under, e.g. a Temperature Controlled
   * Cabinet under a Refrigerator. HEARTH_NO_PARENT means unparented, which
   * is what the two- and three-arg forms forward, so every existing call
   * site keeps its exact behaviour. The parent is part of what makes two
   * compositions identical for the same reason the variant is: a changed
   * parent on an otherwise-identical declaration must trigger a rebuild,
   * since the composed data model the C6 builds from it is different. The
   * firmware validates the parent (earlier entry only, legal parent type
   * for the child, +MTERR:1 otherwise); the registry stores what the sketch
   * declared and leaves that verdict to the wire, where reconcile already
   * aborts loudly on any rejected write. */
  static const uint8_t HEARTH_NO_PARENT = 0xFF;
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId);
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId, uint8_t variant);
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId, uint8_t variant, uint8_t parentIndex);
  static void hearthUndeclare(MatterEndPoint *ep);
  static uint8_t hearthDeclaredCount();
  static MatterEndPoint *hearthDeclaredAt(uint8_t index);
  static uint32_t hearthDeclaredTypeAt(uint8_t index);
  static uint8_t hearthDeclaredVariantAt(size_t index);
  static uint8_t hearthDeclaredParentAt(uint8_t index);
  static void hearthClearDeclarations();

  /*
   * Endpoint 0 never matches. It is the C6's Root Node, which the firmware
   * deliberately never reports over +MTATTR (AT_MT_SPEC.md S4), and it is
   * also the value every declared endpoint carries until reconcile adopts a
   * real one. Without this, a lookup for 0 before (or after a failed)
   * reconcile matched whichever endpoint the sketch happened to declare
   * first, delivering Root Node traffic to an unrelated device object.
   */
  static MatterEndPoint *hearthFindByEndPointId(uint16_t ep);

  /* Called once by Matter.begin() (Task 5) after it has reconciled the
   * declared registry against the C6's live composition. Declaring a new
   * endpoint after that point would never receive an endpoint ID (nothing
   * queries the C6 again), so hearthDeclare() refuses it once this is set.
   * hearthClearDeclarations() resets it, so a test (or a sketch re-running
   * begin()) can reconcile again from a clean slate. */
  static void hearthMarkReconciled();

  /*
   * Hearth's own addition, not part of the upstream surface (same
   * precedent as the hearth* statics above): called once per declared
   * endpoint by ArduinoMatter::begin() (Hearth.cpp), immediately after that
   * endpoint's endpoint_id is adopted from the C6's reported composition,
   * on every reconcile, not only the first. Default no-op; an endpoint
   * type overrides it only when it owns state the C6 does not persist
   * across a reboot and must therefore resend on every link
   * (re-)establishment rather than once at creation, e.g.
   * MatterTemperatureControlledCabinet's TemperatureLevel labels
   * (AT_MT_SPEC.md S3.16: "Labels are NOT persisted... the sketch re-sends
   * them after every boot"). Every other endpoint type in this library has
   * no such state and does not override this.
   */
  virtual void hearthOnReconciled();

  /*
   * Hearth's own addition (not part of the upstream surface, same precedent
   * as hearthOnReconciled() above): called by Hearth.cpp's +MTCMD dispatcher
   * when the firmware forwards a controller-invoked command awaiting an
   * app-level verdict (AT_MT_SPEC.md S3.17; the door lock's LockDoor/
   * UnlockDoor are the first consumers, C3). Default false: fail closed for
   * every endpoint type that does not override it, and for any
   * (cluster_id, command_id) pair a concrete override does not recognise.
   * The dispatcher, not this method, sends AT+MTCMDRESP back to the
   * firmware; this only decides allow (true) or deny (false).
   *
   * Task C7 widening: `hasPayload`/`payload` carry AT_MT_SPEC.md S3.17's
   * reserved fifth `+MTCMD` field (chime's PlayChimeSound `chimeID` is the
   * first consumer). Defaulted so a call site that only ever knew the
   * four-field form -- there are none left in this library after this
   * change, but a sketch could still hold a raw MatterEndPoint* and call
   * this directly -- keeps compiling. An override, however, must repeat the
   * widened signature verbatim (`override` will not compile against a
   * mismatched parameter list, C++'s own protection against a silently
   * un-overridden base default): MatterDoorLock's own override was
   * mechanically updated for this reason, not because LockDoor/UnlockDoor
   * carry a payload -- they do not, so it ignores both new parameters.
   */
  virtual bool hearthOnForwardedCommand(uint32_t cluster_id, uint32_t command_id, bool hasPayload = false, uint32_t payload = 0);

  /*
   * Task 6 (RVC + Microwave batch) widening: the multi-field successor to
   * hearthOnForwardedCommand() above, carrying every tail position
   * HearthCmdFields (this file, above) can hold instead of just the first.
   * hearthDispatchCmd() (Hearth.cpp) now always calls THIS virtual, never
   * the four-argument one directly; the base class default below is what
   * still calls the four-argument one, so every endpoint type in this
   * library that overrides only the old virtual (every one of them, as of
   * this task) keeps working unmodified -- the whole point of adding a
   * second virtual instead of widening the first one's signature, which
   * would have forced a mechanical override update in every existing class
   * the way MatterDoorLock's Task C7 update did for the hasPayload/payload
   * pair. A class with genuine multi-field needs (MatterRoboticVacuum,
   * MatterMicrowaveOven, both Tasks 7-8) overrides this one instead and
   * never touches the four-argument form at all.
   *
   * Same fail-closed default as hearthOnForwardedCommand(): a cluster_id/
   * command_id pair this override does not recognise returns false.
   */
  virtual bool hearthOnForwardedCommandFields(uint32_t cluster_id, uint32_t command_id, const HearthCmdFields &fields);

  /*
   * Hearth's own addition (Task 6, energy round B; same precedent as
   * hearthOnReconciled() above): called on every declared endpoint by
   * HearthClass::hearthDrainDeferredWork() (Hearth.cpp) after any queued
   * AT+MTCMDRESP verdicts have been sent, once the link's busy gate is
   * released. It exists because a wire write from inside a +MTCMD dispatch
   * is refused HEARTH_CMD_REENTRANT (HearthLink.h), so an endpoint type
   * whose accepted command must be FOLLOWED by a wire push of its own (the
   * water heater's BoostState push after an allowed Boost/CancelBoost,
   * AT_MT_SPEC.md S3.17/S3.25) records the intent in the dispatch and
   * performs the push here, after its verdict has gone out. Default no-op;
   * an override that has nothing pending must also be a no-op, since this
   * runs after every verdict drain, not only its own. Endpoint types call
   * Hearth.hearthRequestDeferredWork() from their dispatch to arm a drain.
   */
  virtual void hearthOnDeferredWork();

protected:
  uint16_t endpoint_id = 0;
  EndPointIdentifyCB _onEndPointIdentifyCB = nullptr;

private:
  struct HearthDeclaration {
    MatterEndPoint *ep;
    uint32_t deviceTypeId;
    uint8_t variant;
    uint8_t parentIndex;
  };
  static HearthDeclaration _hearthDeclared[HEARTH_MAX_ENDPOINTS];
  static uint8_t _hearthDeclaredCount;
  static bool _hearthReconciled;

  bool hearthWriteAttr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal, int mode);
  bool hearthEndPointAddressable();
  static void hearthOnAttrLine(const char *line, void *arg);
};

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

/* Registry capacity. Matches the firmware's MT_COMP_MAX_ENDPOINTS. */
#ifndef HEARTH_MAX_ENDPOINTS
#define HEARTH_MAX_ENDPOINTS 16
#endif

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
   * rebuild the same way a changed device type does. */
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId);
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId, uint8_t variant);
  static void hearthUndeclare(MatterEndPoint *ep);
  static uint8_t hearthDeclaredCount();
  static MatterEndPoint *hearthDeclaredAt(uint8_t index);
  static uint32_t hearthDeclaredTypeAt(uint8_t index);
  static uint8_t hearthDeclaredVariantAt(size_t index);
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

protected:
  uint16_t endpoint_id = 0;
  EndPointIdentifyCB _onEndPointIdentifyCB = nullptr;

private:
  struct HearthDeclaration {
    MatterEndPoint *ep;
    uint32_t deviceTypeId;
    uint8_t variant;
  };
  static HearthDeclaration _hearthDeclared[HEARTH_MAX_ENDPOINTS];
  static uint8_t _hearthDeclaredCount;
  static bool _hearthReconciled;

  bool hearthWriteAttr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal, int mode);
  bool hearthEndPointAddressable();
  static void hearthOnAttrLine(const char *line, void *arg);
};

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
  enum attrOperation_t {
    ATTR_SET = false,
    ATTR_UPDATE = true
  };

  using EndPointIdentifyCB = std::function<bool(bool)>;

  /* Invoked when a subscribed attribute changes. The base class has no data
   * model of its own, so this stays pure virtual; each concrete endpoint
   * type provides it. */
  virtual bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) = 0;

  uint16_t getEndPointId();
  void setEndPointId(uint16_t ep);

  bool getAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);
  bool setAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);
  bool updateAttributeVal(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal);

  bool endpointIdentifyCB(uint16_t endpoint_id, bool identifyIsEnabled);
  void onIdentify(EndPointIdentifyCB onEndPointIdentifyCB);

  /* Hearth additions, not part of the upstream surface: the ordered
   * registry of endpoints a sketch declared. */
  static bool hearthDeclare(MatterEndPoint *ep, uint32_t deviceTypeId);
  static uint8_t hearthDeclaredCount();
  static MatterEndPoint *hearthDeclaredAt(uint8_t index);
  static uint32_t hearthDeclaredTypeAt(uint8_t index);
  static void hearthClearDeclarations();
  static MatterEndPoint *hearthFindByEndPointId(uint16_t ep);

  /* Called once by Matter.begin() (Task 5) after it has reconciled the
   * declared registry against the C6's live composition. Declaring a new
   * endpoint after that point would never receive an endpoint ID (nothing
   * queries the C6 again), so hearthDeclare() refuses it once this is set.
   * hearthClearDeclarations() resets it, so a test (or a sketch re-running
   * begin()) can reconcile again from a clean slate. */
  static void hearthMarkReconciled();

protected:
  uint16_t endpoint_id = 0;
  EndPointIdentifyCB _onEndPointIdentifyCB = nullptr;

private:
  struct HearthDeclaration {
    MatterEndPoint *ep;
    uint32_t deviceTypeId;
  };
  static HearthDeclaration _hearthDeclared[HEARTH_MAX_ENDPOINTS];
  static uint8_t _hearthDeclaredCount;
  static bool _hearthReconciled;

  bool hearthWriteAttr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal, int mode);
  static void hearthOnAttrLine(const char *line, void *arg);
};

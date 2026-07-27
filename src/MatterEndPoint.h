/*
 * MatterEndPoint.h - base class every Hearth endpoint type derives from.
 *
 * Mirrors arduino-esp32's Matter library MatterEndPoint (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndPoint.h),
 * with two differences:
 *
 * - createSecondaryNetworkInterface() and getSecondaryNetworkEndPointId()
 *   are not implemented. They exist upstream for devices with more than one
 *   network interface; the C6 image has one. The gap is recorded in the
 *   README (Task 9).
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

  bool hearthWriteAttr(uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *attrVal, int mode);
  static void hearthOnAttrLine(const char *line, void *arg);
};

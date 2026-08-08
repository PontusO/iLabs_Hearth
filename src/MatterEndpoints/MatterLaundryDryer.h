/*
 * MatterLaundryDryer.h - Task C8's Laundry Dryer endpoint type.
 *
 * One of the OperationalState trio: the entire wire contract, callback
 * shape, and design rationale live in
 * MatterOperationalStateEndpoint.h/.cpp, which this class subclasses
 * unchanged. The only thing this class adds is its own device type
 * constant, passed to hearthBeginOperationalState() from begin() below.
 * See that header's comment for the quoted pinned-header evidence and the
 * "why one shared implementation" reasoning (AT_MT_SPEC.md S3.21: "all
 * three of which wire the identical base OperationalState cluster with no
 * device-specific extension").
 *
 * Device type 0x007C is laundry_dryer
 * (esp_matter_endpoint.h's ESP_MATTER_LAUNDRY_DRYER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:93,
 * "#define ESP_MATTER_LAUNDRY_DRYER_DEVICE_TYPE_ID 0x007C").
 */
#pragma once

#include "MatterEndpoints/MatterOperationalStateEndpoint.h"

class MatterLaundryDryer : public MatterOperationalStateEndpoint {
public:
  MatterLaundryDryer();
  ~MatterLaundryDryer();

  // declares only; no initial state to reconcile (see
  // MatterOperationalStateEndpoint.h's header comment).
  bool begin();
};

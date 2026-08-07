/*
 * MatterLaundryWasher.h - Task C8's Laundry Washer endpoint type.
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
 * Device type 0x0073 is laundry_washer
 * (esp_matter_endpoint.h's ESP_MATTER_LAUNDRY_WASHER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:85,
 * "#define ESP_MATTER_LAUNDRY_WASHER_DEVICE_TYPE_ID 0x0073").
 */
#pragma once

#include "MatterEndpoints/MatterOperationalStateEndpoint.h"

class MatterLaundryWasher : public MatterOperationalStateEndpoint {
public:
  MatterLaundryWasher();
  ~MatterLaundryWasher();

  // declares only; no initial state to reconcile (see
  // MatterOperationalStateEndpoint.h's header comment).
  bool begin();
};

/*
 * MatterDishwasher.cpp - implementation. See the header, and
 * MatterOperationalStateEndpoint.h, for the full wire contract this class
 * inherits unchanged.
 */
#include "MatterEndpoints/MatterDishwasher.h"

namespace {
/* dish_washer (ESP_MATTER_DISH_WASHER_DEVICE_TYPE_ID). See the header
 * comment for the quoted pinned-header source. */
const uint32_t kDishwasherDeviceType = 0x0075;
}  // namespace

MatterDishwasher::MatterDishwasher() {}

/* Cleanup is owned entirely by MatterOperationalStateEndpoint's own
 * destructor (calls end()); nothing here to add. */
MatterDishwasher::~MatterDishwasher() {}

bool MatterDishwasher::begin() {
  return hearthBeginOperationalState(kDishwasherDeviceType);
}

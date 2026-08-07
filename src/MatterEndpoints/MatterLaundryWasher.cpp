/*
 * MatterLaundryWasher.cpp - implementation. See the header, and
 * MatterOperationalStateEndpoint.h, for the full wire contract this class
 * inherits unchanged.
 */
#include "MatterEndpoints/MatterLaundryWasher.h"

namespace {
/* laundry_washer (ESP_MATTER_LAUNDRY_WASHER_DEVICE_TYPE_ID). See the
 * header comment for the quoted pinned-header source. */
const uint32_t kLaundryWasherDeviceType = 0x0073;
}  // namespace

MatterLaundryWasher::MatterLaundryWasher() {}

/* Cleanup is owned entirely by MatterOperationalStateEndpoint's own
 * destructor (calls end()); nothing here to add. */
MatterLaundryWasher::~MatterLaundryWasher() {}

bool MatterLaundryWasher::begin() {
  return hearthBeginOperationalState(kLaundryWasherDeviceType);
}

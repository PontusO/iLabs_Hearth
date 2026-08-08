/*
 * MatterLaundryDryer.cpp - implementation. See the header, and
 * MatterOperationalStateEndpoint.h, for the full wire contract this class
 * inherits unchanged.
 */
#include "MatterEndpoints/MatterLaundryDryer.h"

namespace {
/* laundry_dryer (ESP_MATTER_LAUNDRY_DRYER_DEVICE_TYPE_ID). See the header
 * comment for the quoted pinned-header source. */
const uint32_t kLaundryDryerDeviceType = 0x007C;
}  // namespace

MatterLaundryDryer::MatterLaundryDryer() {}

/* Cleanup is owned entirely by MatterOperationalStateEndpoint's own
 * destructor (calls end()); nothing here to add. */
MatterLaundryDryer::~MatterLaundryDryer() {}

bool MatterLaundryDryer::begin() {
  return hearthBeginOperationalState(kLaundryDryerDeviceType);
}

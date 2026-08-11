/*
 * MatterElectricalMeter.cpp - implementation. See the header, and
 * MatterElectricalSensor.h, for the full wire contract this class
 * inherits unchanged.
 */
#include "MatterEndpoints/MatterElectricalMeter.h"

namespace {
/* electrical_meter (ESP_MATTER_ELECTRICAL_METER_DEVICE_TYPE_ID). See the
 * header comment for the quoted pinned-header source. */
const uint32_t kElectricalMeterDeviceType = 0x0514;
}  // namespace

MatterElectricalMeter::MatterElectricalMeter() {}

/* Cleanup is owned entirely by MatterElectricalSensor's own destructor
 * (calls end()); nothing here to add. */
MatterElectricalMeter::~MatterElectricalMeter() {}

bool MatterElectricalMeter::begin(Variant_t variant) {
  return hearthBeginElectrical(kElectricalMeterDeviceType, variant);
}

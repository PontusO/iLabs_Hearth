/*
 * MatterElectricalMeter.h - Task 7 (energy round A): the Electrical Meter,
 * device type 0x0514, the second measurement-push type behind AT+MTMEAS
 * (AT_MT_SPEC.md S3.25). A Hearth original, like its base class.
 *
 * SUBCLASSES MatterElectricalSensor the thin-subclass way (the
 * MatterLaundryWasher shape): on the wire the meter differs from the
 * sensor in exactly two things, its device type id and the ABSENCE of the
 * sensor's PowerTopology cluster, and the topology cluster never had a
 * host surface in the first place (it is firmware-derived from the device
 * type, S3.9). Nothing topology-shaped therefore leaks into either class,
 * and the meter's entire host surface, setters, batch push, energy
 * adders, getters, variant scheme, IS the sensor's. The only member here
 * is begin(), hiding the base's to pass 0x0514 to the shared
 * hearthBeginElectrical(); everything else, including the class-level
 * behaviour documentation, lives in MatterElectricalSensor.h.
 *
 * CONFORMANCE NOTE on the inherited Variant_t (Task 4's firmware
 * finding): spec 1.5.1's device type XML marks BOTH measurement clusters
 * mandatory on the Electrical Meter, so a POWER_ONLY meter is
 * permissive-beyond-conformance, accepted by the firmware for variant
 * scheme symmetry only. The strictly conformant current-clamp declaration
 * is the POWER_ONLY *sensor* (0x0510 lists its clusters as a
 * pick-at-least-one choice). A product that measures power without energy
 * accounting should declare MatterElectricalSensor(POWER_ONLY), not a
 * POWER_ONLY meter.
 *
 * Device type 0x0514 is electrical_meter
 * (ESP_MATTER_ELECTRICAL_METER_DEVICE_TYPE_ID,
 * ~/esp/esp-matter/components/esp_matter/data_model/esp_matter_endpoint.h:168,
 * "#define ESP_MATTER_ELECTRICAL_METER_DEVICE_TYPE_ID 0x0514").
 */
#pragma once

#include "MatterEndpoints/MatterElectricalSensor.h"

class MatterElectricalMeter : public MatterElectricalSensor {
public:
  MatterElectricalMeter();
  ~MatterElectricalMeter();

  // declares 0x0514; the variant scheme is the sensor's, but see the
  // header comment: POWER_ONLY on a METER is permissive-beyond-conformance
  // and exists for scheme symmetry only.
  bool begin(Variant_t variant = FULL);
};

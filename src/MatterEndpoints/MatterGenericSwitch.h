/*
 * MatterGenericSwitch.h - the eighteenth concrete Hearth endpoint type.
 *
 * Mirrors arduino-esp32's Matter library MatterGenericSwitch (see
 * ~/.arduino15/packages/esp32/hardware/esp32/3.3.8/libraries/Matter/src/MatterEndpoints/MatterGenericSwitch.h
 * and the paired .cpp): the public section below is reproduced verbatim,
 * protected members included, with one signature deviation documented below.
 * Device type 0x000F is generic_switch (esp_matter_endpoint.h's
 * ESP_MATTER_GENERIC_SWITCH_DEVICE_TYPE_ID, already used and verified on
 * the firmware side: main/mt_devtypes.cpp's `generic_switch::get_device_type_id()`
 * row, and AT_MT_SPEC.md's own device type table). There is no such header on
 * a host build, so it is given as a plain integer in the .cpp, the same
 * pattern every other endpoint class in this library follows.
 *
 * The class drives no attribute at all: upstream's own switch_cluster carries
 * CurrentPosition/NumberOfPositions, but they are read-only and neither
 * upstream's click() nor this port ever touches them. attributeChangeCB is
 * therefore present only because MatterEndPoint declares it pure virtual; it
 * is a documented no-op, exactly mirroring upstream's own body (log, then
 * return whether the device has begun). hearthAttrTypeFor is not overridden:
 * with no attribute this class knows about, it falls straight through to
 * MatterEndPoint's base default.
 *
 * One deliberate deviation from a literal transcription of upstream's .cpp:
 *
 * 1. click() returns bool here, not upstream's void. Upstream's click()
 *    schedules a CHIP event locally and cannot fail synchronously in any way
 *    a caller could observe; this port's click() is a real AT+MTSWITCH round
 *    trip (AT_MT_SPEC.md S3.15) that can come back +MTERR:2 (unknown
 *    endpoint) or +MTERR:3 (the endpoint has no Switch cluster), and a caller
 *    with no way to learn that would have no way to tell "the controller
 *    just never reacted" from "the command never reached the device type it
 *    needed to". Every other write-capable endpoint class in this library
 *    already returns bool from its setters for the same reason; this is that
 *    same precedent applied to the one command that is an event instead of
 *    an attribute write.
 *
 * Also unlike every attribute-driven endpoint class in this library, click()
 * does not go through updateAttributeVal()/setAttributeVal(): AT+MTSWITCH is
 * its own command, not AT+MTATTR. It builds and sends "AT+MTSWITCH=<ep>"
 * directly through Hearth.hearthCommand(), guarded by the same
 * started/endpoint-adopted check MatterEndPoint's own write helpers apply
 * before touching the wire.
 *
 * As with every endpoint type in this library, begin() declares only (no AT
 * traffic): the endpoint ID is not known until Matter.begin() reconciles the
 * declared registry against the C6.
 */
#pragma once

#include <cstddef>
#include "MatterEndPoint.h"

class MatterGenericSwitch : public MatterEndPoint {
public:
  MatterGenericSwitch();
  ~MatterGenericSwitch();
  virtual bool begin();
  void end();  // this will just stop processing Matter events

  // send a simple click event to the Matter Controller; returns true if the
  // firmware accepted the event (see this header's deviation note above)
  bool click();

  // this function is called by Matter internal event processor. It could be overwritten by the application, if necessary.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);

protected:
  bool started = false;
};

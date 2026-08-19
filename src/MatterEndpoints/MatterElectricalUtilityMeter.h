/*
 * MatterElectricalUtilityMeter.h - Task 12 (energy round C2): the Electrical
 * Utility Meter, device type 0x0511. A Hearth original: arduino-esp32's
 * Matter library ships no meter-identity class at all (see Hearth.h's
 * umbrella comment), so the public surface below is this port's own design
 * against the firmware's wire contract (main/mt_meter.cpp, main/mt_at.c's
 * cmd_mtmeterid, main/include/mt_matter.h's mt_meter_identity_t) and the
 * round's design spec section 6 and 7.3. `docs/AT_MT_SPEC.md` does not
 * describe AT+MTMETERID yet (Task 14 owns that); every wire fact cited here
 * was verified directly against the firmware source, not transcribed from a
 * document.
 *
 * THE WIRE IS ONE BUNDLED COMMAND, THE HOST SURFACE IS FIVE SETTERS. Unlike
 * the sensor/meter's per-field AT+MTMEAS pushes, MeterIdentification's five
 * attributes (MeterType, PointOfDelivery, MeterSerialNumber, ProtocolVersion,
 * PowerThreshold) travel on ONE line,
 * "AT+MTMETERID=<ep>,<type>,\"<pod>\",\"<serial>\",\"<protocol>\",<pwr>,
 * <apparent>,<src>" (mt_at.c's cmd_mtmeterid), because that is the only way
 * any of the five attributes ever gets a value (task 9's report; there is no
 * partial-update verb). So every public setter below mutates its own field,
 * folds in the CURRENT cached value of every other field (a sensible default
 * for anything never set: MeterType Utility(0), the three strings "", the
 * power threshold absent), and sends the WHOLE line. This is not five
 * independent wire verbs wearing one name: it is the identity's one true
 * push, called from five different entry points.
 *
 * THE POWER THRESHOLD MUST BE SET AT LEAST ONCE BEFORE ANY PUSH CAN SUCCEED.
 * PowerThresholdStruct's own "choice b" (meter-identification-cluster.xml,
 * cited in mt_meter.cpp) requires at least one of PowerThreshold and
 * ApparentPowerThreshold present on EVERY push, not merely the first: the
 * struct is not merged field-by-field on the wire, it is replaced wholesale.
 * Consequently every setter here, not only setPowerThreshold() itself, checks
 * this invariant before sending: calling setMeterType() before
 * setPowerThreshold() has ever succeeded answers Hearth error 1 with zero
 * wire traffic, exactly the way an attempted push with neither power field
 * would be refused on the wire (+MTERR:1), just caught a command earlier.
 *
 * HOST-SIDE VALIDATION, BEFORE ANY WIRE TRAFFIC (the MatterModeSelect
 * setSupportedModes() precedent, same reasoning): string length (0..64
 * bytes, MT_METERID_MAX_STR, CHIP's own kMaximumStringSize), printable ASCII
 * (0x20..0x7E) and the exclusion of a raw '"' (which would corrupt the wire
 * line's own quoted-field boundary rather than surviving as a clean
 * +MTERR:1, mt_meter.cpp/mt_at.c). A comma inside a string is deliberately
 * NOT rejected: it is legal content and the firmware's quote-delimited scan
 * (mtmeterid_scan_string(), mt_at.c) already lets it through unharmed.
 * MeterType and PowerThresholdSource are range-checked (0..2, the enums
 * MeterTypeEnum and PowerThresholdSourceEnum, mt_meter.cpp's own citations).
 *
 * THERE IS NO READ-BACK VERB, deliberately, on the firmware side (task 9's
 * report, design spec 6.3): a worst-case identity line is 265 bytes
 * (the field-by-field arithmetic is in mt_meter.cpp's "why there is no
 * read-back verb" comment, not cmd_mtmeterid's, which documents the
 * command's grammar and error codes but carries no byte count) and the
 * host's own receive limit would silently discard it. HEARTH_LINE_MAX is 256
 * (HearthLink.h), of which 255 bytes are usable payload: HearthLink.cpp's
 * accumulator guards on `_acc_len < HEARTH_LINE_MAX - 1`, reserving the
 * last byte for the terminator. Both figures were wrong in an earlier
 * version of this comment (271 and "HEARTH_LINE_MAX 255"), corrected by
 * round C2's final review; the conclusion is unchanged and if anything
 * firmer, since 265 still overruns 255 by ten bytes. This class therefore exposes no getters
 * either: the identity is host-originated, so a sketch that needs its own
 * values already has them, the same reasoning that already governs why
 * MatterEvse exposes no getters for its own scalar setters.
 *
 * THE B229 PATTERN, hearthOnReconciled() (this class's central reason to
 * exist per the design spec): every field here is CONFIGURATION, an
 * installation/product fact, not a volatile reading, so a reboot of the C6
 * must not leave the fabric holding stale (null) MeterIdentification
 * attributes forever. Without an override, each setter's own unchanged-value
 * guard (a repeat setMeterType(0) is normally a no-op) would survive the
 * reboot in this HOST's RAM even though the co-processor's own attribute
 * store came back empty, so a post-reboot sketch that only ever calls the
 * same setters with the same values (the common case: an installation fact
 * does not change) would never re-push anything and the meter would serve a
 * null identity forever, with no read-back to notice the absence. Unlike
 * MatterEvse's split between CONFIGURATION (CircuitCapacity, re-pushed) and
 * VOLATILE (SupplyState/FaultState, cleared without a resend) fields, every
 * field here is the first kind: hearthOnReconciled() unconditionally
 * re-sends the CURRENT cached identity via the low-level wire-only helper
 * (bypassing every setter's own unchanged-value guard, the same mechanism
 * MatterEvse::hearthOnReconciled() uses for CircuitCapacity), as long as
 * anything has ever been configured (the power-threshold invariant above
 * means this is equivalent to "a power threshold has been set").
 *
 * CONFORMANCE / DEVICE TYPE NOTE: 0x0511 carries no variant scheme of its
 * own (mk_electrical_utility_meter(), main/mt_devtypes.cpp, discards its
 * `variant` argument outright: "(void)variant;"), unlike the sensor/meter or
 * EVSE. begin() below takes no variant parameter for that reason.
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "MatterEndPoint.h"

class MatterElectricalUtilityMeter : public MatterEndPoint {
public:
  MatterElectricalUtilityMeter();
  ~MatterElectricalUtilityMeter();

  // declares the endpoint only (device type 0x0511, no variant scheme, see
  // the header comment) and resets every cache. No wire traffic.
  bool begin();
  // this will stop processing Electrical Utility Meter Matter events
  void end();

  // MeterTypeEnum: 0 Utility, 1 Private, 2 Generic. Host-validated range;
  // requires a power threshold already set (see the header comment).
  bool setMeterType(uint8_t type);

  // PointOfDelivery / MeterSerialNumber / ProtocolVersion: 0..64 bytes of
  // printable ASCII (0x20..0x7E), no raw '"'. A comma is legal content. Each
  // requires a power threshold already set (see the header comment).
  bool setPointOfDelivery(const char *pod);
  bool setSerialNumber(const char *serial);
  bool setProtocolVersion(const char *protocol);

  // PowerThresholdStruct, wholesale replacement (not a merge): pwrPresent/
  // apparentPresent are the wire's own choice-b optionals (mW and mVA,
  // firmware units), at least one MUST be true; srcPresent/src is the
  // nullable PowerThresholdSourceEnum (0 Contract, 1 Regulator,
  // 2 Equipment). Every call to this class's other four setters needs this
  // to have succeeded at least once first (see the header comment).
  bool setPowerThreshold(bool pwrPresent, int64_t pwr, bool apparentPresent, int64_t apparent, bool srcPresent, uint8_t src);

  // this function is called by Matter internal event processor. Cluster
  // 0x0B06 is Instance-served for every one of its five attributes (task 9's
  // report): no +MTATTR URC is ever raised for it, and an injected one must
  // move nothing, the MatterEvse/MatterElectricalSensor shape.
  bool attributeChangeCB(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val) override;

protected:
  // clang-format off
  /* Wire constants, verified against main/mt_meter.cpp, main/mt_at.c and
   * main/include/mt_matter.h, not transcribed from the design spec. Plain
   * integers, the library's usual pattern: there is no connectedhomeip
   * header on a host build. */
  static const uint32_t kMeterIdClusterId = 0x0B06;  // 2822, MeterIdentification
  static const uint8_t  kMaxStrLen        = 64;      // MT_METERID_MAX_STR / CHIP's kMaximumStringSize
  // clang-format on

  // Hearth's own hook (MatterEndPoint.h): unconditionally re-pushes the
  // current cached identity, bypassing every setter's own unchanged-value
  // guard (the B229 pattern; see the header comment). A no-op until a power
  // threshold has been set at least once.
  void hearthOnReconciled() override;

  bool started = false;

  // Cached identity, mirroring the wire's own fields. hasX tracks "this
  // field has been explicitly set at least once", used both for each
  // setter's own unchanged-value guard and to pick a sensible default (0 or
  // "") for a field a push must carry but the sketch has never touched.
  bool hasMeterType = false;
  uint8_t meterType = 0;
  bool hasPod = false;
  char pod[kMaxStrLen + 1] = "";
  bool hasSerial = false;
  char serial[kMaxStrLen + 1] = "";
  bool hasProtocol = false;
  char protocol[kMaxStrLen + 1] = "";

  // The power threshold: hasPwr/hasApparent/hasSrc ARE the wire's own
  // presence flags (unlike the four above, there is no separate "ever set"
  // bookkeeping needed: PowerThresholdStruct's choice-b invariant already
  // makes the all-absent state unreachable once a real push has succeeded).
  bool hasPwr = false;
  int64_t pwr = 0;
  bool hasApparent = false;
  int64_t apparent = 0;
  bool hasSrc = false;
  uint8_t src = 0;

  // "AT+MTMETERID=<ep>,<type>,\"<pod>\",\"<serial>\",\"<protocol>\",<pwr>,
  // <apparent>,<src>" (mt_at.c's cmd_mtmeterid), built from exactly the
  // values given. Wire-only: the caller decides whether/what to commit to
  // the cache afterwards (house discipline: a failed write must not update
  // it), and hearthOnReconciled() calls this directly to bypass every
  // setter's own unchanged-value guard.
  bool hearthSendIdentity(
    uint8_t type, const char *podVal, const char *serialVal, const char *protocolVal, bool pwrPresent, int64_t pwrVal,
    bool apparentPresent, int64_t apparentVal, bool srcPresent, uint8_t srcVal
  );

  // Host-side grammar enforcement for a MeterIdentification string field
  // (PointOfDelivery / MeterSerialNumber / ProtocolVersion), the
  // MatterModeSelect::setSupportedModes() precedent: 0..kMaxStrLen bytes,
  // every byte printable ASCII (0x20..0x7E), never a '"' (an unescaped
  // quote would corrupt the wire line's own field boundary rather than
  // surviving as a clean +MTERR:1). A comma is deliberately NOT rejected:
  // mtmeterid_scan_string() (mt_at.c) lets it through as legal content.
  static bool hearthValidateMeterString(const char *s);
};

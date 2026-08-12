/*
 * Hearth.h - the Hearth global (link configuration, diagnostics and the
 * link-event surface for the AT+MT link to the ESP32-C6 co-processor), and
 * matterEvent_t / ArduinoMatter / Matter, the Matter-named surface.
 *
 * The HearthClass parts below have no arduino-esp32 counterpart and live on
 * Hearth rather than on the Matter-named class because the naming rule bars
 * adding members to a Matter-named class: that surface must stay exactly
 * what upstream defines, nothing more. See CLAUDE.md, "Naming and legal
 * constraint".
 *
 * ArduinoMatter and matterEvent_t, further down, ARE that Matter-named
 * surface: this is the file src/Matter.h's `#include "Hearth.h"` shim
 * resolves to, so an unmodified sketch's `Matter.*` and `matterEvent_t`
 * calls land here. It gets exactly the members upstream's Matter.h defines,
 * nothing more; see that class's own comment below for the two exceptions
 * and why they are not really exceptions.
 *
 * This header is also the umbrella upstream's own Matter.h is: upstream
 * pulls in all twenty of its MatterEndpoints/Matter*.h headers so that a
 * sketch's bare `#include <Matter.h>` is enough to declare a device object
 * at file scope, without the sketch including any endpoint header itself.
 * Task 9's compile pass found this file did not do the same (a gap the
 * task's own file scope did not cover, since this file predates it), so a
 * sketch built from nothing but `#include <Matter.h>` plus a file-scope
 * `MatterOnOffLight light;` failed to compile: exactly the pattern every
 * upstream example uses. Fixed by including MatterEndPoint.h and, below,
 * every endpoint header this library implements: the original four, plus
 * the thirteen the devtype expansion (Tasks L1-L6) added, seventeen of
 * upstream's twenty in total. None of those headers includes this one back
 * (checked: only the endpoint .cpp files and MatterEndPoint.cpp include
 * Hearth.h, and .cpp files are leaves nothing else includes), so this is a
 * one-way dependency, not a cycle broken only by #pragma once appearing to
 * work.
 *
 * Task S4 (switch + color light) added MatterGenericSwitch.h and
 * MatterColorLight.h, taking this list to nineteen. Task C5 added the
 * twentieth and last, MatterTemperatureControlledCabinet.h, completing
 * upstream's class set. See README.md, "Supported device types".
 *
 * Task C3 (command forwarding) adds a twenty-first: MatterDoorLock.h. It has
 * no arduino-esp32 counterpart (upstream's Matter library ships no door lock
 * class at all), so it is not part of "completing upstream's class set"
 * above; it is this library's own addition, included here for the same
 * reason as every class before it -- a sketch's bare `#include <Matter.h>`
 * plus a file-scope `MatterDoorLock lock;` must work with no endpoint header
 * of its own.
 *
 * The ten-type swoop (Tasks C2-C4) adds the twenty-second through
 * thirty-first: MatterLightSensor.h, MatterFlowSensor.h,
 * MatterAirQualitySensor.h (C2, Hearth-original sensors), MatterMountedOnOffControl.h,
 * MatterMountedDimmableLoadControl.h, MatterAirPurifier.h,
 * MatterExtractorHood.h, MatterCooktop.h (C3, actuator clones), and
 * MatterRoomAirConditioner.h, MatterPump.h (C4). None has an arduino-esp32
 * counterpart, same reason as MatterDoorLock.h above: a bare `#include
 * <Matter.h>` plus a file-scope declaration must work with no endpoint
 * header of its own. See README.md, "Hearth originals".
 *
 * Task C7 (the seven-type batch's first library task) adds the thirty-second
 * through thirty-fourth: MatterWaterValve.h, MatterModeSelect.h,
 * MatterChime.h. Same reasoning again, same "Hearth originals" section.
 *
 * Task C8 (the seven-type batch's second library task) adds the
 * thirty-fifth through thirty-ninth: MatterSmokeCOAlarm.h,
 * MatterPowerSource.h, and the OperationalState trio MatterLaundryWasher.h,
 * MatterDishwasher.h, MatterLaundryDryer.h. The trio's three headers each
 * include MatterEndpoints/MatterOperationalStateEndpoint.h themselves (the
 * shared implementation core the three subclass), so that header is not
 * listed separately here: it carries no device type of its own for a
 * sketch to declare, and the "no cycle" one-way dependency this comment
 * already establishes covers it transitively. Same reasoning as every
 * class before it, same "Hearth originals" section.
 *
 * Task 7 (RVC + Microwave batch) adds the fortieth: MatterRoboticVacuum.h.
 * Same reasoning again, same "Hearth originals" section: no arduino-esp32
 * counterpart, a sketch's bare `#include <Matter.h>` plus a file-scope
 * `MatterRoboticVacuum vacuum;` must work with no endpoint header of its
 * own.
 *
 * Task 8 (RVC + Microwave batch, last library task) adds the forty-first:
 * MatterMicrowaveOven.h. Same reasoning again, same "Hearth originals"
 * section. It includes MatterEndpoints/MatterOperationalStateEndpoint.h
 * itself (it subclasses that shared core, the same way the
 * OperationalState trio's three headers do), so that header is still not
 * listed separately here.
 *
 * Task 8 (composed-appliance round) adds the forty-second:
 * MatterRefrigerator.h, the first composed appliance (a 0x0070 parent
 * owning Temperature Controlled Cabinet children over Task 7's
 * parent-aware declaration). No arduino-esp32 counterpart, same "Hearth
 * originals" reasoning as every class above: a sketch's bare `#include
 * <Matter.h>` plus a file-scope `MatterRefrigerator fridge;` must work
 * with no endpoint header of its own. It includes
 * MatterEndpoints/MatterTemperatureControlledCabinet.h itself (its owned
 * cabinets are members of that class), which this list also carries
 * directly, as it always has: both routes are one-way includes, no cycle.
 *
 * Task 9 (composed-appliance round) adds the forty-third: MatterOven.h,
 * the second composed appliance (a 0x007B bare parent owning
 * MatterOvenCavity children, this library's first TYPED owned child
 * class). No arduino-esp32 counterpart, same "Hearth originals" reasoning:
 * a sketch's bare `#include <Matter.h>` plus a file-scope `MatterOven
 * oven;` must work with no endpoint header of its own. MatterOven.h
 * includes MatterEndpoints/MatterOvenCavity.h itself (its owned cavities
 * are members of that class), so the cavity header is not listed
 * separately here, the same transitive-coverage reasoning as the
 * OperationalState trio's shared core; MatterOvenCavity.h in turn includes
 * MatterTemperatureControlledCabinet.h (its base class), which this list
 * also carries directly. All routes are one-way includes, no cycle.
 *
 * Task 10 (composed-appliance round) adds the forty-fourth and last of the
 * round: MatterCookSurface.h, the owned child of the now-composed
 * MatterCooktop (0x0077 under 0x0078, the first parent-mandatory device
 * type). It is NOT listed below: MatterCooktop.h, already in this list,
 * includes it itself (its owned surfaces are members of that class), the
 * same transitive-coverage reasoning as MatterOven.h and its cavity above.
 * One-way includes only, no cycle: MatterCookSurface.h includes the
 * cabinet base header, never MatterCooktop.h.
 *
 * Task 7 (energy round A) adds the forty-fourth and forty-fifth device
 * type classes: MatterElectricalSensor.h and MatterElectricalMeter.h, the
 * two measurement-push types behind AT+MTMEAS. Hearth originals, same
 * reasoning as every class above: a sketch's bare `#include <Matter.h>`
 * plus a file-scope `MatterElectricalSensor sensor;` must work with no
 * endpoint header of its own. The meter header includes the sensor's (its
 * base class) itself; listing both below is belt-and-braces coverage, not
 * a cycle.
 *
 * Task 6 (energy round B) adds the forty-sixth and forty-seventh:
 * MatterWaterHeater.h and MatterHeatPump.h, both Hearth originals and
 * both direct MatterEndPoint children embedding the shared
 * HearthMeasurementPush helper (never a thin subclass: the water heater
 * carries three more surfaces on its one endpoint). Same umbrella
 * reasoning as every class above; one-way includes only, no cycle.
 *
 * Task 6 (energy round C1) adds the forty-eighth, forty-ninth and
 * fiftieth: MatterSolarPower.h, MatterBatteryStorage.h and
 * MatterDeviceEnergyManagement.h, three more Hearth originals. Solar
 * embeds HearthMeasurementPush, the DEM endpoint embeds the round's
 * HearthDemControl, and battery storage embeds BOTH on one endpoint
 * alongside its ember PowerSource attributes. Same umbrella reasoning
 * again; one-way includes only, no cycle.
 *
 * Task 4 (Thread role API, 0.11.0) adds HearthThread.h, not an endpoint
 * header at all: HearthThreadRole/HearthThreadInfo, the types
 * HearthClass::threadInfo()/threadRole() below use, and
 * hearthThreadRoleName(). No device type carries a Thread role API on any
 * SDK this library tracks, so this is a Hearth global surface (the
 * README's split rule) rather than a fifty-third MatterEndpoints/ entry;
 * included here, ahead of MatterEndPoint.h, because HearthClass's own
 * member declarations further down need the types before they are used.
 */
#pragma once

#include <functional>
#include <Arduino.h>
#include "HearthLink.h"
#include "HearthCompat.h"
#include "HearthThread.h"
#include "MatterEndPoint.h"
#include "MatterEndpoints/MatterOnOffLight.h"
#include "MatterEndpoints/MatterDimmableLight.h"
#include "MatterEndpoints/MatterColorTemperatureLight.h"
#include "MatterEndpoints/MatterTemperatureSensor.h"
#include "MatterEndpoints/MatterOnOffPlugin.h"
#include "MatterEndpoints/MatterDimmablePlugin.h"
#include "MatterEndpoints/MatterContactSensor.h"
#include "MatterEndpoints/MatterRainSensor.h"
#include "MatterEndpoints/MatterWaterFreezeDetector.h"
#include "MatterEndpoints/MatterWaterLeakDetector.h"
#include "MatterEndpoints/MatterHumiditySensor.h"
#include "MatterEndpoints/MatterPressureSensor.h"
#include "MatterEndpoints/MatterOccupancySensor.h"
#include "MatterEndpoints/MatterFan.h"
#include "MatterEndpoints/MatterWindowCovering.h"
#include "MatterEndpoints/MatterThermostat.h"
#include "MatterEndpoints/MatterEnhancedColorLight.h"
#include "MatterEndpoints/MatterGenericSwitch.h"
#include "MatterEndpoints/MatterColorLight.h"
#include "MatterEndpoints/MatterTemperatureControlledCabinet.h"
#include "MatterEndpoints/MatterDoorLock.h"
#include "MatterEndpoints/MatterLightSensor.h"
#include "MatterEndpoints/MatterFlowSensor.h"
#include "MatterEndpoints/MatterAirQualitySensor.h"
#include "MatterEndpoints/MatterMountedOnOffControl.h"
#include "MatterEndpoints/MatterMountedDimmableLoadControl.h"
#include "MatterEndpoints/MatterAirPurifier.h"
#include "MatterEndpoints/MatterExtractorHood.h"
#include "MatterEndpoints/MatterCooktop.h"
#include "MatterEndpoints/MatterRoomAirConditioner.h"
#include "MatterEndpoints/MatterPump.h"
#include "MatterEndpoints/MatterWaterValve.h"
#include "MatterEndpoints/MatterModeSelect.h"
#include "MatterEndpoints/MatterChime.h"
#include "MatterEndpoints/MatterSmokeCOAlarm.h"
#include "MatterEndpoints/MatterPowerSource.h"
#include "MatterEndpoints/MatterLaundryWasher.h"
#include "MatterEndpoints/MatterDishwasher.h"
#include "MatterEndpoints/MatterLaundryDryer.h"
#include "MatterEndpoints/MatterRoboticVacuum.h"
#include "MatterEndpoints/MatterMicrowaveOven.h"
#include "MatterEndpoints/MatterRefrigerator.h"
#include "MatterEndpoints/MatterOven.h"
#include "MatterEndpoints/MatterElectricalSensor.h"
#include "MatterEndpoints/MatterElectricalMeter.h"
#include "MatterEndpoints/MatterWaterHeater.h"
#include "MatterEndpoints/MatterHeatPump.h"
#include "MatterEndpoints/MatterSolarPower.h"
#include "MatterEndpoints/MatterBatteryStorage.h"
#include "MatterEndpoints/MatterDeviceEnergyManagement.h"

/*
 * The board variant is the single source of truth for the link: which UART
 * reaches the co-processor and which pins drive its reset and boot-mode
 * lines. A sketch never names a serial port, exactly as in the sibling
 * iLabs_ESP-NOW library, and for the same two reasons: the arduino-esp32
 * Matter API this library mirrors has nowhere to name one, and the wiring
 * is a property of the board rather than of the sketch.
 *
 * On a Challenger RP2350 WiFi6/BLE5 the variant supplies ESP_SERIAL_PORT =
 * Serial2 (GP4 TX, GP5 RX), PIN_ESP_MODE = GP14 and PIN_ESP_RST = GP15.
 * Serial1 there is the board's *external* UART on GP12/13 and is not the
 * co-processor link; earlier revisions of this library assumed it was.
 *
 * A board that wires the C6 elsewhere overrides HEARTH_SERIAL_PORT with a
 * build flag. Without either macro the library refuses to compile for the
 * target: see hearthEnsureLink() in Hearth.cpp.
 */
#if !defined(HEARTH_SERIAL_PORT) && defined(ESP_SERIAL_PORT)
#define HEARTH_SERIAL_PORT ESP_SERIAL_PORT
#endif

/*
 * Baud for the AT link. Matches the firmware's boot baud, AT_UART_BAUD in
 * at_core (shared with the ESP-NOW firmware).
 *
 * The firmware can be retuned at runtime with AT+MTBAUD, but this library
 * deliberately does not: the rate is not persisted, so every co-processor
 * reset returns the link to 115200, and hearthEnsureLink() resets the C6 on
 * every host start. Nothing in the Matter parity surface needs a faster
 * link. A sketch that wants one can drive AT+MTBAUD through
 * Hearth.hearthCommand() and restart the host UART itself.
 */
#ifndef HEARTH_LINK_BAUD
#define HEARTH_LINK_BAUD 115200
#endif

/*
 * RX buffer for the AT link, in bytes (bug B166, bench finding F-C10-3).
 *
 * The co-processor sends URCs unsolicited and this link has no flow
 * control: no C6 board routes RTS/CTS (the firmware's AT+MTFLOW accepts
 * only mode 0 for that reason), so nothing can tell the C6 to wait. The
 * only thing standing between a burst of URCs and lost bytes is the host
 * UART's own receive buffer, and that buffer has to survive the whole gap
 * between two of the sketch's poll() calls.
 *
 * arduino-pico's SerialUART defaults to 32 entries, 31 usable
 * (cores/rp2040/SerialUART.h, `size_t _fifoSize = 32`), and its ISR drops
 * the NEWEST byte once the queue is full (SerialUART.cpp, `if
 * (!_queue->write(val)) { _overflow = true; }`). One controller
 * SelfTestRequest makes the firmware emit 53 contiguous bytes -- measured
 * on the wire, 2026-08-08:
 *
 *   +MTATTR:1,92,5,1\r\n  +MTATTR:1,92,0,4\r\n  +MTCMD:0,1,92,0\r\n
 *
 * with the +MTCMD the SmokeCoAlarm self test needs arriving LAST, at
 * offset 36. On the default buffer the tail of that burst was discarded by
 * the UART driver before HearthLink ever saw it, five times out of five,
 * and MatterSmokeCOAlarm::onSelfTest() never ran. A single-line burst
 * (every adjudicated +MTCMD, e.g. the OperationalState trio's 17 bytes)
 * fits in 31 and worked throughout, which is exactly why this looked like
 * a defect in the notify-only dispatch path and was invisible to the host
 * test suite, whose MockStream has no bounded buffer at all.
 *
 * 1024 bytes is ~89 ms of continuous 115200 traffic, which covers the
 * delay(10)-per-iteration cadence the examples use with two orders of
 * magnitude to spare, and costs 1 KB of the RP2350's 520 KB. A sketch
 * whose loop() blocks for longer than that between poll() calls should
 * raise this rather than assume the link is lossless.
 */
#ifndef HEARTH_LINK_RX_BUFFER
#define HEARTH_LINK_RX_BUFFER 1024
#endif

/*
 * Max wait for +MTREADY after releasing the co-processor's reset.
 *
 * Deliberately far more generous than the 3000 ms the ESP-NOW library
 * allows its own +ENREADY. Hearth raises the marker only after app_main has
 * rebuilt the stored endpoint composition and run esp_matter::start(), a
 * much heavier boot than a bare ESP-NOW image. The real figure has not been
 * measured on hardware yet; tighten this once it has.
 */
#ifndef HEARTH_READY_TIMEOUT_MS
#define HEARTH_READY_TIMEOUT_MS 10000
#endif

/*
 * Safety-net ceiling on hearthArmExpectedReboot(): how long an arm may sit
 * unconsumed before it self-clears. Not a tuned value, just generous enough
 * to cover a full co-processor reboot and Matter stack reinit; its only job
 * is to stop a caller that forgets to call hearthDisarmExpectedReboot() on
 * its own timeout path from leaving the arm live to swallow an unrelated,
 * later spontaneous reboot as if it were the expected one.
 */
#ifndef HEARTH_REBOOT_ARM_TIMEOUT_MS
#define HEARTH_REBOOT_ARM_TIMEOUT_MS 20000
#endif

#define HEARTH_ERR_NOT_SUPPORTED 8  /* the wire's +MTERR:8, named */

typedef enum {
  HEARTH_TRANSPORT_WIFI = 0,
  HEARTH_TRANSPORT_THREAD = 1,
} HearthTransport;

enum hearthEvent_t {
  HEARTH_LINK_UP = 0,
  HEARTH_LINK_DOWN,
  HEARTH_COPROCESSOR_REBOOTED,
  HEARTH_PROTOCOL_ERROR,
  /* Firmware +MTEVT:27 (AT_MT_SPEC.md S3.12.1): the device holds a fabric
   * but its active transport is not provisioned, so it opened a
   * commissioning window. Hearth-specific, so it arrives here and not on
   * the upstream-parity ArduinoMatter::onEvent() surface. Poll
   * Hearth.transportMismatch() for the same state on demand. */
  HEARTH_TRANSPORT_MISMATCH,
  /* Firmware +MTCMDTO:<seq> (AT_MT_SPEC.md S3.17): a forwarded command's
   * 1000 ms verdict window (see mt_at_urc()'s dispatch of +MTCMD) closed
   * with no AT+MTCMDRESP from this host, so the firmware default-denied it
   * on its own. No further action is needed or possible on this side: by
   * the time this arrives, the window that AT+MTCMDRESP would have
   * answered is already gone. */
  HEARTH_CMD_TIMEOUT,
};

class HearthClass {
public:
  using hearthEventCB = std::function<void(hearthEvent_t)>;

  HearthClass();

  /*
   * Escape hatch: run the link on a caller-supplied Stream instead of the
   * variant's UART. For a board no variant describes, or a bench rig that
   * puts something else in the middle. The caller owns that stream
   * completely: it must already be started at the intended baud (Stream
   * itself has no begin(), so `baud` is accepted for interface parity and
   * ignored), and no automatic co-processor reset is performed, because the
   * pins that would drive it are not implied by an arbitrary Stream. Call
   * hearthResetCoprocessor() afterwards if the variant does define them.
   *
   * The caller also owns the receive buffer, and on this link that is not a
   * formality: HEARTH_LINK_RX_BUFFER above documents a real 53-byte
   * unsolicited burst that a stock arduino-pico UART buffer loses the tail
   * of (bug B166). Size the stream's own buffer accordingly, because
   * nothing here can: Stream exposes no way to.
   *
   * The normal path is not to call this at all. The first use of the link
   * then brings up HEARTH_SERIAL_PORT at HEARTH_LINK_BAUD and resets the
   * co-processor into a known state on its own. See hearthEnsureLink().
   */
  void begin(Stream &serial, unsigned long baud = HEARTH_LINK_BAUD);

  /*
   * Drive the co-processor's boot-mode and reset lines to give it a clean,
   * deterministic boot into run mode, then block until its +MTREADY (up to
   * HEARTH_READY_TIMEOUT_MS), discarding the boot ROM chatter that shares
   * this UART. The resulting boot marker is consumed as expected, so it
   * raises no HEARTH_COPROCESSOR_REBOOTED.
   *
   * A no-op unless the board variant defines PIN_ESP_MODE and PIN_ESP_RST,
   * and on the host test build. Called automatically by hearthEnsureLink()
   * on the variant-driven path; public so a sketch can recover a wedged
   * co-processor, and so the begin(Stream&) escape hatch has a way to ask
   * for the same treatment.
   *
   * Safe with a device already commissioned: this pin reset does not touch
   * the fabric or the stored composition (the AT+MTRESET and AT+MTFRESET
   * commands do that; the latter also erases the composition).
   */
  void hearthResetCoprocessor();

  /* True if a bare AT probe round-trips OK. */
  bool linkUp();

  /* The S3.12.1 transport-mismatch flag from a live AT+MTNET? round-trip:
   * true when the device holds a fabric but its active transport is not
   * provisioned. Always a fresh query, like every other network predicate
   * in this library. Older firmware never reports it, so this is false
   * there. */
  bool transportMismatch();

  /*
   * Stage the network transport for the NEXT boot (combined-image
   * firmware, AT_MT_SPEC.md S3.12.2). Returns true when the firmware
   * confirms the setting is stored; nothing changes until the
   * co-processor reboots, and the host owns that reboot. On
   * single-transport firmware the command does not exist and this
   * returns false with lastError() == HEARTH_ERR_NOT_SUPPORTED.
   */
  bool setTransport(HearthTransport t);

  /*
   * Read the active and stored transport. A pending switch shows as
   * active != stored. Same not-supported behavior as setTransport().
   */
  bool transport(HearthTransport *active, HearthTransport *stored);

  /*
   * AT+MTTHREAD? (AT_MT_SPEC.md S3.27, 0.11.0): the device's Thread routing
   * role and, when it has one, which mesh it is on. Always a fresh round
   * trip that fills every field, including the has* flags for the wire's
   * four nullable ones (channel/panId/extPanId/partitionId) -- a caller
   * must gate on those flags, never infer "unknown" from a value looking
   * implausible (a real PanId of 0xFFFF or an all-ones ExtPanId renders
   * identically to null, S3.27's own caveat). On success also syncs the
   * cache threadRole() below reads. Returns false on a WiFi image (or the
   * combined image booted in WiFi mode), with lastError() == 8
   * (HEARTH_ERR_NOT_SUPPORTED): that image has no ThreadNetworkDiagnostics
   * cluster to read.
   */
  bool threadInfo(HearthThreadInfo &out);

  /*
   * The Thread routing role, cached: NO wire traffic, ever. Seeded to
   * HEARTH_THREAD_UNSPECIFIED (begin()'s own reset, and the enum's zero
   * value, agree: "nothing known yet" and "the wire's own honest answer
   * when the interface is down" are the same value on purpose), and
   * refreshed by threadInfo() and by every +MTEVT:28 (HearthThread.cpp's
   * hearthDispatchThreadRoleEvt(), called from Hearth.cpp's
   * hearthDispatchEvt()). This is the read path a sketch polling its role
   * every loop() iteration should use: threadInfo() pays a UART round
   * trip every call, this one never does.
   */
  HearthThreadRole threadRole() const {
    return _threadRole;
  }

  /*
   * Register the role-change callback (AT_MT_SPEC.md S3.27/+MTEVT:28,
   * 0.11.0). A PLAIN function pointer, the HearthDemControl::onPowerAdjust
   * convention, not std::function: there is no verdict to return here,
   * only a notification. At most one; a later call replaces the previous
   * one, nullptr removes it (never cleared automatically by begin(), the
   * same "no existing endpoint class clears a callback registration on
   * begin()" precedent every other callback in this library follows).
   *
   * REGISTERING IS WHAT SUBSCRIBES (bench review, 0.11.0): bit 28 is
   * opt-in (AT_MT_SPEC.md S3.11's default mask is 0x0800003F, no Thread
   * role bit), so the device never emits +MTEVT:28 to a host that never
   * asked for it, no matter how correct this callback's own dispatch is.
   * A non-null `cb` here arms a background read-modify-write of the
   * event mask (AT+MTEVT? then AT+MTEVT=<mask|bit28>, HearthThread.cpp's
   * hearthDrainEvtResubscribe()) on the next poll()/hearthCommand(), never
   * blindly overwriting whatever else is already subscribed; nullptr arms
   * the same exchange with bit 28 ANDed out instead, leaving every other
   * bit alone. Because AT_MT_SPEC.md S3.11 also states the mask is
   * RAM-only and resets to the firmware default on every co-processor
   * reboot, this library re-arms the same background resubscribe on every
   * +MTREADY (hearthOnURCLine()) while a callback is registered, and once
   * more from begin() for a registration made before the link exists --
   * a sketch that calls this once in setup() stays subscribed across a
   * reboot the sketch itself never has to notice.
   *
   * RUNS INSIDE URC DISPATCH, with the link's busy gate held by whatever
   * poll()/hearthCommand() call is delivering the +MTEVT:28 line: a wire
   * write from inside it -- including calling threadInfo() itself -- is
   * refused HEARTH_CMD_REENTRANT and reaches nothing (the README's "Your
   * onChange fires from inside your own setter" rule, and
   * HearthDemControl's identical warning on its own two callbacks). Set a
   * flag in the callback and act on it from loop(); never call back into
   * this library from inside it.
   */
  void onThreadRoleChange(void (*cb)(HearthThreadRole)) {
    _onThreadRoleChangeCB = cb;
    _evtResubscribeNeeded = true;
  }

  /* Last +MTERR code any layer above HearthLink reported; 0 if none.
   *
   * A HOST-SIDE 1 IS NOT A WIRE 1. Some classes refuse a call locally and
   * set 1 with no traffic at all, so a 1 can mean "this library refused"
   * rather than "the device answered +MTERR:1", and the device would often
   * have answered something else: a capability setter on the DEM
   * REPORT_ONLY variant sets 1 where the device would have said 4 (the
   * attribute is not served there), and any DEM setter on a NO_DEM or
   * POWER_ONLY endpoint sets 1 where the device would have said 3 (no such
   * cluster on that endpoint). Read it as a diagnostic, not as a
   * transcript of the wire. */
  int lastError() const {
    return _lastError;
  }

  /* Register the link-event callback (co-processor reboot, protocol
   * trouble). At most one; a later call replaces the previous one. */
  void onLinkEvent(hearthEventCB cb);

  /* Drain any pending URCs without blocking. Call from loop(). */
  void poll();

  /* AT+MTVER?. Empty String on failure. */
  String firmwareVersion();

  /*
   * True once the Matter-named layer (Task 5) has warned the host's Serial
   * that applying the sketch's declared endpoint composition will
   * invalidate controller caches on a device that already belongs to a
   * fabric. Lives here, not on Matter, because the naming rule bars adding
   * members to Matter-named classes, including for test introspection.
   */
  bool warnedAboutRecommission() const {
    return _warnedAboutRecommission;
  }
  /* For the Matter-named layer to call once it has issued that warning. */
  void hearthSetWarnedAboutRecommission(bool warned = true) {
    _warnedAboutRecommission = warned;
  }

  /*
   * True once a composition reconcile (ArduinoMatter::begin()) has failed,
   * and therefore must not be attempted again for the rest of this boot.
   *
   * kHearthMaxReconcileAttempts (Hearth.cpp) bounds one call; this bounds
   * the boot. Upstream's own examples poll the library from loop(), and
   * this library's README used to suggest calling Matter.begin() there too,
   * so without a latch a composition the C6 rejects (unknown device type,
   * or past its 16-endpoint cap) would run AT+MTEPCLEAR, the writes,
   * AT+MTEPAPPLY and a co-processor reboot on every single loop iteration,
   * forever: unbounded NVS wear on the C6 and a device that never finishes
   * booting. Nothing about the inputs changes between those iterations, so
   * a retry can only repeat the damage.
   *
   * The latch is cleared by begin() (a new link is a new start) and by
   * MatterEndPoint::hearthClearDeclarations() (a new composition is a new
   * set of inputs, so it deserves a real attempt), which are also the two
   * hooks the host tests use to start clean.
   *
   * Lives here rather than on ArduinoMatter because the naming rule bars
   * adding members to a Matter-named class.
   */
  bool hearthReconcileFailed() const {
    return _reconcileFailed;
  }
  /* For the Matter-named layer, which cannot hold this state itself. */
  void hearthSetReconcileFailed(bool failed = true) {
    _reconcileFailed = failed;
  }

  /*
   * Arm the link for a reboot the host itself is about to trigger, e.g.
   * immediately before sending AT+MTEPAPPLY. While armed, the next
   * +MTREADY completes the arm silently instead of raising
   * HEARTH_COPROCESSOR_REBOOTED: see hearthExpectedRebootSeen().
   *
   * The arm cannot outlive its wait unattended: `timeout_ms` after arming,
   * poll() and hearthCommand() self-clear it even if no +MTREADY ever
   * arrives (the co-processor crashed or wedged mid-apply), so a later,
   * unrelated spontaneous reboot is not silently swallowed as if it were
   * this one. A caller that detects its own timeout first should still call
   * hearthDisarmExpectedReboot() immediately rather than wait for this
   * ceiling; the deadline is a backstop, not the primary mechanism.
   */
  void hearthArmExpectedReboot(uint32_t timeout_ms = HEARTH_REBOOT_ARM_TIMEOUT_MS);

  /*
   * Clear an arm from hearthArmExpectedReboot() without waiting for its
   * +MTREADY or its deadline. Call this on the caller's own timeout path,
   * so the very next spontaneous reboot (which is now what is happening,
   * since the expected one did not show up in time) is reported rather than
   * swallowed. A no-op if not currently armed.
   */
  void hearthDisarmExpectedReboot();

  /*
   * True once the +MTREADY armed by hearthArmExpectedReboot() has arrived.
   * Does not clear itself; call hearthArmExpectedReboot() again before
   * arming the next wait. The intended use (Task 5's AT+MTEPAPPLY
   * sequence) is a non-blocking poll loop the caller drives itself, the
   * same pattern HearthLink::poll() already uses for URCs in general:
   *   Hearth.hearthArmExpectedReboot();
   *   Hearth.hearthCommand("AT+MTEPAPPLY");
   *   uint32_t start = millis();
   *   while (!Hearth.hearthExpectedRebootSeen()) {
   *     Hearth.poll();
   *     if (millis() - start > OWN_TIMEOUT_MS) {
   *       Hearth.hearthDisarmExpectedReboot();  // do not leave the arm live
   *       break;
   *     }
   *     yield();
   *   }
   */
  bool hearthExpectedRebootSeen() const {
    return _expectedRebootSeen;
  }

  /*
   * Send one AT+MT command through the link and remember its outcome as the
   * last error: a +MTERR code on failure, 0 otherwise. Returns the
   * HearthLink::command() result unchanged (0 OK, >0 +MTERR code, -1 plain
   * ERROR, -2 timeout / link not started).
   */
  int hearthCommand(const char *cmd, HearthLink::LineCb onLine = nullptr, void *arg = nullptr);

  /* Record an error code for a caller that fails before reaching the wire,
   * e.g. an attribute value type the AT protocol cannot carry. */
  void hearthSetError(int code);

  /*
   * Arm a deferred-work drain (Task 6, energy round B). Called by an
   * endpoint type from inside a +MTCMD dispatch, where a wire write of its
   * own would be refused HEARTH_CMD_REENTRANT: once the queued
   * AT+MTCMDRESP verdicts have been sent and the busy gate is released,
   * hearthDrainDeferredWork() calls every declared endpoint's
   * hearthOnDeferredWork() (MatterEndPoint.h) exactly once. The endpoint
   * itself records WHAT is pending; this only records THAT something is.
   */
  void hearthRequestDeferredWork();

  /* The underlying transport, for callers that need it directly. */
  HearthLink &link() {
    return _link;
  }

  /*
   * Report protocol-level trouble the Matter-named layer detected itself,
   * e.g. when the +MTREADY that AT+MTEPAPPLY promises never arrives. That
   * layer cannot hold the event callback itself (the naming rule bars
   * adding anything, including private state, to a Matter-named class), and
   * cannot be a friend of HearthClass without HearthClass naming a
   * Matter-named class back, which is the same rule working the other way.
   * Deliberately narrower than a general "raise any event" method: it only
   * ever raises HEARTH_PROTOCOL_ERROR, so an external caller cannot
   * synthesize HEARTH_LINK_UP or HEARTH_COPROCESSOR_REBOOTED, which this
   * class alone should ever assert really happened.
   */
  void hearthReportProtocolError();

private:
  void hearthEnsureLink();
  void hearthRaiseEvent(hearthEvent_t e);
  void hearthCheckExpectedRebootExpiry();
  static void hearthOnURCLine(const char *line, void *arg);
  static void hearthOnVerLine(const char *line, void *arg);
  static void hearthDispatchEvt(const char *rest, HearthClass *self);
  static void hearthDispatchCmd(const char *rest, HearthClass *self);
  static void hearthDispatchCmdTimeout(const char *rest, HearthClass *self);
  /* +MTEVT:28's payload (Task 4, 0.11.0): defined in HearthThread.cpp, not
   * Hearth.cpp, alongside the rest of the Thread role surface; declared
   * here because hearthDispatchEvt() (Hearth.cpp) calls it directly, ahead
   * of the generic numeric <detail> parse every other bit uses. */
  static void hearthDispatchThreadRoleEvt(const char *rest, HearthClass *self);

  /*
   * The event-mask read-modify-write behind onThreadRoleChange()'s
   * subscription (bench review, 0.11.0): defined in HearthThread.cpp,
   * alongside the rest of the Thread role surface. Runs from poll() and
   * hearthCommand(), same two call sites and same "after the outer
   * _link call has returned" ordering as hearthDrainCmdRespQueue() and
   * hearthDrainDeferredWork() -- for the identical reason: it must reach
   * the wire through _link.command() directly, never through
   * hearthCommand() itself, or a nested poll()/drain call would recurse
   * back into here.
   */
  void hearthDrainEvtResubscribe();

  /*
   * Fix round 1 (C3 review, CRITICAL): a +MTCMD verdict must never be
   * written to the wire from inside dispatchURC() itself. That call runs
   * with HearthLink's busy gate already held by the outer command()/poll()
   * that delivered the URC, so a fire-and-forget write there leaves its own
   * OK/+MTERR terminal ownerless on the single-reader link: a later
   * command()'s read loop can claim it as ITS OWN terminal instead of the
   * reply it actually asked for (confirmed against the real library: a
   * rejected AT+MTLOCK read back as success because the queued AT+MTCMDRESP
   * reply's OK arrived first and was consumed by the next hearthCommand()).
   *
   * The fix: hearthDispatchCmd() only runs the verdict callback and enqueues
   * (seq, verdict) here; hearthDrainCmdRespQueue() -- called from
   * hearthCommand() and poll(), both AFTER their own _link.command()/
   * _link.poll() call has returned, i.e. with the busy gate released --
   * sends each queued reply through the ordinary _link.command() path,
   * which waits for and consumes its own terminal before the next entry (or
   * the caller) gets to run. No orphan terminal can exist: every
   * AT+MTCMDRESP exchange, including its OK/+MTERR:1, is fully resolved
   * before this class ever does anything else on the link.
   *
   * Depth 4: the firmware serializes command forwards (only one window open
   * at a time), so more than one or two entries here is already an unusual
   * burst (e.g. a +MTCMDTO followed immediately by a new forward); 4 is
   * headroom, not a tuned minimum. An overflow silently drops the newest
   * entry -- the firmware's own 1000 ms deadline default-denies it anyway,
   * so a dropped reply degrades to exactly the timeout path already handled
   * (HEARTH_CMD_TIMEOUT), not a hang.
   */
  struct HearthPendingCmdResp {
    uint32_t seq;
    bool verdict;
  };
  static const uint8_t kHearthCmdRespQueueDepth = 4;
  void hearthEnqueueCmdResp(uint32_t seq, bool verdict);
  void hearthDrainCmdRespQueue();

  /*
   * The verdict-then-push half of the same fix (Task 6, energy round B):
   * an endpoint whose ACCEPTED command must be followed by a wire push of
   * its own (the water heater's BoostState push, AT_MT_SPEC.md
   * S3.17/S3.25) has exactly the same re-entrancy problem the verdict
   * itself had, one step later. It arms this flag from its dispatch via
   * hearthRequestDeferredWork() above; hearthDrainDeferredWork() -- called
   * from hearthCommand() and poll() AFTER hearthDrainCmdRespQueue(), so
   * the verdict always precedes the push on the wire -- clears the flag
   * FIRST and then walks the declared registry calling each endpoint's
   * hearthOnDeferredWork(). Clearing first is the re-entrancy guard: the
   * endpoint's own wire write runs poll() at its top, which reaches this
   * same drain and finds the flag already down. A busy link bails with the
   * flag intact, the hearthDrainCmdRespQueue() discipline.
   */
  void hearthDrainDeferredWork();

  HearthLink _link;
  int _lastError;
  bool _warnedAboutRecommission;
  bool _reconcileFailed;
  bool _expectingReboot;
  bool _expectedRebootSeen;
  uint32_t _expectedRebootArmedAt;
  uint32_t _expectedRebootTimeoutMs;
  hearthEventCB _linkEventCB;
  HearthPendingCmdResp _cmdRespQueue[kHearthCmdRespQueueDepth];
  uint8_t _cmdRespQueueCount;
  bool _deferredWorkPending;

  /* Task 4 (Thread role API, 0.11.0): threadRole()'s cache and
   * onThreadRoleChange()'s registration. See both methods' own comments
   * above. */
  HearthThreadRole _threadRole;
  void (*_onThreadRoleChangeCB)(HearthThreadRole);

  /*
   * Bench review round: true whenever the device-side event mask may not
   * match what onThreadRoleChange()'s current registration wants (bit 28
   * subscribed if _onThreadRoleChangeCB is non-null, unsubscribed if it is
   * null). Set by onThreadRoleChange() itself (every call, register or
   * clear), by begin() (a fresh link's device-side mask is unknown, or
   * about to be reset by hearthResetCoprocessor()), and by hearthOnURCLine()
   * on every +MTREADY while a callback is registered (AT_MT_SPEC.md S3.11:
   * the mask is RAM-only and reverts to the firmware default on every
   * co-processor reboot, so a subscription made before a reboot is
   * silently lost device-side unless re-armed here). Cleared by
   * hearthDrainEvtResubscribe() before it runs, the same re-entrancy
   * guard hearthDrainDeferredWork() uses for its own flag.
   */
  bool _evtResubscribeNeeded;
};

/*
 * The same opt-out upstream's Matter.h gives its own global (see the guard
 * on `Matter` at the bottom of this file), under a Hearth-named macro
 * because this global is Hearth's own invention. A sketch that already has
 * a symbol called Hearth, or that simply does not want the library
 * declaring names it did not ask for, defines NO_GLOBAL_HEARTH, or
 * arduino-esp32's blanket NO_GLOBAL_INSTANCES, and this declaration goes
 * away.
 *
 * As upstream does, the *definition* in Hearth.cpp is left unguarded, so an
 * opted-out sketch still links against an object that exists.
 *
 * That alone is not enough here, and the earlier version of this comment got
 * the reason wrong: it reasoned about the definition disappearing, when what
 * actually disappears under the macro is the *declaration*, and the
 * declaration is what the library's own translation units need. Upstream can
 * get away with the bare guard because none of its own .cpp files ever names
 * its Matter global. This library's do: MatterEndPoint.cpp calls through the
 * Hearth object nine times, Hearth.cpp more. The macro is never scoped to
 * the sketch, either: it arrives as a build-wide -D and reaches every
 * library source. So a build-wide NO_GLOBAL_INSTANCES did not compile at
 * all, which is the opposite of an opt-out.
 *
 * The fix is HearthGlobal.h, an internal header carrying the same `extern`
 * with no guard, which every library source that touches the object includes
 * instead of this one. The guard below then does exactly, and only, what it
 * is for: keep the name out of the *sketch's* translation unit.
 */
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_HEARTH)
extern HearthClass Hearth;
#endif

/*
 * CHIP platform event identifiers. Names and values are read from source,
 * not transcribed from memory (CLAUDE.md's rule on device type IDs applies
 * here for the same reason: a wrong value here is a silent, hard-to-notice
 * bug for any sketch that switches on it):
 *
 *   - the base values: connectedhomeip's
 *     src/include/platform/CHIPDeviceEvent.h, kFlag_IsPublic = 0x8000,
 *     kRange_Public = kFlag_IsPublic, and the PublicEventTypes enum, whose
 *     first member kWiFiConnectivityChange = kRange_Public and the rest
 *     increment from there;
 *   - the four commissioning-session/window events and the four fabric
 *     events (0xD000-0xD007): esp-matter's own components/esp_matter/
 *     esp_matter.h, which extends DeviceEventType with
 *     kCommissioningSessionStarted = kRange_PublicPlatformSpecific + 0x1000,
 *     where kRange_PublicPlatformSpecific = kFlag_IsPublic |
 *     kFlag_IsPlatformSpecific = 0xC000, and the rest increment from there;
 *   - the enumerator names, MATTER_ESP32_SPECIFIC_EVENT's "previous + 1"
 *     value (upstream's own comment claims 0x9000, but the code has no
 *     explicit value there, so it takes MATTER_BLE_DEINITIALIZED + 1 by
 *     ordinary C++ enum rules), and MATTER_ESP32_PUBLIC_SPECIFIC_EVENT =
 *     kRange_PublicPlatformSpecific (0xC000): arduino-esp32 3.3.8's
 *     libraries/Matter/src/Matter.h, lines 56-160.
 */
enum matterEvent_t {
  MATTER_WIFI_CONNECTIVITY_CHANGE = 0x8000,
  MATTER_THREAD_CONNECTIVITY_CHANGE,
  MATTER_INTERNET_CONNECTIVITY_CHANGE,
  MATTER_SERVICE_CONNECTIVITY_CHANGE,
  MATTER_SERVICE_PROVISIONING_CHANGE,
  MATTER_TIME_SYNC_CHANGE,
  MATTER_CHIPOBLE_CONNECTION_ESTABLISHED,
  MATTER_CHIPOBLE_CONNECTION_CLOSED,
  MATTER_CLOSE_ALL_BLE_CONNECTIONS,
  MATTER_WIFI_DEVICE_AVAILABLE,
  MATTER_OPERATIONAL_NETWORK_STARTED,
  MATTER_THREAD_STATE_CHANGE,
  MATTER_THREAD_INTERFACE_STATE_CHANGE,
  MATTER_CHIPOBLE_ADVERTISING_CHANGE,
  MATTER_INTERFACE_IP_ADDRESS_CHANGED,
  MATTER_COMMISSIONING_COMPLETE,
  MATTER_FAIL_SAFE_TIMER_EXPIRED,
  MATTER_OPERATIONAL_NETWORK_ENABLED,
  MATTER_DNSSD_INITIALIZED,
  MATTER_DNSSD_RESTART_NEEDED,
  MATTER_BINDINGS_CHANGED_VIA_CLUSTER,
  MATTER_OTA_STATE_CHANGED,
  MATTER_SERVER_READY,
  MATTER_BLE_DEINITIALIZED,
  MATTER_ESP32_SPECIFIC_EVENT, /* = previous + 1, see the comment above */
  MATTER_COMMISSIONING_SESSION_STARTED = 0xD000,
  MATTER_COMMISSIONING_SESSION_STOPPED,
  MATTER_COMMISSIONING_WINDOW_OPEN,
  MATTER_COMMISSIONING_WINDOW_CLOSED,
  MATTER_FABRIC_WILL_BE_REMOVED,
  MATTER_FABRIC_REMOVED,
  MATTER_FABRIC_COMMITTED,
  MATTER_FABRIC_UPDATED,
  MATTER_ESP32_PUBLIC_SPECIFIC_EVENT = 0xC000,
};

/*
 * ArduinoMatter: the Matter-named surface. Gets exactly the members
 * upstream's Matter.h (arduino-esp32 3.3.8, line ~165 on) defines, nothing
 * more; anything this port needs to invent lives on HearthClass above
 * instead, per CLAUDE.md's naming rule. Two things upstream has are handled
 * differently here, neither of which is really an exception:
 *
 *   - _matterEventCB is upstream's own public static member, not something
 *     this port added. It stays public, as upstream has it, which is what
 *     lets HearthClass's URC dispatcher (Hearth.cpp) invoke it directly
 *     without either class naming the other as a friend.
 *   - the friend class list (MatterOnOffLight, MatterTemperatureSensor,
 *     ...) and the protected _init() upstream has exist so concrete
 *     endpoint classes can reach into ArduinoMatter's private esp_matter
 *     setup. This port's endpoint types (Task 6 on) talk to the C6 over
 *     AT+MTEP / AT+MTATTR instead of a local esp_matter data model, so
 *     there is no equivalent setup to reach into; both are omitted as not
 *     applicable rather than reproduced as dead code.
 *
 * getManualPairingCode() and getOnboardingQRCodeUrl() are declared here and
 * defined in Hearth.cpp rather than inline as upstream has them: upstream's
 * bodies return a hardcoded test credential; these query AT+MTCODES? for
 * the device's real one, which does not fit in a one-line inline body. The
 * signatures match upstream exactly; only the implementation differs.
 */
class ArduinoMatter {
public:
  using matterEventCB = std::function<void(matterEvent_t, const chip::DeviceLayer::ChipDeviceEvent *)>;
  static matterEventCB _matterEventCB;

  static void onEvent(matterEventCB cb) {
    _matterEventCB = cb;
  }

  static void begin();

  static String getManualPairingCode();
  static String getOnboardingQRCodeUrl();

  static bool isWiFiStationEnabled();
  static bool isWiFiAccessPointEnabled();
  static bool isThreadEnabled();
  static bool isBLECommissioningEnabled();

  static bool isDeviceCommissioned();
  static bool isWiFiConnected();
  static bool isThreadConnected();
  static bool isDeviceConnected();
  static void decommission();
};

/* Upstream's own guard, verbatim (arduino-esp32 3.3.8, Matter.h:222). Not
 * an addition to the Matter-named surface: it is part of that surface. */
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MATTER)
extern ArduinoMatter Matter;
#endif

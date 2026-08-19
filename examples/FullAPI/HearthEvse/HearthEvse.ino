/*
 * FullAPI reference: MatterEvse
 *
 * Demonstrates the complete public API of this class. The banner below is
 * the coverage checklist: every public member, and where this sketch
 * exercises it. A Hearth original: arduino-esp32's Matter library ships no
 * EVSE class at all (see the class header), so this is this port's own
 * design against the firmware's wire contract (energy round C2,
 * main/mt_evse.cpp / main/mt_at.c; docs/AT_MT_SPEC.md does not describe
 * EnergyEvse yet, Task 14 owns that).
 *
 *   Variant_t:              FULL=0 (SOC feature: every charging target
 *                            must carry a target SoC), NO_SOC=1 (a
 *                            target's SoC must be absent or exactly 100).
 *                            This sketch runs NO_SOC, the simpler shape.
 *   EnableChargingInfo:      the unpacked EnableCharging payload
 *                            (chargingEnabledUntil nullable, the two
 *                            currents mandatory)
 *
 *   MatterEvse()             global object below
 *   begin(variant)           setup(), NO_SOC; declares only, no wire
 *                            traffic
 *   setSupplyState(state)    setup() after Matter.begin(), and after every
 *                            accepted Disable/EnableCharging: the firmware
 *                            deliberately leaves this to the host (the
 *                            class header's "one authority" rule)
 *   setFaultState(state)     menu 'f' (simulates a fault, then clears it)
 *   setCircuitCapacity(mA)   setup() after Matter.begin() (an installation
 *                            fact, re-pushed on every reconcile)
 *   setChargingSchedule(s)   setup() (an initial schedule) and menu 'n'
 *                            (replaces Monday's targets only, the
 *                            merge-by-day rule)
 *   chargingSchedule()       menu 'g' (prints the cached view)
 *   onSetTargets(cb)         setup(); the ADJUDICATED, deferred path (see
 *                            the class header): this handler runs from
 *                            hearthOnDeferredWork(), not from the +MTCMD
 *                            dispatch itself, so it is free to take as
 *                            long as ordinary sketch code would, and it
 *                            must never be assumed to run synchronously
 *                            with the controller's own invoke. Its second
 *                            argument is the AFFECTED-DAY MASK, which is
 *                            not derivable from the schedule: a day named
 *                            by the mask with no row in the proposal is
 *                            being EMPTIED. This sketch reports those
 *                            days explicitly.
 *
 * STACK HEADROOM, with the hardware figures (measured 2026-08-19 on a
 * Challenger RP2350 over Thread, by painting core 0's stack and scanning
 * for the deepest word touched, so libc, arduino-core and interrupt frames
 * are all counted). Core 0's stack is 4 KB, core 1's sits immediately
 * below it, and stack protection is off, so an overflow corrupts core 1
 * silently instead of faulting.
 *
 * Dual core (a sketch that also defines setup1/loop1), on this sketch's
 * own shape, at the deepest point of a controller SetTargets exchange:
 *
 *   library 0.12.0:  56 to 156 bytes free. Nothing overflowed, but that
 *                    is an order of magnitude under the library's own
 *                    512-byte "act" floor.
 *   library 0.12.1:  616 to 804 bytes free (the merge now runs in place
 *                    instead of building a second schedule).
 *   plus this file's own fix, below: about 1200 more, because loop() no
 *                    longer holds a HearthChargingSchedule across
 *                    Hearth.poll(). See replaceMondayTargets().
 *
 * Jitter is 50 to 100 bytes run to run (interrupt frames land on this
 * stack too), so judge a band by the worst of several runs, never by one.
 *
 * THE PRINT BELOW IS NOT THAT MEASUREMENT, and it reads HIGH. It is a
 * point probe inside onSetTargets(), and the deepest point of the chain is
 * not here: up to 0.12.0 it came later (the merge, 1356 to 1456 bytes
 * below this line), and from 0.12.1 it comes earlier (the AT+MTROWGET
 * proposal fetch, 716 bytes deep, already unwound by the time this runs,
 * 708 to 896 below this line). Either way the number printed here
 * OVERSTATES the margin: 1512 in every measured run, against 56-156 and
 * 616-804 actually free. Painting the stack is what avoids the whole
 * question. The original bench procedure
 * (.superpowers/sdd/2026-08-14-energy-round-c2/task-13-report.md section
 * 7.17) applied a correction term to this print instead; the correction it
 * used (1192) was itself short, because a frame is its locals plus its
 * saved registers.
 *
 *   onDisableCharging(cb)    setup(); the verdict answers the controller,
 *                            then setSupplyState() reports the outcome
 *   onEnableCharging(cb)     setup(); same shape, three fields
 *   attributeChangeCB(...)   not called directly by a sketch: wired by the
 *                            library, a documented no-op for this class
 *                            (cluster 0x0099 is Instance-served)
 *   end()                    not called: tearing the endpoint down
 *                            mid-demo is not a usable demo
 *
 * THE ADJUDICATED SetTargets PATH IS THE POINT OF THIS ROUND. A
 * controller's chip-tool `energyevse set-targets` invoke arrives as a
 * "+MTCMD:..." line, is pulled by the library (AT+MTROWGET, seq-qualified),
 * and reaches onSetTargets() with the PROPOSED HearthChargingSchedule,
 * never applied yet. Returning true merges it into the device's stored
 * schedule (see the class header for the exact merge-by-day rule); this
 * sketch logs every proposed day/target and accepts unconditionally, which
 * is a reasonable default for a demo but not for a product: a real sketch
 * would check the proposal against a tariff, a site limit, or a user
 * preference before deciding.
 *
 * Configuration goes AFTER Matter.begin(): the endpoint id is adopted
 * there (the seven-type batch lesson, repeated in every FullAPI example
 * since).
 *
 * Observe controller-side (device type 0x050C, energy_evse):
 *   chip-tool energyevse read supply-state <node> <ep>
 *   chip-tool energyevse read fault-state <node> <ep>
 *   chip-tool energyevse read circuit-capacity <node> <ep>
 *   chip-tool energyevse get-targets <node> <ep> --timedInteractionTimeoutMs 5000
 *   chip-tool energyevse set-targets '[{"dayOfWeekForSequence":2,"chargingTargets":[{"targetTimeMinutesPastMidnight":480,"addedEnergy":25000000}]}]' <node> <ep> --timedInteractionTimeoutMs 5000
 *   chip-tool energyevse disable <node> <ep> --timedInteractionTimeoutMs 5000
 *   chip-tool energyevse enable-charging null 6000 32000 <node> <ep> --timedInteractionTimeoutMs 5000
 * Every EnergyEvse command is mustUseTimedInvoke, so
 * --timedInteractionTimeoutMs is mandatory on all of them.
 */
#include <Matter.h>

MatterEvse Evse;

// Local tracking only: this class exposes no getters for the three scalar
// setters (the wire is Instance-served and never echoes a value back), so
// the sketch remembers what it last pushed, purely for the status menu.
uint8_t lastSupplyState = 0;    // 0 = Disabled, the pre-push default
uint8_t lastFaultState = 0;     // 0 = NoError
int64_t lastCircuitCapacityMa = 32000;
bool chargingEnabled = false;

void printSchedule() {
  const HearthChargingSchedule &s = Evse.chargingSchedule();
  Serial.print("charging schedule: ");
  Serial.print(s.count());
  Serial.println(" target(s)");
  for (uint8_t i = 0; i < s.count(); i++) {
    const HearthChargingTarget &t = s.targetAt(i);
    Serial.print("  day 0x");
    Serial.print(s.dayBitmapAt(i), HEX);
    Serial.print(" @ ");
    Serial.print(t.minutesPastMidnight / 60);
    Serial.print(":");
    if (t.minutesPastMidnight % 60 < 10) {
      Serial.print("0");
    }
    Serial.print(t.minutesPastMidnight % 60);
    if (t.hasTargetSoC) {
      Serial.print(" soc=");
      Serial.print(t.targetSoC);
    }
    if (t.hasAddedEnergy) {
      Serial.print(" energy(mWh)=");
      Serial.print((long)t.addedEnergy);
    }
    Serial.println();
  }
}

/*
 * Out of line for the same reason replaceMondayTargets() is (see its
 * comment): written inline in setup(), this schedule is hoisted into
 * setup()'s prologue and is therefore live across Matter.begin() and the
 * setters that follow it, all of which dispatch URCs. Less dangerous than
 * the loop() case because setup() runs once, but it is the same 1216 bytes
 * under the same call chain, and this file is a teaching example.
 */
static void __attribute__((noinline)) pushInitialSchedule() {
  // An initial schedule: two weekday mornings, energy-only targets (legal
  // on NO_SOC, the class header's variant rule).
  HearthChargingSchedule initial;
  HearthChargingTarget morday;
  morday.minutesPastMidnight = 360;  // 06:00
  morday.hasAddedEnergy = true;
  morday.addedEnergy = 20000000;  // 20 kWh
  initial.addTarget(0x02 /* Monday */, morday);
  HearthChargingTarget wednesday;
  wednesday.minutesPastMidnight = 360;
  wednesday.hasAddedEnergy = true;
  wednesday.addedEnergy = 20000000;
  initial.addTarget(0x08 /* Wednesday */, wednesday);
  Serial.println(Evse.setChargingSchedule(initial) ? "initial schedule: OK" : "initial schedule: failed");
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 8000) {}

  Evse.begin(MatterEvse::NO_SOC);  // declares only; no wire traffic

  // SetTargets: the deferred adjudication path (see the class header and
  // this file's own top comment). Runs from hearthOnDeferredWork(), well
  // after the +MTCMD line that triggered it; the proposal has NOT been
  // applied yet when this fires.
  Evse.onSetTargets([](const HearthChargingSchedule &proposed,
                       uint8_t affectedDayMask) -> bool {
    // Stack headroom probe (see this file's top comment): this callback
    // sits near the deepest point of the library's deepest call chain,
    // and core 0's stack on an RP2350 is 4 KB with core 1's immediately
    // below it and stack protection off. Printed, not asserted: the
    // bench records the figure, and a number in the low hundreds is the
    // signal to act on.
    //
    // READ IT WITH TWO CORRECTIONS (see this file's top comment for the
    // measured figures this print is NOT).
    //
    // 1. rp2040.getFreeStack() measures against __scratch_x_start__,
    //    which is CORE 1's stack base, unless the sketch defines setup1
    //    or loop1 (RP2040Support.h). This sketch defines neither, so the
    //    figure printed here includes core 1's whole idle 4 KB region on
    //    top of core 0's own margin. Adding a core-1 body to this sketch
    //    made the same run print exactly 4096 less, confirming it: 5608
    //    single-core, 1512 with a core-1 body, in every run and for both
    //    a 1-target and a 70-target proposal.
    // 2. It is a point probe, and the peak is never here, so it OVERSTATES
    //    the margin. On the bench, 1512 printed corresponded to 56-156
    //    bytes actually free under library 0.12.0 (the merge ran deeper,
    //    afterwards) and 616-804 under 0.12.1 (the proposal fetch ran
    //    deeper, before). Both directions are the same direction: the
    //    print is optimistic.
    //
    // A number in the low hundreds here is not "some margin left", it is
    // an overflow that has not happened yet.

    Serial.print("SetTargets proposal: ");
    Serial.print(proposed.count());
    Serial.print(" target(s), affected days 0x");
    Serial.print(affectedDayMask, HEX);
    Serial.print(", free stack ");
    Serial.println(rp2040.getFreeStack());
    for (uint8_t i = 0; i < proposed.count(); i++) {
      const HearthChargingTarget &t = proposed.targetAt(i);
      Serial.print("  day 0x");
      Serial.print(proposed.dayBitmapAt(i), HEX);
      Serial.print(" @ minute ");
      Serial.print(t.minutesPastMidnight);
      if (t.hasAddedEnergy) {
        Serial.print(" energy(mWh)=");
        Serial.print((long)t.addedEnergy);
      }
      Serial.println();
    }
    // The mask is NOT derivable from `proposed`, which is the whole
    // reason it is an argument. A day named by the mask but carrying no
    // row in `proposed` is being EMPTIED: the controller sent it with an
    // empty chargingTargets list. Accepting this proposal DELETES that
    // day's stored targets.
    uint8_t clearedDays = 0;
    for (uint8_t bit = 0; bit < 7; bit++) {
      uint8_t dayBit = (uint8_t)(1u << bit);
      if (!(affectedDayMask & dayBit)) {
        continue;
      }
      bool hasRow = false;
      for (uint8_t i = 0; i < proposed.count(); i++) {
        if (proposed.dayBitmapAt(i) & dayBit) {
          hasRow = true;
          break;
        }
      }
      if (!hasRow) {
        clearedDays = (uint8_t)(clearedDays | dayBit);
      }
    }
    if (clearedDays != 0) {
      Serial.print("  NOTE: accepting this CLEARS day bits 0x");
      Serial.println(clearedDays, HEX);
    }
    Serial.println("-> accepted (a real sketch would check tariff/site-limit/user preference first)");
    return true;
  });

  Evse.onDisableCharging([]() -> bool {
    Serial.println("Disable -> accepted");
    chargingEnabled = false;
    return true;  // the verdict answers the controller; this sketch reports
                   // the outcome with its own setSupplyState() call below,
                   // never from inside this callback (reentrant refusal)
  });
  Evse.onEnableCharging([](const MatterEvse::EnableChargingInfo &info) -> bool {
    Serial.print("EnableCharging -> accepted, min=");
    Serial.print((long)info.minimumChargeCurrent);
    Serial.print("mA max=");
    Serial.print((long)info.maximumChargeCurrent);
    Serial.print("mA until=");
    if (info.hasChargingEnabledUntil) {
      Serial.println(info.chargingEnabledUntil);
    } else {
      Serial.println("indefinite");
    }
    chargingEnabled = true;
    return true;
  });

  Matter.begin();

  // Endpoint-addressed configuration goes AFTER Matter.begin() (the
  // endpoint id is adopted there).
  Evse.setCircuitCapacity(lastCircuitCapacityMa);
  Evse.setSupplyState(lastSupplyState);
  Evse.setFaultState(lastFaultState);

  pushInitialSchedule();

  Serial.println("FullAPI MatterEvse ready; '?' for menu");
}

void printHelp() {
  Serial.println("1=SupplyState ChargingEnabled  0=SupplyState Disabled  f=FaultState toggle");
  Serial.println("c=setCircuitCapacity(40000mA)  n=replace Monday's targets  g=print schedule  s=status  ?=help");
}

void statusDump() {
  Serial.print("charging ");
  Serial.print(chargingEnabled ? "ENABLED" : "disabled");
  Serial.print(", last SupplyState ");
  Serial.print(lastSupplyState);
  Serial.print(", last FaultState ");
  Serial.print(lastFaultState);
  Serial.print(", circuit capacity(mA) ");
  Serial.println((long)lastCircuitCapacityMa);
  printSchedule();
}

/*
 * KEEP THIS IN ITS OWN NON-INLINED FUNCTION. It is not style: it is 1216
 * bytes of core 0's 4096-byte stack, and this sketch is what people copy.
 *
 * A HearthChargingSchedule is 1192 bytes. Declared inside the `case 'n':`
 * block of loop() below, which is where it used to live, GCC hoists it into
 * loop()'s PROLOGUE -- `subw sp, sp, #1212` before the first instruction --
 * so it is live across Hearth.poll() on every single iteration, whether or
 * not the key is ever pressed. Everything poll() dispatches then runs 1216
 * bytes deeper: the SetTargets adjudication, the proposal fetch, your own
 * onSetTargets() callback. Measured on hardware 2026-08-19 by painting the
 * stack and scanning for the deepest word touched, that one local was worth
 * more than the whole in-place merge rewrite it was measured alongside
 * (about 1200 bytes against about 600).
 *
 * In here, the schedule exists only while the key is being serviced, and
 * poll() never sees it. `noinline` is load-bearing: at -Os GCC would
 * otherwise inline a single-call-site helper straight back into loop() and
 * hoist the object again, which is exactly the shape being fixed. Verify
 * with `arm-none-eabi-objdump -d` and look at loop()'s first instructions;
 * they should be a `push` and no `sub sp` of any size.
 *
 * The general rule this is an instance of: a large value type declared
 * anywhere inside a loop() that also calls into this library costs its full
 * size on every iteration, at the deepest point of the library's own call
 * chain, no matter which branch declared it.
 */
static void __attribute__((noinline)) replaceMondayTargets() {
  // Replace ONLY Monday's targets; Wednesday's (set in setup()) is
  // untouched by this call, the merge-by-day rule demonstrated live.
  HearthChargingSchedule mondayOnly;
  HearthChargingTarget t;
  t.minutesPastMidnight = 420;  // 07:00, moved later
  t.hasAddedEnergy = true;
  t.addedEnergy = 15000000;
  mondayOnly.addTarget(0x02, t);
  Serial.println(Evse.setChargingSchedule(mondayOnly) ? "replace Monday: OK" : "replace Monday: failed");
}

void loop() {
  /* URC dispatch (SetTargets adjudication, Disable/EnableCharging
   * verdicts, and the deferred pull-then-answer sequence they arm) all
   * live here: poll unconditionally, exactly as every other FullAPI
   * example does. A sketch whose loop() is slow enough to miss the
   * firmware's own 3000 ms row-bearing verdict window will simply never
   * see onSetTargets() fire for that particular proposal (the class
   * header's own "slow-loop case"). */
  Hearth.poll();

  if (Serial.available()) {
    switch (Serial.read()) {
      case '1':
        lastSupplyState = 1;  // ChargingEnabled
        Serial.println(Evse.setSupplyState(1) ? "setSupplyState(1): OK" : "setSupplyState: failed");
        break;
      case '0':
        lastSupplyState = 0;  // Disabled
        Serial.println(Evse.setSupplyState(0) ? "setSupplyState(0): OK" : "setSupplyState: failed");
        break;
      case 'f':
        lastFaultState = (lastFaultState == 0) ? 3 : 0;  // 3 = GroundFault, arbitrary demo fault
        Serial.println(Evse.setFaultState(lastFaultState) ? "setFaultState: OK" : "setFaultState: failed");
        break;
      case 'c':
        lastCircuitCapacityMa = 40000;
        Serial.println(Evse.setCircuitCapacity(lastCircuitCapacityMa) ? "setCircuitCapacity(40000): OK" : "setCircuitCapacity: failed");
        break;
      /* Deliberately a call, not an inline block: see
       * replaceMondayTargets()'s own comment. A HearthChargingSchedule
       * declared here would sit in loop()'s prologue and be live across
       * Hearth.poll() forever. */
      case 'n': replaceMondayTargets(); break;
      case 'g': printSchedule(); break;
      case 's': statusDump(); break;
      case '?': printHelp(); break;
    }
  }
  delay(10);
}

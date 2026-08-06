/*
 * test_doorlock.cpp - Task C3: generic +MTCMD command-forwarding dispatch
 * (Hearth core) and the MatterDoorLock class, this library's first endpoint
 * type with no arduino-esp32 counterpart.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"
#include "MatterEndpoints/MatterDoorLock.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/*
 * begin(locked) itself issues no AT traffic (B120 norm): the state reaches
 * the wire from hearthOnReconciled(), which Matter.begin() below actually
 * triggers, as its own AT+MTLOCK exchange. Callers that need to see begin()
 * issue nothing call c.begin() directly instead; this helper is for tests
 * that just need a reconciled lock.
 */
static void bringUp(MockStream &s, MatterDoorLock &lock, bool locked = true) {
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  lock.begin(locked);
  s.expect("AT+MTEP?", "+MTEP:0,1,0x000A\r\nOK\r\n");
  uint8_t state = locked ? MatterDoorLock::kStateLocked : MatterDoorLock::kStateUnlocked;
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+MTLOCK=1,%u,%u", (unsigned)state, (unsigned)MatterDoorLock::kSourceManual);
  s.expect(cmd, "OK\r\n");
  Matter.begin();
}

/* ===== Step 1: +MTCMD / +MTCMDTO dispatch (Hearth core) ===== */

static void test_forwarded_lock_allow_sends_verdict_1(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false);
  lock.onLock([]() { return true; });
  s.expect("AT+MTCMDRESP=7,1", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0"); /* LockDoor on ep 1 */
  Hearth.poll();
  check("an allowing onLock() answers exactly AT+MTCMDRESP=7,1", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_lock_deny_sends_verdict_0(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false);
  lock.onLock([]() { return false; });
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0");
  Hearth.poll();
  check("a denying onLock() answers exactly AT+MTCMDRESP=7,0", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_lock_no_callback_denies(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false);
  /* no onLock() registered at all */
  s.expect("AT+MTCMDRESP=7,0", "OK\r\n");
  s.injectURC("+MTCMD:7,1,257,0");
  Hearth.poll();
  check("no callback registered denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_forwarded_command_1_routes_to_onunlock(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true);
  int lockCalls = 0, unlockCalls = 0;
  lock.onLock([&]() { lockCalls++; return true; });
  lock.onUnlock([&]() { unlockCalls++; return true; });
  s.expect("AT+MTCMDRESP=8,1", "OK\r\n");
  s.injectURC("+MTCMD:8,1,257,1"); /* UnlockDoor */
  Hearth.poll();
  check("command id 1 routes to onUnlock, not onLock", unlockCalls == 1 && lockCalls == 0);
  check("and answers the allow verdict", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* An endpoint id the sketch never declared: Hearth core itself replies deny,
 * before ever reaching an endpoint object -- even one with a callback
 * registered for a DIFFERENT endpoint must not be consulted. */
static void test_forwarded_command_unknown_endpoint_denies(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true); /* lock adopts endpoint 1 */
  lock.onLock([]() { return true; });
  s.expect("AT+MTCMDRESP=9,0", "OK\r\n");
  s.injectURC("+MTCMD:9,5,257,0"); /* ep 5: no object registered there */
  Hearth.poll();
  check("an unknown endpoint denies even though a callback exists elsewhere", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

/* A command id this class does not recognise (neither LockDoor nor
 * UnlockDoor) denies too, even with both callbacks registered. */
static void test_forwarded_command_unrecognised_id_denies(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true);
  lock.onLock([]() { return true; });
  lock.onUnlock([]() { return true; });
  s.expect("AT+MTCMDRESP=10,0", "OK\r\n");
  s.injectURC("+MTCMD:10,1,257,9"); /* command id 9: not LockDoor/UnlockDoor */
  Hearth.poll();
  check("an unrecognised command id denies (fail closed)", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_command_timeout_raises_link_event(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true);
  hearthEvent_t got = HEARTH_LINK_UP;
  int seen = 0;
  Hearth.onLinkEvent([&](hearthEvent_t e) {
    got = e;
    seen++;
  });
  s.injectURC("+MTCMDTO:7");
  Hearth.poll();
  check("+MTCMDTO raises HEARTH_CMD_TIMEOUT", seen == 1 && got == HEARTH_CMD_TIMEOUT);
  check("no AT traffic is issued in response", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
  Hearth.onLinkEvent(nullptr);
}

/* ===== Step 2: class behaviour ===== */

static void test_begin_declares_0x000a_variant0(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true);
  check("declared as the door_lock device type", MatterEndPoint::hearthDeclaredTypeAt(0) == 0x000A);
  check("declared variant 0 (two-arg hearthDeclare)", MatterEndPoint::hearthDeclaredVariantAt(0) == 0);
  check("adopted endpoint 1", lock.getEndPointId() == 1);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_rebegin_after_reconcile_refused(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true);
  check("a second begin() after Matter.begin() is refused", !lock.begin(false));
  check("and reports the composition-rejected code", Hearth.lastError() == 10);
  check("the cached lock state was not overwritten", lock.getLockState() == MatterDoorLock::kStateLocked);
  check("the refused begin() issued no AT traffic", s.scriptDrained());
}

static void test_setlockstate_exact_wire_pin_and_cache_update(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false); /* cache starts Unlocked */
  s.expect("AT+MTLOCK=1,1,1", "OK\r\n"); /* state=Locked(1), source=kSourceManual(1) */
  check("setLockState(kStateLocked, kSourceManual) sends the exact wire pin", lock.setLockState(MatterDoorLock::kStateLocked, MatterDoorLock::kSourceManual));
  check("cache updated to Locked", lock.getLockState() == MatterDoorLock::kStateLocked);
  check("script drained", s.scriptDrained());
  check("no unexpected commands", s.unexpected().empty());
}

static void test_setlockstate_failed_write_leaves_cache(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false);
  s.expect("AT+MTLOCK=1,1,1", "+MTERR:2\r\nERROR\r\n");
  check("a rejected write returns false", !lock.setLockState(MatterDoorLock::kStateLocked, MatterDoorLock::kSourceManual));
  check("cache untouched (still Unlocked)", lock.getLockState() == MatterDoorLock::kStateUnlocked);
  check("no unexpected commands", s.unexpected().empty());
}

static void test_lock_and_unlock_pins(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, false); /* start Unlocked */
  s.expect("AT+MTLOCK=1,1,1", "OK\r\n");
  check("lock() pins state=Locked, source=kSourceManual", lock.lock());
  s.expect("AT+MTLOCK=1,2,1", "OK\r\n");
  check("unlock() pins state=Unlocked, source=kSourceManual", lock.unlock());
  check("no unexpected commands", s.unexpected().empty());
}

/* Endpoint 0 (not yet reconciled): setLockState() must fail without ever
 * reaching the wire, the same "endpoint 0 is unaddressable" reasoning every
 * sibling class's write path uses, including the error code. */
static void test_setlockstate_before_reconcile_fails_without_traffic(void) {
  MockStream s; MatterDoorLock lock;
  MatterEndPoint::hearthClearDeclarations();
  Hearth.begin(s);
  check("begin() declares", lock.begin(true));
  check("setLockState() before reconcile fails", !lock.setLockState(MatterDoorLock::kStateUnlocked, MatterDoorLock::kSourceManual));
  check("and reports the unaddressable-endpoint code", Hearth.lastError() == 2);
  check("no traffic issued", s.scriptDrained());
}

/* Controller/firmware-driven change: LockState (cluster 257, attr 0) over
 * +MTATTR updates the cache the same way every other class's cache-fed
 * attribute does, via the generic attributeChangeCB dispatch. */
static void test_controller_attr_urc_updates_cache(void) {
  MockStream s; MatterDoorLock lock;
  bringUp(s, lock, true); /* cache starts Locked */
  s.injectURC("+MTATTR:1,257,0,2"); /* LockState -> Unlocked */
  Hearth.poll();
  check("a controller-driven LockState change updates the cache", lock.getLockState() == MatterDoorLock::kStateUnlocked);
  check("no unexpected commands", s.unexpected().empty());
}

int main(void) {
  printf("\n===== MatterDoorLock / +MTCMD dispatch tests =====\n");
  test_forwarded_lock_allow_sends_verdict_1();
  test_forwarded_lock_deny_sends_verdict_0();
  test_forwarded_lock_no_callback_denies();
  test_forwarded_command_1_routes_to_onunlock();
  test_forwarded_command_unknown_endpoint_denies();
  test_forwarded_command_unrecognised_id_denies();
  test_command_timeout_raises_link_event();
  test_begin_declares_0x000a_variant0();
  test_rebegin_after_reconcile_refused();
  test_setlockstate_exact_wire_pin_and_cache_update();
  test_setlockstate_failed_write_leaves_cache();
  test_lock_and_unlock_pins();
  test_setlockstate_before_reconcile_fails_without_traffic();
  test_controller_attr_urc_updates_cache();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

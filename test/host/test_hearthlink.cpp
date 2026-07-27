/*
 * Host unit tests for the AT line protocol client. No framework: the library
 * has no test dependency and needs none. Build with `make -C test/host run`.
 */
#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "HearthLink.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void collect(const char *line, void *arg) {
  ((std::string *)arg)->append(line).append(";");
}

static void test_ok(void) {
  MockStream s;
  s.expect("AT", "OK\r\n");
  HearthLink link;
  link.begin(s);
  check("plain OK returns 0", link.command("AT") == 0);
  check("script drained", s.scriptDrained());
}

static void test_coded_error(void) {
  MockStream s;
  s.expect("AT+MTATTR=9,6,0", "+MTERR:2\r\nERROR\r\n");
  HearthLink link;
  link.begin(s);
  check("+MTERR:2 is returned as 2", link.command("AT+MTATTR=9,6,0") == 2);
}

static void test_plain_error(void) {
  MockStream s;
  s.expect("AT+MTBOGUS", "ERROR\r\n");
  HearthLink link;
  link.begin(s);
  check("bare ERROR returns -1", link.command("AT+MTBOGUS") == -1);
}

static void test_intermediate_lines(void) {
  MockStream s;
  s.expect("AT+MTEP?", "+MTEP:0,1,0x0100\r\n+MTEP:1,2,0x0302\r\nOK\r\n");
  HearthLink link;
  link.begin(s);
  std::string got;
  check("query returns 0", link.command("AT+MTEP?", collect, &got) == 0);
  check("both result lines delivered",
        got == "+MTEP:0,1,0x0100;+MTEP:1,2,0x0302;");
}

static void test_urc_during_command(void) {
  MockStream s;
  /* A URC lands between the command and its terminal OK. It must go to the
   * URC handler, not to the command's line callback. */
  s.expect("AT+MTCODES?",
           "+MTEVT:3\r\n+MTCODES:MT:Y.K9042C00KA0648G00,34970112332\r\nOK\r\n");
  HearthLink link;
  link.begin(s);
  std::string urcs, lines;
  link.onURC(collect, &urcs);
  check("command still returns 0", link.command("AT+MTCODES?", collect, &lines) == 0);
  check("URC routed to the URC handler", urcs == "+MTEVT:3;");
  check("only the result line reached the command callback",
        lines == "+MTCODES:MT:Y.K9042C00KA0648G00,34970112332;");
}

static void test_attr_read_result_not_urc(void) {
  MockStream s;
  /* AT+MTATTR=<ep>,<cl>,<attr> is a read; its own answer arrives as a
   * +MTATTR: line, which is also one of isAsyncURC()'s prefixes. While this
   * command is in flight that line must be claimed as the result, not
   * routed to the URC handler: this is the exact case the +MTATTR
   * exclusion in command() exists for, and the one most likely to regress
   * silently if a later refactor drops it. */
  s.expect("AT+MTATTR=1,6,0", "+MTATTR:1,6,0,1\r\nOK\r\n");
  HearthLink link;
  link.begin(s);
  std::string urcs, lines;
  link.onURC(collect, &urcs);
  check("attribute read returns 0",
        link.command("AT+MTATTR=1,6,0", collect, &lines) == 0);
  check("+MTATTR result reached the command callback",
        lines == "+MTATTR:1,6,0,1;");
  check("URC handler received nothing", urcs.empty());
}

static void test_poll_dispatches_urc(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  s.injectURC("+MTATTR:1,6,0,1");
  link.poll();
  check("poll dispatches a pending URC", urcs == "+MTATTR:1,6,0,1;");
}

/*
 * FINAL REVIEW, IMPORTANT 1. A URC dispatched mid-command reaches a sketch
 * callback, and a sketch callback may perfectly reasonably call back into
 * the library (upstream's onChange handlers do exactly this sort of thing).
 * Without a guard the inner command() shares the outer's stream: it writes
 * its own command line, then consumes the outer command's result lines and
 * terminal OK, returning "success" with no result line of its own, while
 * the outer blocks its full timeout and returns -2. Both callers get a
 * wrong answer and neither can tell. Fixing the +MTATTR exclusion (CRITICAL
 * 1) makes URCs dispatch far more often, so this stops being theoretical.
 *
 * The contract: the re-entrant call is refused outright with
 * HEARTH_CMD_REENTRANT and sends nothing, and the outer command completes
 * exactly as if the callback had done nothing.
 */
static HearthLink *g_reentrantLink = nullptr;
static int g_reentrantRc = 0;
static int g_reentrantCalls = 0;

static void reenterFromURC(const char *line, void *arg) {
  ((std::string *)arg)->append(line).append(";");
  g_reentrantCalls++;
  g_reentrantRc = g_reentrantLink->command("AT+MTVER?");
}

static void test_reentrant_command_is_refused(void) {
  MockStream s;
  s.expect("AT+MTCODES?", "+MTEVT:3\r\n+MTCODES:MT:Y.K9042C00KA0648G00,34970112332\r\nOK\r\n");
  HearthLink link;
  link.begin(s);
  g_reentrantLink = &link;
  g_reentrantRc = 0;
  g_reentrantCalls = 0;
  std::string urcs, lines;
  link.onURC(reenterFromURC, &urcs);

  /* If the guard regresses, the outer command blocks its full timeout; keep
   * the simulated clock moving so that is a failure rather than a hang. */
  g_yieldAdvanceMs = 1;
  int rc = link.command("AT+MTCODES?", collect, &lines);
  g_yieldAdvanceMs = 0;

  check("the callback really did re-enter", g_reentrantCalls == 1);
  check("the re-entrant call is refused", g_reentrantRc == HEARTH_CMD_REENTRANT);
  check("the re-entrant call put nothing on the wire", s.unexpected().empty());
  check("the outer command still returns 0", rc == 0);
  check("and still receives its own result line",
        lines == "+MTCODES:MT:Y.K9042C00KA0648G00,34970112332;");
  check("the URC was still dispatched", urcs == "+MTEVT:3;");

  link.onURC(nullptr, nullptr);
  g_reentrantLink = nullptr;
}

/* The same guard, seen from poll(): a callback that calls poll() again must
 * not drain lines the command in flight is waiting for. */
static void test_reentrant_poll_is_a_noop(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  s.injectURC("+MTEVT:3");
  link.poll();
  check("poll still dispatches normally", urcs == "+MTEVT:3;");
  check("poll outside a command is not refused", s.unexpected().empty());
}

static void test_timeout(void) {
  MockStream s;
  s.expect("AT", "");  /* peer says nothing */
  HearthLink link;
  link.begin(s);
  /* readLine()'s wait loop polls millis() and calls yield() between reads;
   * opt this test alone into advancing the simulated clock so the timeout
   * elapses instead of spinning forever, then hand the clock back inert. */
  g_yieldAdvanceMs = 1;
  check("silence returns -2", link.command("AT", nullptr, nullptr, 50) == -2);
  g_yieldAdvanceMs = 0;
}

static void test_not_started(void) {
  HearthLink link;
  check("command before begin returns -2", link.command("AT") == -2);
}

int main(void) {
  printf("\n===== HearthLink line protocol tests =====\n");
  test_ok();
  test_coded_error();
  test_plain_error();
  test_intermediate_lines();
  test_urc_during_command();
  test_attr_read_result_not_urc();
  test_poll_dispatches_urc();
  test_reentrant_command_is_refused();
  test_reentrant_poll_is_a_noop();
  test_timeout();
  test_not_started();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

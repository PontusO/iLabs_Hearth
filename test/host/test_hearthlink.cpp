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

/*
 * RE-REVIEW, MINOR 1. This used to be called
 * test_reentrant_poll_is_a_noop and injected one URC, called poll() once,
 * and nested nothing: it tested that poll() works, which is
 * test_poll_dispatches_urc's job, and named itself after a property it
 * never exercised. Renamed to what it actually checks. The two tests below
 * are the re-entrancy ones.
 */
static void test_poll_outside_a_command_dispatches_normally(void) {
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

/*
 * Shared by the two genuinely re-entrant poll tests below: a URC handler
 * that calls poll() on the same link that is currently dispatching it.
 *
 * It also tracks how deeply dispatches nest, which is the only assertion
 * that distinguishes a working guard from a missing one in the
 * poll-inside-poll case: a nested poll that really drained the queue would
 * still dispatch every URC exactly once and still in arrival order, just
 * from inside the previous URC's handler instead of from the outer loop.
 * Depth is what tells those apart, and depth is what would grow without
 * bound on a busy link.
 */
static HearthLink *g_pollLink = nullptr;
static int g_nestedPolls = 0;
static int g_dispatchDepth = 0;
static int g_maxDispatchDepth = 0;

static void pollFromURC(const char *line, void *arg) {
  g_dispatchDepth++;
  if (g_dispatchDepth > g_maxDispatchDepth) {
    g_maxDispatchDepth = g_dispatchDepth;
  }
  ((std::string *)arg)->append(line).append(";");
  g_nestedPolls++;
  g_pollLink->poll();
  g_dispatchDepth--;
}

/*
 * RE-REVIEW, MINOR 1, the real thing: a URC dispatched *from inside a
 * command* reaches a handler that calls poll(). This is the hazard the
 * guard exists for. Without it the nested poll() drains the rest of the
 * stream looking for URCs, swallowing the outer command's own result line
 * and its terminal OK (neither is a URC, so both are simply discarded), and
 * the outer command then blocks its full timeout and returns -2 having seen
 * nothing.
 *
 * With the mode-1 write echo now modelled (see the endpoint suites), this
 * is not a contrived arrangement: every setter dispatches a URC mid-command,
 * so any sketch handler that calls Hearth.poll() lands here.
 */
static void test_reentrant_poll_during_a_command_is_a_noop(void) {
  MockStream s;
  s.expect("AT+MTCODES?", "+MTEVT:3\r\n+MTCODES:MT:Y.K9042C00KA0648G00,34970112332\r\nOK\r\n");
  HearthLink link;
  link.begin(s);
  g_pollLink = &link;
  g_nestedPolls = 0;
  g_dispatchDepth = 0;
  g_maxDispatchDepth = 0;
  std::string urcs, lines;
  link.onURC(pollFromURC, &urcs);

  /* If the guard regresses, the outer command blocks its full timeout; keep
   * the simulated clock moving so that is a failure rather than a hang. */
  g_yieldAdvanceMs = 1;
  int rc = link.command("AT+MTCODES?", collect, &lines);
  g_yieldAdvanceMs = 0;

  check("the handler really did call poll() re-entrantly", g_nestedPolls == 1);
  check("the outer command still returns 0", rc == 0);
  check("and still receives its own result line",
        lines == "+MTCODES:MT:Y.K9042C00KA0648G00,34970112332;");
  check("the URC was dispatched exactly once", urcs == "+MTEVT:3;");

  link.onURC(nullptr, nullptr);
  g_pollLink = nullptr;
}

/*
 * RE-REVIEW, MINOR 1, second half: poll() re-entered from inside poll().
 * The nested call must consume nothing, so the outer loop still sees the
 * second URC and each is dispatched exactly once, in arrival order. A
 * missing guard shows up here as the second URC arriving before the first
 * one's handler has finished, i.e. out of order and, on a longer queue,
 * with unbounded recursion depth.
 */
static void test_reentrant_poll_during_a_poll_is_a_noop(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  g_pollLink = &link;
  g_nestedPolls = 0;
  g_dispatchDepth = 0;
  g_maxDispatchDepth = 0;
  std::string urcs;
  link.onURC(pollFromURC, &urcs);

  s.injectURC("+MTEVT:3");
  s.injectURC("+MTEVT:4");
  link.poll();

  check("both handlers ran and both re-entered", g_nestedPolls == 2);
  check("each URC was dispatched exactly once, in order", urcs == "+MTEVT:3;+MTEVT:4;");
  check("the nested poll consumed nothing, so dispatches never nested", g_maxDispatchDepth == 1);
  check("nothing reached the wire", s.unexpected().empty());

  link.onURC(nullptr, nullptr);
  g_pollLink = nullptr;
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
  check("waitReady before begin returns false", !link.waitReady(10));
}

/*
 * flushInput() exists for the hardware-reset path: bytes already on the wire
 * when reset is asserted belong to the pre-reset firmware and must not be
 * mistaken for the post-reset boot. A half-assembled line in the accumulator
 * is part of that state, so it has to go too, otherwise the leading fragment
 * of the discarded line would be glued onto the first real post-boot line.
 */
static void test_flush_input(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  s.injectURC("+MTEVT:3");
  s.injectURC("+MTIDENT:1,0");
  link.flushInput();
  link.poll();
  check("flushInput drops queued URCs", urcs.empty());
  check("flushInput drains the stream", s.available() == 0);
}

static void test_flush_input_drops_partial_line(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  /* An unterminated fragment: poll() reads it into the accumulator and
   * returns, leaving it there for the next call to finish. */
  s.injectRaw("+MTEVT:");
  link.poll();
  link.flushInput();
  /* Without the accumulator reset, this would assemble as "+MTEVT:+MTREADY". */
  s.injectURC("+MTREADY");
  link.poll();
  check("a partial line does not survive flushInput", urcs == "+MTREADY;");
}

/*
 * waitReady() is what turns "reset released" into "the firmware is up".
 * The C6's boot ROM prints on the same UART as the AT link (it knows nothing
 * about the custom console pin), so everything ahead of +MTREADY is noise
 * that must be discarded rather than dispatched.
 */
static void test_wait_ready_finds_marker(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  s.injectURC("ESP-ROM:esp32c6-20220919");
  s.injectURC("load:0x4086c410,len:0xb94");
  s.injectURC("+MTREADY");
  check("waitReady returns true on +MTREADY", link.waitReady(1000));
  check("the boot marker is dispatched as a URC", urcs == "+MTREADY;");
  check("ROM chatter is discarded, not dispatched",
        urcs.find("ESP-ROM") == std::string::npos);
}

static void test_wait_ready_times_out(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  std::string urcs;
  link.onURC(collect, &urcs);
  s.injectURC("ESP-ROM:esp32c6-20220919");
  g_yieldAdvanceMs = 10;  // let the wait loop reach its deadline
  check("waitReady returns false when no +MTREADY arrives", !link.waitReady(200));
  g_yieldAdvanceMs = 0;
  check("nothing was dispatched", urcs.empty());
}

/*
 * waitReady() reads and dispatches, so it is a stream reader like command()
 * and poll() and must refuse to run nested inside either for the same
 * reason: one accumulator, one stream, and a nested reader steals the outer
 * reader's lines.
 */
static HearthLink *g_readyLink = nullptr;
static bool g_nestedWaitReady = false;

static void waitReadyFromURC(const char *line, void *arg) {
  ((std::string *)arg)->append(line).append(";");
  g_nestedWaitReady = g_readyLink->waitReady(100);
}

static void test_reentrant_wait_ready_is_refused(void) {
  MockStream s;
  HearthLink link;
  link.begin(s);
  g_readyLink = &link;
  g_nestedWaitReady = true;
  std::string urcs;
  link.onURC(waitReadyFromURC, &urcs);
  s.injectURC("+MTEVT:3");
  s.injectURC("+MTREADY");
  link.poll();
  check("waitReady from inside a dispatch returns false", !g_nestedWaitReady);
  check("the outer poll still saw both URCs", urcs == "+MTEVT:3;+MTREADY;");
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
  test_poll_outside_a_command_dispatches_normally();
  test_reentrant_poll_during_a_command_is_a_noop();
  test_reentrant_poll_during_a_poll_is_a_noop();
  test_timeout();
  test_not_started();
  test_flush_input();
  test_flush_input_drops_partial_line();
  test_wait_ready_finds_marker();
  test_wait_ready_times_out();
  test_reentrant_wait_ready_is_refused();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

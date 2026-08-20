#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Hearth.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

static void test_set_transport_ok() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT=THREAD", "OK\r\n");
  check("setTransport(THREAD) true", Hearth.setTransport(HEARTH_TRANSPORT_THREAD));
  ms.expect("AT+MTTRANSPORT=WIFI", "OK\r\n");
  check("setTransport(WIFI) true", Hearth.setTransport(HEARTH_TRANSPORT_WIFI));
  check("script drained", ms.scriptDrained());
}

static void test_set_transport_not_supported() {
  /* Single-stack firmware: unknown command answers +MTERR:8 then ERROR. */
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT=THREAD", "+MTERR:8\r\nERROR\r\n");
  check("setTransport false on +MTERR:8", !Hearth.setTransport(HEARTH_TRANSPORT_THREAD));
  check("lastError is HEARTH_ERR_NOT_SUPPORTED",
        Hearth.lastError() == HEARTH_ERR_NOT_SUPPORTED);
}

static void test_set_transport_bad_value_err1() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT=THREAD", "+MTERR:1\r\nERROR\r\n");
  check("setTransport false on +MTERR:1", !Hearth.setTransport(HEARTH_TRANSPORT_THREAD));
  check("lastError is 1", Hearth.lastError() == 1);
}

static void test_transport_query_steady() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT?", "+MTTRANSPORT:WIFI,WIFI\r\nOK\r\n");
  HearthTransport a, s;
  check("transport() true", Hearth.transport(&a, &s));
  check("active WIFI", a == HEARTH_TRANSPORT_WIFI);
  check("stored WIFI", s == HEARTH_TRANSPORT_WIFI);
}

static void test_transport_query_pending_switch() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT?", "+MTTRANSPORT:WIFI,THREAD\r\nOK\r\n");
  HearthTransport a, s;
  check("transport() true", Hearth.transport(&a, &s));
  check("active WIFI", a == HEARTH_TRANSPORT_WIFI);
  check("stored THREAD (pending)", s == HEARTH_TRANSPORT_THREAD);
}

static void test_transport_query_not_supported() {
  MockStream ms;
  Hearth.begin(ms);
  ms.expect("AT+MTTRANSPORT?", "+MTERR:8\r\nERROR\r\n");
  HearthTransport a, s;
  check("transport() false on +MTERR:8", !Hearth.transport(&a, &s));
  check("lastError is HEARTH_ERR_NOT_SUPPORTED",
        Hearth.lastError() == HEARTH_ERR_NOT_SUPPORTED);
}

int main() {
  test_set_transport_ok();
  test_set_transport_not_supported();
  test_set_transport_bad_value_err1();
  test_transport_query_steady();
  test_transport_query_pending_switch();
  test_transport_query_not_supported();
  /* The same banner every other binary in this suite prints. It used to be
     "TOTAL: %d pass, %d fail", which is why the 1.0.0 review found the suite
     total reported as 4701 instead of 4716: a sum over the banner format
     could not see this one binary. One format, and `make count` now aborts
     on any binary that does not print it. */
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

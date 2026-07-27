/*
 * FINAL REVIEW, MINOR 1. Regression test for the NO_GLOBAL_INSTANCES /
 * NO_GLOBAL_MATTER / NO_GLOBAL_HEARTH opt-out.
 *
 * Mostly a *compile-time* test, which is the only kind available for a
 * declaration that is supposed to be absent: this translation unit defines
 * NO_GLOBAL_INSTANCES, includes the library exactly as a sketch does, and
 * then declares its own file-scope types named Matter and Hearth. If either
 * `extern` leaked past its guard, those declarations are a redeclaration of
 * an object with a different type and this file does not build. That is the
 * failure a sketch defining the macro (or already owning a symbol by one of
 * those names) used to hit, only in the sketch rather than here.
 *
 * The runtime half confirms the guard suppresses only the convenience
 * object and not the API: every ArduinoMatter member is static upstream, so
 * a sketch that opted out can still call the whole Matter-named surface
 * through the class name.
 */
#define NO_GLOBAL_INSTANCES

#include <stdio.h>
#include "ArduinoShim.h"
#include "MockStream.h"
#include "Matter.h"

static int g_pass = 0, g_fail = 0;
static void check(const char *name, bool cond) {
  printf("  [%s] %s\n", cond ? "PASS" : "FAIL", name);
  cond ? g_pass++ : g_fail++;
}

/* These are the assertion. Both are ill-formed if the corresponding extern
 * declaration survived NO_GLOBAL_INSTANCES. */
struct Matter {
  int suppressed;
};
struct Hearth {
  int suppressed;
};

static void test_globals_are_suppressed(void) {
  Matter m;
  m.suppressed = 1;
  Hearth h;
  h.suppressed = 1;
  check("the sketch's own Matter type compiles", m.suppressed == 1);
  check("the sketch's own Hearth type compiles", h.suppressed == 1);
}

static void test_class_surface_survives(void) {
  /* Static members, reachable with no global object at all. Neither of
   * these touches the link, so no MockStream is needed. */
  check("ArduinoMatter's static API is still callable", ArduinoMatter::isWiFiAccessPointEnabled() == false);
  check("and so is the rest of it", ArduinoMatter::isBLECommissioningEnabled() == true);
  check("endpoint classes are still declarable", MatterEndPoint::hearthDeclaredCount() == 0);
}

int main(void) {
  printf("\n===== NO_GLOBAL_INSTANCES opt-out tests =====\n");
  test_globals_are_suppressed();
  test_class_surface_survives();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

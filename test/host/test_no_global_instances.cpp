/*
 * FINAL REVIEW, MINOR 1, and the re-review's residual IMPORTANT 1.
 * Regression test for the NO_GLOBAL_INSTANCES / NO_GLOBAL_MATTER /
 * NO_GLOBAL_HEARTH opt-out.
 *
 * Mostly a *compile-time* test, which is the only kind available for a
 * declaration that is supposed to be absent: this translation unit is
 * compiled with NO_GLOBAL_INSTANCES set, includes the library exactly as a
 * sketch does, and then declares its own file-scope types named Matter and
 * Hearth. If either `extern` leaked past its guard, those declarations are a
 * redeclaration of an object with a different type and this file does not
 * build. That is the failure a sketch defining the macro (or already owning
 * a symbol by one of those names) used to hit, only in the sketch rather
 * than here.
 *
 * The macro is NOT #define'd here: it comes from -DNO_GLOBAL_INSTANCES on
 * the Makefile recipe, which puts it on every source in this binary,
 * including the library's own. That is deliberate and is the finding this
 * file previously could not see. A sketch does not get to set the macro for
 * its own translation unit alone: whether it comes from the sketch (which
 * the Arduino builder compiles together with nothing else), a board variant,
 * or platform.local.txt, the library's .cpp files see it too, and those are
 * the ones that failed to compile. Suppressing the declaration there broke
 * MatterEndPoint.cpp's nine uses of the Hearth object. Keeping the
 * definition unguarded was never enough; see Hearth.h's comment on the
 * guard, and HearthGlobal.h, which is what the library's own translation
 * units include instead.
 *
 * The runtime half confirms the guard suppresses only the convenience
 * object and not the API: every ArduinoMatter member is static upstream, so
 * a sketch that opted out can still call the whole Matter-named surface
 * through the class name. Every endpoint class is linked in as well, so a
 * regression that reintroduced a guarded use of the global anywhere in the
 * library shows up here as a build error.
 */
#if !defined(NO_GLOBAL_INSTANCES)
#error "this test must be compiled with -DNO_GLOBAL_INSTANCES; see the Makefile recipe"
#endif

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

/* All four device types at file scope, exactly as an opted-out sketch would
 * declare them. Instantiating them is what forces the endpoint translation
 * units (and, through them, MatterEndPoint.cpp) to be compiled and linked
 * under the macro rather than merely listed in the recipe. */
static MatterOnOffLight g_onOff;
static MatterDimmableLight g_dimmable;
static MatterColorTemperatureLight g_colorTemp;
static MatterTemperatureSensor g_sensor;

static void test_endpoint_objects_are_usable(void) {
  /* Nothing here reaches the wire: begin() only registers, and the four
   * objects are declared but never reconciled. */
  check("an on/off light declares", g_onOff.begin(false));
  check("a dimmable light declares", g_dimmable.begin(false, 64));
  check("a colour temperature light declares", g_colorTemp.begin(false, 64, 200));
  check("a temperature sensor declares", g_sensor.begin(0.00));
  check("all four are in the registry", MatterEndPoint::hearthDeclaredCount() == 4);
  MatterEndPoint::hearthClearDeclarations();
}

int main(void) {
  printf("\n===== NO_GLOBAL_INSTANCES opt-out tests =====\n");
  test_globals_are_suppressed();
  test_class_surface_survives();
  test_endpoint_objects_are_usable();
  printf("\n===== RESULT: %d passed, %d failed =====\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}

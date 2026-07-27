/*
 * HearthGlobal.h - the Hearth global as the library's own translation units
 * see it: always declared, never guarded.
 *
 * Hearth.h declares `extern HearthClass Hearth;` behind
 * NO_GLOBAL_INSTANCES / NO_GLOBAL_HEARTH, so a sketch that already owns a
 * symbol by that name, or that simply does not want the library declaring
 * names it did not ask for, can suppress it. That guard is right for a
 * sketch and wrong for this library, because the macro is never scoped to
 * the sketch alone: it arrives as a build-wide -D (from a board variant,
 * from platform.local.txt, from a build-property flag), so the library's own
 * .cpp files are compiled with it too. MatterEndPoint.cpp has nine uses of
 * the Hearth object and Hearth.cpp has more; suppressing the *declaration*
 * there is a compile error, and no amount of leaving the *definition*
 * unguarded helps, because the definition was never what went missing.
 *
 * So: every library .cpp that touches the Hearth object includes this header
 * instead of Hearth.h, and gets the declaration unconditionally. The sketch
 * still includes Matter.h / Hearth.h and still gets the guard. A sketch that
 * opted out and then wrote its own `struct Hearth` sees no conflict: this
 * declaration lives only in the library's translation units, and the
 * definition it names is the one Hearth.cpp has always emitted.
 *
 * Redeclaring the same object after Hearth.h has already declared it (the
 * ordinary, un-opted-out build) is well-formed and means nothing extra.
 */
#pragma once

#include "Hearth.h"

extern HearthClass Hearth;

/*
 * Matter.h - source-compatibility shim.
 *
 * Exists so an unmodified arduino-esp32 sketch's `#include <Matter.h>`
 * resolves. The library's own canonical header is Hearth.h; see
 * iLabs_AT_Hearth/docs/superpowers/specs/2026-07-27-c4-host-library-naming-design.md
 * for why the product is never named Matter.
 */

#pragma once
#include "Hearth.h"

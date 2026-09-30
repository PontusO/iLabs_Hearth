/* The single translation unit that compiles the vendored micro-ecc.
 *
 * Upstream's uECC.c is kept as uECC_impl.inc (byte-identical, see
 * VENDORED.md) because an Arduino build compiles every .c file under
 * src/ recursively: a plain uECC.c would be compiled once with the
 * defaults and again through a wrapper, defining every symbol twice.
 * This file is the wrapper: it sets the Hearth configuration first and
 * then includes the implementation. Host builds compile this file with
 * the C compiler; the host test Makefile never compiles the upstream
 * header uECC.c (it does not exist) directly. */
#include "uECC_hearth_config.h"
#include "uECC_impl.inc"

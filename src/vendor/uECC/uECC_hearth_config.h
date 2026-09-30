/* The one configuration of vendored micro-ecc v1.1 that Hearth builds with,
 * shared by the C translation unit (uECC_build.c) and every C++ translation
 * unit that includes uECC.h, so the two cannot drift apart. The library's
 * own uECC.h guards every macro with #ifndef, so defining them before the
 * include is the supported way to configure it (the tag has no
 * uECC_CONFIG hook of its own).
 *
 * Only the P-256 curve the bundle signature uses, no compressed points,
 * optimization level 2, and uECC_PLATFORM uECC_arch_other: the pure C
 * fallbacks, no inline assembly on either the PC or the M33. uECC_verify()
 * of a bundle signature takes a few hundred ms on the RP2350, once per
 * bundle, which is fine. */
#pragma once

/* uECC_arch_other is declared in uECC.h, which this header does not
 * include (it is included by whoever includes this one). It is value 0 in
 * the tag, but spell the value with a comment rather than the name: the
 * name is only available after uECC.h, and this header must work when it
 * is included first. */
#define uECC_PLATFORM 0 /* uECC_arch_other */
#define uECC_OPTIMIZATION_LEVEL 2
#define uECC_SUPPORTS_secp256r1 1
#define uECC_SUPPORTS_secp160r1 0
#define uECC_SUPPORTS_secp192r1 0
#define uECC_SUPPORTS_secp224r1 0
#define uECC_SUPPORTS_secp256k1 0
#define uECC_SUPPORT_COMPRESSED_POINT 0

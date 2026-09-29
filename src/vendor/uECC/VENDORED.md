# Vendored: micro-ecc

- Upstream: https://github.com/kmackay/micro-ecc
- Version: tag v1.1, commit 24c60e243580c7868f4334a1ba3123481fe1aa48
- Licence: BSD 2-clause, see LICENSE.txt (kept)
- Files copied, byte-identical to upstream:
  uECC.h, uECC_vli.h, types.h, curve-specific.inc, platform-specific.inc,
  asm_arm.inc, asm_arm_mult_square.inc, asm_arm_mult_square_umaal.inc,
  asm_avr.inc, asm_avr_mult_square.inc, LICENSE.txt, and upstream's uECC.c
  saved as uECC_impl.inc.

## The rename, and why

upstream's uECC.c is stored as uECC_impl.inc. An Arduino build compiles
every .c file under src/ recursively with default flags, so a plain uECC.c
would be compiled once directly (default configuration, all five curves)
and again through a configuration wrapper, defining every symbol twice.
Renaming it to an .inc makes it includable but not compilable.

Hearth's own files in this directory (not part of the upstream copy):

- uECC_hearth_config.h: the one configuration Hearth builds with, shared
  by the C translation unit (uECC_build.c) and every C++ translation unit
  that includes uECC.h, so the two cannot drift apart. micro-ecc v1.1 has
  no uECC_CONFIG hook; its own uECC.h guards every macro with #ifndef, so
  defining them before the include is the supported way to configure it.
  The configuration: only secp256r1, no compressed points, optimization
  level 2, and uECC_PLATFORM uECC_arch_other, the pure C fallbacks with
  no inline assembly on either the PC or the M33. uECC_verify() of one
  bundle signature takes a few hundred ms on the RP2350, once per bundle,
  which is fine.
- uECC_build.c: the single translation unit that compiles the library. It
  includes uECC_hearth_config.h and then uECC_impl.inc.

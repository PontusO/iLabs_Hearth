# Vendored: esp-serial-flasher

- Upstream: https://github.com/espressif/esp-serial-flasher
- Version: tag v1.11.0, commit 71bac88f810728e178782c58aa73523356dfb48f
- Licence: Apache 2.0, see LICENSE (kept)
- Files copied, byte-identical to upstream, the UART subset only:
  - from include/: esp_loader.h, esp_loader_io.h, serial_io.h
  - from private_include/: esp_stubs.h, esp_targets.h, md5_hash.h,
    protocol.h, protocol_prv.h, slip.h
  - from src/, each saved as .inc: esp_loader, esp_targets, md5_hash,
    protocol_serial, protocol_uart, slip, esp_stubs
- Not copied: the SPI, SDIO and USB protocol files, the port/ directory
  (Hearth's port layer is HearthFlasherEsp.cpp), the examples, tests and
  the CMake and Kconfig files.

## Layout, and why

The headers from include/ and private_include/ sit flat in this directory
beside the .inc files. An Arduino build adds only src/ to the include
path, and upstream's quoted includes ("esp_loader.h", "protocol.h") then
resolve from the including file's own directory.

The C files are stored as .inc because an Arduino build compiles every .c
file under src/ recursively with default flags and takes no -D: a plain
esp_loader.c would be compiled without SERIAL_FLASHER_INTERFACE_UART.

Hearth's own files in this directory (not part of the upstream copy):

- esp_loader_hearth_config.h: the configuration, shared by the C
  translation unit and every C++ file that includes esp_loader.h:
  SERIAL_FLASHER_INTERFACE_UART, MD5_ENABLED (the flash is verified by
  the ROM's MD5 after writing) and SERIAL_FLASHER_WRITE_BLOCK_RETRIES 3
  (upstream's CMake default).
- esp_loader_build.c: the single translation unit that compiles the
  library. It includes the configuration and then every .inc.

## esp_stubs.inc

Upstream's stub table holds the flasher-stub blobs for every supported
chip (about 534 KB of source). It is compiled because esp_loader.inc
references it, but Hearth never calls esp_loader_connect_with_stub() or
the RAM-load functions, so the linker's section garbage collection drops
the table from the image. The measured cost is recorded with the port
layer's commit.

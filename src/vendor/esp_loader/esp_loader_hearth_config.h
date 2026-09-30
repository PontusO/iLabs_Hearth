/* The one configuration Hearth builds esp-serial-flasher with, shared by
 * the C translation unit (esp_loader_build.c) and every C++ translation
 * unit that includes esp_loader.h, so the two cannot drift apart. Upstream
 * sets these from CMake or Kconfig; an Arduino build takes no -D, so they
 * are defined here before any upstream header. */
#pragma once
#define SERIAL_FLASHER_INTERFACE_UART 1
#define MD5_ENABLED 1
#define SERIAL_FLASHER_WRITE_BLOCK_RETRIES 3

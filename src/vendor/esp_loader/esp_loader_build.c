/* The single translation unit that compiles the vendored esp-serial-flasher.
 *
 * Upstream's src/ C files are kept as .inc files (byte-identical, see
 * VENDORED.md) because an Arduino build compiles every .c file under src/
 * recursively with default flags and no -D: the plain .c files would be
 * compiled without SERIAL_FLASHER_INTERFACE_UART. This file is the
 * wrapper: it sets the Hearth configuration first and then includes the
 * implementation. */
#include "esp_loader_hearth_config.h"
#include "esp_loader.inc"
#include "esp_targets.inc"
#include "md5_hash.inc"
#include "protocol_serial.inc"
#include "protocol_uart.inc"
#include "slip.inc"
#include "esp_stubs.inc"

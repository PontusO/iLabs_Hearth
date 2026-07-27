/*
 * HearthLink.h - AT+MT transport client for the ESP32-C6 Hearth co-processor.
 *
 * Thin line-oriented client for the iLabs AT+MT protocol spoken by an
 * ESP32-C6 running the Hearth firmware over a Serial link. Handles
 * command/response framing (OK / ERROR / +MTERR:<n>) and demultiplexes
 * asynchronous URCs (+MTEVT, +MTATTR, +MTIDENT, +MTREADY, ...) from
 * synchronous query results.
 *
 * Single-threaded and cooperative: command() blocks (with a timeout) until
 * the terminal OK/ERROR, dispatching any URCs that interleave; poll() drains
 * pending URCs without blocking. The Hearth class layer sits on top.
 */

#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

/* Max accepted AT line length (bytes). The longest line in the AT+MT
 * protocol is a +MTCODES: pair of a QR payload and an 11-digit manual
 * code; 256 covers it with headroom. */
#ifndef HEARTH_LINE_MAX
#define HEARTH_LINE_MAX 256
#endif

/* Default per-command wait for the terminal OK/ERROR (ms). */
#ifndef HEARTH_CMD_TIMEOUT_MS
#define HEARTH_CMD_TIMEOUT_MS 1500
#endif

class HearthLink {
public:
  typedef void (*LineCb)(const char *line, void *arg);

  HearthLink();

  void begin(Stream &serial);
  bool started() const {
    return _started;
  }
  Stream *stream() const {
    return _s;
  }

  /*
   * Send one AT command line (CRLF appended) and wait for its terminal
   * response. Intermediate query-result lines (e.g. +MTEP:, +MTATTR:,
   * +MTCODES:) are delivered to onLine; asynchronous URCs that arrive
   * in between are routed to the URC handler instead.
   *
   * Returns  0  on OK,
   *         >0  the +MTERR:<n> code on a coded error,
   *         -1  on plain ERROR,
   *         -2  on timeout / link not started.
   */
  int command(const char *cmd, LineCb onLine = nullptr, void *arg = nullptr, uint32_t timeout_ms = 0);

  /* Non-blocking: read and dispatch any pending asynchronous URC lines. */
  void poll();

  /* Register the asynchronous-URC handler (installed by the Hearth layer). */
  void onURC(LineCb cb, void *arg) {
    _urc_cb = cb;
    _urc_arg = arg;
  }

  static bool isAsyncURC(const char *line);

private:
  const char *readLine(uint32_t timeout_ms);
  void dispatchURC(const char *line) {
    if (_urc_cb) {
      _urc_cb(line, _urc_arg);
    }
  }

  Stream *_s;
  bool _started;

  char _acc[HEARTH_LINE_MAX];
  size_t _acc_len;
  bool _overflow;

  LineCb _urc_cb;
  void *_urc_arg;
};

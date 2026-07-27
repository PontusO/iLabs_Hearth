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

/*
 * command() return value for a call made from inside another command() or
 * from inside poll(), i.e. from a URC callback the link itself dispatched.
 * The link is single-threaded and cooperative and owns exactly one stream
 * and one line accumulator, so a nested call cannot be served: it would
 * consume the outer command's result lines and terminal OK. Refusing it
 * with a code of its own (rather than -2, which a caller may reasonably
 * retry as a transient timeout) makes the situation something a sketch can
 * detect and act on. Not a tunable, so deliberately not #ifndef-guarded.
 */
#define HEARTH_CMD_REENTRANT (-3)

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
   *         -2  on timeout / link not started,
   *         -3  (HEARTH_CMD_REENTRANT) if called from inside another
   *             command() or from inside poll(), e.g. from a URC callback
   *             this link itself dispatched. See that macro's comment.
   */
  int command(const char *cmd, LineCb onLine = nullptr, void *arg = nullptr, uint32_t timeout_ms = 0);

  /* Non-blocking: read and dispatch any pending asynchronous URC lines.
   * A no-op when called re-entrantly (from a URC callback, or from a
   * callback running inside command()): draining there would consume lines
   * the command in flight is waiting for. */
  void poll();

  /* Register the asynchronous-URC handler (installed by the Hearth layer). */
  void onURC(LineCb cb, void *arg) {
    _urc_cb = cb;
    _urc_arg = arg;
  }

  static bool isAsyncURC(const char *line);

  /*
   * True if `cmd` is an attribute *read*, the one command whose own answer
   * shares a prefix with a URC. See command()'s implementation for why the
   * distinction matters; exposed for the host tests, which pin it directly.
   */
  static bool hearthIsAttrRead(const char *cmd);

private:
  const char *readLine(uint32_t timeout_ms);
  void dispatchURC(const char *line) {
    if (_urc_cb) {
      _urc_cb(line, _urc_arg);
    }
  }

  /*
   * RAII latch for _busy: command() has several return paths and poll()
   * dispatches callbacks that can throw the flow anywhere, so setting and
   * clearing the flag by hand invites exactly the leak that would wedge the
   * link permanently.
   */
  class HearthBusyLatch {
  public:
    explicit HearthBusyLatch(bool &flag) : _flag(flag) {
      _flag = true;
    }
    ~HearthBusyLatch() {
      _flag = false;
    }

  private:
    bool &_flag;
    HearthBusyLatch(const HearthBusyLatch &);
    HearthBusyLatch &operator=(const HearthBusyLatch &);
  };

  Stream *_s;
  bool _started;

  /*
   * Set while command() or poll() is reading from the stream and
   * dispatching what it reads. Both refuse to run while it is set: this
   * link is single-threaded and cooperative and owns one stream and one
   * line accumulator, so a nested reader would steal the outer reader's
   * lines. See HEARTH_CMD_REENTRANT.
   */
  bool _busy;

  char _acc[HEARTH_LINE_MAX];
  size_t _acc_len;
  bool _overflow;

  LineCb _urc_cb;
  void *_urc_arg;
};

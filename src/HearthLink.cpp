/*
 * HearthLink.cpp - AT+MT transport client implementation.
 */

#include "HearthLink.h"
#include <string.h>
#include <stdlib.h>

HearthLink::HearthLink()
  : _s(nullptr), _started(false), _busy(false), _acc_len(0), _overflow(false), _urc_cb(nullptr), _urc_arg(nullptr) {}

void HearthLink::begin(Stream &serial) {
  _s = &serial;
  _acc_len = 0;
  _overflow = false;
  _busy = false;
  _started = true;
}

bool HearthLink::isAsyncURC(const char *line) {
  return strncmp(line, "+MTEVT", 6) == 0 || strncmp(line, "+MTATTR", 7) == 0
      || strncmp(line, "+MTIDENT", 8) == 0 || strncmp(line, "+MTREADY", 8) == 0;
}

/*
 * An attribute read is AT+MTATTR=<ep>,<cl>,<attr>: three fields, so two
 * commas after the '='. A write is AT+MTATTR=<ep>,<cl>,<attr>,<val>,<mode>
 * (MatterEndPoint::hearthWriteAttr). Its own answer is a bare OK: both
 * write modes can have a +MTATTR: URC ahead of that OK (the firmware's own
 * echo, raised on POST_UPDATE for mode 1 and mode 0 alike), but that line
 * is never claimed as the write's answer, only dispatched as an ordinary
 * async URC. Deriving read-vs-write from the command text rather than from
 * a flag the caller passes is deliberate: there is exactly one command
 * form that behaves this way, it is not something a caller can usefully
 * choose, and a caller that forgot the flag would silently reintroduce the
 * bug this distinction exists to fix.
 */
bool HearthLink::hearthIsAttrRead(const char *cmd) {
  static const size_t kPrefixLen = 10;  // strlen("AT+MTATTR=")
  if (!cmd || strncmp(cmd, "AT+MTATTR=", kPrefixLen) != 0) {
    return false;
  }
  int commas = 0;
  for (const char *p = cmd + kPrefixLen; *p; p++) {
    if (*p == ',') {
      commas++;
    }
  }
  return commas == 2;
}

/*
 * Assemble one CR/LF-terminated line from the stream. Partial input is kept
 * in _acc across calls, so this is safe to call with a zero timeout from
 * poll(). Over-length lines are discarded up to the next newline.
 */
const char *HearthLink::readLine(uint32_t timeout_ms) {
  if (!_s) {
    return nullptr;
  }
  uint32_t start = millis();
  for (;;) {
    while (_s->available() > 0) {
      char c = (char)_s->read();
      if (c == '\n' || c == '\r') {
        if (_overflow) {
          _overflow = false;
          _acc_len = 0;
          continue;
        }
        if (_acc_len > 0) {
          _acc[_acc_len] = '\0';
          _acc_len = 0;
          return _acc;
        }
        continue;  // ignore empty line (bare CR/LF pair)
      }
      if (_overflow) {
        continue;  // dropping an over-length line until its newline
      }
      if (_acc_len < HEARTH_LINE_MAX - 1) {
        _acc[_acc_len++] = c;
      } else {
        _overflow = true;
        _acc_len = 0;
      }
    }
    if ((millis() - start) >= timeout_ms) {
      return nullptr;
    }
    yield();
  }
}

int HearthLink::command(const char *cmd, LineCb onLine, void *arg, uint32_t timeout_ms) {
  if (!_started || !_s) {
    return -2;
  }
  /*
   * Re-entrancy: this is reachable from a URC callback that command() or
   * poll() dispatched, because a sketch's onChange handler may perfectly
   * reasonably call back into the library. Serving the nested call would
   * mean two readers on one stream and one line accumulator: the inner one
   * consumes the outer's result lines and terminal OK, returns "success"
   * having seen none of its own, and the outer then blocks its full timeout
   * and reports -2. Both callers get a wrong answer and neither can tell.
   * Refuse instead, with a code of its own so a caller can distinguish it
   * from a transient timeout worth retrying.
   */
  if (_busy) {
    return HEARTH_CMD_REENTRANT;
  }
  HearthBusyLatch busy(_busy);

  if (timeout_ms == 0) {
    timeout_ms = HEARTH_CMD_TIMEOUT_MS;
  }

  /*
   * The one command whose own answer shares a prefix with a URC. While an
   * attribute read is in flight, a +MTATTR: line is claimed as its result;
   * for every other command in flight it is a genuine URC and is dispatched
   * as one. This used to be unconditional, which meant a controller-driven
   * change arriving during, say, AT+MTFABRICS? was offered to that
   * command's line handler, dropped on its prefix check, and lost: the
   * change callback the library exists to deliver never fired.
   */
  const bool attrRead = hearthIsAttrRead(cmd);

  _s->print(cmd);
  _s->print("\r\n");

  int pending_mterr = 0;
  uint32_t start = millis();
  for (;;) {
    uint32_t elapsed = millis() - start;
    if (elapsed >= timeout_ms) {
      return -2;
    }
    const char *line = readLine(timeout_ms - elapsed);
    if (!line) {
      return -2;
    }
    if (strcmp(line, "OK") == 0) {
      return 0;
    }
    if (strcmp(line, "ERROR") == 0) {
      return pending_mterr > 0 ? pending_mterr : -1;
    }
    if (strncmp(line, "+MTERR:", 7) == 0) {
      pending_mterr = atoi(line + 7);
      continue;
    }
    /*
     * +MTATTR is the one URC prefix that also names a query result: a read
     * of AT+MTATTR=<ep>,<cl>,<attr> answers with a +MTATTR: line. While
     * *that* command is in flight the line is claimed as the result and
     * must not go through isAsyncURC(), or the read would misroute its own
     * answer to the URC handler. Under every other command in flight it is
     * an ordinary URC, and so is every other URC prefix.
     */
    if (!(attrRead && strncmp(line, "+MTATTR", 7) == 0) && isAsyncURC(line)) {
      dispatchURC(line);
      continue;
    }
    if (onLine) {
      onLine(line, arg);
    }
    // otherwise: unexpected intermediate line, ignore
  }
}

void HearthLink::poll() {
  /* Same re-entrancy reasoning as command(): a URC callback that polls
   * again would drain lines the command in flight is waiting for. Silently
   * a no-op rather than an error, because poll() has no return value and
   * "there was nothing to do here" is the honest description. */
  if (!_started || _busy) {
    return;
  }
  HearthBusyLatch busy(_busy);
  for (;;) {
    const char *line = readLine(0);
    if (!line) {
      break;
    }
    if (isAsyncURC(line)) {
      dispatchURC(line);
    }
    // ignore stray OK/ERROR/result lines outside a command
  }
}

/*
 * HearthLink.cpp - AT+MT transport client implementation.
 */

#include "HearthLink.h"
#include <string.h>
#include <stdlib.h>

HearthLink::HearthLink()
  : _s(nullptr), _started(false), _acc_len(0), _overflow(false), _urc_cb(nullptr), _urc_arg(nullptr) {}

void HearthLink::begin(Stream &serial) {
  _s = &serial;
  _acc_len = 0;
  _overflow = false;
  _started = true;
}

bool HearthLink::isAsyncURC(const char *line) {
  return strncmp(line, "+MTEVT", 6) == 0 || strncmp(line, "+MTATTR", 7) == 0
      || strncmp(line, "+MTIDENT", 8) == 0 || strncmp(line, "+MTREADY", 8) == 0;
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
  if (timeout_ms == 0) {
    timeout_ms = HEARTH_CMD_TIMEOUT_MS;
  }

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
     * of AT+MTATTR=<ep>,<cl>,<attr> answers with a +MTATTR: line. While a
     * command is in flight that line is claimed as the result and must not
     * go through isAsyncURC(), or an attribute read would misroute its own
     * answer to the URC handler. Every other URC prefix is unambiguous and
     * is dispatched as usual even mid-command.
     */
    if (strncmp(line, "+MTATTR", 7) != 0 && isAsyncURC(line)) {
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
  if (!_started) {
    return;
  }
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

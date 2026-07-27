/*
 * Hearth.cpp - the Hearth global, implementation.
 */
#include "Hearth.h"

HearthClass::HearthClass() : _lastError(0) {}

void HearthClass::begin(Stream &serial) {
  link.begin(serial);
}

int HearthClass::hearthCommand(const char *cmd, HearthLink::LineCb onLine, void *arg) {
  int rc = link.command(cmd, onLine, arg);
  _lastError = (rc > 0) ? rc : 0;
  return rc;
}

void HearthClass::hearthSetError(int code) {
  _lastError = code;
}

HearthClass Hearth;

/*
 * Hearth.h - the Hearth global: the AT+MT link to the ESP32-C6 co-processor,
 * plus the last +MTERR code any layer above HearthLink reported.
 *
 * This is the minimal shape Task 3's tests need: begin(), hearthCommand(),
 * hearthSetError() and lastError(). Task 4 grows it (link events, the
 * version query, a warnedAboutRecommission() flag) on top of this; nothing
 * here should need to change shape to accommodate that, only grow.
 */
#pragma once

#include <Arduino.h>
#include "HearthLink.h"

class HearthClass {
public:
  HearthClass();

  /* Start the underlying link. */
  void begin(Stream &serial);

  /*
   * Send one AT+MT command through the link and remember its outcome as the
   * last error: a +MTERR code on failure, 0 otherwise. Returns the
   * HearthLink::command() result unchanged (0 OK, >0 +MTERR code, -1 plain
   * ERROR, -2 timeout / link not started).
   */
  int hearthCommand(const char *cmd, HearthLink::LineCb onLine = nullptr, void *arg = nullptr);

  /* Record an error code for a caller that fails before reaching the wire,
   * e.g. an attribute value type the AT protocol cannot carry. */
  void hearthSetError(int code);

  int lastError() const {
    return _lastError;
  }

  HearthLink link;

private:
  int _lastError;
};

extern HearthClass Hearth;

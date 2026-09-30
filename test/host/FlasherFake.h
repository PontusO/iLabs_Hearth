#pragma once
#include "ArduinoShim.h"
#include "HearthFlasher.h"
#include "HearthBundle.h"
#include <functional>
#include <string>
#include <vector>

/*
 * FlasherFake: a HearthFlasher for the apply tests (plan Task 6), written by
 * the controller. It stands where the real SMP, XMODEM or ESP flasher would,
 * installed with Hearth.update.hearthSetFlasher(&fake).
 *
 * Every flash() call is recorded: the range and digest it was handed, the
 * pins, and the bytes of that range read back from the source (so a test can
 * compare them with the fixture's part, apply_parts.txt, or with a retained
 * image). A source read that fails is recorded as readOk false and the call
 * returns HEARTH_FLASH_ERR_SOURCE, as a real flasher would.
 *
 * results[i] is what call i returns (HEARTH_FLASH_OK when the list is
 * shorter than the calls). onFlash(i), when set, runs inside call i after the
 * recording and before the return: the place a test plays the co-processor's
 * side of a real flash, typically injecting "+MTREADY" into the MockStream
 * (a successful flash ends with the co-processor rebooting into the new
 * image) and checking MockStream::nextExpected() to prove which commands had
 * gone out before the flash began.
 */
class FlasherFake : public HearthFlasher {
public:
  struct Call {
    uint32_t off;
    uint32_t len;
    uint8_t sha256[32];
    HearthCoprocPins pins;
    bool readOk;
    std::vector<uint8_t> image;
  };

  std::vector<int> results;
  std::function<void(int)> onFlash;

  const std::vector<Call> &calls() const { return _calls; }

  int flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
            const uint8_t sha256[32]) override {
    (void)uart;
    Call c;
    c.off = off;
    c.len = len;
    memcpy(c.sha256, sha256, 32);
    c.pins = pins;
    c.image.resize(len);
    c.readOk = len == 0 || src.read(off, c.image.data(), len);
    int idx = (int)_calls.size();
    _calls.push_back(c);
    if (onFlash) {
      onFlash(idx);
    }
    if (!c.readOk) {
      return HEARTH_FLASH_ERR_SOURCE;
    }
    return idx < (int)results.size() ? results[idx] : HEARTH_FLASH_OK;
  }

private:
  std::vector<Call> _calls;
};

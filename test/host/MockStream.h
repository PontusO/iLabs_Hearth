#pragma once
#include "ArduinoShim.h"
#include <deque>
#include <vector>

/*
 * Scripted AT peer. expect() queues one command the library is required to
 * send next, together with the bytes to hand back. injectURC() queues a line
 * the library must be able to receive without any command in flight.
 */
class MockStream : public Stream {
public:
  /*
   * deferred (opt-in, default false): the reply is NOT appended to the
   * simulated incoming stream the instant its command is matched. It
   * becomes visible either (a) the moment any LATER write is delivered --
   * matched or not -- which flushes it ahead of that write's own outcome,
   * modeling an earlier asynchronous reply finally landing on the wire just
   * as something else starts, or (b) after one full read attempt has found
   * the stream genuinely empty, so a caller blocked waiting on this exact
   * reply with nothing else written in between still gets it rather than
   * hanging. See pumpDeferred()'s own comment for why it takes two rounds,
   * not one.
   *
   * Exists because this class's default behaviour -- deliver() appends a
   * reply synchronously at the write's own '\r' -- cannot represent a write
   * whose reply has not arrived yet at the instant the write completes,
   * which is exactly the real-hardware condition (UART transmission and
   * firmware processing both take real time) a fire-and-forget write on the
   * real link races against. A same-call read loop that always finds the
   * reply already sitting there self-heals a desync no real link would.
   *
   * House precedent for this exact kind of mock-fidelity fix: the C3
   * fix-round-2 review (this class), and independently the firmware's own
   * `FakeLink` (test/test_mt_regression.py) needed the reverse correction
   * for the same underlying reason -- a mock that is unconditionally MORE
   * synchronous than the real link it stands in for hides genuine protocol
   * bugs. `FakeLink` grew an opt-in `no_reset` for immediate URC seeding
   * (URCs otherwise arrive only after drain()/AT+MTRESET, matching the real
   * device) and fixed `drain()` to have honest semantics rather than
   * quietly always-available ones. Same lesson here, opposite direction:
   * default stays synchronous (every existing test is unaffected), and a
   * caller opts in only when a test specifically needs the desync modelled.
   */
  void expect(const std::string &cmd, const std::string &response, bool deferred = false) {
    _script.push_back({cmd, response, deferred});
  }
  void injectURC(const std::string &line) { _rx += line + "\r\n"; }
  /* Bytes with no line terminator, so the reader is left holding a partial
   * line. What a co-processor reset mid-transmission actually leaves behind. */
  void injectRaw(const std::string &bytes) { _rx += bytes; }

  bool scriptDrained() const { return _script.empty(); }
  const std::vector<std::string> &unexpected() const { return _unexpected; }

  size_t write(uint8_t c) override {
    if (c == '\n') { return 1; }
    if (c == '\r') { deliver(_txline); _txline.clear(); return 1; }
    _txline.push_back((char)c);
    return 1;
  }
  int available() override { pumpDeferred(); return (int)_rx.size(); }
  int read() override {
    pumpDeferred();
    if (_rx.empty()) { return -1; }
    int c = (unsigned char)_rx.front();
    _rx.erase(0, 1);
    return c;
  }
  int peek() override { pumpDeferred(); return _rx.empty() ? -1 : (unsigned char)_rx.front(); }

private:
  struct Exchange { std::string cmd, response; bool deferred; };
  struct DeferredReply { std::string response; bool primed; };

  void deliver(const std::string &sent) {
    /*
     * Flush every still-pending deferred reply first, ahead of whatever
     * this write's own outcome is about to be -- matched or not: once
     * something else is happening on the wire, an earlier asynchronous
     * reply is no longer "still in flight", regardless of how many reads
     * have or have not been attempted since it was queued. This is the
     * only place a deferred reply is ever reordered ahead of anything
     * else; pumpDeferred() below never does, so a deferred reply can never
     * jump ahead of real bytes a caller is still waiting to read.
     */
    while (!_deferred.empty()) {
      _rx += _deferred.front().response;
      _deferred.pop_front();
    }
    if (_script.empty() || _script.front().cmd != sent) {
      _unexpected.push_back(sent);
      return;
    }
    if (_script.front().deferred) {
      _deferred.push_back({_script.front().response, false});
    } else {
      _rx += _script.front().response;
    }
    _script.pop_front();
  }

  /*
   * Self-resolution for a deferred reply with nothing else written in the
   * meantime (case (b) of expect()'s own comment). Gated on _rx being
   * empty, not on a call count alone, so this never reorders a deferred
   * reply ahead of real bytes still waiting -- only deliver()'s flush,
   * above, does that. Two rounds, not one: a single zero-timeout poll()
   * cycle (HearthLink::readLine(0) -- gives up the instant it sees nothing,
   * it never retries) always sees "nothing available" exactly once before
   * giving up, so releasing on the FIRST such round would make a deferred
   * reply visible within the very call that queued it, defeating the whole
   * point. Requiring a second round is what a genuinely different, later
   * caller (one that actually keeps looking, e.g. a real HearthLink::
   * command() with its own retry loop) provides and a single poll() cycle
   * structurally cannot.
   */
  void pumpDeferred() {
    if (_deferred.empty() || !_rx.empty()) {
      return;
    }
    if (!_deferred.front().primed) {
      _deferred.front().primed = true;
      return;
    }
    _rx += _deferred.front().response;
    _deferred.pop_front();
  }

  std::deque<Exchange> _script;
  std::deque<DeferredReply> _deferred;
  std::vector<std::string> _unexpected;
  std::string _txline, _rx;
};

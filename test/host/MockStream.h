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
  void expect(const std::string &cmd, const std::string &response) {
    _script.push_back({cmd, response});
  }
  void injectURC(const std::string &line) { _rx += line + "\r\n"; }

  bool scriptDrained() const { return _script.empty(); }
  const std::vector<std::string> &unexpected() const { return _unexpected; }

  size_t write(uint8_t c) override {
    if (c == '\n') { return 1; }
    if (c == '\r') { deliver(_txline); _txline.clear(); return 1; }
    _txline.push_back((char)c);
    return 1;
  }
  int available() override { return (int)_rx.size(); }
  int read() override {
    if (_rx.empty()) { return -1; }
    int c = (unsigned char)_rx.front();
    _rx.erase(0, 1);
    return c;
  }
  int peek() override { return _rx.empty() ? -1 : (unsigned char)_rx.front(); }

private:
  struct Exchange { std::string cmd, response; };
  void deliver(const std::string &sent) {
    if (_script.empty() || _script.front().cmd != sent) {
      _unexpected.push_back(sent);
      return;
    }
    _rx += _script.front().response;
    _script.pop_front();
  }
  std::deque<Exchange> _script;
  std::vector<std::string> _unexpected;
  std::string _txline, _rx;
};

#pragma once
#include "ArduinoShim.h"
#include <string>
#include <vector>

/*
 * ByteStream: a binary host Stream for the ESP ROM loader's tests (Task 5c),
 * written by the controller. MockStream is line-oriented (it swallows '\n'
 * and splits on '\r'), which a SLIP-framed binary protocol cannot use.
 *
 * Every byte written is recorded, in order, in tx(). Nothing is ever
 * answered on its own: with no feed() the stream is a target that never
 * replies (an ESP32-C6 not in download mode, or a wrong UART), which is
 * the realistic failure a host test can reach without a ROM model.
 * feed(bytes) queues bytes for read(), for the port layer's read checks.
 */
class ByteStream : public Stream {
public:
  void feed(const std::string &bytes) { _rx += bytes; }
  const std::vector<uint8_t> &tx() const { return _tx; }

  using Stream::write;   /* keep the base overloads visible beside the override */
  size_t write(uint8_t c) override { _tx.push_back(c); return 1; }
  int available() override { return (int)_rx.size(); }
  int read() override {
    if (_rx.empty()) { return -1; }
    int c = (unsigned char)_rx[0];
    _rx.erase(0, 1);
    return c;
  }
  int peek() override { return _rx.empty() ? -1 : (unsigned char)_rx[0]; }

private:
  std::string _rx;
  std::vector<uint8_t> _tx;
};

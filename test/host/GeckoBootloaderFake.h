#pragma once
#include "ArduinoShim.h"
#include <string>
#include <vector>

/*
 * GeckoBootloaderFake: the MG24's Gecko Bootloader v3.02.01 uart-xmodem build
 * as a host-side Stream, written by the controller as ground truth for the
 * XMODEM flasher's tests (Task 5b). It answers exactly as the firmware repo's
 * platform/silabs/fw/flash.py and xmodem.py found the real bootloader answering
 * (the MG24 README, "Uploading an application: fw/flash.py"):
 *
 *   - after the strap-and-reset the menu is already waiting:
 *       "\r\nGecko Bootloader v3.02.01\r\n1. upload gbl\r\n2. run\r\n3. ebl info\r\nBL > \0"
 *   - "1" is answered by the 17-byte echo "\r\nbegin upload\r\n\0", THEN one 'C'
 *     (it is released only after the echo, never before);
 *   - each 133-byte SOH block (SOH, seq, 255-seq, 128 bytes, CRC16-XMODEM big
 *     endian) is ACKed when its sequence number is the expected one and its
 *     CRC is right, NAKed otherwise; an STX (1K) block is always NAKed and
 *     counted (the bootloader takes 128-byte blocks only);
 *   - EOT is ACKed, then "\r\nSerial upload complete\r\n\0" and the menu again;
 *   - "2" from the menu starts the application (ran() becomes true).
 *
 * Replies are queued only when the byte that triggers them has been written,
 * so the flasher sees a request/response exchange, never pre-loaded bytes.
 *
 * Fault injection for the negative tests:
 *   nakOnce(n)      NAK block n the first time it arrives (ACK the resend);
 *   silentAfter(n)  answer nothing at all once block n has arrived (block n
 *                   itself gets no reply either): a missing reply must be fatal;
 *   cancelAt(n)     answer block n with CAN (0x18);
 *   noMenu()        the menu never appears (a module that did not enter the
 *                   bootloader).
 */
class GeckoBootloaderFake : public Stream {
public:
  GeckoBootloaderFake() { _rx = menuText(); }

  void nakOnce(int block) { _nakOnce = block; }
  void silentAfter(int block) { _silentAfter = block; }
  void cancelAt(int block) { _cancelAt = block; }
  void noMenu() { _rx.clear(); _dead = true; }

  /* What the bootloader received and stored: the payload of every accepted
   * block, in order, including the 0x1A padding of the last one. */
  const std::vector<uint8_t> &image() const { return _image; }
  bool ran() const { return _ran; }
  /* B674: true when flush() was called after the "2" was written. On the
   * device the caller re-clocks the port the moment flash() returns, and
   * SerialUART::begin() ends the running UART with the "2" still in the
   * TX FIFO; the bootloader then reprints its menu instead of running. */
  bool flushedAfterRun() const { return _flushedAfterRun; }
  void flush() override { if (_ran) _flushedAfterRun = true; }
  bool uploadComplete() const { return _complete; }
  int blocksAccepted() const { return _accepted; }
  int naksSent() const { return _naks; }
  int stxSeen() const { return _stx; }
  /* Every byte the flasher wrote, in order (compare with the transcript oracle). */
  const std::vector<uint8_t> &tx() const { return _tx; }

  size_t write(uint8_t c) override {
    _tx.push_back(c);
    if (_dead || _silent) { return 1; }
    switch (_state) {
    case MENU:
      if (c == '1') {
        _rx += std::string("\r\nbegin upload\r\n", 16) + std::string(1, '\0');
        _rx += "C";
        _state = XFER;
        _frame.clear();
      } else if (c == '2' && _complete) {
        _ran = true;
        _state = RUNNING;
      }
      break;
    case XFER:
      if (_frame.empty()) {
        if (c == 0x04) {                       /* EOT */
          _rx += "\x06";
          _rx += std::string("\r\nSerial upload complete\r\n", 26) + std::string(1, '\0');
          _rx += menuText();
          _complete = true;
          _state = MENU;
          break;
        }
        if (c == 0x02) { _stx++; }             /* 1K block: always refused */
        if (c != 0x01 && c != 0x02) { break; } /* stray 'C' echoes etc. are ignored */
      }
      _frame.push_back(c);
      if (_frame.size() == (size_t)(_frame[0] == 0x02 ? 1029 : 133)) {
        onFrame();
        _frame.clear();
      }
      break;
    case RUNNING:
      break;
    }
    return 1;
  }
  int available() override { return (int)_rx.size(); }
  int read() override {
    if (_rx.empty()) { return -1; }
    int c = (unsigned char)_rx[0];
    _rx.erase(0, 1);
    return c;
  }
  int peek() override { return _rx.empty() ? -1 : (unsigned char)_rx[0]; }

  static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; i++) {
      crc ^= (uint16_t)p[i] << 8;
      for (int b = 0; b < 8; b++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
      }
    }
    return crc;
  }

private:
  enum State { MENU, XFER, RUNNING };

  static std::string menuText() {
    return std::string("\r\nGecko Bootloader v3.02.01\r\n1. upload gbl\r\n2. run\r\n3. ebl info\r\nBL > ")
           + std::string(1, '\0');
  }

  void onFrame() {
    int n = _frame[1];
    _blockCount++;
    if (_silentAfter > 0 && _blockCount >= _silentAfter) { _silent = true; return; }
    if (_cancelAt > 0 && _blockCount == _cancelAt) { _rx += "\x18"; return; }
    if (_frame[0] == 0x02) { _rx += "\x15"; _naks++; return; }
    bool seqOk = (n == (_expectedSeq & 0xFF)) && ((_frame[2] ^ 0xFF) == n);
    uint16_t got = ((uint16_t)_frame[131] << 8) | _frame[132];
    bool crcOk = crc16(&_frame[3], 128) == got;
    if (!seqOk || !crcOk || (_nakOnce > 0 && _blockCount == _nakOnce && !_nakDone)) {
      if (_nakOnce > 0 && _blockCount == _nakOnce) { _nakDone = true; }
      _rx += "\x15";
      _naks++;
      return;
    }
    _image.insert(_image.end(), _frame.begin() + 3, _frame.begin() + 131);
    _accepted++;
    _expectedSeq++;
    _rx += "\x06";
  }

  std::string _rx;
  std::vector<uint8_t> _tx, _frame, _image;
  State _state = MENU;
  int _expectedSeq = 1, _blockCount = 0, _accepted = 0, _naks = 0, _stx = 0;
  int _nakOnce = 0, _silentAfter = 0, _cancelAt = 0;
  bool _nakDone = false, _silent = false, _dead = false, _complete = false, _ran = false, _flushedAfterRun = false;
};

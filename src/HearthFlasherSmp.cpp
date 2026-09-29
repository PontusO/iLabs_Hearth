/*
 * HearthFlasherSmp: the nRF co-processor's MCUboot serial-recovery
 * client, ported from the firmware repo's platform/nrf54l15/fw/smp.py
 * (the authoritative Python client: every constant is cited there to
 * boot_serial.c, and the serial/base64/CRC framing mirrors
 * boot_serial_output()/boot_serial_in_dec() byte for byte) and from
 * flash.py's entry, handshake, upload and exit sequence.
 *
 * The wire, in the order this file meets it:
 *   1. a frame: 8-byte SMP header op(1) flags(1) len(2,BE) group(2,BE)
 *      seq(1) id(1), then a CBOR payload (definite-length maps; the
 *      standard zcbor decoders accept definite input as well as
 *      indefinite, so nothing on the wire requires one form or the
 *      other).
 *   2. a packet: u16 BE totlen (= frame length + 2) || frame ||
 *      crc16-xmodem(frame) as u16 BE, base64-encoded as a whole.
 *   3. lines: the base64 split into runs of at most 124 characters,
 *      the first prefixed 0x06 0x09, continuations 0x04 0x14, each
 *      ending with a newline.
 *   4. replies come back in the same packet/line framing, and their
 *      payload is an INDEFINITE-length CBOR map (0xBF ... 0xFF): this
 *      build leaves CONFIG_ZCBOR_CANONICAL unset, so the decoder
 *      accepts both the definite and the indefinite forms.
 *
 * Recovery entry is the strap sequence on the same UART: strap on,
 * settle, reset pulse, hold the strap, then an SMP echo as the
 * handshake (this build answers ENOTSUP to echo, so any well-framed
 * reply proves the link). The exit is NOT an SMP reset command: the
 * strap released and a short reset pulse; the caller (the update's
 * apply path) waits for +MTREADY after this returns.
 *
 * The image is the MCUboot-signed zephyr.signed.bin: the first 32 bytes
 * carry the image_header, whose ih_magic (0x96F3B83D, little-endian)
 * is checked before the first chunk goes out, and the digest the
 * bundle gives us is the "sha" key of that first chunk (this build
 * ignores it for resume; it is sent because the reference client
 * sends it, byte for byte).
 */
#include "HearthFlasherSmp.h"
#include "HearthBundle.h"   /* HearthByteSource */

#include <string.h>

#ifdef ARDUINO
#include <Arduino.h>
#else
#include "ArduinoShim.h"    /* millis()/yield()/Stream for the host suite */
#endif

/* ---- op / group / id / rc constants (boot_serial_priv.h, smp.py) ---- */
#define HEARTH_SMP_OP_WRITE            2   /* NMGR_OP_WRITE; the reply is op+1 */
#define HEARTH_SMP_GROUP_DEFAULT       0   /* MGMT_GROUP_ID_DEFAULT */
#define HEARTH_SMP_GROUP_IMAGE         1   /* MGMT_GROUP_ID_IMAGE */
#define HEARTH_SMP_ID_ECHO             0   /* NMGR_ID_ECHO (group 0) */
#define HEARTH_SMP_ID_UPLOAD           1   /* IMGMGR_NMGR_ID_UPLOAD (group 1) */
#define HEARTH_SMP_RC_OK               0   /* MGMT_ERR_OK */

/* ---- framing constants (boot_serial.c:125, smp.py) ---- */
#define HEARTH_SMP_FRAME_MTU           124 /* BOOT_SERIAL_FRAME_MTU base64 chars per line */
#define HEARTH_SMP_HDR_LEN             8   /* sizeof(struct nmgr_hdr) */
#define HEARTH_SMP_PKT_START_1         0x06
#define HEARTH_SMP_PKT_START_2         0x09
#define HEARTH_SMP_PKT_CONT_1          0x04
#define HEARTH_SMP_PKT_CONT_2          0x14
#define HEARTH_SMP_CHUNK               512 /* flash.py CHUNK, under CONFIG_BOOT_SERIAL_MAX_RECEIVE_SIZE 1024 */
#define HEARTH_SMP_PACKET_MAX          (1024 + 8 + 2) /* totlen + header + payload + crc: CONFIG_BOOT_SERIAL_MAX_RECEIVE_SIZE 1024 + nmgr_hdr 8 + crc 2 */
#define HEARTH_SMP_IMAGE_MAGIC         0x96F3B83DU /* bootutil/image.h:51, ih_magic little-endian on disk */
#define HEARTH_SMP_IMAGE_HDR           32    /* image_header is 32 bytes (bootutil/image.h:57) */

/* Entry and upload timings, flash.py: strap settle 100 ms, reset pulse
 * 100 ms, hold 400 ms; up to three entry attempts, three echo attempts
 * per entry, a 2 s echo deadline and a 5 s per-chunk deadline. Five
 * consecutive chunk timeouts or five non-advancing replies end the
 * transfer, the same bounds as flash.py. */
#define HEARTH_SMP_ENTER_SETTLE_MS     100UL
#define HEARTH_SMP_ENTER_PULSE_MS      100UL
#define HEARTH_SMP_ENTER_HOLD_MS       400UL
#define HEARTH_SMP_ENTRY_ATTEMPTS      3
#define HEARTH_SMP_ECHO_ATTEMPTS       3
#define HEARTH_SMP_ECHO_TIMEOUT_MS     2000UL
#define HEARTH_SMP_CHUNK_TIMEOUT_MS    5000UL
#define HEARTH_SMP_MAX_CONSEC_TIMEOUTS 5
#define HEARTH_SMP_MAX_NO_PROGRESS     5
#define HEARTH_SMP_EXIT_PULSE_MS       50UL

/* ---- crc16-xmodem (poly 0x1021, init 0, no reflection, no xor-out) ---- */

static uint16_t smpCrc16(const uint8_t *data, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (int b = 0; b < 8; b++) {
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;
}

/* ---- minimal CBOR encoder: unsigned ints, text keys, byte strings,
 * definite maps. Nothing else is needed for the SMP requests. ---- */

static void cborHead(uint8_t *out, size_t &pos, int major, uint32_t value) {
  uint8_t majorBits = (uint8_t)(major << 5);
  if (value < 24) {
    out[pos++] = (uint8_t)(majorBits | value);
  } else if (value < 256) {
    out[pos++] = (uint8_t)(majorBits | 24);
    out[pos++] = (uint8_t)value;
  } else if (value < 0x10000) {
    out[pos++] = (uint8_t)(majorBits | 25);
    out[pos++] = (uint8_t)(value >> 8);
    out[pos++] = (uint8_t)value;
  } else {
    out[pos++] = (uint8_t)(majorBits | 26);
    out[pos++] = (uint8_t)(value >> 24);
    out[pos++] = (uint8_t)(value >> 16);
    out[pos++] = (uint8_t)(value >> 8);
    out[pos++] = (uint8_t)value;
  }
}

static void cborUint(uint8_t *out, size_t &pos, uint32_t v) {
  cborHead(out, pos, 0, v);
}

static void cborText(uint8_t *out, size_t &pos, const char *s) {
  size_t n = strlen(s);
  cborHead(out, pos, 3, (uint32_t)n);
  memcpy(out + pos, s, n);
  pos += n;
}

static void cborBytes(uint8_t *out, size_t &pos, const uint8_t *d, uint32_t n) {
  cborHead(out, pos, 2, n);
  memcpy(out + pos, d, n);
  pos += n;
}

/* Encode the upload payload map. The key order matches the reference
 * client byte for byte (smp.py's dict order, flash.py's upload()):
 *   first chunk:  "image", "data", "off", "len", "sha"
 *   later chunk:  "image", "data", "off"
 * "sha" is sent on the first chunk only, byte for byte with flash.py,
 * although this MCUboot build ignores it (its image_upload_decode
 * table takes "image"/"data"/"len"/"off" and skips the rest). */
static size_t smpEncodeUploadMap(uint8_t *out, uint32_t off, uint32_t len, const uint8_t *data, uint32_t dataLen,
                                 bool first, const uint8_t sha[32]) {
  size_t pos = 0;
  cborHead(out, pos, 5, first ? 5 : 3);
  cborText(out, pos, "image");
  cborUint(out, pos, 0);
  cborText(out, pos, "data");
  cborBytes(out, pos, data, dataLen);
  cborText(out, pos, "off");
  cborUint(out, pos, off);
  if (first) {
    cborText(out, pos, "len");
    cborUint(out, pos, len);
    cborText(out, pos, "sha");
    cborBytes(out, pos, sha, 32);
  }
  return pos;
}

static size_t smpEncodeEchoMap(uint8_t *out) {
  size_t pos = 0;
  cborHead(out, pos, 5, 1);
  cborText(out, pos, "d");
  cborText(out, pos, "hearth");
  return pos;
}

/* ---- tolerant CBOR decoder: uint, negative int, bstr, tstr, bool,
 * null, list, map; definite AND indefinite length, mirroring
 * smp.py's _decode_item. The 0xFF break byte ends an indefinite
 * item; like the Python sentinel, it is reported through the out
 * parameter (type SMP_CBOR_BREAK). ---- */

#define SMP_CBOR_BREAK 7

struct SmpCborVal {
  uint8_t type;  /* 0 int, 1 bytes, 2 text, 3 bool, 4 null, 5 list, 6 map, SMP_CBOR_BREAK */
  int32_t i;     /* type 0/3: the value */
  const uint8_t *bytes; size_t blen;   /* type 1 */
  const char *text; size_t tlen;       /* type 2 */
};

static bool smpCborHead(const uint8_t *buf, size_t n, size_t &pos, int &major, uint32_t &value, bool &indef) {
  if (pos >= n) return false;
  uint8_t head = buf[pos++];
  major = head >> 5;
  uint8_t info = head & 0x1F;
  indef = false;
  if (info < 24) {
    value = info;
  } else if (info == 24) {
    if (pos + 1 > n) return false;
    value = buf[pos++];
  } else if (info == 25) {
    if (pos + 2 > n) return false;
    value = ((uint32_t)buf[pos] << 8) | buf[pos + 1];
    pos += 2;
  } else if (info == 26) {
    if (pos + 4 > n) return false;
    value = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos + 1] << 16) | ((uint32_t)buf[pos + 2] << 8) | buf[pos + 3];
    pos += 4;
  } else if (info == 27) {
    return false;   /* 64-bit lengths: the replies never carry one */
  } else if (info == 31) {
    /* info 31 is the indefinite-length marker for the container majors
     * (bytes, text, array, map). For the simple-value major (7) the same
     * encoding is the Break byte (0xFF) that closes an indefinite item,
     * not a length: report it as value 31 so smpCborItem maps it to
     * SMP_CBOR_BREAK. */
    if (major == 7) {
      value = 31;
    } else {
      value = 0;
      indef = true;
    }
  }
  return true;
}

static bool smpCborItem(const uint8_t *buf, size_t n, size_t &pos, SmpCborVal &v, int depth) {
  if (depth > 8) return false;
  int major; uint32_t value; bool indef;
  if (!smpCborHead(buf, n, pos, major, value, indef)) return false;

  if (major == 7) {
    if (value == 31) {
      v.type = SMP_CBOR_BREAK;   /* the break that closes an indefinite item */
      return true;
    }
    if (value == 20) { v.type = 3; v.i = 0; return true; }
    if (value == 21) { v.type = 3; v.i = 1; return true; }
    if (value == 22 || value == 23) { v.type = 4; return true; }
    return false;                /* any other simple value: unsupported */
  }

  if (indef) {
    if (major == 2) {            /* indefinite bstr: chunks to the break */
      v.type = 1;
      v.bytes = buf + pos;
      v.blen = 0;
      for (;;) {
        SmpCborVal item;
        if (!smpCborItem(buf, n, pos, item, depth + 1)) return false;
        if (item.type == SMP_CBOR_BREAK) break;
        if (item.type != 1) return false;
        /* the chunks are contiguous in the buffer, so the last one's end
         * is the whole value's end */
        v.blen = (size_t)(item.bytes + item.blen - v.bytes);
      }
      return true;
    }
    if (major == 3) {            /* indefinite tstr: same shape */
      v.type = 2;
      v.text = (const char *)(buf + pos);
      for (;;) {
        SmpCborVal item;
        if (!smpCborItem(buf, n, pos, item, depth + 1)) return false;
        if (item.type == SMP_CBOR_BREAK) break;
        if (item.type != 2) return false;
        v.tlen = (size_t)(item.text + item.tlen - v.text);
      }
      return true;
    }
    if (major == 4) {            /* indefinite list: walk to the break */
      v.type = 5;
      for (;;) {
        SmpCborVal item;
        if (!smpCborItem(buf, n, pos, item, depth + 1)) return false;
        if (item.type == SMP_CBOR_BREAK) break;
      }
      return true;
    }
    if (major == 5) {            /* indefinite map: key/value pairs to the break */
      v.type = 6;
      for (;;) {
        SmpCborVal key;
        if (!smpCborItem(buf, n, pos, key, depth + 1)) return false;
        if (key.type == SMP_CBOR_BREAK) break;
        SmpCborVal val;
        if (!smpCborItem(buf, n, pos, val, depth + 1)) return false;
      }
      return true;
    }
    return false;
  }

  switch (major) {
    case 0: v.type = 0; v.i = (int32_t)value; return true;
    case 1: v.type = 0; v.i = -1 - (int32_t)value; return true;
    case 2:
      if (pos + value > n) return false;
      v.type = 1; v.bytes = buf + pos; v.blen = value; pos += value;
      return true;
    case 3:
      if (pos + value > n) return false;
      v.type = 2; v.text = (const char *)(buf + pos); v.tlen = value; pos += value;
      return true;
    case 4:
      v.type = 5;
      for (uint32_t k = 0; k < value; k++) {
        SmpCborVal item;
        if (!smpCborItem(buf, n, pos, item, depth + 1)) return false;
      }
      return true;
    case 5:
      v.type = 6;
      for (uint32_t k = 0; k < value; k++) {
        SmpCborVal key, val;
        if (!smpCborItem(buf, n, pos, key, depth + 1)) return false;
        if (!smpCborItem(buf, n, pos, val, depth + 1)) return false;
      }
      return true;
    case 6:
      return smpCborItem(buf, n, pos, v, depth + 1);   /* tag: unwrap */
    default:
      return false;
  }
}

/*
 * Decode one SMP response payload into the two fields this protocol
 * carries: "rc" (always) and "off" (only when rc is 0). The payload is
 * a map, definite or indefinite; anything that is not a map, or a map
 * whose entries are not the shapes above, is a protocol error.
 */
static bool smpDecodeResponse(const uint8_t *payload, size_t n, int32_t &rc, bool &hasOff, uint32_t &off) {
  size_t pos = 0;
  SmpCborVal map;
  if (!smpCborItem(payload, n, pos, map, 0) || map.type != 6) return false;
  rc = -1;
  hasOff = false;
  size_t p = 0;
  int major; uint32_t count; bool indef;
  if (!smpCborHead(payload, n, p, major, count, indef)) return false;
  if (major != 5) return false;
  for (;;) {
    SmpCborVal key, val;
    if (!smpCborItem(payload, n, p, key, 1)) return false;
    if (key.type == SMP_CBOR_BREAK) break;
    if (key.type != 2) return false;   /* keys are text */
    if (!smpCborItem(payload, n, p, val, 1)) return false;
    if (val.type != 0) return false;   /* values are ints here */
    if (key.tlen == 2 && memcmp(key.text, "rc", 2) == 0) {
      rc = val.i;
    } else if (key.tlen == 3 && memcmp(key.text, "off", 3) == 0) {
      hasOff = val.i >= 0;
      off = (uint32_t)val.i;
    }
    if (indef) continue;
    count--;
    if (count == 0) break;
  }
  return rc >= 0;
}

/* ---- base64 (the standard A-Za-z0-9+/ alphabet with = padding) ---- */

static const char SMP_B64_ALPHABET[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t smpBase64Encode(const uint8_t *in, size_t n, char *out) {
  size_t o = 0;
  size_t i = 0;
  while (i + 3 <= n) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
    out[o++] = SMP_B64_ALPHABET[(v >> 18) & 63];
    out[o++] = SMP_B64_ALPHABET[(v >> 12) & 63];
    out[o++] = SMP_B64_ALPHABET[(v >> 6) & 63];
    out[o++] = SMP_B64_ALPHABET[v & 63];
    i += 3;
  }
  if (i + 1 == n) {
    uint32_t v = (uint32_t)in[i] << 16;
    out[o++] = SMP_B64_ALPHABET[(v >> 18) & 63];
    out[o++] = SMP_B64_ALPHABET[(v >> 12) & 63];
    out[o++] = '=';
    out[o++] = '=';
  } else if (i + 2 == n) {
    uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
    out[o++] = SMP_B64_ALPHABET[(v >> 18) & 63];
    out[o++] = SMP_B64_ALPHABET[(v >> 12) & 63];
    out[o++] = SMP_B64_ALPHABET[(v >> 6) & 63];
    out[o++] = '=';
  }
  return o;
}

static int smpBase64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

/*
 * Decode one line's base64 run (1 to 124 chars, the '=' padding only
 * ever sits at the end of the last run). Returns the number of decoded
 * bytes, or -1 when a character is not in the alphabet or the run has a
 * fractional quantum (1 or 3 characters with no padding).
 */
static int smpBase64Decode(const char *s, size_t n, uint8_t *out) {
  /* Strip the trailing '=' padding: it carries no data. */
  while (n > 0 && s[n - 1] == '=') n--;
  size_t o = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; i++) {
    int v = smpBase64Value(s[i]);
    if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (uint8_t)((acc >> bits) & 0xFF);
    }
  }
  if (bits >= 6) return -1;   /* the leftover is a padding fraction, not a byte */
  return (int)o;
}

/* ---- serial framing: frame -> packet -> lines, and back ---- */

/*
 * Encode one frame (8-byte header + CBOR payload) into the base64 line
 * runs, writing them straight to the stream: u16 BE totlen || frame ||
 * crc16(frame) as u16 BE, base64 as a whole, split into runs of at most
 * HEARTH_SMP_FRAME_MTU characters, the first run prefixed 0x06 0x09 and
 * the continuations 0x04 0x14, each ending with a newline. This is
 * smp.py's serial_encode() byte for byte.
 */
static void smpWriteFrameLines(Stream &uart, const uint8_t *frame, size_t frameLen) {
  /* totlen(2) + frame + crc(2): the +2 is the crc itself */
  uint8_t raw[HEARTH_SMP_PACKET_MAX];
  if (frameLen + 4 > sizeof(raw)) return;
  size_t pos = 0;
  raw[pos++] = (uint8_t)((frameLen + 2) >> 8);
  raw[pos++] = (uint8_t)(frameLen + 2);
  memcpy(raw + pos, frame, frameLen);
  pos += frameLen;
  uint16_t crc = smpCrc16(frame, frameLen);
  raw[pos++] = (uint8_t)(crc >> 8);
  raw[pos++] = (uint8_t)crc;

  char b64[(HEARTH_SMP_PACKET_MAX + 2) / 3 * 4];
  size_t b64len = smpBase64Encode(raw, pos, b64);

  size_t off = 0;
  while (off < b64len) {
    if (off == 0) {
      uart.write(HEARTH_SMP_PKT_START_1);
      uart.write(HEARTH_SMP_PKT_START_2);
    } else {
      uart.write(HEARTH_SMP_PKT_CONT_1);
      uart.write(HEARTH_SMP_PKT_CONT_2);
    }
    size_t run = b64len - off;
    if (run > HEARTH_SMP_FRAME_MTU) run = HEARTH_SMP_FRAME_MTU;
    for (size_t i = 0; i < run; i++) {
      uart.write((uint8_t)b64[off + i]);
    }
    uart.write('\n');
    off += run;
  }
}

/*
 * The receive side of the framing, mirroring smp.py's SerialDecoder /
 * boot_serial_in_dec(): line by line, the marker selects start versus
 * continuation, the base64 run accumulates, and once totlen bytes have
 * landed the packet's CRC is checked as a residue (crc over
 * totlen || frame || crc == 0, the self-checking property of a
 * non-reflected CRC with the check bytes appended in wire order).
 */
class SmpSerialDecoder {
public:
  SmpSerialDecoder() { reset(); }

  /*
   * Feed raw bytes; the decoded frame (header + payload) lands in out
   * (at most HEARTH_SMP_PACKET_MAX - 4 bytes) and its length is
   * returned, or 0 for nothing complete yet. *consumed is set to the
   * number of bytes actually consumed from data: when a frame is
   * returned it is the index just past the newline that completed the
   * frame, so the caller feeds the remainder on its next call. This
   * matters because a batch read from the UART can hold several
   * responses at once (the mock delivers them eagerly, and a real port
   * can buffer just as well): returning only the first frame without
   * reporting how much was used would drop the rest.
   */
  size_t feed(const uint8_t *data, size_t n, uint8_t *out) {
    /*
     * Pending bytes from the previous call (the tail of the batch after
     * the frame that call returned) come first, then the new bytes. A
     * batch read from the UART can hold several responses at once (the
     * mock delivers them eagerly, and a real port can buffer just as
     * well), and the caller takes one frame per read cycle, so the tail
     * after a returned frame must survive until it is consumed.
     *
     * Two passes: the first processes the pending bytes (which were
     * already vetted on the call that set them), the second processes
     * the new bytes. A frame completed in either pass stores the
     * remainder of that pass's source in the pending buffer.
     */
    if (pendLen) {
      size_t i = 0;
      for (; i < pendLen; i++) {
        uint8_t c = pend[i];
        if (c == '\n') {
          if (feedLine(out)) {
            lineLen = 0;
            size_t rest = pendLen - (i + 1);
            if (rest) {
              pendLen = rest > sizeof(pend) ? sizeof(pend) : rest;
              memmove(pend, pend + i + 1, pendLen);
            } else {
              pendLen = 0;
            }
            return outLen;
          }
          lineLen = 0;
          continue;
        }
        if (lineLen < sizeof(line)) line[lineLen++] = c;
      }
      pendLen = 0;
    }
    for (size_t i = 0; i < n; i++) {
      uint8_t c = data[i];
      if (c == '\n') {
        if (feedLine(out)) {
          lineLen = 0;
          size_t rest = n - (i + 1);
          if (rest) {
            pendLen = rest > sizeof(pend) ? sizeof(pend) : rest;
            memcpy(pend, data + i + 1, pendLen);
          }
          return outLen;
        }
        lineLen = 0;
        continue;
      }
      if (lineLen < sizeof(line)) line[lineLen++] = c;
      /* an overlong line: drop it (the marker check rejects it anyway) */
    }
    return 0;
  }

  /*
   * Drop everything in flight: the partial line, the partial packet
   * accumulation, and the pending tail. Called between recovery entry
   * attempts, where a stale reply left over from an abandoned attempt
   * must not be mistaken for the next attempt's answer (flash.py
   * clears its input buffer and starts a fresh decoder for exactly this
   * reason).
   */
  void reset() {
    lineLen = 0;
    accLen = 0;
    pendLen = 0;
  }

private:
  bool feedLine(uint8_t *out) {
    if (lineLen < 2) return false;
    uint8_t m1 = line[0], m2 = line[1];
    if (m1 == HEARTH_SMP_PKT_START_1 && m2 == HEARTH_SMP_PKT_START_2) {
      accLen = 0;
    } else if (m1 == HEARTH_SMP_PKT_CONT_1 && m2 == HEARTH_SMP_PKT_CONT_2) {
      if (accLen == 0) return false;   /* continuation with no start seen: drop it */
    } else {
      return false;                     /* not an SMP line: ignore */
    }
    size_t run = lineLen - 2;
    if (run > sizeof(acc) - accLen) return false;   /* over the packet budget: drop it */
    int d = smpBase64Decode((const char *)line + 2, run, acc + accLen);
    if (d < 0) {
      accLen = 0;
      return false;
    }
    accLen += (size_t)d;
    if (accLen <= 2) return false;      /* still waiting for the totlen to fill */
    uint32_t totlen = ((uint32_t)acc[0] << 8) | acc[1];
    if (accLen - 2 != totlen) return false;   /* not complete yet (or a mismatch): keep waiting */
    /* The CRC covers frame + crc (NOT the totlen prefix): the residue
     * over header+payload+the-transmitted-crc-bytes must be 0, which is
     * the self-checking property of a non-reflected CRC with the check
     * bytes appended in wire order (boot_serial.c:1479-1489). */
    if (smpCrc16(acc + 2, (size_t)totlen) != 0) {
      accLen = 0;
      return false;
    }
    if (totlen <= 2) {
      accLen = 0;
      return false;
    }
    size_t frameLen = (size_t)totlen - 2;
    if (frameLen > sizeof(acc) - 4) {
      accLen = 0;
      return false;
    }
    memcpy(out, acc + 2, frameLen);
    outLen = frameLen;
    accLen = 0;
    return true;
  }

  uint8_t line[2 + HEARTH_SMP_FRAME_MTU + 8];
  size_t lineLen;
  uint8_t acc[HEARTH_SMP_PACKET_MAX];
  size_t accLen;
  uint8_t pend[HEARTH_SMP_PACKET_MAX];
  size_t pendLen;
  size_t outLen;
};

/* ---- the SMP request/response exchange over the UART ---- */

/*
 * One session-wide monotonic seq counter, wrapping mod 256 (the seq is
 * a single byte on the wire). The handshake and the upload share it on
 * purpose, as flash.py does: a late reply to an abandoned request must
 * never carry the seq of the one in flight.
 */
class SmpSeq {
public:
  SmpSeq() : n(0) {}
  uint8_t next() {
    n = (uint8_t)((n + 1) & 0xFF);
    return n;
  }
private:
  uint8_t n;
};

struct SmpFrame {
  uint8_t op;
  uint8_t flags;
  uint16_t len;
  uint16_t group;
  uint8_t seq;
  uint8_t id;
  const uint8_t *payload;   /* into the decoder's frame buffer */
};

/*
 * Read from the UART until one decoded SMP frame whose header.seq
 * matches expectedSeq arrives, or the millis() deadline passes (0 then).
 * Frames with a different seq are dropped and reading continues: the
 * stale reply to an already-abandoned request must not be mistaken for
 * this one (flash.py's "sticky desync" finding). A frame whose header
 * claims a longer payload than the packet carries is dropped, not an
 * error.
 */
static bool smpReadFrame(Stream &uart, SmpSerialDecoder &dec, uint32_t timeoutMs, uint8_t expectedSeq, SmpFrame &out) {
  uint8_t frame[HEARTH_SMP_PACKET_MAX];
  uint32_t deadline = millis() + timeoutMs;
  for (;;) {
    /* Read in batches and feed them whole, the way the reference
     * decoder is fed (a chunk of the port's input): feeding one byte at
     * a time would starve the line buffer, whose start-marker check
     * needs two bytes in hand. The decoder keeps the bytes after the
     * frame it returned in its own pending buffer, so a batch holding
     * several responses loses none of them: each feed takes one frame
     * and the next feed picks up the rest. */
    for (;;) {
      int avail = uart.available();
      uint8_t batch[256];
      int n = 0;
      if (avail > 0) {
        n = avail > (int)sizeof(batch) ? (int)sizeof(batch) : avail;
        for (int i = 0; i < n; i++) {
          int c = uart.read();
          if (c < 0) {
            n = i;
            break;
          }
          batch[i] = (uint8_t)c;
        }
      }
      /* Feed the decoder even when the stream is empty: it may hold
       * pending bytes from a previous batch (the tail after the frame
       * that batch returned), and those may complete a frame without
       * any new bytes from the wire. */
      size_t got = dec.feed(n > 0 ? batch : nullptr, (size_t)n, frame);
      if (got) {
        if (got < HEARTH_SMP_HDR_LEN) continue;   /* shorter than a header: drop */
        SmpFrame f;
        f.op = frame[0];
        f.flags = frame[1];
        f.len = ((uint16_t)frame[2] << 8) | frame[3];
        f.group = ((uint16_t)frame[4] << 8) | frame[5];
        f.seq = frame[6];
        f.id = frame[7];
        f.payload = frame + HEARTH_SMP_HDR_LEN;
        if ((uint32_t)HEARTH_SMP_HDR_LEN + f.len > got) continue;   /* truncated payload: drop */
        if (f.seq != expectedSeq) continue;                         /* stale reply: keep waiting */
        out = f;
        return true;
      }
      if (avail <= 0) break;   /* nothing on the wire and no pending frame */
    }
    if (millis() >= deadline) return false;
    yield();
  }
}

/*
 * The upload loop, flash.py's upload() with its two stall bounds. Each
 * chunk is sent with one seq per offset (a resend of the same offset
 * reuses it, since the reply is matched against that same value); the
 * reply's "off" is the bootloader's own idea of the next offset and is
 * the loop variable, which is what makes a resend safe. rc not 0 is a
 * protocol error straight up; rc 0 without an advancing off is bounded
 * the same way as the timeouts.
 */
static int smpUpload(Stream &uart, SmpSerialDecoder &dec, SmpSeq &seq, HearthByteSource &src,
                     uint32_t off0, uint32_t len, const uint8_t sha256[32]) {
  uint32_t total = len;
  uint32_t off = off0;
  int consecutiveTimeouts = 0;
  int consecutiveNoProgress = 0;
  uint8_t frameSeq = seq.next();
  uint8_t chunk[HEARTH_SMP_CHUNK];
  uint8_t frame[8 + 8 + HEARTH_SMP_CHUNK + 80];   /* header + map overhead + chunk + len/sha keys */

  while (off < total) {
    uint32_t n = total - off;
    if (n > HEARTH_SMP_CHUNK) n = HEARTH_SMP_CHUNK;
    if (!src.read(off, chunk, n)) {
      return HEARTH_FLASH_ERR_SOURCE;
    }
    size_t payloadLen = smpEncodeUploadMap(frame + HEARTH_SMP_HDR_LEN, off, total, chunk, n, off == 0, sha256);
    frame[0] = HEARTH_SMP_OP_WRITE;
    frame[1] = 0;
    frame[2] = (uint8_t)(payloadLen >> 8);
    frame[3] = (uint8_t)payloadLen;
    frame[4] = (uint8_t)(HEARTH_SMP_GROUP_IMAGE >> 8);
    frame[5] = (uint8_t)HEARTH_SMP_GROUP_IMAGE;
    frame[6] = frameSeq;
    frame[7] = HEARTH_SMP_ID_UPLOAD;
    smpWriteFrameLines(uart, frame, HEARTH_SMP_HDR_LEN + payloadLen);

    SmpFrame rep;
    if (!smpReadFrame(uart, dec, HEARTH_SMP_CHUNK_TIMEOUT_MS, frameSeq, rep)) {
      consecutiveTimeouts++;
      if (consecutiveTimeouts > HEARTH_SMP_MAX_CONSEC_TIMEOUTS) {
        return HEARTH_FLASH_ERR_PROTOCOL;   /* upload stalled: timeouts at a fixed offset */
      }
      continue;                              /* resend from the last acked offset */
    }
    consecutiveTimeouts = 0;
    int32_t rc; bool hasOff; uint32_t newOff = 0;
    if (!smpDecodeResponse(rep.payload, rep.len, rc, hasOff, newOff) || rc != HEARTH_SMP_RC_OK) {
      return HEARTH_FLASH_ERR_PROTOCOL;
    }
    if (!hasOff) {
      return HEARTH_FLASH_ERR_PROTOCOL;     /* rc 0 must carry the next offset */
    }
    if (newOff <= off) {
      consecutiveNoProgress++;
      if (consecutiveNoProgress > HEARTH_SMP_MAX_NO_PROGRESS) {
        return HEARTH_FLASH_ERR_PROTOCOL;   /* parked at a different offset than this one */
      }
    } else {
      consecutiveNoProgress = 0;
    }
    off = newOff;
    frameSeq = seq.next();
  }
  return HEARTH_FLASH_OK;
}

/*
 * Recovery entry: the strap sequence (strap on, settle, reset pulse,
 * hold the strap) repeated up to HEARTH_SMP_ENTRY_ATTEMPTS times, each
 * followed by the echo handshake (any well-framed reply proves the
 * link, this build answers ENOTSUP). The decoder is reset per attempt
 * so a stale reply from an abandoned attempt cannot be mistaken for
 * this one.
 */
static int smpEnterRecovery(Stream &uart, const HearthCoprocPins &pins, SmpSerialDecoder &dec, SmpSeq &seq) {
  for (int attempt = 0; attempt < HEARTH_SMP_ENTRY_ATTEMPTS; attempt++) {
    hearthCoprocStrap(pins, true);
    delay(HEARTH_SMP_ENTER_SETTLE_MS);
    hearthCoprocReset(pins, HEARTH_SMP_ENTER_PULSE_MS);
    delay(HEARTH_SMP_ENTER_HOLD_MS);

    dec.reset();
    for (int e = 0; e < HEARTH_SMP_ECHO_ATTEMPTS; e++) {
      uint8_t frame[8 + 32];
      size_t payloadLen = smpEncodeEchoMap(frame + HEARTH_SMP_HDR_LEN);
      frame[0] = HEARTH_SMP_OP_WRITE;
      frame[1] = 0;
      frame[2] = (uint8_t)(payloadLen >> 8);
      frame[3] = (uint8_t)payloadLen;
      frame[4] = 0;
      frame[5] = 0;
      frame[6] = seq.next();
      frame[7] = HEARTH_SMP_ID_ECHO;
      smpWriteFrameLines(uart, frame, HEARTH_SMP_HDR_LEN + payloadLen);

      SmpFrame rep;
      if (smpReadFrame(uart, dec, HEARTH_SMP_ECHO_TIMEOUT_MS, frame[6], rep)) {
        return HEARTH_FLASH_OK;   /* any decodable, seq-matched reply: the link is up */
      }
    }
  }
  return HEARTH_FLASH_ERR_ENTER;
}

/*
 * Recovery exit, flash.py's exit_recovery(): NOT an SMP reset command
 * (the reset command would go out before the device is reset into the
 * new application, which is a chicken and egg). The strap is released
 * and a short reset pulse sends the device into the new image; the
 * caller waits for +MTREADY on the AT link.
 */
static void smpExitRecovery(const HearthCoprocPins &pins) {
  hearthCoprocStrap(pins, false);
  hearthCoprocReset(pins, HEARTH_SMP_EXIT_PULSE_MS);
}

HearthFlasherSmp::HearthFlasherSmp() {}

int HearthFlasherSmp::flash(Stream &uart, const HearthCoprocPins &pins, HearthByteSource &src, uint32_t off, uint32_t len,
                            const uint8_t sha256[32]) {
  /* The pins are the whole entry story: no strap and no reset means
   * this board cannot put the co-processor into recovery, so refuse
   * before touching the wire (the log line is the caller's, with the
   * pin numbers). */
  if (!hearthCoprocStrap(pins, false) || !hearthCoprocReset(pins, 1)) {
    return HEARTH_FLASH_ERR_ENTER;
  }
  if (len < HEARTH_SMP_IMAGE_HDR) {
    return HEARTH_FLASH_ERR_SOURCE;
  }
  /* ih_magic, little-endian, at offset 0 of the signed image. */
  uint8_t hdr[HEARTH_SMP_IMAGE_HDR];
  if (!src.read(off, hdr, sizeof(hdr))) {
    return HEARTH_FLASH_ERR_SOURCE;
  }
  uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
  if (magic != HEARTH_SMP_IMAGE_MAGIC) {
    return HEARTH_FLASH_ERR_SOURCE;
  }

  SmpSerialDecoder dec;
  SmpSeq seq;
  if (smpEnterRecovery(uart, pins, dec, seq) != HEARTH_FLASH_OK) {
    hearthCoprocStrap(pins, false);
    return HEARTH_FLASH_ERR_ENTER;
  }

  int err = smpUpload(uart, dec, seq, src, off, len, sha256);

  smpExitRecovery(pins);
  return err;
}

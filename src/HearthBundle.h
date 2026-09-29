/* HearthBundle: parses and verifies a signed Hearth firmware bundle
 * (spec 3.1: 64-byte header, 108-byte parts table rows, a 64-byte raw
 * r||s ECDSA P-256 signature over SHA-256(header || table), parts at
 * 16-byte aligned offsets) inside a standard Matter OTA header.
 *
 * The bytes come from a HearthByteSource, so the same code runs on the
 * host tests (memory) and on the device (flash, file, whatever). The
 * signature is mandatory; there is no unsigned mode. The development
 * key is public (HearthDevKey.h) and gives integrity, not authenticity:
 * a product compiles in its own key. */
#pragma once
#include <stddef.h>
#include <stdint.h>

class HearthByteSource {
public:
  virtual ~HearthByteSource() {}
  virtual bool read(uint32_t offset, uint8_t *buf, size_t n) = 0;
  virtual uint32_t size() const = 0;
};

enum HearthBundleError {
  HEARTH_BUNDLE_OK = 0,
  HEARTH_BUNDLE_ERR_SIGNATURE = 1,
  HEARTH_BUNDLE_ERR_DIGEST = 2,
  HEARTH_BUNDLE_ERR_TARGET = 3,
  HEARTH_BUNDLE_ERR_VERSION = 4,
  HEARTH_BUNDLE_ERR_STORAGE = 5,
  HEARTH_BUNDLE_ERR_FORMAT = 6
};

struct HearthBundlePart {
  uint8_t type;        /* 1 host image, 2 Hearth image */
  uint8_t variant;     /* 0 none, 1 wifi, 2 thread, 3 combined */
  uint16_t flags;      /* bit 0: the host image is gzip */
  uint32_t offset;     /* inside the container, 16-byte aligned */
  uint32_t length;
  char target[33];     /* the model string AT+CGMM answers */
  char version[33];
  uint8_t sha256[32];
};

struct HearthBundleInfo {
  uint16_t vendorId, productId;
  uint32_t productVersion;
  char productVersionString[33];
  uint8_t partCount;
  HearthBundlePart parts[2];
  uint32_t containerOffset;  /* where the 64-byte header starts */
};

class HearthBundle {
public:
  /* Parse the container and verify its signature against pubkey.
   * Returns HEARTH_BUNDLE_OK on success; the part digests are NOT
   * checked here (verifyPart does that, one part at a time). */
  static HearthBundleError open(HearthByteSource &src, const uint8_t pubkey[64], HearthBundleInfo &out);
  static HearthBundleError verifyPart(HearthByteSource &src, const HearthBundleInfo &info, int idx);
  static const uint32_t kOtaMagic = 0x1BEEF11E;
};

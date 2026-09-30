#include "HearthBundle.h"
#include <string.h>
#include "vendor/uECC/uECC_hearth_config.h"
extern "C" {
#include "vendor/uECC/uECC.h"
#include "vendor/sha256/sha256.h"
}

namespace {
const uint8_t kMagic[8] = { 'H', 'R', 'T', 'H', 'B', 'N', 'D', 'L' };
const uint32_t kHeaderLen = 64, kRowLen = 108, kSigLen = 64;

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
void cstr(char *dst, const uint8_t *src, size_t n) { memcpy(dst, src, n); dst[n] = '\0'; }
}

HearthBundleError HearthBundle::open(HearthByteSource &src, const uint8_t pubkey[64], HearthBundleInfo &out) {
  memset(&out, 0, sizeof(out));
  uint8_t fixed[16];
  if (src.size() < 16 || !src.read(0, fixed, 16)) return HEARTH_BUNDLE_ERR_FORMAT;
  if (le32(fixed) != kOtaMagic) return HEARTH_BUNDLE_ERR_FORMAT;
  uint32_t total = le32(fixed + 4);                     // low half of the u64 is enough (< 4 GB)
  uint32_t hlen = le32(fixed + 12);
  if (le32(fixed + 8) != 0 || total != src.size() || hlen > 1024) return HEARTH_BUNDLE_ERR_FORMAT;
  out.containerOffset = 16 + hlen;

  uint8_t hdr[kHeaderLen];
  if (!src.read(out.containerOffset, hdr, kHeaderLen)) return HEARTH_BUNDLE_ERR_FORMAT;
  if (memcmp(hdr, kMagic, 8) != 0 || le16(hdr + 8) != 1 || le16(hdr + 10) != kHeaderLen) return HEARTH_BUNDLE_ERR_FORMAT;
  out.vendorId = le16(hdr + 12); out.productId = le16(hdr + 14); out.productVersion = le32(hdr + 16);
  cstr(out.productVersionString, hdr + 20, 32);
  out.partCount = hdr[52];
  if (out.partCount < 1 || out.partCount > 2) return HEARTH_BUNDLE_ERR_FORMAT;

  SHA256_CTX ctx; sha256_init(&ctx); sha256_update(&ctx, hdr, kHeaderLen);
  uint32_t off = out.containerOffset + kHeaderLen;
  for (int i = 0; i < out.partCount; i++, off += kRowLen) {
    uint8_t row[kRowLen];
    if (!src.read(off, row, kRowLen)) return HEARTH_BUNDLE_ERR_FORMAT;
    sha256_update(&ctx, row, kRowLen);
    HearthBundlePart &p = out.parts[i];
    p.type = row[0]; p.variant = row[1]; p.flags = le16(row + 2); p.offset = le32(row + 4); p.length = le32(row + 8);
    cstr(p.target, row + 12, 32); cstr(p.version, row + 44, 32); memcpy(p.sha256, row + 76, 32);
    if ((uint64_t)out.containerOffset + p.offset + p.length > src.size()) return HEARTH_BUNDLE_ERR_FORMAT;
    if (p.type != 1 && p.type != 2) return HEARTH_BUNDLE_ERR_FORMAT;
  }
  uint8_t hash[32]; sha256_final(&ctx, hash);
  uint8_t sig[kSigLen];
  if (!src.read(off, sig, kSigLen)) return HEARTH_BUNDLE_ERR_FORMAT;
  if (uECC_verify(pubkey, hash, 32, sig, uECC_secp256r1()) != 1) return HEARTH_BUNDLE_ERR_SIGNATURE;
  return HEARTH_BUNDLE_OK;
}

HearthBundleError HearthBundle::verifyPart(HearthByteSource &src, const HearthBundleInfo &info, int idx) {
  if (idx < 0 || idx >= info.partCount) return HEARTH_BUNDLE_ERR_FORMAT;
  const HearthBundlePart &p = info.parts[idx];
  SHA256_CTX ctx; sha256_init(&ctx);
  uint8_t buf[256];
  uint32_t base = info.containerOffset + p.offset;
  for (uint32_t done = 0; done < p.length;) {
    size_t n = p.length - done; if (n > sizeof(buf)) n = sizeof(buf);
    if (!src.read(base + done, buf, n)) return HEARTH_BUNDLE_ERR_STORAGE;
    sha256_update(&ctx, buf, n); done += n;
  }
  uint8_t hash[32]; sha256_final(&ctx, hash);
  return memcmp(hash, p.sha256, 32) == 0 ? HEARTH_BUNDLE_OK : HEARTH_BUNDLE_ERR_DIGEST;
}

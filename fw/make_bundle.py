#!/usr/bin/env python3
"""Build and sign a Hearth firmware bundle, wrapped as a Matter OTA image.

  python3 fw/make_bundle.py --vendor 0xFFF1 --product 0x8000 --version 0x00010400 \
      --version-string 1.4.0 --key fw/keys/hearth_bundle_dev_p256.pem \
      [--host sketch.bin --host-gz] \
      [--fw hearth-wifi-1.3.0.bin --target "ESP32-C6 Hearth" --variant wifi --fw-version 1.3.0] \
      -o product-1.4.0.ota
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hearth_bundle as hb


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--vendor", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--product", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--version", type=lambda x: int(x, 0), required=True)
    ap.add_argument("--version-string", required=True)
    ap.add_argument("--key", required=True, help="ECDSA P-256 private key, PEM")
    ap.add_argument("--host", help="the host sketch image (arduino-cli export)")
    ap.add_argument("--host-gz", action="store_true", help="the host image is gzip (PicoOTA inflates)")
    ap.add_argument("--fw", help="the Hearth image for the co-processor")
    ap.add_argument("--target", help="the model string AT+CGMM answers, e.g. 'ESP32-C6 Hearth'")
    ap.add_argument("--variant", choices=list(hb.VARIANT), default="none")
    ap.add_argument("--fw-version", help="the Hearth version the image reports to AT+MTVER?")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    parts = []
    if a.fw:
        if not (a.target and a.fw_version):
            ap.error("--fw needs --target and --fw-version")
        parts.append({"type": hb.PART_HEARTH, "variant": hb.VARIANT[a.variant], "target": a.target,
                      "version": a.fw_version, "data": open(a.fw, "rb").read()})
    if a.host:
        parts.append({"type": hb.PART_HOST, "variant": 0, "flags": 1 if a.host_gz else 0,
                      "target": "product", "version": a.version_string, "data": open(a.host, "rb").read()})
    if not parts:
        ap.error("nothing to bundle: give --fw and/or --host")
    blob = hb.build_container(a.vendor, a.product, a.version, a.version_string, parts, a.key)
    img = hb.wrap_ota(blob, a.vendor, a.product, a.version, a.version_string)
    with open(a.output, "wb") as f:
        f.write(img)
    print("%s: %d bytes, %d part(s), product %s (%#x)" % (a.output, len(img), len(parts),
                                                          a.version_string, a.version))


if __name__ == "__main__":
    main()

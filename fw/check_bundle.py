#!/usr/bin/env python3
"""Check a Hearth firmware bundle: the same checks the host performs, in the
same order, with the spec's reason numbers in the failure message (spec
2026-09-07-fota-design 5.4: 1 signature, 2 digest, 3 target mismatch,
4 version not accepted, 5 storage, 6 malformed).

  python3 fw/check_bundle.py <file.ota> --pub fw/keys/hearth_bundle_dev_p256.pub.pem

Exits 0 only when everything verifies. The checks the host cannot reach
without its own configuration (reason 3 target match, reason 4 version
acceptance, reason 5 storage) are noted, not run.
"""
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hearth_bundle as hb

TYPE = {hb.PART_HOST: "host", hb.PART_HEARTH: "hearth"}
VARIANT_NAME = {v: k for k, v in hb.VARIANT.items()}


def die(reason, msg):
    print("refused (reason %d): %s" % (reason, msg), file=sys.stderr)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ota", help="the bundle, a Matter OTA image")
    ap.add_argument("--pub", required=True, help="the public key that signs bundles, PEM")
    a = ap.parse_args()
    try:
        img = open(a.ota, "rb").read()
    except OSError as e:
        die(6, "cannot read %s: %s" % (a.ota, e))
    try:
        ota, blob = hb.unwrap_ota(img)
    except ValueError as e:
        die(6, "Matter header: %s" % e)
    print("Matter header: vendor %x product %x version %d (%s) payload %d bytes" %
          (ota.get("vendor_id", 0), ota.get("product_id", 0), ota.get("version", 0),
           ota.get("version_string", "?"), ota.get("payload_size", 0)))
    try:
        info = hb.parse_container(blob)
    except ValueError as e:
        die(6, "container: %s" % e)
    if ota.get("payload_size") and ota["payload_size"] != len(blob):
        die(6, "payload size %d does not match the container, %d" % (ota["payload_size"], len(blob)))
    print("container: %d part(s), product %s (%#x)" %
          (len(info["parts"]), info["product_version_string"], info["product_version"]))
    if not hb.verify_container(blob, a.pub):
        die(1, "signature does not verify with the given public key")
    for i, p in enumerate(info["parts"]):
        data = blob[p["offset"]:p["offset"] + p["length"]]
        if len(data) != p["length"]:
            die(2, "part %d: only %d of %d bytes present" % (i, len(data), p["length"]))
    if not hb.verify_parts(blob):
        die(2, "a part digest does not match its row")
    print("signature: OK, part digests: OK")
    for i, p in enumerate(info["parts"]):
        print("  part %d: type %s, target %r, variant %s, version %s, %d bytes" %
              (i, TYPE.get(p["type"], p["type"]), p["target"],
               VARIANT_NAME.get(p["variant"], p["variant"]), p["version"], p["length"]))
    print("host checks not run here: 3 target mismatch, 4 version not accepted, 5 storage")
    print("OK")


if __name__ == "__main__":
    main()

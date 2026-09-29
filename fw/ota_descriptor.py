#!/usr/bin/env python3
"""Emit the Home Assistant provider-directory descriptor for a Hearth bundle.

Field names checked against python-matter-server's
matter/server/ota/dcl.py at 2026-09-29 (offline; no network access in this
environment, so the check is by the project's own copy of the model):
OtaProviderFileEntry uses vid, pid, softwareVersion, softwareVersionString,
otaUrl, otaChecksum, otaChecksumType, minApplicableSoftwareVersion,
maxApplicableSoftwareVersion, releaseNotesUrl.

  python3 fw/ota_descriptor.py <file.ota> [--url file:///path] -o descriptor.json

  otaUrl is required. otaChecksum is the sha256 of the file, base64;
  otaChecksumType is 1 (sha256). maxApplicableSoftwareVersion is the
  product version minus one, the highest version that needs an update.
"""
import argparse, base64, hashlib, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hearth_bundle as hb


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ota", help="the bundle, a Matter OTA image")
    ap.add_argument("--url", required=True,
                    help="where the image is served from, e.g. file:///path/to/file.ota")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    img = open(a.ota, "rb").read()
    try:
        ota, _blob = hb.unwrap_ota(img)
    except ValueError as e:
        sys.exit("%s: %s" % (a.ota, e))
    version = ota["version"]
    desc = {
        "vid": ota["vendor_id"],
        "pid": ota["product_id"],
        "softwareVersion": version,
        "softwareVersionString": ota.get("version_string", ""),
        "otaUrl": a.url,
        "otaChecksum": base64.b64encode(hashlib.sha256(img).digest()).decode(),
        "otaChecksumType": 1,
        "minApplicableSoftwareVersion": 0,
        "maxApplicableSoftwareVersion": version - 1,
        "releaseNotesUrl": "",
    }
    with open(a.output, "w") as f:
        json.dump(desc, f, indent=2, sort_keys=True)
        f.write("\n")
    print("%s: %s -> %s" % (a.ota, desc["softwareVersionString"], a.output))


if __name__ == "__main__":
    main()

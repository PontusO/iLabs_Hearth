#!/usr/bin/env python3
"""Emit the Home Assistant provider-directory descriptor for a Hearth bundle.

The field names are those of python-matter-server's OtaProviderFileEntry
(matter/server/ota/dcl.py): vid, pid, softwareVersion, softwareVersionString,
otaUrl, otaChecksum, otaChecksumType, minApplicableSoftwareVersion,
maxApplicableSoftwareVersion, releaseNotesUrl. The descriptor has not yet
been loaded by a running Matter server on the project's bench.

  python3 fw/ota_descriptor.py <file.ota> --url file:///path/to/file.ota -o descriptor.json

  otaUrl is required. otaChecksum is the sha256 of the file, base64;
  otaChecksumType is 1 (sha256). maxApplicableSoftwareVersion is the
  product version minus one, the highest version that needs an update.
  A product version of 0 is refused (exit 2), because version - 1 would
  wrap to 0xFFFFFFFF.
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
    if version == 0:
        print("%s: product version 0 is not allowed, because maxApplicableSoftwareVersion"
              " (version - 1) would wrap to 0xFFFFFFFF" % a.ota, file=sys.stderr)
        sys.exit(2)
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

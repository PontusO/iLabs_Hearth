"""Regenerate fw/manifest.json from the images actually present.

Run this after copying new images in. The manifest is what the flasher
prints before it writes, and what fw/test_manifest.py checks, so a stale
manifest is caught by the suite rather than by a user flashing the wrong
thing.
"""
import hashlib, json, os, sys

FW = os.path.dirname(os.path.abspath(__file__))
VARIANTS = ("wifi", "thread", "combined")

def version():
    props = open(os.path.join(FW, "..", "library.properties")).read()
    for line in props.splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("no version= in library.properties")

def main():
    out = {"version": version(), "images": {}}
    for v in VARIANTS:
        d = os.path.join(FW, "images", v)
        if not os.path.isdir(d):
            raise SystemExit("missing image directory: %s" % d)
        files = {}
        for name in sorted(os.listdir(d)):
            blob = open(os.path.join(d, name), "rb").read()
            files[name] = {"sha256": hashlib.sha256(blob).hexdigest(),
                           "size": len(blob)}
        out["images"][v] = {"files": files}
    with open(os.path.join(FW, "manifest.json"), "w") as fh:
        json.dump(out, fh, indent=2, sort_keys=True)
        fh.write("\n")
    print("wrote manifest for version %s" % out["version"])

if __name__ == "__main__":
    main()

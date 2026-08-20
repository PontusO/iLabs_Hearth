"""Regenerate fw/manifest.json from the files actually present.

Run this after copying new images or a new bridge in. The manifest is what the
flasher prints before it writes, and what fw/test_manifest.py checks, so a
stale manifest is caught by the suite rather than by a user flashing the wrong
thing.

Every file this library ships to a board is covered, the bridge UF2 included.
The bridge is written to the board FIRST, before any image, so it is the file
whose corruption strands a user earliest. Directories are hashed whole rather
than by filename, so dropping a second bridge sketch into fw/bridge/ or a
fourth image into fw/images/ needs no change here.
"""
import hashlib, json, os, sys

FW = os.path.dirname(os.path.abspath(__file__))
VARIANTS = ("wifi", "thread", "combined")
BRIDGE_DIR = "bridge"

def version():
    props = open(os.path.join(FW, "..", "library.properties")).read()
    for line in props.splitlines():
        if line.startswith("version="):
            return line.split("=", 1)[1].strip()
    raise SystemExit("no version= in library.properties")

def hash_dir(rel):
    """{name: {sha256, size}} for every file in FW/rel. Not by name: whatever
    is in the directory is what ships, so whatever is in the directory is what
    gets recorded."""
    d = os.path.join(FW, rel)
    if not os.path.isdir(d):
        raise SystemExit("missing directory: %s" % d)
    files = {}
    for name in sorted(os.listdir(d)):
        path = os.path.join(d, name)
        if not os.path.isfile(path):
            continue
        blob = open(path, "rb").read()
        files[name] = {"sha256": hashlib.sha256(blob).hexdigest(),
                       "size": len(blob)}
    if not files:
        raise SystemExit("no files in %s" % d)
    return files

def main():
    out = {"version": version(),
           "bridge": {"files": hash_dir(BRIDGE_DIR)},
           "images": {}}
    for v in VARIANTS:
        out["images"][v] = {"files": hash_dir(os.path.join("images", v))}
    with open(os.path.join(FW, "manifest.json"), "w") as fh:
        json.dump(out, fh, indent=2, sort_keys=True)
        fh.write("\n")
    n = len(out["bridge"]["files"]) + sum(len(e["files"])
                                         for e in out["images"].values())
    print("wrote manifest for version %s (%d files)" % (out["version"], n))

if __name__ == "__main__":
    main()

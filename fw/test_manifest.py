import hashlib, json, os, unittest

FW = os.path.dirname(os.path.abspath(__file__))


def load():
    return json.load(open(os.path.join(FW, "manifest.json")))


def sections(m):
    """(relative directory, files dict) for every group the manifest records.

    Derived from the manifest rather than from a hardcoded list, so a section
    added by make_manifest.py is checked here without editing this file.
    """
    out = [(os.path.join("images", v), e["files"])
           for v, e in m["images"].items()]
    out.append(("bridge", m["bridge"]["files"]))
    return out


class TestManifest(unittest.TestCase):
    def test_every_shipped_file_matches_its_recorded_hash(self):
        m = load()
        for rel, files in sections(m):
            for name, rec in files.items():
                path = os.path.join(FW, rel, name)
                self.assertTrue(os.path.exists(path), "%s missing" % path)
                h = hashlib.sha256(open(path, "rb").read()).hexdigest()
                self.assertEqual(h, rec["sha256"],
                                 "%s/%s hash mismatch" % (rel, name))
                self.assertEqual(os.path.getsize(path), rec["size"])

    def test_no_shipped_file_is_missing_from_the_manifest(self):
        """The other direction. A file on disk that the manifest does not know
        about is a file nobody checked, and fw/README.md claims the manifest
        covers everything shipped. The bridge UF2 was exactly that gap."""
        m = load()
        for rel, files in sections(m):
            d = os.path.join(FW, rel)
            on_disk = {n for n in sorted(os.listdir(d))
                       if os.path.isfile(os.path.join(d, n))}
            self.assertEqual(on_disk, set(files),
                             "%s: on disk but unrecorded: %s" %
                             (rel, sorted(on_disk - set(files))))

    def test_all_three_variants_are_present(self):
        self.assertEqual(sorted(load()["images"]), ["combined", "thread", "wifi"])

    def test_the_bridge_is_present(self):
        self.assertTrue(load()["bridge"]["files"], "no bridge recorded")

    def test_version_matches_library_properties(self):
        m = load()
        props = open(os.path.join(FW, "..", "library.properties")).read()
        version = [l.split("=", 1)[1].strip()
                   for l in props.splitlines() if l.startswith("version=")][0]
        self.assertEqual(m["version"], version)

    def test_each_variant_ships_an_app_named_for_it(self):
        """A wifi image under images/thread/ would satisfy every hash check and
        still be the wrong firmware. The name carries the variant, so check it."""
        m = load()
        for v, e in m["images"].items():
            want = "hearth-%s-%s.bin" % (v, m["version"])
            self.assertIn(want, e["files"], "%s: no %s" % (v, want))


if __name__ == "__main__":
    unittest.main()

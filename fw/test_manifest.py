import hashlib, json, os, unittest

FW = os.path.dirname(os.path.abspath(__file__))

class TestManifest(unittest.TestCase):
    def test_every_image_file_matches_its_recorded_hash(self):
        m = json.load(open(os.path.join(FW, "manifest.json")))
        for variant, entry in m["images"].items():
            for name, rec in entry["files"].items():
                path = os.path.join(FW, "images", variant, name)
                self.assertTrue(os.path.exists(path), "%s missing" % path)
                h = hashlib.sha256(open(path, "rb").read()).hexdigest()
                self.assertEqual(h, rec["sha256"], "%s/%s hash mismatch" % (variant, name))
                self.assertEqual(os.path.getsize(path), rec["size"])

    def test_all_three_variants_are_present(self):
        m = json.load(open(os.path.join(FW, "manifest.json")))
        self.assertEqual(sorted(m["images"]), ["combined", "thread", "wifi"])

    def test_version_matches_library_properties(self):
        m = json.load(open(os.path.join(FW, "manifest.json")))
        props = open(os.path.join(FW, "..", "library.properties")).read()
        version = [l.split("=", 1)[1].strip()
                   for l in props.splitlines() if l.startswith("version=")][0]
        self.assertEqual(m["version"], version)

if __name__ == "__main__":
    unittest.main()

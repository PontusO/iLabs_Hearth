import os, struct, subprocess, sys, tempfile, unittest
sys.path.insert(0, os.path.dirname(__file__))
import hearth_bundle as hb

FW = os.path.dirname(os.path.abspath(__file__))


def make_key(d):
    key = os.path.join(d, "k.pem"); pub = os.path.join(d, "k.pub.pem")
    subprocess.check_call(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", key])
    subprocess.check_call(["openssl", "ec", "-in", key, "-pubout", "-out", pub], stderr=subprocess.DEVNULL)
    return key, pub


class TestBundle(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.key, self.pub = make_key(self.d)
        self.parts = [
            {"type": hb.PART_HEARTH, "variant": hb.VARIANT["wifi"], "target": "ESP32-C6 Hearth",
             "version": "1.3.0", "data": b"H" * 1000},
            {"type": hb.PART_HOST, "variant": 0, "target": "product", "version": "1.4.0",
             "data": b"P" * 777},
        ]

    def test_header_and_row_sizes_are_the_spec(self):
        self.assertEqual(hb.HEADER.size, 64)
        self.assertEqual(hb.PART.size, 108)

    def test_round_trip(self):
        blob = hb.build_container(0xFFF1, 0x8000, 0x00010400, "1.4.0", self.parts, self.key)
        info = hb.parse_container(blob)
        self.assertEqual(info["product_version"], 0x00010400)
        self.assertEqual(info["product_version_string"], "1.4.0")
        self.assertEqual([p["target"] for p in info["parts"]], ["ESP32-C6 Hearth", "product"])
        for p, src in zip(info["parts"], self.parts):
            self.assertEqual(blob[p["offset"]:p["offset"] + p["length"]], src["data"])
            self.assertEqual(p["offset"] % 16, 0)
        self.assertTrue(hb.verify_container(blob, self.pub))

    def test_tampered_table_fails_verification(self):
        blob = bytearray(hb.build_container(0xFFF1, 0x8000, 1, "1", self.parts[:1], self.key))
        blob[64 + 44] ^= 0x01          # first byte of the version string in row 0
        self.assertFalse(hb.verify_container(bytes(blob), self.pub))

    def test_tampered_part_fails_digest(self):
        blob = bytearray(hb.build_container(0xFFF1, 0x8000, 1, "1", self.parts[:1], self.key))
        info = hb.parse_container(bytes(blob))
        blob[info["parts"][0]["offset"]] ^= 0x01
        self.assertTrue(hb.verify_container(bytes(blob), self.pub))       # table intact
        self.assertFalse(hb.verify_parts(bytes(blob)))                       # part digest wrong

    def test_wrong_key_fails(self):
        blob = hb.build_container(0xFFF1, 0x8000, 1, "1", self.parts[:1], self.key)
        _, other = make_key(tempfile.mkdtemp())
        self.assertFalse(hb.verify_container(blob, other))

    def test_ota_wrap_unwrap(self):
        payload = b"x" * 100
        img = hb.wrap_ota(payload, 0xFFF1, 0x8000, 5, "0.0.5")
        self.assertEqual(img[:4], struct.pack("<I", 0x1BEEF11E))
        info, out = hb.unwrap_ota(img)
        self.assertEqual(out, payload)
        self.assertEqual(info["version"], 5)
        self.assertEqual(info["version_string"], "0.0.5")

    def test_pubkey_raw_is_64_bytes(self):
        self.assertEqual(len(hb.pubkey_raw(self.pub)), 64)

    def test_version_string_limits(self):
        with self.assertRaises(ValueError):
            hb.build_container(1, 1, 1, "x" * 32, self.parts[:1], self.key)
        with self.assertRaises(ValueError):
            hb.build_container(1, 1, 1, "ok", [dict(self.parts[0], target="t" * 32)], self.key)

    def test_committed_dev_key_matches_header(self):
        pub = os.path.join(FW, "keys", "hearth_bundle_dev_p256.pub.pem")
        raw = hb.pubkey_raw(pub)
        hdr = open(os.path.join(FW, "..", "src", "HearthDevKey.h")).read()
        for b in raw:
            self.assertIn("0x%02X" % b, hdr)


if __name__ == "__main__":
    unittest.main()

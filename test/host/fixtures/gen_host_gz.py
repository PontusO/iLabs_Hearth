# Controller's fixture generator for test/host/fixtures/host-gz.ota: good.ota's two
# parts, byte for byte, rebuilt with fw/hearth_bundle.py's own builder and the host
# part's gzip flag (flags bit 0) set. Bug B662: the RP2350 OTA bootloader inflates
# only a file that starts with the gzip magic, so a gzip host part inside a staged
# bundle cannot be applied and the library must refuse it. The flag is what is
# under test; the bytes stay good.ota's. make_bundle.py refuses --host-gz, so this
# calls the builder directly.
# Rerun from the repo root: python3 test/host/fixtures/gen_host_gz.py
import sys
sys.path.insert(0, 'fw')
import hearth_bundle as hb

blob = open('test/host/fixtures/good.ota', 'rb').read()
_info, payload = hb.unwrap_ota(blob)
c = hb.parse_container(payload)
parts = []
for p in c['parts']:
    parts.append({'type': p['type'], 'variant': p['variant'], 'target': p['target'], 'version': p['version'],
                  'flags': 1 if p['type'] == hb.PART_HOST else p['flags'],
                  'data': payload[p['offset']:p['offset'] + p['length']]})
out = hb.build_container(c['vendor_id'], c['product_id'], c['product_version'], c['product_version_string'],
                         parts, 'fw/keys/hearth_bundle_dev_p256.pem')
img = hb.wrap_ota(out, c['vendor_id'], c['product_id'], c['product_version'], c['product_version_string'])
open('test/host/fixtures/host-gz.ota', 'wb').write(img)
print('test/host/fixtures/host-gz.ota: %d bytes' % len(img))

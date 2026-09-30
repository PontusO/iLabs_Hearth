# Bundle signing keys

`hearth_bundle_dev_p256.pem` (with its public half `hearth_bundle_dev_p256.pub.pem`
and the header `src/HearthDevKey.h` generated from it) signs development
bundles built with `fw/make_bundle.py`. This key is public: it is in the
repo, so a bundle signed with it proves nothing about where it came from,
only that the parts have not been corrupted or edited since signing.

A product generates its own key with the same two commands

    openssl ecparam -name prime256v1 -genkey -noout -out my_key.pem
    openssl ec -in my_key.pem -pubout -out my_key.pub.pem

keeps the private half out of the repo, and passes the 64-byte public half
in `HearthUpdateConfig::publicKey` (regenerate a header like
`src/HearthDevKey.h` with `hearth_bundle.emit_dev_key_header`).

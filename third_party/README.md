# EPUB dependencies

These sources are vendored so building on Vita does not require downloading or
installing additional SDK packages.

- TinyXML-2 **10.0.0**, from [leethomason/tinyxml2](https://github.com/leethomason/tinyxml2/tree/10.0.0).
  See [license](tinyxml2/LICENSE.txt).
- Classic minizip from zlib **1.3.1**, from
  [madler/zlib](https://github.com/madler/zlib/tree/v1.3.1/contrib/minizip).
  See the copyright/license headers in each file and [zlib license](minizip/LICENSE).
  Only the unzip and file I/O modules are built. Decryption is disabled.

Both use the SDK/system zlib. Minizip uses portable stdio via `USE_FILE32API`;
Novel Reader bounds archive sizes below 512 MiB. No vendored files are modified.
TinyXML-2 does not fetch external DTDs or expand external entities.

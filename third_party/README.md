# Vendored dependencies

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

Online novels additionally use:

- Lua **5.4.8**, from [lua.org](https://www.lua.org/ftp/lua-5.4.8.tar.gz).
  Its MIT license is in [lua.h](lua/src/lua.h) and [upstream readme](lua/doc/readme.html).
  The library is compiled as C++ with C linkage to allow native objects to unwind
  when Lua reports an allocation error. No upstream source modifications.
- curl **8.17.0**, from [curl.se](https://curl.se/download/curl-8.17.0.tar.gz).
  See [license](curl/COPYING). Only its HTTP/HTTPS static library is built; CLI,
  tests, examples, and documentation generation are disabled. Unused upstream
  test, CLI, packaging, and documentation files are omitted.
- Mbed TLS **3.6.5**, from [Mbed-TLS/mbedtls](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.5).
  See [license](mbedtls3/LICENSE). The library subdirectory is built directly with
  checked-in generated files; upstream programs/tests/docs are omitted. Vita
  configuration uses the SDK's random-number API and monotonic process clock;
  curl owns the sockets. See [configuration](../include/vita_tls_config.h) and
  [platform implementation](../src/vita_entropy.c). No upstream source modifications.
- Mozilla CA certificates, converted by [curl's CA Extract](https://curl.se/docs/caextract.html),
  packaged in [cacert.pem](../assets/cacert.pem). The bundle header records its
  extraction date, provenance, and license link. Certificate and hostname
  verification remain enabled.

[Online dependency configuration](../cmake/OnlineDependencies.cmake) applies to
desktop tests and Vita builds. The bundled dependencies require no Python,
network downloads, or extra SDK packages during configuration/build.

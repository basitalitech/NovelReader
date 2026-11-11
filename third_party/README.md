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

Offline narration uses **Flite 2.2**, from
[festvox/flite v2.2](https://github.com/festvox/flite/tree/v2.2).
Its source archive SHA-256 is
`ab1555fe5adc3f99f1d4a1a0eb1596d329fd6d74f1464a0097c81f53c0cf9e5c`.
Only the sources/headers compiled by the core, US English language,
CMU lexicon, and built-in SLT voice are retained. Other voices, platform audio
drivers, upstream tools/tests, and unused source files are excluded.
Shared core modules contain disabled socket stubs and a no-output audio backend;
the app supplies playback through its own buffered Vita audio adapter.
[Integration](../cmake/FliteDependencies.cmake) disables networking, mmap and
desktop audio. The voice is linked into the reader and needs no separate
download. Only the production reader and its normal tests are retained.

Upstream modification: [cst_track.c](flite/src/speech/cst_track.c) skips copying
an empty track's NULL times pointer. Sanitizer checking found that upstream
passed it to `memmove` even with a zero byte count, which is undefined behavior.
The guard is marked in the source and does not change non-empty track copies.

See [Flite's license](flite/COPYING) and
[acknowledgements](flite/ACKNOWLEDGEMENTS), both included in the reader VPK.
The VPK also includes the vendored TinyXML-2, minizip, Lua, curl, and Mbed TLS
license notices under `app0:/licenses/`. Font notices remain under `app0:/fonts/`;
the CA bundle retains its embedded provenance and license link. About → Credits
and License provide pageable attribution and notice summaries.
The previous eSpeak dependency, compiled runtime data, endian shim, native
data-generation tools and temporary SLT test app have been removed.

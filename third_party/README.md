# Third-party dependencies

[README](../README.md) · [Build requirements](../docs/building.md#requirements) ·
[Bundled fonts](../assets/fonts/README.md)

The dependencies below are vendored. Configuring and building NovelReader does
not download them. VitaSDK must still provide the platform, graphics, font,
image, audio, thread, and compression libraries listed in the build guide.

## Sources and notices

| Component | Version and upstream | License/notice |
| --- | --- | --- |
| TinyXML-2 | [10.0.0](https://github.com/leethomason/tinyxml2/tree/10.0.0) | [zlib license](tinyxml2/LICENSE.txt) |
| Classic minizip | [zlib 1.3.1](https://github.com/madler/zlib/tree/v1.3.1/contrib/minizip) | [Notice](minizip/LICENSE) |
| Lua | [5.4.8](https://github.com/lua/lua/tree/v5.4.8) | [MIT license and upstream readme](lua/doc/readme.html) |
| curl | [8.17.0](https://curl.se/download/curl-8.17.0.tar.gz) | [curl license](curl/COPYING) |
| Mbed TLS | [3.6.5](https://github.com/Mbed-TLS/mbedtls/tree/mbedtls-3.6.5) | [Apache License 2.0 option](mbedtls3/LICENSE) |
| Flite and CMU SLT | [Flite 2.2](https://github.com/festvox/flite/tree/v2.2) | [COPYING](flite/COPYING) and [acknowledgements](flite/ACKNOWLEDGEMENTS) |
| Mozilla CA certificates | [curl CA Extract](https://curl.se/docs/caextract.html) | Extraction date, provenance, and license link in [cacert.pem](../assets/cacert.pem) |

## Build configuration

- **EPUB:** TinyXML-2 and minizip use SDK/system zlib. Only unzip/file I/O modules
  are built; decryption is disabled. `USE_FILE32API` selects portable stdio.
  Archive reads are bounded and XML parsing does not fetch external DTDs.
- **Lua:** built as C++ with C linkage so allocation errors can unwind native
  objects. Adapters run in bounded states with selected libraries and packaged
  helpers; filesystem, OS, package-loading, and debug libraries are not exposed.
- **HTTPS:** [OnlineDependencies.cmake](../cmake/OnlineDependencies.cmake) builds
  curl's static HTTP/HTTPS library with Mbed TLS. Upstream CLI, tests, and generated
  documentation are not built. Checked-in generated TLS files avoid generation
  tools at project build time. Vita uses the SDK random-number API and process
  clock through [vita_entropy.c](../src/vita_entropy.c); curl owns the sockets.
  Certificate and hostname verification are enabled with the packaged CA bundle.
- **Narration:** [FliteDependencies.cmake](../cmake/FliteDependencies.cmake) retains
  the shared core, US English support, CMU lexicon, and SLT model. Other voices,
  tools, tests, and platform audio drivers are omitted. Networking, mmap, and
  desktop audio are disabled; NovelReader supplies buffered Vita playback. No
  separate voice download or runtime data directory is required.

The vendored TinyXML-2, minizip, Lua, and Mbed TLS sources are unmodified; upstream
files not required by this build may be omitted. The Flite integration includes
an explicit guard in [cst_track.c](flite/src/speech/cst_track.c): copying an empty
track skips its null times pointer instead of passing it to `memmove` with a zero
byte count. Non-empty track copies retain their upstream behavior.

## Packaged attribution

[CMake packaging](../CMakeLists.txt) includes TinyXML-2, minizip, Lua, curl,
Mbed TLS, and Flite notices under `app0:/licenses/`. Font notices are under
`app0:/fonts/`, and the CA bundle retains its embedded provenance. About → Credits
and License provide pageable summaries. These notices cover their respective
components; books and artwork retain their original copyrights.

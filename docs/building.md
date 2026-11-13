# Building and development

[README](../README.md) · [Performance and validation](performance.md) ·
[Third-party notices](../third_party/README.md)

## Requirements

| Build | Requirements |
| --- | --- |
| All builds | CMake 3.16+, a C/C++ build toolchain, and a Makefile-compatible build tool |
| PS Vita | VitaSDK with a C++14 compiler, packaging tools, SDK pthread support, libvita2d, FreeType, bzip2, libpng with simplified read support, libjpeg-turbo's JPEG/TurboJPEG libraries, and zlib |
| Host tests | A desktop C++14 compiler, thread support, and zlib development headers/library |

Install VitaSDK and its library packages following the
[official setup guide](https://vitasdk.org/) and
[VDPM instructions](https://github.com/vitasdk/vdpm). Use the environment recommended
there for your operating system; Windows development can use WSL2. These project
commands assume a Unix-style shell and an already installed SDK.

XML/ZIP parsers, Lua, curl, Mbed TLS, and Flite SLT are checked into
[third_party](../third_party/README.md). The project build does not download sources,
fonts, or voices. SDK installation itself may require network access and tools
listed by VitaSDK.

## Build the Vita package

Run from the repository root:

```sh
export VITASDK=/usr/local/vitasdk  # Use your installation path.
export PATH="$VITASDK/bin:$PATH"

cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/vita -j2
```

[CMakeLists.txt](../CMakeLists.txt) selects `$VITASDK/share/vita.toolchain.cmake`
when host tests are disabled. Keep Vita and host builds in separate directories.
The result is `build/vita/NovelReader.vpk`, using title ID `NVRD00001` and package
version `00.70` for source version `0.7.0`.

Configuration validates the packaged LiveArea PNGs. After linking, the build
checks that the pthread implementation needed by the workers is present.
Install the VPK with VitaShell on a homebrew-enabled Vita; see
[installation](../README.md#installation). A successful cross-build does not verify
on-device rendering, input, networking, or audio.

## Build and run host tests

The desktop target runs tests; it is not a desktop GUI port.

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

There are 14 CTest suites covering text/progress/settings, EPUB parsing, library
and image headers, mixed layout, controls, page-curl geometry, online adapters and
storage, Vita network startup, font routing, UI geometry, native frontend flows,
and speech synthesis/buffering/audio. Tests use generated fixtures and injected
transports rather than requiring live source sites. Frontend and platform tests
mock SDK rendering, input, IME, filesystem, or audio calls as appropriate. Speech
tests synthesize real SLT PCM through test sinks.

To list or select tests:

```sh
ctest --test-dir build/host -N
ctest --test-dir build/host -R 'reader_core|epub_reader' --output-on-failure
```

Optional diagnostic builds and hardware validation are described in
[performance and validation](performance.md).

## VS Code and IntelliSense

[Editor configuration](../.vscode/c_cpp_properties.json) reads the compilation
databases from both `build/vita` and `build/host`. Configure both builds before
editing app and tests together; `CMAKE_EXPORT_COMPILE_COMMANDS=ON` generates these
files. Launch VS Code with `VITASDK` available in its environment.

This gives each source file its actual compiler, include paths, and CMake-defined
macros. Do not add the entire repository recursively to the app include path:
the test suites contain different `vita2d.h` stubs, which can hide the SDK header
and cause cascading undefined-identifier errors. Do not patch application headers
or hard-code test macros to silence those diagnostics.

If errors persist after configuring, run **C/C++: Reset IntelliSense Database** and
reopen the affected file. The existing **Build Vita package** task uses the same
Vita commands shown above. VS Code's
[compilation database reference](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)
explains per-file configuration.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `Set VITASDK` configuration error | Export the SDK installation path in the build shell |
| Missing SDK headers or libraries | Install the required VitaSDK library packages; verify `$VITASDK/bin` is on `PATH` |
| FreeType or PNG build errors | Verify FreeType headers and libpng's simplified read API are available in the SDK |
| Host zlib detection failure | Install the host system's zlib development package |
| Vita and desktop compiler flags mixed | Use separate build directories; configure host tests only in the host directory |
| Editor-only missing graphics functions/macros | Regenerate both compilation databases and check the IntelliSense setup above |
| Artwork or pthread validation failure | Resolve the reported asset/link problem; keep the checks enabled |

## Source layout

| Area | Purpose |
| --- | --- |
| [src/main.cpp](../src/main.cpp) | Native screens, input dispatch, lifecycle, and resource caches |
| [include](../include) | Reader, online, speech, controls, and UI interfaces |
| [src/epub.cpp](../src/epub.cpp), [src/reader.cpp](../src/reader.cpp), [src/layout.cpp](../src/layout.cpp) | Publication loading, text/persistence, and pagination |
| [src/online_service.cpp](../src/online_service.cpp), [assets/sources](../assets/sources) | Network worker and packaged Lua adapters |
| [src/speech.cpp](../src/speech.cpp), [src/speech_buffer.cpp](../src/speech_buffer.cpp) | Narration lifecycle and bounded playback queue |
| [tests](../tests) | Portable regression tests and SDK stubs |
| [cmake](../cmake) | Dependency configuration and package validation |

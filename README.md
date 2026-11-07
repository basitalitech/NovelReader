# Novel Reader for PS Vita

A local light novel reader built with C++14, VitaSDK, and vita2d. This first
version reads **UTF-8 TXT** books entirely offline. Online sources and Lua
parsers are future work.

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` books into that folder using VitaShell's USB or FTP mode.
4. Press **Triangle** in the library to refresh, then **Cross** to open a book.

The bundled welcome guide is always available in the library. Books are scanned
directly in the books folder; subfolders are not scanned. File names become book
titles, with underscores replaced by spaces. Uppercase `.TXT` extensions work.

| Screen | Controls |
| --- | --- |
| Library | Up/Down: browse; Cross: read; Triangle: refresh |
| Reader | L/R or Left/Right: turn page; Up/Down: move one line |
| Reader | Square: set/replace bookmark; Select: jump to bookmark |
| Reader | Triangle: settings; Circle: library |
| Settings | Up/Down: select; Left/Right: adjust; Circle/Cross/Triangle: apply |
| All screens | Start: save and exit |

Reading progress saves on navigation, bookmark changes, and return to the
library. Each book has one bookmark and an independent reading position. Font
and spacing changes preserve the text position rather than a page number.
Night, Paper, and Sepia themes are available. Saves live in
`ux0:data/NovelReader/progress/`; settings live in
`ux0:data/NovelReader/settings.txt`. Renaming a book gives it a new save identity.
Replacing a book's contents may invalidate its old position.

## Build

Set `VITASDK` to your VitaSDK installation. The installed SDK needs vita2d and
its FreeType, PNG, JPEG, and zlib dependencies. No additional downloads are made
by this project.

```sh
cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita -j2
```

The installable package is `build/vita/NovelReader.vpk`, title ID `NVRD00001`.
The package requests extended homebrew filesystem access to read and save under
`ux0:data`. VS Code's **Build Vita package** task performs the same build.

Portable core tests run on a desktop without VitaSDK:

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

## Current limits

- Plain UTF-8 TXT only, optionally with a UTF-8 BOM. Convert UTF-16 or legacy
  encodings first. Windows, Unix, and classic Mac line endings are accepted.
- Maximum 8 MiB of input and normalized text, and 100,000 display lines. Tabs
  expand to four spaces. Oversized layouts are rejected to bound Vita memory use.
- Horizontal text; glyph coverage follows the Vita system font. No ruby,
  illustrations, EPUB, PDF, chapter detection, search, or touch controls yet.
- Layout is synchronous when opening a book or applying settings. Large books
  can take time to paginate. The page-count-independent position is a byte
  offset into normalized text.
- Saves use a temporary file and rename; failed writes are shown in the reader.
  Forced termination can lose the latest in-progress filesystem operation.
- Cross-compilation and desktop tests do not replace testing on Vita hardware.

## Structure and next steps

- [Portable reader core](include/reader.hpp): loading, normalization, UTF-8-safe
  wrapping with a font measurement callback, settings, and position persistence.
- [Vita frontend](src/main.cpp): filesystem library scan, system font rendering,
  controller navigation, and reader settings.
- [Core tests](tests/reader_tests.cpp): malformed text, wrapping, reflow anchors,
  save replacement, corrupt state, and file size limits.

Next local milestone: DRM-free EPUB, reading its OPF spine in order and converting
XHTML into the same reader text model, followed by chapter navigation and images.
Future online sources can use Lua adapters for catalogue/search/chapter fetching.
Downloaded chapters should enter the local library so offline reading uses the
same reader. No Lua runtime or network code is included yet.

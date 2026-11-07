# Novel Reader for PS Vita

A local light novel reader built with C++14, VitaSDK, and vita2d. Version 0.2
reads **UTF-8 TXT and DRM-free EPUB** books entirely offline, with chapter
navigation and saved reading positions. Online sources and Lua parsers are
future work.

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` or DRM-free `.epub` books into that folder using VitaShell's USB or FTP mode.
4. Press **Triangle** in the library to refresh, then **Cross** to open a book.

The bundled welcome guide is always available in the library. Books are scanned
directly in the books folder; subfolders are not scanned. File names become
library titles, with underscores replaced by spaces. After opening an EPUB, its
metadata title replaces the filename in the current library session. Its title
and author appear in the reader. Uppercase `.TXT` and `.EPUB` extensions work.

| Screen | Controls |
| --- | --- |
| Library | Up/Down: browse; Cross: read; Triangle: refresh |
| Reader | Left/Right: turn page; Up/Down: move one line |
| EPUB reader | L/R: previous/next chapter; Cross: contents |
| TXT reader | L/R: turn page |
| Contents | Up/Down: choose chapter; Left/Right: browse six entries; Cross: open; Circle: return |
| Reader | Square: set/replace bookmark; Select: jump to bookmark |
| Reader | Triangle: settings; Circle: library |
| Settings | Up/Down: select; Left/Right: adjust; Circle/Cross/Triangle: apply |
| All screens | Start: save and exit |

Reading progress saves on navigation, bookmark changes, and return to the
library. Each book has one bookmark and an independent reading position. EPUB
positions and bookmarks include the chapter and a text offset within it; jumping
to a bookmark in another chapter opens that chapter. Font and spacing changes
preserve the text position rather than a page number. Existing TXT saves from
version 0.1 are read automatically and upgraded on the next save.
Night, Paper, and Sepia themes are available. Saves live in
`ux0:data/NovelReader/progress/`; settings live in
`ux0:data/NovelReader/settings.txt`. Renaming a book gives it a new save identity.
Replacing a book's contents or changing its chapter order may invalidate its old
position. Out-of-range saved chapters return to the beginning; out-of-range
bookmarks are disabled.

## EPUB reading

- Chapters follow the OPF spine's primary reading order (`linear="no"` entries
  are omitted), independently of ZIP entry or filename order.
- The contents menu lists one entry per spine document. Labels come from the
  EPUB 3 navigation document, then the EPUB 2 NCX, with numbered chapter labels
  as a fallback. Opening a chapter can replace its fallback label with a heading.
  Multiple contents links within one document share a single chapter entry;
  fragment-level contents jumps are not implemented.
- Left/Right crosses to the adjacent chapter at a chapter boundary. Left from
  the first line opens the previous chapter's final page. L/R opens the adjacent
  chapter at its beginning. Contents selection of the current chapter keeps its
  reading position.
- Only the current chapter's text is kept in memory; opening a different chapter
  reads and lays out that document on demand. Failed chapter loads keep the
  current chapter and position intact so you can choose another chapter.
- Paragraphs, headings, explicit line breaks, bold, and emphasis are retained.
  Headings use the accent color, bold uses a second glyph pass, and emphasis is
  underlined using the system font. Scripts, styles, and ruby annotations are
  omitted; ruby base text remains. Images show an illustration/alt-text marker.
- CSS, custom fonts, exact publisher layouts, and actual image rendering are not
  supported. XHTML must be well-formed UTF-8 XML. XML/numeric entities and common
  named XHTML entities are supported; external DTDs are never loaded.

## Build

Set `VITASDK` to your VitaSDK installation. The installed SDK needs vita2d and
its FreeType, PNG, JPEG, and zlib dependencies. XML and ZIP readers are vendored;
see [dependency versions and licenses](third_party/README.md). No downloads are
made during configuration or building.

```sh
cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita -j2
```

The installable package is `build/vita/NovelReader.vpk`, title ID `NVRD00001`.
The package requests extended homebrew filesystem access to read and save under
`ux0:data`. VS Code's **Build Vita package** task performs the same build.

Portable core tests run on a desktop without VitaSDK, using a C/C++ toolchain,
CMake, and the system zlib development package:

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

## Current limits

- Plain UTF-8 TXT, optionally with a UTF-8 BOM. Convert UTF-16 or legacy
  encodings first. Windows, Unix, and classic Mac line endings are accepted.
- Maximum 8 MiB of input and normalized text, and 100,000 display lines. Tabs
  expand to four spaces. Oversized layouts are rejected to bound Vita memory use.
- EPUB limits: 512 MiB archive, 20,000 ZIP entries, 4,096 primary chapters,
  512 KiB per metadata/navigation document, and 2 MiB per XHTML document and
  resulting chapter text. Formatting is capped at 100,000 style spans. ZIP
  stored/deflated entries are accepted and their checksums verified. Encrypted
  chapter content is unsupported; font obfuscation does not block text reading.
- Horizontal text; glyph coverage follows the Vita system font. No rendered
  illustrations, PDF, TXT chapter detection, search, or touch controls yet.
- Layout is synchronous when opening a book or applying settings. Large books
  can take time to paginate. EPUB pagination is limited to the current chapter.
- Saves use a temporary file and rename; failed writes are shown in the reader.
  Forced termination can lose the latest in-progress filesystem operation.
- Cross-compilation and desktop tests do not replace testing on Vita hardware.

## Structure and next steps

- [Portable reader API](include/reader.hpp): publications, chapters, style spans,
  UTF-8-safe wrapping, settings, and chapter-aware position persistence.
- [EPUB loader](src/epub.cpp): bounded ZIP reads, container/package metadata,
  spine/navigation parsing, and XHTML conversion; no archive extraction to disk.
- [Vita frontend](src/main.cpp): filesystem library scan, system font rendering,
  controller navigation, and reader settings.
- [Core tests](tests/reader_tests.cpp): malformed text, wrapping, reflow anchors,
  save replacement, corrupt state, and file size limits.
- [EPUB tests](tests/epub_tests.cpp): generated stored/deflated ZIP fixtures,
  spine order, navigation labels, formatting, legacy saves, lazy chapter reads,
  corrupt ZIP/XML, encrypted chapters, path handling, and expansion limits.

Next local milestone: embedded illustrations with zoom, followed by cover art and
library sorting/progress indicators.
Future online sources can use Lua adapters for catalogue/search/chapter fetching.
Downloaded chapters should enter the local library so offline reading uses the
same reader. No Lua runtime or network code is included yet.

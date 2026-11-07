# Novel Reader for PS Vita

A local light novel reader built with C++14, VitaSDK, and vita2d. Version 0.3
reads **UTF-8 TXT and DRM-free EPUB** books entirely offline, with chapter
navigation, cover previews, library filters, and a zoomable illustration viewer.
Online sources and Lua parsers are future work.

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` or DRM-free `.epub` books into that folder using VitaShell's USB or FTP mode.
4. Press **Triangle** in the library to refresh, then **Cross** to open a book.

The bundled welcome guide is always available in the library. Books are scanned
directly in the books folder; subfolders are not scanned. File names become
library titles for TXT books, with underscores replaced by spaces. EPUB metadata
is read when scanning the library, so titles and authors are available before
opening a book. Uppercase `.TXT` and `.EPUB` extensions work.

| Screen | Controls |
| --- | --- |
| Library | Up/Down: browse; Cross: read; Triangle: refresh |
| Library | Square: cycle Title/Recent/Progress sorting; L/R: cycle All/Reading/Finished filters |
| Reader | Left/Right: turn page; Up/Down: move one line |
| EPUB reader | L/R: previous/next chapter; Cross: contents |
| TXT reader | L/R: turn page |
| Contents | Up/Down: choose chapter; Left/Right: browse six entries; Cross: open; Circle: return |
| Contents | Square: illustrations in the current reading chapter |
| Illustrations | Up/Down: choose; Cross: view; Circle: contents |
| Image viewer | Cross: zoom in; Square: zoom out; D-pad: pan; Triangle: fit |
| Image viewer | L/R: previous/next image in chapter; Circle: illustration list |
| Reader | Square: set/replace bookmark; Select: jump to bookmark |
| Reader | Triangle: settings; Circle: library |
| Settings | Up/Down: select; Left/Right: adjust; Circle/Cross/Triangle: apply |
| All screens | Start: save and exit |

Reading progress saves on navigation, bookmark changes, and return to the
library. Each book has one bookmark and an independent reading position. EPUB
positions and bookmarks include the chapter and a text offset within it; jumping
to a bookmark in another chapter opens that chapter. Font and spacing changes
preserve the text position rather than a page number. Existing TXT saves from
versions 0.1 and 0.2 are read automatically and upgraded on the next save.
Night, Paper, and Sepia themes are available. Saves live in
`ux0:data/NovelReader/progress/`; settings live in
`ux0:data/NovelReader/settings.txt`. Renaming a book gives it a new save identity.
Replacing a book's contents or changing its chapter order may invalidate its old
position. Out-of-range saved chapters return to the beginning; out-of-range
bookmarks are disabled.

## Library

- Six books per page, with a selected-book cover preview, author, and chapter
  count. EPUB 3 `cover-image` and EPUB 2 `meta name="cover"` references are
  supported. TXT books and books without readable covers get a drawn placeholder.
- Title sorting ignores ASCII letter case. Recent sorting uses last-read time;
  Progress sorting places the highest saved percentages first. Ties resolve by
  title and filename. The bundled guide is last in All books and omitted from
  Reading/Finished filters.
- Sort and filter preferences survive restarts. Reading includes books opened
  in this version, plus legacy saves with a nonzero position. Finished means
  the last page of the final chapter has been displayed. Returning to an earlier
  page changes the saved percentage and Reading/Finished classification.
- Progress bars show saved progress. EPUB percentages estimate progress by
  weighting chapters equally, then using the text offset within the current
  chapter; they are not a precise word-count percentage. TXT progress uses its
  text offset. Legacy saves show Continue reading until reopened.
- Metadata scanning is synchronous on launch and refresh. Only the selected
  cover is decoded, and changing selection releases the previous cover. Broken
  books remain listed with an error so one file does not hide the rest of the
  library. There is no persistent metadata cache yet.

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
  omitted; ruby base text remains. Images keep an illustration/alt-text marker
  in the text to preserve existing reading positions. Open **Cross → Square**
  from the reader to view the current chapter's images.
- CSS, custom fonts, and exact publisher layouts are not supported. XHTML must
  be well-formed UTF-8 XML. XML/numeric entities and common
  named XHTML entities are supported; external DTDs are never loaded.

## Illustrations

The illustration list follows image occurrence order in the current chapter,
including raster images referenced by SVG `<image>` wrappers. Paths are resolved
relative to the chapter document, including parent directories and URL-encoded
filenames. External images are not fetched.

The viewer supports embedded PNG and JPEG images, fits them to the screen, and
zooms from 1× to 4× the fitted size. Pan is clamped to the image edges; smaller
axes stay centered. The image is clipped to the viewing area, leaving controls
visible. Viewing images does not move the reading position or bookmark.

Each image is limited to 8 MiB encoded, 4,096 pixels per side, and 4 megapixels
total. Headers are checked before GPU allocation; the PNG/JPEG decoder then
validates the full stream. Failed loads keep the current image/reader state and
show an error. A cover or viewer texture is released when leaving its screen;
replacing a viewer image temporarily retains the old texture until the new one
has decoded successfully. Full SVG artwork, GIF, WebP, animation, EXIF rotation,
and automatic downsampling of larger images are not implemented.

## Build

Set `VITASDK` to your VitaSDK installation. The installed SDK needs vita2d and
its FreeType, PNG (with simplified read API), TurboJPEG, JPEG, and zlib dependencies.
XML and ZIP readers are vendored;
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
- Horizontal text; glyph coverage follows the Vita system font. No PDF, TXT
  chapter detection, search, or touch controls yet. Illustrations use a separate
  viewer rather than inline page layout.
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
  library controls, image viewing, controller navigation, and reader settings.
- [Library logic](src/library.cpp): deterministic sorting/filtering and saved
  progress estimates.
- [Image validation](src/image_info.cpp) and [Vita decoder](src/vita_images.cpp):
  bounded image headers and PNG/TurboJPEG decoding into GPU textures.
- [Core tests](tests/reader_tests.cpp): malformed text, wrapping, reflow anchors,
  save replacement, corrupt state, and file size limits.
- [EPUB tests](tests/epub_tests.cpp): generated stored/deflated ZIP fixtures,
  spine order, navigation labels, formatting, legacy saves, lazy chapter reads,
  corrupt ZIP/XML, encrypted chapters, path handling, and expansion limits.
- [Library and image tests](tests/library_tests.cpp): filters, sorting, progress
  estimates, preference/save migration, image dimensions, and malformed headers.
  Desktop tests cover transport and headers; actual GPU image decoding and
  zoom/pan controls need Vita hardware testing.

Next local milestones: a metadata/thumbnail cache for larger libraries, more
image formats/downsampling, and search.
Future online sources can use Lua adapters for catalogue/search/chapter fetching.
Downloaded chapters should enter the local library so offline reading uses the
same reader. No Lua runtime or network code is included yet.

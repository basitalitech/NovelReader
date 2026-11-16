# NovelReader for PS Vita

[![Platform: PS Vita](https://img.shields.io/badge/platform-PS%20Vita-003791)](https://vitasdk.org/)
[![C++14](https://img.shields.io/badge/C%2B%2B-14-00599C)](CMakeLists.txt)

NovelReader is a native PS Vita homebrew app for reading Unicode text, DRM-free EPUBs,
and downloaded web novels. It supports offline reading, illustrations, saved
progress, bookmarks, and English text-to-speech. The current source version is
**0.8.0**.

[Installation](#installation) · [Usage](#usage) · [Controls](#controls) ·
[Formats](#supported-formats) · [Building](#building-from-source) ·
[Documentation](#documentation) · [Credits](#credits)

## What's new in 0.8.0

- The Library saves smaller cover thumbnails in `ux0:data/NovelReader/thumbnails/` and loads them in batches as you scroll. Startup prepares the sidebar screens and reuses saved covers.
- Drag the grid with front touch or move between rows with the D-pad or either joystick.
- EPUB loading reuses an archive directory index and reads illustration dimensions from image headers when possible. Full image validation still runs before rendering.
- **Find in book** searches local and downloaded text and shows previews you can jump to. Vita firmware fonts add Latin, Japanese, Chinese, and Korean glyph fallback.
- **Margins** is now grouped with the other Reader Settings above the live preview.

## Features

- Persistent library with local imports, online favorites, cover thumbnails,
  title search, sorting, and reading-status filters.
- EPUB section and footnote navigation, offline full-text search, automatic reading-position saves, and multiple bookmarks per book.
- EPUB text formatting and inline PNG/JPEG illustrations, with image inspection,
  zoom, and pan.
- Literata, Atkinson Hyperlegible Next, and Vita system reading fonts; adjustable
  size and spacing; five themes; optional immersive reading.
- Automatic glyph fallback across bundled fonts, optional user fonts, and the
  Vita's Latin, Japanese, Chinese, and Korean firmware fonts.
- Page Curl, Slide and Fade, or immediate page turns, with controller and front
  touchscreen input.
- Bundled Flite SLT English narration with saved speed, optional chapter advance, and a sleep timer.
- Online catalogs and public chapter downloads for Novgo, Asura Novels, and Beast
  Novels. Saved chapters and supported cover artwork remain available offline.

## Installation

### Requirements

- A PS Vita configured to run homebrew, with [VitaShell](https://github.com/TheOfficialFloW/VitaShell) installed.
- Storage for the app, books, and downloads.
- Wi-Fi for online browsing, chapter downloads, and update checks. Local reading and narration work offline.

### Install the app

1. Obtain `NovelReader.vpk` by [building it from source](#building-from-source). Packaged downloads, when published, are listed under [Releases](https://github.com/basitalitech/NovelReader/releases).
2. Transfer the VPK to the Vita using VitaShell's USB or FTP connection.
3. Select the VPK in VitaShell and install it. The package requests extended filesystem access for reading and saving on user storage.
4. Launch **NovelReader** from LiveArea.

Go to **About → Check for Updates** for a stable GitHub release.
Install available updates manually through VitaShell.

See [library, downloads, and backups](docs/library-and-online.md#saved-data-and-removal).

## Usage

### Add local books

1. Copy supported books to `ux0:data/NovelReader/books/`, or another folder on `ux0:`.
2. Open **Library → Add Book → Import books** to browse user storage.
3. Open folders and select books with **Cross** or a tap. Press **Square** to add the selection.
4. Open a library cover, then activate **Start Reading** or **Continue Reading**.

Import saves references to files; it does not copy or move the originals. The browser currently exposes **`ux0:` only** and does not scan directories recursively.

Drag vertically to scroll the library. **Left/Right** moves between books across row boundaries, and **Up/Down** moves between rows. Use **Square** for title search and **Triangle** to sort by title, recency, or progress and filter by reading status.
Home's Continue card resumes the most recently read available book, or opens Library when there is no reading history.

### Read and customize

Progress saves as you navigate and leave the reader. Press **Square** to add or remove a bookmark at the current position. **Select** returns to the most recently saved bookmark. Open **Start → Find in book** for search results, passage previews, and next/previous matches. Searches include local and downloaded chapter text.

For EPUBs, press **Cross → Square** to switch from Chapters to Sections. Use **Start → Links on this page** to open an internal link and **Back to reading position** to return.

Press **Start → Reader Settings** while reading to change typeface, font size, line spacing, page margins, page theme, and page effect. **Cross / Apply** saves changes; **Circle / Cancel** discards the preview; **Select / Reset** previews defaults.
Themes are Night, Paper, Sepia, OLED, and OLED Night. Tap the center of the reading area, or use **Show/Hide reading bar** in Reading controls, to toggle immersive mode.

For narration, open **Start → Read aloud (TTS) → Start here**. The bundled US English voice reads from the visible position to the end of the current chapter. See [offline narration](docs/text-to-speech.md) for controls and limitations.

### Browse online novels

Open **Library → Add Book → Online novels**, then choose a source. Use **Square** to search its catalog. On novel details, **Square / Add to Library** saves a favorite; use **Start Reading / Continue Reading**, **Chapters**, or **Download all** as needed. Reading fetches a missing public chapter before opening it. Browsing or downloading alone does not add a favorite to Library.

**Circle** cancels an active request. Completed chapter downloads are retained; retrying skips valid cached chapters. Sources can change or require browser verification. Login, purchased chapters, and remote inline illustrations are not supported. See [library, downloads, and backups](docs/library-and-online.md).

## Controls

| Context | Controls |
| --- | --- |
| Menus and lists | D-pad or either stick moves content focus; Cross activates; Circle returns or cancels |
| Sidebar screens | L/R switches Home, Library, Bookmarks, Settings, and About |
| Library | Left/Right moves across rows, Up/Down moves by row, Cross opens details or Add Book, Square searches, Triangle sorts or filters, and Circle clears an active search or returns Home |
| Reader | Left/Right turns pages; Up/Down moves by line; Cross opens Chapters; Square toggles a bookmark; Select jumps to the newest bookmark |
| Reader shoulders | L/R changes EPUB/online chapters; for TXT, turns pages |
| Reader controls | Start opens Reading controls; Circle saves and returns to the originating collection |
| Settings | Up/Down selects; Left/Right adjusts; Cross applies; Circle cancels; Select resets the preview |
| Popup menus | Up/Down selects; Cross confirms; Circle goes back/closes; Start closes the menu stack |

Front touch supports sidebar/footer actions, list selection, reader edge taps,
page drags, vertical scrolling, bookmark holds, and image pan/pinch. Image pages
reserve the sticks for image controls. See the [complete controls guide](docs/controls.md)
for viewer, online, import, and gesture mappings. Rear touch is not used.

## Supported formats

| Format | Support and limits |
| --- | --- |
| Unicode TXT | UTF-8 (optional BOM), UTF-16 LE/BE (BOM required); case-insensitive `.txt` extension; Unix, Windows, and classic Mac line endings; 8 MiB input and normalized UTF-8 limit |
| DRM-free EPUB | Case-insensitive `.epub` extension; stored/deflated ZIP entries; up to 512 MiB per archive and 4,096 spine chapters; 2 MiB per chapter document and resulting text |
| Downloaded web novels | App-generated `.nvr` index plus its chapter cache; up to 20,000 chapter entries per novel |
| Illustrations and covers | PNG and JPEG; image bytes and dimensions are bounded before decoding |

EPUB rendering supports chapter order/navigation, headings, emphasis, paragraphs, and inline images. It does not implement full publisher CSS layout, embedded fonts, or text flowing around images. PDF, encrypted EPUB chapter content, and TXT chapter detection are not supported. Glyph availability depends on the bundled, optional user, and firmware fonts. See [language coverage and custom fallback fonts](assets/fonts/README.md). Menus and offline narration remain English; Arabic shaping, bidirectional layout, and complex-script shaping are not implemented.

## Building from source

Use CMake **3.16 or newer**, a C++14-capable toolchain, and [VitaSDK](https://vitasdk.org/) with its library packages. Required SDK libraries
are libvita2d, FreeType, bzip2, libpng with the simplified read API, libjpeg-turbo (JPEG and TurboJPEG), zlib, and pthread support. XML, ZIP, Lua, HTTPS, and speech sources are vendored; configuring and building the project does not fetch them.

From a checkout of this repository, with the SDK installed:

```sh
export VITASDK=/usr/local/vitasdk  # Adjust to your SDK installation.
export PATH="$VITASDK/bin:$PATH"

cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release -DNOVELREADER_PROFILE=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/vita -j2
```

The package is **`build/vita/NovelReader.vpk`**, with title ID **`NVRD00001`** and package version **`00.80`**. The build checks LiveArea artwork and Vita thread linkage before packaging.

Portable tests require a desktop C/C++ toolchain, CMake, and zlib development files:

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

The host target is a test harness, not a desktop reader app. See the [build and development guide](docs/building.md) for SDK setup, IntelliSense, troubleshooting, profiling, and the source layout.

## Documentation

- [Controls](docs/controls.md) — buttons, analog sticks, touch, and modal behavior.
- [Library and online novels](docs/library-and-online.md) — imports, downloads, saves, removal, backups, and source limits.
- [Interface reference](docs/interface.md) — current screens and reading preferences.
- [Offline narration](docs/text-to-speech.md) — Flite SLT usage and implementation.
- [Building and development](docs/building.md) — toolchains, tests, and editor setup.
- [Performance and validation](docs/performance.md) — resource lifetimes, profiling, and hardware checks.
- [Bundled fonts](assets/fonts/README.md) and [third-party notices](third_party/README.md).

## Credits

NovelReader is developed by **basitalitech**, using VitaSDK and vita2d, with FreeType and the Vita system font for text rendering. Thanks to the Vita homebrew community and the upstream contributors to TinyXML-2, minizip/zlib, Lua, curl, Mbed TLS, Flite and the CMU SLT voice, and Mozilla's CA certificates.

Interface fonts are Source Sans 3 by Adobe. Reading fonts are Literata and Atkinson Hyperlegible Next. Dependency licenses and font notices are documented in [third-party notices](third_party/README.md) and [bundled fonts](assets/fonts/README.md), and packaged under `app0:/licenses/` and `app0:/fonts/`. About provides Credits and License summaries. Books and artwork retain their respective copyrights.
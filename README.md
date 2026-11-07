# Novel Reader for PS Vita

A light novel reader built with C++14, VitaSDK, and vita2d. Version 0.7
reads **UTF-8 TXT, DRM-free EPUB, and downloaded web novels** offline, with chapter
navigation, cover previews, library filters, inline illustrations, and a zoomable
illustration viewer, folder browsing, analog-stick navigation, and front touchscreen controls.
Online novel sources are **Novgo, Asura Novels, and Beast Novels**, using packaged
Lua adapters for public catalogs and chapters.

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` or DRM-free `.epub` books into that folder using VitaShell's USB or FTP mode.
4. Press **Triangle** in the library to refresh, then **Cross** to open a book.

The bundled welcome guide is available at the library root under All books.
Books and subfolders are listed in the current folder. File names become
library titles for TXT books, with underscores replaced by spaces. EPUB metadata
is read when scanning the library, so titles and authors are available before
opening a book. Uppercase `.TXT` and `.EPUB` extensions work.

| Screen | Controls |
| --- | --- |
| Library | Up/Down: browse; Cross: open folder/read book; Circle: parent folder; Triangle: refresh |
| Library | Square: cycle Title/Recent/Progress sorting; L/R: cycle All/Reading/Finished filters |
| Library | Select: online sources |
| Reader | Left/Right: turn page; Up/Down: move one text line or illustration |
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

## Online novels

Connect Wi-Fi in Vita Settings, then press **Select** in the library or tap
**Online**. Choose Novgo, Asura Novels, or Beast Novels. Asura uses its **novels**
catalog; comics are not supported.

Browse titles, use **Square / Search** to enter a title with the system keyboard,
then open a novel for its description and chapter list. **Read** fetches a missing
public chapter and saves it before opening the existing reader. Reading downloaded
chapters does not require a connection. Turning to a missing chapter fetches it
on demand; failed requests leave the current chapter and position intact.

| Screen | Controls |
| --- | --- |
| Sources | Up/Down: choose; Cross: browse; Circle: local library |
| Catalog | Up/Down: choose; Left/Right: move six entries; Cross: details |
| Catalog | Square: search; Select: initial catalog; Triangle: next source page, or retry the current page |
| Novel details | Cross: chapters; Square: download all public chapters; Select: resume saved position; Triangle: refresh chapter list |
| Chapters | Up/Down: choose; Left/Right: move six entries; Cross: read; Square: download selected chapter |
| Chapters | Select: download all public chapters; Triangle: refresh; Circle: details |
| Request/download in progress | Circle: cancel; Start: cancel, save, and exit |

Online lists also support analog navigation, tapping rows, swipe paging, and
the bottom touch toolbar. Requests run on one background worker; progress and
cancellation remain available while it works. Downloads are sequential, with a
short pause between chapters. An error stops the batch and retains completed
chapters. Repeat the download action to retry: valid completed chapters are
skipped. Interrupted individual chapters restart from the beginning. Cancellation
during connection setup may wait for the network timeout.
Worker startup failures display the underlying error.

Novel indexes appear in **Library → Online → source** as soon as their chapter
lists have loaded. Open one to select a downloaded chapter while offline. Chapter
text is stored in a hidden `.chapters` directory alongside the indexes; keep that
directory together with the `.nvr` indexes when copying downloads. Novel and chapter
filenames use hashes of their source URLs. Progress and bookmarks track stable
chapter paths, so inserting or reordering chapters during refresh preserves their
identity. A removed bookmarked chapter disables that bookmark. Renaming or moving
the download folders still changes the library save identity.

This phase supports **public/free text chapters**. Locked entries are labeled
and skipped by Download all; account login and purchased chapters are not
implemented. Online chapter formatting is plain UTF-8 text with paragraph breaks;
remote illustrations and covers are not downloaded. The initial catalogs reflect
each site's public listing, and search results may be limited by the source.
Source layout changes, browser verification, and service outages can interrupt
access; errors are shown without replacing a saved chapter with an error page.

HTTPS uses bundled curl and Mbed TLS with certificate and hostname verification,
TLS 1.2 or newer, and a packaged Mozilla CA bundle. Redirects must remain on the
source's allowed HTTPS hosts. The Vita's date/time must be correct. Responses are
limited to 4 MiB, chapter text to 2 MiB, and a novel to 20,000 chapter entries / 400
chapter-list pages. Downloads use temporary files and rename; insufficient space
stops the operation. Desktop live checks verify a public chapter from each source;
Wi-Fi, TLS, the system keyboard, and controls still need Vita hardware testing.

## Analog and touch controls

Both analog sticks navigate menus and the reader: vertical movement selects
items or moves through text/illustrations, while horizontal movement browses
list pages or turns reading pages. Settings use vertical selection and horizontal
adjustment. In the image viewer, either stick pans smoothly; the D-pad still
provides discrete pan steps. Sticks have a dead zone, axis hysteresis, and the
same 350 ms initial / 140 ms repeat timing as directional buttons. After a
screen transition, return the stick to center before moving again.

The **front touchscreen** supports:

| Screen | Touch controls |
| --- | --- |
| Library | Tap a folder, book, or selected preview to open; Up returns to the parent folder; swipe to browse six entries |
| Reader | Swipe left/right for next/previous page; swipe up/down to move through text and images |
| Reader | Tap an inline illustration to zoom it; other left/right body taps turn pages, center taps open contents |
| Reader | Hold and release on the reading area to save a bookmark |
| Contents / illustration list | Tap an entry to open; swipe to browse six entries |
| Settings | Tap a row to select, then use Decrease/Increase and Apply |
| Image viewer | Drag to pan; pinch to zoom around the fingers' midpoint; tap the image to toggle fit/2× zoom |

Every screen has a touch toolbar along the bottom. It provides library sorting,
filters, refresh and exit; reader bookmarks, contents and settings; and image
navigation/zoom controls. Viewer **Back** returns directly to reading when opened
by tapping an inline image, or to the illustration list when opened from there.
Touch gestures act on release outside the image viewer, where drag and pinch
updates are live. Swiping a list never opens the row under the finger. Physical
button/directional actions take priority over an active touch gesture; changing
screens cancels it until the fingers lift. Rear touch is not used.

Reading progress saves on navigation, bookmark changes, and return to the
library. Each book has one bookmark and an independent reading position. EPUB
positions and bookmarks include the chapter and a text offset within it; jumping
to a bookmark in another chapter opens that chapter. Font and spacing changes
preserve the text position rather than a page number. Existing TXT saves from
versions 0.1–0.3 are read automatically. Text and image positions remain compatible
because image anchors use the existing normalized-text offsets.
Night, Paper, and Sepia themes are available. Saves live in
`ux0:data/NovelReader/progress/`; settings live in
`ux0:data/NovelReader/settings.txt`. Renaming a book or moving it to another
folder gives it a new save identity.
Replacing a book's contents or changing its chapter order may invalidate its old
position. Out-of-range saved chapters return to the beginning; out-of-range
bookmarks are disabled.

## Library

- Folders appear first, alphabetically, followed by books in the selected sort
  order. Open a folder with Cross or a tap. Circle or the touch **Up** button
  returns to its parent and selects the folder you just left. At the library
  root, the button reads **Root** and Circle does nothing. The current folder's
  name appears in the heading and its relative path appears above the toolbar.
- Each view lists only its immediate children; nested folders can be browsed
  independently. Reading/Finished filters apply to books in the current folder.
  Folders remain visible under every filter, even if they contain no matching
  books. The guide appears only at the root under All books. Launching the app
  starts at the root; returning from a book keeps you in its containing folder.
- Bookmarks, progress, and cover previews use paths relative to the library
  root. For example, `Series A/Volume 1.epub` and `Series B/Volume 1.epub` have
  independent saves. Existing root-level saves keep their previous identities.
  Moving a book or renaming a folder changes the affected save identities;
  progress is not automatically migrated. Hidden names, links, and unsupported
  file types are omitted. Relative paths are limited to 768 UTF-8 bytes and
  may be further limited by the Vita filesystem.
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
- Metadata scanning is synchronous on launch, folder entry, and refresh, and
  reads only the current folder's books. Only the selected
  cover is decoded, and changing selection releases the previous cover. Broken
  books remain listed with an error so one file does not hide the rest of the
  library. There is no persistent metadata cache yet.

For example, copy a collection with VitaShell using this structure:

```text
ux0:data/NovelReader/books/
  Standalone.epub
  Series A/
    Volume 1.epub
    Volume 2.epub
    Extras/
      Side Story.txt
  Series B/
    Volume 1.epub
```

Empty or unavailable folders can be left using Circle/Up. Refresh rescans the
current folder after files are copied, removed, or renamed. Folder browsing does
not combine separate files into a single book.

## EPUB reading

- Chapters follow the OPF spine's primary reading order (`linear="no"` entries
  are omitted), independently of ZIP entry or filename order.
- The contents menu lists one entry per spine document. Labels come from the
  EPUB 3 navigation document, then the EPUB 2 NCX, with numbered chapter labels
  as a fallback. Opening a chapter can replace its fallback label with a heading.
  Multiple contents links within one document share a single chapter entry;
  fragment-level contents jumps are not implemented.
- Left/Right crosses to the adjacent chapter at a chapter boundary. Left from
  the first text line or image opens the previous chapter's final page. L/R opens the adjacent
  chapter at its beginning. Contents selection of the current chapter keeps its
  reading position.
- Only the current chapter's text is kept in memory; opening a different chapter
  reads and lays out that document on demand. Failed chapter loads keep the
  current chapter and position intact so you can choose another chapter.
- Paragraphs, headings, explicit line breaks, bold, and emphasis are retained.
  Headings use the accent color, bold uses a second glyph pass, and emphasis is
  underlined using the system font. Scripts, styles, and ruby annotations are
  omitted; ruby base text remains. Illustrations render between their surrounding
  paragraphs. Open **Cross → Square** from the reader for the full chapter image
  list and zoom viewer.
- CSS, custom fonts, and exact publisher layouts are not supported. XHTML must
  be well-formed UTF-8 XML. XML/numeric entities and common
  named XHTML entities are supported; external DTDs are never loaded.

## Illustrations

PNG and JPEG illustrations appear automatically in the reading flow, preserving
aspect ratio. Small illustrations share the page with text; images occupying
more than two thirds of the reading area's height get a dedicated page. Images
fit within the reading area and are not enlarged beyond their native dimensions.
Page turns account for image height, so images are never cut across pages. Up/Down
steps through individual text lines and illustrations.

The normalized text retains its original image-marker bytes for compatibility,
but the visual layout replaces each marker with an image. Old positions inside
a marker restore to that illustration; text positions after it keep their original
offsets. Bookmarks on illustrations and font/spacing reflow use these same anchors.
Missing, damaged, or unsupported images become labeled placeholders in the flow;
surrounding text remains readable.

The illustration list follows image occurrence order in the current chapter,
including raster images referenced by SVG `<image>` wrappers. Paths are resolved
relative to the chapter document, including parent directories and URL-encoded
filenames. External images are not fetched.

The viewer supports embedded PNG and JPEG images, fits them to the screen, and
zooms from 1× to 4× the fitted size. Pan is clamped to the image edges; smaller
axes stay centered. The image is clipped to the viewing area, leaving controls
visible. Viewing images does not move the reading position or bookmark. Tap an
inline image to zoom it directly, or use **Cross → Square** to choose an image
from the chapter list. **Circle** or the touch Back button returns to the screen
that opened the viewer.

Each image is limited to 8 MiB encoded, 4,096 pixels per side, and 4 megapixels
total. Headers are checked before GPU allocation; the PNG/JPEG decoder then
validates the full stream. Failed loads keep the current image/reader state and
show an error. Chapter opening probes image dimensions once for layout, which
can add loading time for chapters with many images. Font changes reuse these
dimensions. Only visible inline images are decoded; their textures are reduced
to reading-area sizes and released when they leave the visible page. Thumbnail
creation temporarily decodes one full image within the existing pixel limits.
A cover or viewer texture is released when leaving its screen;
replacing a viewer image temporarily retains the old texture until the new one
has decoded successfully. Full SVG artwork, GIF, WebP, animation, EXIF rotation,
and decoding images above the source size limits are not implemented.

## Build

Set `VITASDK` to your VitaSDK installation. The installed SDK needs vita2d and
its FreeType, PNG (with simplified read API), TurboJPEG, JPEG, and zlib dependencies.
XML, ZIP, Lua, and HTTPS dependencies are vendored;
see [dependency versions and licenses](third_party/README.md). No downloads are
made during configuration or building.

```sh
cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release
cmake --build build/vita -j2
```

The installable package is `build/vita/NovelReader.vpk`, title ID `NVRD00001`.
The Vita build links the complete pthread library and checks the executable's
thread-support symbols before packaging. This prevents unresolved weak probes
from making the C++ runtime reject worker creation on hardware.
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
- Maximum 8 MiB of input and normalized text, and 100,000 display blocks (text
  lines and images) per chapter. Tabs
  expand to four spaces. Oversized layouts are rejected to bound Vita memory use.
- EPUB limits: 512 MiB archive, 20,000 ZIP entries, 4,096 primary chapters,
  512 KiB per metadata/navigation document, and 2 MiB per XHTML document and
  resulting chapter text. Formatting is capped at 100,000 style spans. ZIP
  stored/deflated entries are accepted and their checksums verified. Encrypted
  chapter content is unsupported; font obfuscation does not block text reading.
- Horizontal text; glyph coverage follows the Vita system font. No PDF, TXT
  chapter detection or search yet. Images flow between text
  paragraphs; publisher CSS positioning and text wrapping around images are
  not supported.
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
- [Library logic](src/library.cpp): folder path validation, folders-first
  sorting/filtering, and saved progress estimates.
- [Mixed text/image layout](src/layout.cpp): variable-height pagination,
  dedicated illustration pages, and compatible text/image anchors.
- [Portable input logic](src/controls.cpp): analog dead zones/repeats, touch
  coordinate scaling, gesture recognition, and touch hit testing. The Vita
  frontend polls front touch through `SceTouch_stub` and enables analog sampling.
- [Image validation](src/image_info.cpp) and [Vita decoder](src/vita_images.cpp):
  bounded image headers and PNG/TurboJPEG decoding into GPU textures.
- [Core tests](tests/reader_tests.cpp): malformed text, wrapping, reflow anchors,
  save replacement, corrupt state, and file size limits.
- [EPUB tests](tests/epub_tests.cpp): generated stored/deflated ZIP fixtures,
  spine order, navigation labels, formatting, legacy saves, lazy chapter reads,
  corrupt ZIP/XML, encrypted chapters, path handling, and expansion limits.
- [Library and image tests](tests/library_tests.cpp): filters, sorting, progress
  estimates, preference/save migration, folder boundaries, duplicate-name saves,
  image dimensions, and malformed headers.
  Desktop tests cover transport and headers; actual GPU image decoding and
  zoom/pan controls need Vita hardware testing.
- [Inline layout tests](tests/layout_tests.cpp): mixed pages, image sizing,
  dedicated pages, unavailable images, image-only chapters, page navigation,
  UTF-8 text, bookmark anchors, and font reflow.
- [Input tests](tests/controls_tests.cpp): stick drift/hysteresis, repeat timing,
  touch bounds, taps, swipes, long presses, reordered pinch reports, drag,
  multitouch cancellation, and screen-transition suppression. Hardware testing
  is still needed to tune gesture feel and confirm touch calibration.

Next local milestones: a metadata/thumbnail cache for larger libraries, more
image formats/downsampling, and search.
Online source adapters live in [assets/sources](assets/sources). They expose
catalog/search, novel/chapter-list, and chapter-text operations through bounded
Lua states with HTTP and HTML helpers; filesystem, OS, package loading, and debug
libraries are unavailable. Additions must stay within the C++ source host allowlist.
[Online service](src/online_service.cpp) owns the network worker and sequential
download jobs. [Cache storage](src/online_store.cpp) owns downloaded publications
and stable chapter identities. [Online tests](tests/online_tests.cpp) cover source
fixtures, malformed HTML/JSON, host restrictions, interrupted downloads, retry,
locked chapters, offline reopening, and progress remapping.

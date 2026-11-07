# Novel Reader for PS Vita

A light novel reader built with C++14, VitaSDK, and vita2d. Version 0.7
reads **UTF-8 TXT, DRM-free EPUB, and downloaded web novels** offline, with chapter
navigation, cover previews, library filters, inline illustrations, and a zoomable
illustration viewer, local book imports, analog-stick navigation, and front touchscreen controls.
Online novel sources are **Novgo, Asura Novels, and Beast Novels**, using packaged
Lua adapters for public catalogs and chapters.

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` or DRM-free `.epub` books into that folder using VitaShell's USB or FTP mode.
4. Tap **Add Book** on Home (or Start → Import books in Library), browse to a book, and press **Cross** to import it.
5. Open the imported book's details from Library with **Cross**, then press **Cross** to read.

Library is a saved collection of imported local books and favorite online novels.
The bundled welcome guide is always available under All books. Import reads TXT
file names and EPUB metadata; uppercase `.TXT` and `.EPUB` extensions work.

| Screen | Controls |
| --- | --- |
| Home / Library | D-pad/sticks: browse sidebar or cover grid; Cross: details, then read; Start: options |
| Library | Square: choose Title/Recent/Progress sorting; L/R: switch All/Reading/Finished filters |
| Library | Select: online sources |
| Import | Cross: open folder/import book; Circle: parent folder or Library at root; Refresh button or Triangle → Refresh folder |
| Reader | Left/Right: turn page; Up/Down: move one text line or illustration |
| EPUB reader | L/R: previous/next chapter; Cross → Contents |
| TXT reader | L/R: turn page |
| Contents | Up/Down: choose chapter; Left/Right: browse six entries; Cross: open; Circle: return |
| Contents | Square: illustrations in the current reading chapter |
| Contents | Jump button or Triangle → Jump to chapter: enter a number and focus that entry |
| Illustrations | Up/Down: choose; Cross: view; Circle: contents |
| Image viewer | Cross: zoom in; Square: zoom out; D-pad: pan; Fit button or Triangle → Fit image |
| Image viewer | L/R: previous/next image in chapter; Circle: illustration list |
| Reader | Cross → Save bookmark (or hold page); Select: jump to bookmark |
| Reader | Cross: menu; Triangle: Bookmarks; Square: font settings; Circle: library |
| Settings | Up/Down: select; Left/Right: adjust; Circle/Cross/Triangle: apply |
| All screens | Start: options; Options → Save and exit |
| Other content screens | Triangle: options/control panel; L+R is an optional shortcut |
| Control panels | D-pad or either stick: choose a large tile; Cross: confirm; Circle: close |

## Interface

The native 960 × 544 interface follows the supplied screen designs: charcoal and
violet surfaces, lavender focus outlines and progress bars, rounded cover cards,
white line icons, and a slim controller legend. A live system clock, Wi-Fi state,
and battery indicator sit above the application bar. The physical Vita housing
in the reference images is not drawn inside the application.
The Wi-Fi indicator stays neutral until an online request initializes the network
module and NetCtl. Offline startup does not call uninitialized networking APIs.

**Home** opens on a moonlit reading scene, with Continue Reading, a four-cover
library shelf, and Add Book. Its sidebar opens Home, Library, Continue,
Bookmarks, Settings, and About. Left from the first Library column focuses the
sidebar; Up/Down selects a destination and Cross or Right enters its content.
Touching a sidebar item opens it directly.

**Library** shows eight covers per page in a four-column grid. D-pad or either
stick moves between covers; swiping pages through the grid. Cross or a cover tap
opens book details; Cross again starts or resumes reading. Search uses the system
keyboard (submit an empty search to clear it). The filter icon or Square opens
Title/Recent/Progress sorting; L/R switches All/Reading/Finished. Import and Online
remain available through Start → Options. Home's Add Book opens import directly.
Only visible covers are decoded, as bounded 144 × 200 thumbnails; missing covers
use title cards. Book details show the cover, author, format, chapters, last-read
time, and actual saved progress.
The bookmark icon saves the book's current position. The remove icon asks for
confirmation, removes only the library reference, and keeps the original file
and saved progress. The bundled guide cannot be removed.

**Reader** starts with the title bar, chapter/page information, purple progress
bar, and bottom action legend. Existing saved reading-view preferences are kept.
Cross opens Reading controls, Triangle opens Bookmarks, Square opens Reading
Settings, and Circle returns to Library (or online chapters). The control panel
contains Contents, settings, Save bookmark, Go to bookmark, and Show/Hide reading
bar. Select still jumps directly to the saved bookmark. Hold and release on the
reading area to save one. Inline illustrations still open their viewer. Control
panels overlay the page without reflowing; changing the reading view or font
reflows around the same text/image anchor.

**Table of Contents** shows the cover beside six chapter rows. Up/Down selects;
Left/Right browses six entries. Jump uses the numeric system keyboard. The
illustration list, downloads, online browsing, and import browser retain their
four-row lists with the shared purple surfaces and controller footer.

**Bookmarks** collects the saved bookmark from each library book, showing its
passage when the local chapter is available, its title, and chapter. Cross/tap
opens that position; Square removes it. There is still one bookmark per book.
Online bookmark jumps use the existing chapter download/retry flow.

**Reading Settings** is available from Home or Reader, with font, size and spacing
sliders, Night/Paper/Sepia swatches, reading view, and a live sample. Left/Right
adjusts the selected row; touch controls adjust the corresponding value. Cross or
Circle applies and returns to the previous screen. Select resets reading values.
About shows the application version, credits, and licensing information. Start
opens Options everywhere; **Options → Save and exit** closes the application.

Brief confirmations disappear automatically; errors remain visible until another
action replaces them. The previous [layout reference](docs/native-ui-preview.html)
documents the older neutral interface and is not a preview of this redesign.
The native layout and controls still need visual testing on Vita hardware.

## Online novels

Connect Wi-Fi in Vita Settings, then press **Select** in Library or choose
**Start → Online novels**. Choose Novgo, Asura Novels, or Beast Novels. Asura uses its **novels**
catalog; comics are not supported.

Browse titles, use **Square / Search** to enter a title with the system keyboard,
then open a novel for its description and chapter list. **Read** fetches a missing
public chapter and saves it before opening the existing reader. Reading downloaded
chapters does not require a connection. Turning to a missing chapter fetches it
on demand; failed requests leave the current chapter and position intact.

| Screen | Controls |
| --- | --- |
| Sources | Up/Down: choose; Cross: browse; Circle: local library |
| Catalog | Up/Down: choose; Left/Right: move four entries; Cross: details |
| Catalog | Square: search; Select: initial catalog; Next/Reload button or Triangle → Next/Reload catalog |
| Novel details | Cross: chapters; Square: save in library; Select: start/resume reading; Refresh chapters button |
| Chapters | Up/Down: choose; Left/Right: move four entries; Cross: read; Square: download selected chapter |
| Chapters | Select or All button: download all public chapters; Triangle → Refresh chapters; Circle: details |
| Request/download in progress | Circle: cancel |

Online lists also support analog navigation, tapping rows, swipe paging, and
the bottom touch toolbar. Requests run on one background worker; progress and
cancellation remain available while it works. Downloads are sequential, with a
short pause between chapters. An error stops the batch and retains completed
chapters. Repeat the download action to retry: valid completed chapters are
skipped. Interrupted individual chapters restart from the beginning. Cancellation
during connection setup may wait for the network timeout.
Worker startup failures display the underlying error.
Novel details include a primary **Start reading / Resume** action, saved chapter,
library membership, and offline chapter count. Online favorites open this details
screen from Library. Chapter rows show **Offline**, **Locked**, or a download hint.
Requests show an activity indicator; downloads show a progress bar and completed
count. Failed requests expose a direct **Retry** button and **Options → Retry request**,
preserving the original operation and completed downloads.

On novel details, press **Square**, tap **Save**, or choose **Options → Add to library** to save a favorite. Browsing
a novel or downloading chapters does not automatically add it to Library. Added
novels appear alongside imported local books without source folders. Open one
to resume or select a chapter; downloaded chapters are available offline. Chapter
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
items or moves through text/illustrations. Horizontal movement traverses the Library
cover grid, browses list pages, or turns reading pages. Settings use vertical selection and horizontal
adjustment. In the image viewer, either stick pans smoothly; the D-pad still
provides discrete pan steps. Sticks have a dead zone, axis hysteresis, and the
same 350 ms initial / 140 ms repeat timing as directional buttons. After a
screen transition, return the stick to center before moving again.

The **front touchscreen** supports:

| Screen | Touch controls |
| --- | --- |
| Home / Library | Tap a cover for details, Continue Reading to resume, or Add Book to import; swipe Library for eight covers |
| Reader | Swipe left/right for next/previous page; swipe up/down to move through text and images |
| Reader | Tap an illustration to zoom; left/right body taps turn pages, center taps open Reading controls |
| Reader | Hold and release on the reading area to save a bookmark |
| Contents / illustration list | Tap an entry to open; swipe to browse six chapters or four illustrations |
| Settings | Tap rows, sliders, arrows, or theme swatches; Apply returns to the previous screen |
| Image viewer | Drag to pan; pinch to zoom around the fingers' midpoint; tap the image to toggle fit/2× zoom |

Screens have large touch action bars along the bottom, hidden during immersive
reading. Start or visible Options controls open control panels. Viewer **Back** returns directly to reading when opened
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

- Library shows a flat, persistent collection of imported local books and online
  favorites. Membership is saved in `ux0:data/NovelReader/library.txt`. Cached
  online indexes alone do not make a novel a favorite. Existing collections start
  with the guide; import previously copied books or cached indexes to add them.
- **Add Book** on Home (or Start → Import books in Library) opens a file browser rooted at
  `ux0:data/NovelReader/books/`. Browse subfolders with Cross and Circle, then
  select a TXT, EPUB, or saved online index with Cross. Import adds a reference
  and returns to All books. Files stay in place; repeated imports do not duplicate
  entries. Circle at the import root returns to Library. Refresh updates the
  import browser after copying files with VitaShell.
- **Select / Online** opens supported internet sources. Search or browse, open
  novel details, and press **Square** or tap **Save**. This saves the favorite
  without downloading every chapter. Use the chapter screen to download one or
  all public chapters. Adding favorites requires a successfully loaded index.
- Imported books retain their relative paths for progress and bookmarks, including
  existing saves. Same-named files in different folders remain distinct. Moving or
  renaming a file changes its identity; restore missing files to their imported
  locations. Import references files rather than copying them.
- Eight covers appear per Library page; opening a cover shows author, chapters, and progress.
  Only visible covers are decoded as thumbnails. Broken or missing files stay visible with
  an error. Metadata is read on launch and return from online browsing or import;
  there is no persistent metadata cache yet.
- Square opens a Title/Recent/Progress chooser; L/R switches All/Reading/Finished.
  Preferences survive restarts. Title sorting ignores ASCII case. Recent uses
  last-read time; Progress puts highest percentages first. The guide appears last
  under All and is omitted from Reading/Finished.
- Reading includes started books; Finished means the final page was displayed.
  Returning to an earlier page changes this status. EPUB percentages weight
  chapters equally; TXT progress uses text offsets. Legacy saves show Continue
  reading until reopened.
- Keep the library index, books, and hidden online chapter folders together when
  backing up the collection. A damaged index is reported and is not overwritten
  by an add operation.

## Fonts

Interface text uses **Source Sans 3 Regular**, with **Semibold** screen headings.
Reading defaults to **Literata** (serif). In **Reader → Square → Font**,
choose Literata, **Atkinson Hyperlegible Next** (sans-serif), or **Vita system**.
The selection is saved across restarts. Older settings retain their size, spacing,
theme, and library preferences and default to Literata.

Font selection, size, and spacing changes reflow the chapter while preserving its
text position and bookmarks. Settings preview uses the selected reading font;
menus keep their interface font. Missing glyphs fall back to the Vita system PGF
font for both measurement and drawing. If a bundled font cannot load, the app
shows a notice and uses PGF instead. System glyph coverage still limits fallback.

The package includes static TrueType fonts and their SIL Open Font License 1.1
notices; see [font sources and licenses](assets/fonts/README.md). No font downloads
are needed at build time or while using the app. Bold retains a second glyph pass;
emphasis is underlined. Publisher-embedded fonts and complex-script shaping are
not implemented. Visual rendering and glyph fallback need Vita hardware testing.

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
  underlined using the selected reading font. Scripts, styles, and ruby annotations are
  omitted; ruby base text remains. Illustrations render between their surrounding
  paragraphs. Open **Cross → Contents → Square** from the reader for the full chapter image
  list and zoom viewer.
- CSS, publisher-embedded fonts, and exact publisher layouts are not supported. XHTML must
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
inline image to zoom it directly, or use **Cross → Contents → Square** to choose an image
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
its FreeType, bzip2, PNG (with simplified read API), TurboJPEG, JPEG, and zlib dependencies.
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
- Horizontal text; glyph coverage follows the selected font and system fallback. No PDF, TXT
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
- [Vita frontend](src/main.cpp): saved library, local import browser, interface and reading fonts,
  library controls, image viewing, controller navigation, and reader settings.
- [Library logic](src/library.cpp): persistent collection membership, folder path validation,
  sorting/filtering, and saved progress estimates.
- [Mixed text/image layout](src/layout.cpp): variable-height pagination,
  dedicated illustration pages, and compatible text/image anchors.
- [Portable input logic](src/controls.cpp): analog dead zones/repeats, touch
  coordinate scaling, gesture recognition, and touch hit testing. The Vita
  frontend polls front touch through `SceTouch_stub` and enables analog sampling.
- [Interface rules](include/ui.hpp): reading-area geometry, recent-book selection,
  shared menu/library hit regions, and validated chapter-number navigation.
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
The [Vita network startup tests](tests/vita_network_tests.cpp) compile the Vita
service path against mocked SDK calls to verify status polling before initialization,
after initialization failures, and during connection changes.

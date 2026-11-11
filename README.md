# NovelReader for PS Vita

A light novel reader built with C++14, VitaSDK, and vita2d. Version 0.7
reads **UTF-8 TXT, DRM-free EPUB, and downloaded web novels** offline, with chapter
navigation, cover previews, library filters, inline illustrations, and a zoomable
illustration viewer, local book imports, analog-stick navigation, and front touchscreen controls.
Online novel sources are **Novgo, Asura Novels, and Beast Novels**, using packaged
Lua adapters for public catalogs and chapters.

**Offline read aloud:** open a book, press START, choose **Read aloud**, then
**Start here**. The bundled **Flite SLT English voice** reads from the current text
position through the end of the chapter. The same panel offers pause/resume,
stop, and a saved speed setting from 100 to 350 words per minute. No connection,
account, or additional voice download is needed. See [speech details](docs/text-to-speech.md).

## Install and read

1. Install `NovelReader.vpk` with VitaShell on a homebrew-enabled Vita.
2. Launch the app once to create `ux0:data/NovelReader/books/`.
3. Copy UTF-8 `.txt` or DRM-free `.epub` books into that folder using VitaShell's USB or FTP mode.
4. Tap **Add Book** in Library (or Start → Import books in Library), browse to a book, and press **Cross** to import it.
5. Open the imported book's details from Library with **Cross**, then press **Cross** to read.

Library is a saved collection of imported local books and favorite online novels.
The bundled welcome guide is always available under All books. Import reads TXT
file names and EPUB metadata; uppercase `.TXT` and `.EPUB` extensions work.

| Screen | Controls |
| --- | --- |
| Home / Library | D-pad/sticks: move focus within content; Cross: activate; Start: options |
| Home / Library / sidebar screens | L/R: previous/next sidebar destination |
| Library | Triangle: sorting/filtering; Square: search; Circle clears an active search before returning Home |
| Library | Add Book → Browse Online, or Options → Online novels |
| Import | Cross: open folder/import book; Circle: parent folder or Library at root; Options → Refresh folder |
| Reader | Left/Right: turn page; Up/Down: move one text line or illustration |
| Reader illustration | Right stick up/down: zoom in/out; left stick: pan; Triangle: return to fit |
| EPUB reader | L/R: previous/next chapter; Cross → Contents |
| TXT reader | L/R: turn page |
| Contents | Up/Down: choose chapter across pages; Left/Right: browse five entries; Cross: open; Circle: return |
| Contents | Square: illustrations in the current reading chapter |
| Contents | Options → Jump to chapter: enter a number and focus that entry |
| Illustrations | Up/Down: choose; Cross: view; Circle: contents |
| Image viewer | Cross: zoom in; Square: zoom out; D-pad: pan; Triangle: fit image |
| Image viewer | Left stick: smooth pan; right stick up/down: smooth zoom |
| Image viewer | L/R: previous/next image in chapter; Circle: illustration list |
| Reader | START → Save bookmark (or hold page); Select: jump to bookmark |
| Reader | Cross: Contents; Triangle: Bookmarks; Square: font settings; Circle: library/online chapters |
| Settings | D-pad: select rows; Left/Right: adjust; Cross: apply; Circle: cancel; Select: reset |
| All screens | Start: options; Options → Save and exit |
| Contextual Triangle | Import: refresh folder; Contents: jump; Catalog: next/reload; online details/chapters: refresh |
| Popup menus | Up/Down or either stick: choose; Cross: confirm; Circle: close/back; START: close |

## Interface

The native 960 × 544 interface follows the supplied screen designs: charcoal and
violet surfaces, lavender focus outlines and progress bars, rounded cover cards,
refined white line icons, text-only NovelReader branding, and a slim controller
legend using native Vita button geometry. The supplied [logo](assets/ui/logo.png)
appears on the About page and as the app icon (using a resized 128 × 128 copy).
The physical Vita housing
in the reference images is not drawn inside the application.
Wi-Fi is used only when an online request initializes the network module and
NetCtl. Offline startup does not call uninitialized networking APIs.

The status strip containing Wi-Fi, clock, and battery indicators is intentionally
omitted. Home, Library, Bookmarks, Settings, and About place NovelReader at the
top of the sidebar and use the full content height. Other screens retain the
top application bar; immersive reading continues to hide it.

Startup shows the full-screen moonlit background, the app logo, **NovelReader**,
and **by basitalitech**, with a purple progress bar beneath and the current task
below it. Startup loads library metadata, prepares the first Library grid's cover
thumbnails and the first three bookmark passages, and renders Home, Library,
Bookmarks, Settings, and About offscreen to prepare their fonts and text layouts
before Home appears. These hidden frames are never displayed. Large libraries
update the bar and a completed/total book count during metadata loading; startup
adds no timed delay. Later list pages and book/chapter contents load as needed.


Collection, header, and modal buttons share a focus registry with touch hit
regions. D-pad moves focus only within content or the active modal; Cross
activates, and Circle returns or closes the current modal. The sidebar is
controlled by L/R triggers or touch. The bottom bar is a shortcut legend whose
visible hints also accept touch; it has no D-pad focus.
Disabled actions are skipped. Focus/page/query
state is restored when returning from details, Settings, import cancellation, or
online details. Sidebar selection is visually distinct from controller focus.
Buttons use subtle purple vertical gradients, rounded highlights and borders,
soft raised shadows, and brighter rounded focus outlines. Press feedback darkens
the face and reduces its shadow; disabled controls use muted translucent purple.
The bottom hint bar occupies the last 48 pixels (y=496–543), with a translucent
purple-black gradient and a fine separator. Native C++ geometry in
[vita_ui.cpp](src/vita_ui.cpp) draws 30-pixel dark circular backgrounds,
white symbols, rounded strokes, and optically balanced face buttons. START and
SELECT use matching dark capsules; L and R use dark rounded shoulder-button
badges. All four have white lettering; labels use the loaded interface font. Disabled and pressed
hints retain their state tints. No generated image or Python runtime is needed.
Open and Back stay on the left, with START / Options aligned to the right.
Screen-specific actions and disabled hints are retained. Immersive reading
continues to hide the footer.

Footer actions and labels are defined by `App::toolbar()` in [main.cpp](src/main.cpp).
Change or add an `Action` there to reuse the footer rendering. Each footer action
must name a physical button and have a matching input handler; footer hints
are never registered as focusable controls. Touch uses the same actions, with
hit regions around the visible icon and label; gaps remain inactive.
The reusable [controller hint component](include/vita_hints.hpp) accepts the
loaded `VitaFonts`, a `ControllerButton` (Cross, Circle, Square, Triangle, Start,
Select, L, R, or None), label, left position, vertical center, and available width.
It draws START/SELECT/L/R as ASCII text inside the button backgrounds, without relying
on symbol glyphs. The component clips long labels to the supplied width.
In Settings, Left/Right adjusts the selected value; Up from Typeface stays on
that row. Cross applies, Circle cancels, and Select resets the preview.
Reading Settings groups its five rows in one charcoal panel with subtle dividers
and a lavender outline around the focused row. Font size and line spacing use
three-step sliders. Page theme uses a text selector for Night, Paper, and Sepia,
without colored boxes. Touch a slider position or the selector arrows to adjust.
Typeface, Page theme, and Page effect use left/right selectors. Page effect offers **Page Curl**,
**Slide and Fade**, and **None**; Apply saves the choice, Cancel discards changes,
and Reset restores Slide and Fade. Existing saves also default to Slide and Fade.
The adjacent live page preview reflects
the typeface, size, spacing, and theme, without reading bars. Reading view is
controlled from the reader's Show/Hide reading bar action. Contextual help below
the panel describes the focused setting.

**About** centers the logo, NovelReader name, **by basitalitech**, version, and
description in the content area. A disabled **Check for Updates** placeholder
sits above the Credits and License buttons; update checking is not connected yet.

**Home** opens on a moonlit reading scene, with one large Continue Reading card.
The narrower, dark translucent card leaves more of the artwork visible, with a
white circled play icon, a larger cover, up to two title lines, and chapter/progress
information along the bottom.
Continue Reading includes imported TXT books (including uppercase `.TXT`) and
the bundled welcome TXT after it has been opened. Legacy saves with a reading
position but no last-read timestamp are eligible too; timestamped reading history
takes priority when both are available.
When there is no recent book, the card opens Library. Add Book lives in Library. Its sidebar opens Home, Library,
Bookmarks, Settings, and About. L/R switches directly to the previous/next
destination and leaves controller focus in its content.

**Library** shows four columns of larger cover cards. D-pad or either
stick moves focus through the action row and covers; Up/Down or vertical swipes
scroll between grid rows. Cross or a cover tap
opens book details; Cross again starts or resumes reading. Add Book is above the
covers; Search (Square) and Filter are in the footer, and Online is available through
Add Book → Browse Online or Options.
Search or Square uses the system keyboard (submit an empty search to clear it).
Circle exits and clears an active search while preserving sorting and filtering.
Triangle or the Filter hint opens Title/Recent/Progress sorting and All/Reading/Finished filtering. L/R cycles
through the sidebar destinations. Import and Online
remain available through Start → Options. Library's Add Book opens import directly.
Library cards have taller artwork, two title lines with an ellipsis for overflow,
and a separate progress line with extra spacing. Only visible covers are decoded, as
bounded 144 × 200 thumbnails; missing covers show plain title text centered
horizontally and vertically. Local and online book details share a three-column layout with
a larger 240 × 360 cover near the top, title and author beside it, and saved
progress, chapters, and last-read time on the right. The reading CTA has no icon;
local format appears once in the right panel. Local details offer a Chapters
button that opens the list without changing reading progress; Back returns to
details until a chapter is selected. Online details retain description
scrolling, Chapters, Download all, Refresh chapters, and Save.
Options → Save bookmark saves the book's current position. Options → Remove from library asks for
confirmation and deletes saved progress and bookmarks. Original local TXT/EPUB
files stay in place. The bundled guide cannot be removed.
Saved online novels also offer Remove from library in their details and chapter
list Options menus; their downloaded chapters, cached artwork, novel index, and
progress are deleted. Download cleanup runs on the existing background worker.

**Reader** starts with the title bar, chapter/page information, purple progress
bar, and bottom shortcut legend. Existing saved reading-view preferences are kept.
Tap the center of the reading area to hide or show all reading bars; this keeps
the current text/image anchor and saves the view preference. Center taps do not
open Reading controls. Page turns use the effect chosen in Reading Settings for
buttons, edge taps, and horizontal swipes, including chapter boundaries. Page Curl
rolls a shaded, curved fold across the **entire 960 × 544 screen**, including the
margins and visible reading bars. Forward turns lift the bottom-right corner;
backward turns lift the bottom-left. The sheet bends around a diagonal rounded
crease, with a full folded reverse side, faint mirrored print, and soft cast
shadows that progressively reveal the incoming page. The existing page colors,
reverse-side translucency, and eased 0.65-second timing are retained.
Slide and Fade uses a brief eased transition;
None turns immediately. Horizontal drags follow the finger, including its height
for the curl's bend. Release after dragging about a quarter of the available width
to turn; shorter drags settle back without changing progress. The next page is
previewed within a chapter; an adjacent chapter loads when the drag is released.
Vertical drags move text with the finger and settle onto a line on release.
Up/Down line movement also eases between positions, including when reversing
direction. Toggling reading bars crossfades the old and new views over 0.24 seconds
while keeping the reading anchor. These transitions return to idle once complete.
The curl reuses one outgoing-page render texture. Its fixed-size geometry uses
24 curved bands and five GPU batches, with no per-frame heap allocation or book
loading in the fold renderer. Texture allocation failure falls back to Slide and
Fade; frame-pool exhaustion uses a bounded whole-page fade. See the
[curl performance notes](docs/performance.md#full-screen-corner-curl).
START opens Reading controls, Cross opens Contents, Triangle opens Bookmarks, Square opens Reading
Settings, and Circle returns to Library (or online chapters). The control panel contains Contents, Illustrations, Reading Settings, Save bookmark,
Go to bookmark, All bookmarks, and Show/Hide reading bar. Select still jumps directly to the saved bookmark. Hold and release on the
reading area to save one. Inline illustrations expand into inspection within the reader. Control
panels overlay the page without reflowing; changing the reading view or font
reflows around the same text/image anchor.

Reading controls also include **Read aloud**. Narration follows each spoken
sentence on screen, skips illustration markers, and uses the existing reading
progress when saving or leaving the reader. Opening another chapter or leaving
the reading screen stops narration. English is the initial supported voice;
narration stops at chapter end, and speed changes take effect at the next sentence.

Narration playback uses alternating device buffers and a short audio queue.
Playback starts after about 0.25 seconds of audio is ready, allowing synthesis
to run ahead and cover short delays while the reader renders. Pause retains the
queued speech; Stop discards it. SLT uses a dedicated 512 KiB synthesis stack
and stays registered between sessions. At chapter end the reader drains playback,
clears borrowed callback pointers, and keeps the book open for reading or replay.
Speed settings are approximate words per minute; voice pace varies with text.

**Chapters / Table of Contents** shows the same large cover as book details beside
five chapter rows, with a heading and total count. A play icon marks the current
chapter; ticks mark chapters before the saved position (including the final one
when finished). Separate icons show offline, available-to-download, and locked
chapters. The total chapter count and page number appear below the list. Up/Down selects;
Left/Right browses five entries; Up/Down also crosses page boundaries. Jump uses the numeric system keyboard. The
illustration list, downloads, online browsing, and import browser retain their
four-row lists with the shared purple surfaces and controller footer.

**Bookmarks** collects the saved bookmark from each library book, showing its
passage when the local chapter is available, its title, and chapter. Cross/tap
opens that position. Larger filled bookmark icons identify each passage.
START Options contains only Open and Remove; Remove opens a confirmation submenu
that defaults to Keep. Square has no delete action. There is still one bookmark per book.
Online bookmark jumps use the existing chapter download/retry flow.

**Reading Settings** is available from Home or Reader, with font, size and spacing
controls, Night/Paper/Sepia choices, reading view, and a live sample. Left/Right
adjusts the selected row; touch controls adjust the corresponding value. Apply saves changes and returns to the previous screen; Cancel or Circle discards
them. Select or Reset restores reading defaults in the preview until Apply.
About shows the application version, credits, and licensing information. Start
opens Options everywhere; **Options → Save and exit** on other screens closes the application.

Brief confirmations disappear automatically; errors use a modal with focusable
Retry (when available) and Dismiss actions. The previous [layout reference](docs/native-ui-preview.html)
documents the older neutral interface and is not a preview of this redesign.
The [native-size layout reference](docs/ux-ui-preview.html) covers Home, Library,
details, Settings, and Reading controls. The native layout and controls still need
visual testing on Vita hardware.

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
| Catalog | Square: search; Select: initial catalog; Options → Next/Reload catalog |
| Novel details | Cross (Select): focused Read/Chapters/Download all/Refresh; Square (Add to Library): save a favorite; Select: start/resume reading; Up/Down scrolls the focused description |
| Chapters | Up/Down: choose; Left/Right: move five entries; Cross: read; Square: download selected chapter |
| Chapters | Select or Options → Download all: download public chapters; Options → Refresh chapters; Circle: details |
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

Loading and error dialogs use a centered 16:9 landscape popup, with Cancel, Retry, or
Dismiss actions along its bottom edge. Online cover artwork is fetched on the
background worker and cached for offline Library thumbnails. For favorites saved
before artwork support, open the novel and use **Refresh chapters** once while
connected. Missing or unsupported artwork keeps the existing title-card fallback.

On novel details, press **Square**, tap **Add to Library**, or choose **Options → Add to library** to save a favorite. Browsing
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
remote inline illustrations are not downloaded; supported PNG/JPEG covers are
cached for offline artwork. The initial catalogs reflect
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

Both analog sticks navigate menus: vertical movement selects items. Horizontal
movement traverses a Library cover row or browses list pages. Settings use vertical
selection and horizontal adjustment. In the reader, sticks are reserved for
illustrations: push the right stick up to inspect the largest visible image,
then up/down to zoom and use the left stick to pan. D-pad and triggers keep their
normal reading actions, including while zoomed. In the separate image viewer,
the left stick pans smoothly and the right stick up/down zooms; the D-pad still
provides discrete pan steps. Sticks have a dead zone, axis hysteresis, and menu navigation uses the
same 350 ms initial / 140 ms repeat timing as directional buttons. After a
screen transition, return the stick to center before moving again.

The **front touchscreen** supports:

| Screen | Touch controls |
| --- | --- |
| Home / Library | Tap Continue Reading to resume, a cover for details, or Library's Add Book to import; swipe Library vertically to scroll |
| Reader | Drag left/right to preview a page turn, then release to complete or cancel; drag up/down to scroll text and images |
| Reader | Tap an illustration to inspect; left/right body taps turn pages, center taps hide/show reading bars |
| Reader illustration inspection | Drag/pinch to pan/zoom; tap to return to the reading page |
| Reader | Hold and release on the reading area to save a bookmark |
| Contents / illustration list | Tap an entry to open; swipe to browse five chapters or four illustrations |
| Settings | Tap the left/right halves of rows to adjust; tap Apply, Cancel, or Reset in the footer |
| Image viewer | Drag to pan; pinch to zoom around the fingers' midpoint; tap the image to toggle fit/2× zoom |

The visible bottom shortcuts and sidebar destinations respond to touch and stay
outside D-pad focus. L/R switches sidebar destinations once per press.
START toggles a compact bottom-right popup. Options and Triangle's Filter/Sort
menu share vertical rows with button and touch support; active sort/filter
choices show checkmarks. Tap a row to select it, or tap outside to dismiss
without activating the underlying screen. Circle returns from a submenu.
The Reader strip is hidden in immersive mode. Viewer **Back** returns directly to reading when opened
by tapping an inline image, or to the illustration list when opened from there.
Reader page turns and vertical scrolling track the finger live; image viewer drag
and pinch updates are live too. Other touch actions occur on release. Swiping a
list never opens the row under the finger. Physical
button/directional holds take priority over an active touch gesture; changing
screens or dialog layers suppresses held buttons and gestures until released.
Directions repeat after 350 ms, then every 140 ms; confirm/cancel and sidebar
switching do not repeat. A new press is immediate after release. Rear touch is not used.

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
- **Add Book** in Library (or Start → Import books in Library) opens a file browser rooted at
  `ux0:data/NovelReader/books/`. Browse subfolders with Cross and Circle, then
  select a TXT, EPUB, or saved online index with Cross. Import adds a reference
  and returns to All books. Files stay in place; repeated imports do not duplicate
  entries. Circle at the import root returns to Library. Refresh updates the
  import browser after copying files with VitaShell.
- **Add Book → Browse Online** or **Options → Online novels** opens supported sources. Search or browse, open
  novel details, and press **Square** or tap **Add to Library**. This saves the favorite
  without downloading every chapter. Use the chapter screen to download one or
  all public chapters. Adding favorites requires a successfully loaded index.
- Imported books retain their relative paths for progress and bookmarks, including
  existing saves. Same-named files in different folders remain distinct. Moving or
  renaming a file changes its identity; restore missing files to their imported
  locations. Import references files rather than copying them.
- Four covers appear per Library page; opening a cover shows author, chapters, and progress.
  Only visible covers are decoded as thumbnails. Broken or missing files stay visible with
  an error. Metadata stays in memory across screen visits. Import adds only the
  selected entry, online updates refresh the affected novel, and removing a book
  preserves the other entries. Import → Options → Refresh folder rereads the
  listed files after external replacements; restart also reloads metadata.
- Triangle opens a Title/Recent/Progress and All/Reading/Finished chooser; Square searches. L/R
  navigates to the previous or next sidebar destination.
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

The interface uses **Source Sans 3 Regular** and **Semibold**. Reader typefaces are
**Literata**, **Atkinson Hyperlegible Next**, and the Vita system font. Reading
Settings previews the selected family, size, spacing, and page theme. Apply reflows
the chapter around the saved text/image anchor; Cancel restores prior settings.
Font preferences and bookmarks survive restart. Existing saved typeface identifiers
remain compatible. Bold publication text uses a second pass in the selected reader
family; emphasis is underlined.

Missing glyphs fall back to the Vita system PGF font for both measurement and
drawing. If a bundled font cannot load, the app shows a notice and uses PGF instead.
The package includes the static TrueType fonts and SIL Open Font License 1.1
notice; see [font sources and licenses](assets/fonts/README.md). No font downloads
are needed. Emphasis is underlined; publisher-embedded fonts and complex-script
shaping are not implemented. Visual rendering and glyph fallback need Vita
hardware testing.

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
  underlined using the selected reader font. Scripts, styles, and ruby annotations are
  omitted; ruby base text remains. Illustrations render between their surrounding
  paragraphs. Open **Cross (Contents) → Square** from the reader for the full chapter image
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

Illustrations can be inspected directly from the reading page without opening
the chapter image list. Push the right stick up to expand the largest visible
illustration into a full-screen inspection view, or tap a specific illustration.
Right stick up/down zooms smoothly from 1× to 4× the fitted size; the left stick
pans with its speed controlled by stick deflection. Pan stays within image edges,
and smaller axes remain centered. A brief hint explains the inspection controls.
Triangle, Circle, a tap, or zooming out to 1× returns to the original reading page.
D-pad navigation and L/R work directly while inspecting: they reset inspection
and perform their existing reading action (EPUB/online chapters, TXT pages for
L/R). The text/image anchor, bookmark, and saved reading-bar preference remain
intact during inspection. Sticks do not turn reading pages.

The separate chapter image viewer supports embedded PNG and JPEG images, fits them to the screen, and
zooms from 1× to 4× the fitted size. Pan is clamped to the image edges; smaller
axes stay centered. The image is clipped to the viewing area, leaving controls
visible. Viewing images does not move the reading position or bookmark. Tap an
inline image to inspect it directly, or use **Cross (Contents) → Square** to choose an image
from the chapter list. In the separate viewer, **Circle** or the touch Back button returns to the screen
that opened the viewer.

Each image is limited to 8 MiB encoded, 4,096 pixels per side, and 4 megapixels
total. Headers are checked before GPU allocation; the PNG/JPEG decoder then
validates the full stream. Failed loads keep the current image/reader state and
show an error. Chapter opening probes image dimensions once for layout, which
can add loading time for chapters with many images. Font changes reuse these
dimensions. Only visible inline images are decoded; their textures are reduced
to reading-area sizes and released when they leave the visible page. Thumbnail
creation temporarily decodes one full image within the existing pixel limits.
A small cache retains up to twelve cover thumbnails across screen visits.
Full-size detail covers and viewer textures are released when leaving their screen;
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
The Vita app icon must be a 128 × 128, 8-bit indexed, non-interlaced PNG;
an RGBA icon can cause VitaShell installation error `0x8010113D`. Configuration
checks this format. After changing the logo, regenerate the app icon with
`convert assets/ui/logo.png -resize 128x128 -strip PNG8:assets/ui/icon0.png`
(ImageMagick); the in-app logo keeps its original full-color format.
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

Idle screens retain the displayed frame and wait for VBlank while polling input.
Input, notices, button feedback, keyboard activity, downloads, and narration
invalidate the display. Bookmark previews and UI text layouts have bounded
in-memory caches. See [performance investigation and profiling](docs/performance.md)
for measured host comparisons, diagnostic builds, and the Vita validation checklist.

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
- Horizontal text; glyph coverage follows the selected bundled font and system fallback. No PDF, TXT
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
- [Vita frontend](src/main.cpp): saved library, local import browser, UI/reading font rendering,
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
- [Native frontend tests](tests/frontend_tests.cpp): actual screen registry and D-pad
  dispatch, empty/partial pages, footer/header access, modal isolation, safe removal
  default, Settings Apply/Cancel, import cancellation, and reading-anchor restoration.
- [Font tests](tests/vita_font_tests.cpp): native font-role routing and consistent
  measurement/drawing through mocked Vita/FreeType calls, including missing fonts
  and mixed glyph fallback. Actual rasterization still needs Vita hardware.
- [Input tests](tests/controls_tests.cpp): stick drift/hysteresis, repeat timing,
  touch bounds, taps, swipes, long presses, reordered pinch reports, drag,
  multitouch cancellation, and screen-transition suppression. Hardware testing
  is still needed to tune gesture feel and confirm touch calibration.

Next local milestones: persistent metadata caching for faster cold startup, more
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

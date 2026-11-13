# Interface reference

[README](../README.md) · [Controls](controls.md) · [Library and online novels](library-and-online.md)

This describes the implemented native interface. The
[frontend](../src/main.cpp), [shared geometry](../include/ui.hpp), and
[visual primitives](../src/vita_ui.cpp) are the rendering and interaction sources
of truth.

## Shared layout

NovelReader renders at **960 × 544**. Home, Library, Bookmarks, Settings, and About
share a five-destination sidebar. Other screens use a top application bar; Reader
can hide its bars for immersive reading. There is no Wi-Fi/clock/battery status
strip. The dark interface uses purple surfaces, lavender focus outlines, rounded
buttons, and controller hints in the bottom bar.

D-pad/stick focus stays within screen content or the active modal. L/R and touch
select sidebar destinations. Footer hints provide physical shortcuts and touch
targets without joining D-pad focus. Disabled controls are skipped. Popups isolate
input and restore the underlying screen when dismissed.

Startup displays packaged artwork, the logo, and a progress bar with task labels.
It loads collection metadata and prepares the first covers, bookmark excerpts,
and sidebar screen resources before showing Home.

## Screens

| Screen | Current behavior |
| --- | --- |
| Home | Moonlit background and a Continue Reading card, with recent-book artwork and progress; Browse Library when no recent book is available |
| Library | Four-column scrolling cover grid; Add Book opens the Import books/Online novels menu; Search and Filter shortcuts are in the footer |
| Book details | Cover, title/author, progress, last-read time, chapter count, and local format; Start/Continue Reading and Chapters actions |
| Online details | Shared details layout, scrollable description, offline count, library membership, and reading/chapter/download/refresh actions |
| Chapters | Large cover beside five chapter rows; current/progress markers, offline/download/locked indicators for online chapters, and numeric Jump action |
| Illustrations and viewer | Four-entry illustration lists; separate fit/zoom/pan viewer, plus inspection of images directly from Reader |
| Bookmarks | Three passage rows per page, one saved bookmark per book; Open and confirmed Remove actions |
| Settings | Five reading-preference rows and a live text preview; explicit Apply, Cancel, and Reset |
| About | App version and author, Check for Updates, Credits, and pageable License summaries |

Missing covers use a title-card fallback. Missing fonts fall back to the Vita
system font and produce a startup notice. The packaged
[fonts and fallback behavior](../assets/fonts/README.md) are documented separately.

## Reader preferences

| Preference | Choices |
| --- | --- |
| Typeface | Literata, Atkinson Hyperlegible Next, Vita system font |
| Font size | Three sizes |
| Line spacing | Three levels |
| Page theme | Night, Paper, Sepia, OLED, OLED Night |
| Page effect | Page Curl, Slide and Fade, None |

New settings default to Literata and Slide and Fade, with reading bars shown.
Existing saved preferences are retained. Apply commits the preview; Cancel
restores the last committed settings; Reset previews reading defaults until Apply.
Settings opened from Reader returns to reading after Apply. Settings opened from
the sidebar stays open after Apply.

Page Curl folds the full screen from the bottom-right for forward turns and the
bottom-left for backward turns. Horizontal drags preview a turn and settle forward
or back on release; vertical drags scroll to a line. The center tap toggles reading
bars while preserving the text/image anchor. Menus overlay the page without
reflowing it. See [controls](controls.md) for all button and touch mappings.

## Validation boundary

[Frontend tests](../tests/frontend_tests.cpp),
[UI tests](../tests/vita_ui_tests.cpp), and
[font tests](../tests/vita_font_tests.cpp) exercise native dispatch, layout rules,
geometry, and font routing through stubs. They do not establish on-device visual
quality. Review actual font rasterization, clipped long titles, themes, focus,
image rendering, and page effects on a Vita; see
[hardware validation](performance.md#hardware-validation).

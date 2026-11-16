# Performance and validation

[README](../README.md) · [Building](building.md) · [Interface](interface.md)

## Current resource behavior

The [frontend](../src/main.cpp) retains collection metadata across screen visits.
Library order is rebuilt when its membership, query, sort/filter, or relevant
metadata/progress changes. Imports and online updates refresh affected entries;
returning from an unchanged screen does not rescan the collection. Progress and
library preferences skip identical writes while changed saves use temporary-file
replacement.

| Resource | Lifetime or bound |
| --- | --- |
| Font metrics and packaged artwork | Owned for the app lifetime; up to 4,096 metric/route entries per cache |
| FreeType text | Shared vita2d glyph atlases per face/size; rotate after 128 distinct glyphs and release old atlases after frame completion |
| Firmware PGF textures | Up to four shared 512 × 512 atlas pages; fonts open by language on demand and layout reads metrics without rasterizing |
| Library cover thumbnails | Up to 12 cached thumbnails, bounded to 144 × 200 pixels |
| Bookmark previews | Up to 24 excerpts keyed by book/chapter/offset |
| Clipped UI text | Up to 128 cached results for input strings of at most 1,024 bytes |
| Wrapped UI text | Up to 32 cached results for input strings of at most 4,096 bytes |
| Home title textures | Up to eight cached line textures, bounded to 2 MiB; evictions released after frame completion |
| Reader content | Current chapter/layout and inline textures released when reading is cleared |
| EPUB directory | Shared immutable ZIP positions for the open publication, with up to 1 MiB of entry names and 20,000 entries |

Cache keys include the relevant font/size/layout parameters. Explicit folder
refresh invalidates affected book assets. Startup loads membership, progress,
and metadata, then prepares the sidebar screens in an offscreen texture. It
loads existing thumbnails for the first Library page and Home and reads up to
three bookmark previews. Missing thumbnails are generated after the screen is
visible. The Library requests cover batches of up to
four and prefetches an adjacent row when the texture cache has room. One worker
decodes one cover at a time. The render thread uploads completed thumbnails, and
title cards stay usable while artwork is loading.

Imported books and saved online favorites queue versioned, compressed thumbnails
in `ux0:data/NovelReader/thumbnails/`. Existing books populate the disk cache
when first displayed. Cache hits read thumbnail-sized pixels without opening the
EPUB archive or decoding the original image. PNG generation keeps only sampled
rows and JPEG generation uses scaled decoding. The original cover stays available
for Book Details.

Unchanged screens keep the displayed frame while the main loop polls input and
waits for VBlank. Input, timers, keyboard activity, downloads, narration, and
animations request redraws. Page Curl reuses an outgoing-page texture and bounded
geometry; its [geometry builder](../src/page_curl.cpp) uses 24 curved bands.
Allocation failure uses the existing transition fallbacks.

## Known synchronous work

Cold library metadata loading, chapter/book reads, full-size details artwork,
inline EPUB image decoding, and font-setting reflow can block the UI. Wrapping now uses
exponential probes and binary search rather than measuring every character
prefix; font measurements cache metrics without filling a GPU atlas. Theme and
page-effect changes skip text reflow. Narration
cancellation joins its worker and can wait for the next synthesis callback.
These are profiling targets; desktop timings do not establish Vita CPU usage,
GPU performance, frame pacing, or hardware latency.

EPUB chapter layout reads image dimensions from PNG headers and short JPEG
prefixes. JPEGs with longer metadata can still require a full read. When an
illustration becomes visible, the reader checks the full ZIP CRC before passing
the encoded image to the texture decoder.

Ordinary interface and reading text reuse glyph atlases across different lines
and pages. Only Home headings use complete-line textures. Font tests draw 2,000
different lines and assert a single reused face/size atlas without per-line
texture allocations. Firmware tests pack thousands of glyphs into shared pages,
assert lazy language loading, and defer atlas eviction until GPU completion.
These operation counts guard against allocation/rasterization regressions; they
do not predict a hardware frame rate.

## Enable profiling

`NOVELREADER_PROFILE` is off by default. Enable it for a diagnostic Vita build:

```sh
cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release -DNOVELREADER_PROFILE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/vita -j2
```

Use **Options → Save and exit app**, or Reader's **Save and exit**, for a clean
shutdown. The app writes `ux0:data/NovelReader/performance.csv` at shutdown.
[performance.hpp](../include/performance.hpp) records call counts, total elapsed
microseconds, and maximum elapsed time for navigation, I/O, decoding, layout,
presentation, idle waits, and resource work. Scopes are nested; do not sum their
durations. Heap samples report newlib allocations after draws, excluding GPU
memory and allocation-time peaks.

`frame_work` excludes presentation waits; `frame_present` measures swap/render
completion; `frame_interval` covers the main loop; `idle_wait` measures VBlank
waiting on unchanged screens. These are elapsed-time measurements, not CPU counters.
Compare identical actions and separate cold and warm visits.

### Compare EPUB loading on Vita

The profiling VPKs share the same title ID and app data. Install the baseline
and optimized builds in turn on the same Vita. Use the same storage device,
settings, and books for both builds. Turn narration off and keep online activity
and book search idle during measurement.

1. For each build, open a large illustrated EPUB from the same saved chapter
   after five fresh app launches. Leave the page at that chapter, exit through
   **Start → Save and exit**, and copy `ux0:data/NovelReader/performance.csv`
   after each run. The next clean exit overwrites the CSV.
2. In a separate run, change chapters 20 times with the same sequence of controls
   and chapters on both builds. Return to the same starting chapter before each
   build's run. Use the `chapter` samples for this comparison.
3. Repeat with a small text-only EPUB. A two-chapter book works as a control if
   you alternate forward and back for all 20 changes.

The `# epub_load` rows show up to 128 successful presented loads in order. The
`kind` field identifies `book` or `chapter`. Compare median and worst `elapsed_us`
within each group. Check ZIP opens, directory scan entries, and inflated image
probe bytes to explain changes in workload. `# heap_bytes` is sampled across
the whole app after drawing and excludes allocation-time peaks. The device goal
is at least 20% lower median loading latency on the illustrated book and no more
than 10% higher median latency on the text-only control.

### Diagnose chapter layout

For a quick diagnosis, open the same EPUB at the same chapter and make 10 chapter
changes with L/R. Save and exit, then copy `performance.csv`. The layout profiling
build adds these columns to each `# epub_load` sample:

| Columns | What they measure |
| --- | --- |
| `layout_us`, `wrap_us`, `measure_us` | Layout total, its wrapping work, and font-width calls inside wrapping. Subtract `measure_us` from `wrap_us` for wrapping work outside the font callback. |
| `measure_calls`, `measured_bytes` | Number of width requests and total UTF-8 bytes in the requested prefixes, including prefixes that reuse cached advances. |
| `first_frame_draw_us`, `first_frame_present_us` | Preparing and drawing the first new reader frame, then swapping buffers and waiting for rendering to complete. |

The timing calls around font measurements add some overhead. Use this build to
identify expensive stages. Compare before-and-after latency using builds with
the same instrumentation.

### Compare startup and the first Library visit

Install each startup profiling VPK in turn. Keep the library, storage, and
settings the same. Launch the app, wait for Home, open Library once, and keep the
first grid page visible until its covers settle. Save and exit from Library
Options and copy `performance.csv` before running the other build.

| Event | What it measures |
| --- | --- |
| `startup_home` | App entry through presentation of the first interactive Home frame. |
| `first_library` | The action that opens Library through presentation of the first interactive grid frame. |
| `first_library_covers` | The same Library action through the first frame with all visible cover requests resolved, including placeholders for unavailable artwork. |

These events distinguish the time spent waiting to navigate from the time spent
waiting for artwork. `scan` still includes the synchronous metadata pass, and
`cover_decode` measures cover work wherever it occurred during the session.

For the host benchmark, enable profiling in the host build and run from its fixture:

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON -DNOVELREADER_PROFILE=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
(cd build/host/frontend-fixture && ../frontend_tests --benchmark)
```

The benchmark writes synthetic books, membership, and saves inside that fixture.
It measures host work with mocked fonts, textures, input, and GPU presentation.
To return a configured Vita build to normal release behavior, configure it again
with `-DNOVELREADER_PROFILE=OFF` and rebuild; CMake retains options in its cache.

## Automated checks

The [CTest targets](../CMakeLists.txt) cover 15 suites. In addition to functional
flows, profiling-enabled frontend tests assert warm-navigation cache behavior,
bounded resources, unchanged idle polls, and online exit without a probe per
chapter. Geometry tests check finite curl coordinates, coverage, and storage
bounds; UI tests check scanline colors and controller/button geometry. Host
AddressSanitizer/UndefinedBehaviorSanitizer checks can find memory/lifetime errors,
but cannot validate GPU or physical input behavior.

## Hardware validation

Before treating a build as device-validated, check on a PS Vita:

- Controller-only, touch-only, and mixed navigation across every screen; focus,
  dialogs, keyboard dismissal, held inputs, and repeated open/back flows.
- Long titles, missing covers/fonts, glyph fallback, all reading themes, and
  reader settings with the current text/image anchor preserved.
- Large TXT/EPUB books, illustrations, image pan/pinch, sustained page turns,
  reversed/canceled drags, and reading bars shown/hidden.
- Cold versus warm navigation, idle behavior, repeated transitions, and heap/VRAM
  stability. Use a large illustrated EPUB to compare cold book open, chapter
  changes, font and margin reflow, and image decoding on the same device.
- Online Wi-Fi/TLS, missing chapters, cancellation, retry, partial downloads,
  artwork failure, and offline reopening.
- Narration audio, pause/resume/stop, speed changes, EOF/restart, and interaction
  with page rendering. Verify chapter advance in airplane mode and compare the
  sleep timer's saved line with the audio that played.
- Put the Vita to sleep during reading, narration, and a chapter download. Check
  input and audio after resume, then confirm saved progress and retained chapter
  files after a clean exit and relaunch.

Cross-compilation and host stubs do not replace these device checks. No current
hardware timing or CPU improvement is claimed by this guide.

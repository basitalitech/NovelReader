# Performance investigation and validation

The changes retain the existing UI geometry, colors, scanline gradients,
button feedback, navigation mappings, search, bookmarks, and progress formats.
No new background worker or dependency was introduced.

## Verified causes

Inspection of the original [frontend](../src/main.cpp) established these paths:

- `navigate()` already retained library entries on ordinary sidebar visits. It
  nevertheless sorted the collection and wrote unchanged preferences on every
  Library activation. Fonts and packaged artwork were already owned by `App`
  for its entire lifetime; they were not recreated for every destination.
- `sync_bookmark_previews()` discarded previews outside Bookmarks. Every return
  reopened up to three publications and loaded/normalized their entire bookmarked
  chapters to extract at most 240 bytes. Bookmark positions themselves were
  already in memory; repeated parsing of a separate bookmark index was not the cause.
- `sync_covers()` freed thumbnails not used by the current screen. Returning to
  Library could read EPUB ZIP data, decode full images, and downsample them again.
  `update_cover()` additionally decoded full-size covers during import focus and
  Home/bookmark activation, where the resulting texture was never displayed.
- `scan()` reconstructed collection metadata/progress after import cancellation
  and online returns. It parsed every EPUB/online index in the collection.
- Local reader exit saved progress, freed its buffers, and sorted the collection;
  it did not reparse the local book. Online reader exit additionally called
  `refresh_online_summary()`: one filesystem probe per chapter plus reopening
  the index. [cached()](../src/online_store.cpp) opens a chapter file and queries
  its length, so a 20,000-chapter novel could cause 20,000 opens during exit.
- Every loop iteration rendered the whole screen despite no visible change.
  VSync was already enabled. Text clipping/wrapping was repeated, and the
  background gradient alone submitted 500 individual rectangles per draw.

## Targeted changes and lifetimes

[main.cpp](../src/main.cpp) keeps the collection alive while the import browser
temporarily owns its separate listing. Membership reconciliation loads new
entries only; removed entries lose their associated cached assets. Returning
from online browsing without changes performs no collection scan. Online
completion refreshes the affected member, including stable bookmark remapping.
Import's explicit **Refresh folder** rereads the listed files, updating matching
collection metadata and invalidating covers/quotes. Restart reloads everything.
External file changes are not polled on every sidebar visit.

Library order is reused until its sort, filter, query, membership, title, or relevant
progress changes. [library.cpp](../src/library.cpp) folds each sort title once,
instead of allocating lowercase copies on every comparator invocation.
Preferences are written only when changed, with failed writes retried.
Identical progress saves within a reader session are skipped; actual progress,
bookmark, and last-read changes still use the original atomic replacement format.

The preview cache retains at most 24 quotes, keyed by book/chapter/offset. A
bookmark in the currently loaded chapter obtains its quote from that buffer.
The cover cache retains at most 12 thumbnails of at most 144 × 200 pixels, about
1.4 MiB RGBA payload in total plus GPU allocation padding. Both use least-recently
used eviction. Failed cover loads are remembered too. No full book or full-size
cover is retained by these caches. Reader exit releases text, layout blocks,
chapter metadata, and inline textures; full-size viewer resources keep their
previous ownership. The existing render-completion wait precedes later eviction.

UI clipping retains at most 128 results with source strings up to 1,024 bytes;
wrapping retains at most 32 results with source strings up to 4,096 bytes.
Width, font role, pixel size, and text are part of their keys. Changed settings
therefore cannot reuse a layout measured with different font metrics.
[vita_fonts.cpp](../src/vita_fonts.cpp) also omits the unused final-run width
measurement after drawing while preserving fallback-run positioning.

[vita_ui.cpp](../src/vita_ui.cpp) submits gradient scanlines together, preserving
the exact original per-row colors and rounding. An idle screen keeps its displayed
buffer: the main loop waits on `sceDisplayWaitVblankStart()` and samples input
each refresh. Changes request redraws; press/notice deadlines, speech, downloads,
pan/pinch, and the system keyboard are included. Rendering still uses the original
VSync configuration. There is no added sleep or reduced input sampling rate.

## Host comparison

Instrumented Debug host build, same generated fixture and scenario before and
after changes. The fixture has 512 TXT collection members plus the guide; three
books contain approximately 2 MiB each and have bookmarks. Twelve cycles each run
Library → Home → Bookmarks → Library, including draws after each activation.
The first bookmark visit is cold in both runs. Setup/startup is excluded.

| Measured operation, totals across the run | Before | After |
| --- | ---: | ---: |
| 48 navigation handlers | 33.496 ms | 0.102 ms |
| Library order builds during navigation | 24 | 0 |
| Settings writes during navigation | 24 | 0 |
| Bookmarked chapter loads | 36 | 3 |
| Bookmark preview work | 629.543 ms | 65.183 ms |
| 48 frame-work scopes, including cold previews | 638.165 ms | 71.917 ms |
| 12 small-TXT reader exit handlers | 16.181 ms | 0.443 ms |
| Reader scenario library order builds, including explicit fixture selection | 13 | 1 |
| Reader scenario progress writes, including opening | 24 | 12 |

Timings are single-run wall-clock samples affected by host scheduling and storage
cache state. Scopes are nested; their durations must not be added together.
Fonts, texture decoding, controller input, and GPU presentation are mocked in
the frontend harness. These numbers demonstrate removed work, not Vita latency
or CPU percentages. The baseline was recorded after adding counters and before
the optimization edits. Once the three previews are cached, subsequent warm
visits perform zero chapter loads, metadata loads, settings writes, or sorts;
the regression test asserts those counts separately.

The deterministic idle regression verifies that 120 polls request no redraw after
an initial draw, and that input and timed UI changes request redraws. A separate
1,024-chapter offline-novel fixture verifies that exit performs zero availability
probes and zero publication opens while preserving the destination and progress.

## Reproduce and profile

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON -DNOVELREADER_PROFILE=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
cd build/host/frontend-fixture
../frontend_tests --benchmark
```

The benchmark writes synthetic books, membership, and saves inside the host fixture.
Run it from that fixture directory. It is separate from the normal test invocation.

For a Vita diagnostic package:

```sh
cmake -S . -B build/vita -DCMAKE_BUILD_TYPE=Release -DNOVELREADER_PROFILE=ON
cmake --build build/vita -j2
```

The optional [profiling header](../include/performance.hpp) uses
`sceKernelGetProcessTimeWide()` on Vita and a monotonic clock on the host. It records
call counts, total microseconds, and worst observed durations, without per-frame
file writes. On **Save and exit**, the app writes
`ux0:data/NovelReader/performance.csv`. Timings include navigation, exit/cleanup,
scans, metadata/chapter/text loads, previews, cover decoding, sorting, persistence,
layout, font/resource initialization, and online availability probes.

`frame_work` excludes presentation waiting; `frame_present` measures swap/render
completion; `idle_wait` measures unchanged-frame VBlank waiting. `frame_interval`
includes the complete loop, including any blocking action. These are elapsed-time
diagnostics, not CPU counters. The final comment row reports newlib allocated heap
bytes sampled after draws: sample count, last value, and maximum sampled value.
It excludes GPU memory and is not an allocation-time peak or total process RSS.
Use separate launches with identical actions to compare idle and navigation runs.

Set `-DNOVELREADER_PROFILE=OFF` for the standard release package; it is off by default.

## Validation and remaining hardware work

- All 13 host tests passed with diagnostics enabled. The existing suite exercises
  search, settings, reading anchors, EPUB/TXT persistence, font fallback, speech,
  controller/touch routing, and online cache identity.
- All 13 AddressSanitizer/UndefinedBehaviorSanitizer tests passed with leak
  detection and diagnostics disabled. LeakSanitizer required execution outside
  the sandbox because the sandbox prevents its process inspection.
- Added frontend checks cover repeated navigation, multiple books, resource-cache
  bounds, import cancel and online shortcut, external replacement plus explicit
  refresh, deletion/reimport, quote invalidation, stable online bookmark remapping,
  1,024-chapter online exit, and idle/timer/keyboard redraw behavior.
- Gradient tests compare every scanline's colors and geometry to the prior formula.
- Release Vita compilation and packaging passed with diagnostics enabled and disabled.

Actual Vita testing is still required for all requested navigation routes, real
EPUB covers, large libraries, idle/transition CPU measurements, frame pacing,
heap/VRAM stability across repeated transitions, keyboard dismissal, touch/pan,
notice/press animation deadlines, and narration-follow/cancellation behavior.
Compare cold and warm visits separately and inspect the diagnostic CSV after
identical runs. No hardware CPU improvement or sub-frame transition time is claimed.

Cold collection startup, first uncached bookmark previews, EPUB image decoding,
book/chapter loading, and font-setting reflow still run synchronously. Active
narration cancellation still joins its existing worker; its worst-case hardware
latency has not been measured. Those paths have timings for further targeted
work, without adding worker ownership complexity to the navigation fixes.

## Files changed

- [main.cpp](../src/main.cpp): lifecycle/data reuse, invalidation, resource limits,
  persistence deduplication, redraw decisions, navigation/cleanup diagnostics.
- [library.cpp](../src/library.cpp), [vita_fonts.cpp](../src/vita_fonts.cpp), and
  [vita_ui.cpp](../src/vita_ui.cpp): sort allocation reduction, font measurements,
  and gradient batching.
- [performance.hpp](../include/performance.hpp), [CMakeLists.txt](../CMakeLists.txt),
  [reader.cpp](../src/reader.cpp), [epub.cpp](../src/epub.cpp),
  [layout.cpp](../src/layout.cpp), and [online_store.cpp](../src/online_store.cpp):
  optional timings/counters and heap snapshots.
- [frontend_tests.cpp](../tests/frontend_tests.cpp),
  [vita_ui_tests.cpp](../tests/vita_ui_tests.cpp),
  [frontend platform stubs](../tests/frontend_stubs/vita_frontend_stubs.hpp), and
  [drawing stubs](../tests/frontend_stubs/vita2d.h): regression coverage and benchmark.
- [README.md](../README.md) and this report: cache refresh behavior, measurements,
  build instructions, and hardware checks.

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
| Fonts and packaged artwork | Owned for the app lifetime; requested font sizes are cached lazily |
| Library cover thumbnails | Up to 12 cached thumbnails, bounded to 144 × 200 pixels |
| Bookmark previews | Up to 24 excerpts keyed by book/chapter/offset |
| Clipped UI text | Up to 128 cached results for input strings of at most 1,024 bytes |
| Wrapped UI text | Up to 32 cached results for input strings of at most 4,096 bytes |
| Home title textures | Up to eight cached line textures |
| Reader content | Current chapter/layout and inline textures released when reading is cleared |

Cache keys include the relevant font/size/layout parameters. Explicit folder
refresh invalidates affected book assets. Startup prepares the first Library
covers, three bookmark passages, and sidebar screen resources in an offscreen
target without presenting those frames. Failed target allocation leaves text
resources to initialize on demand.

Unchanged screens keep the displayed frame while the main loop polls input and
waits for VBlank. Input, timers, keyboard activity, downloads, narration, and
animations request redraws. Page Curl reuses an outgoing-page texture and bounded
geometry; its [geometry builder](../src/page_curl.cpp) uses 24 curved bands.
Allocation failure uses the existing transition fallbacks.

## Known synchronous work

Cold library metadata loading, uncached bookmark previews, chapter/book reads,
EPUB image decoding, and font-setting reflow can block the UI. Narration
cancellation joins its worker and can wait for the next synthesis callback.
These are profiling targets; desktop timings do not establish Vita CPU usage,
GPU performance, frame pacing, or hardware latency.

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

The [CTest targets](../CMakeLists.txt) cover 14 suites. In addition to functional
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
  stability; use the same inputs and book set for comparisons.
- Online Wi-Fi/TLS, missing chapters, cancellation, retry, partial downloads,
  artwork failure, and offline reopening.
- Narration audio, pause/resume/stop, speed changes, EOF/restart, and interaction
  with page rendering. Check saved progress after a clean exit and relaunch.

Cross-compilation and host stubs do not replace these device checks. No current
hardware timing or CPU improvement is claimed by this guide.

# Offline narration with Flite SLT

In Reader, press **START → Read aloud → Start here**. The bundled English (US)
SLT voice reads from the current text offset through the current chapter.
Pause/resume, Stop, and slower/faster controls are in the same panel. The saved
speed is an approximate 100–350 words per minute, with 175 as the default; Flite
scales duration relative to its normal voice pace. Adjustments apply at the next
text chunk. Older settings files retain the default speed.

The reading view follows source text offsets and skips illustration markers.
Normal progress saves retain the visible reading position. The displayed text
can lead audible speech by the short playback lookahead. Changing chapter or
leaving the reading screen stops narration. At chapter end, playback drains,
the reader reports completion, and the book stays open. **Start here** can replay
from the visible position. Narration does not automatically cross chapters or
run while the app is suspended. The English voice may mispronounce other languages.

The voice, US English support and CMU lexicon are linked into the executable.
There are no accounts, network services, downloaded voice packs, or runtime
speech-data folders. The replaced eSpeak engine/data/compiler are no longer
part of the source distribution or VPK.

## Implementation

[Speech player](../src/speech.cpp) keeps an immutable snapshot of the chapter
text and illustration ranges. It segments lazily, preserving UTF-8 byte offsets,
with a 180-byte limit on each text chunk. This bounds sentence-model/waveform
allocations and the analysis work that can precede the next cancellation point.
Flite's CG streaming callback sends samples as the vocoder generates them;
the player retains the utterance until synthesis returns, then frees it before
moving to the next chunk. The extra waveform copy from `flite_text_to_wave`
is avoided.

[SLT engine](../src/slt_engine.cpp) serializes access to the process-global voice.
It registers the voice once and keeps it for the process lifetime. Completion
does not unregister/re-register voice features. After all utterances are released,
the player clears streaming context/utterance pointers. It publishes Finished
only after the queue/device has drained and this cleanup is complete. The
callback context belongs to the player and cannot outlive its joined worker.

The synthesis worker uses an explicit **512 KiB stack**, verified on hardware.
The [playback queue](../src/speech_buffer.cpp) uses its own **64 KiB**
stack, a four-block prefill (256 ms at 16 kHz), and at most eight queued blocks
(512 ms), plus producer/consumer blocks. It aggregates callback chunks without
padding between them. [Vita audio](../src/speech_audio.cpp) resamples 16 kHz mono
PCM to 48 kHz and alternates 1,024-sample device buffers, preserving submitted
data while it is playing.

Pause retains queued speech and gates future output after the current device
write. Stop wakes a blocked producer and discards the queue, then joins the
worker. Text/parameter analysis may continue until the next Flite callback;
the UI can briefly wait for this bounded work. Natural completion drains the
queue and the device before the reader receives Finished. Device errors are
reported in the reader.

[Build configuration](../cmake/FliteDependencies.cmake) builds only Flite's
shared core, US English support, CMU lexicon and SLT model. Socket support, mmap
and desktop audio are disabled. The app supplies its own buffered playback.
Licenses and acknowledgements are included in `app0:/licenses/`; source
provenance and the documented empty-track copy fix are in
[third-party notices](../third_party/README.md).

## Verification

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

Tests synthesize real non-silent SLT PCM, check UTF-8 boundaries and illustration
skipping, speed changes, pause/resume, cancellation, output failures, and repeated
EOF/restart cycles. Frontend tests run real synthesis through the reader menu and
draw/update its completion state repeatedly. Audio tests retain submitted device
pointers to detect buffer overwrites. Host audio goes to capture sinks.

Initial standalone Vita measurements were **0.614 s** to first PCM, synthesis/audio
ratio **0.397**, and worst sentence **0.408**. After integration and completion
lifecycle changes, the user confirmed the reader works on Vita. These results
do not guarantee performance for every book, speed or rendering load. The
temporary standalone probe has been removed; real speech and repeated completion
tests remain in the normal host suite.

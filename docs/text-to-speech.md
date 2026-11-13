# Offline narration

[README](../README.md) · [Controls](controls.md) · [Building](building.md)

## Use read aloud

In Reader, open **Start → Read aloud (TTS) → Start here**. The bundled Flite SLT
US English voice reads from the visible text position to the end of the current
chapter. The same menu offers Pause/Resume, Stop narration, and Slower/Faster.

Speed is saved, defaults to **175 words per minute**, and changes in 25 WPM steps
between **100 and 350 WPM**. It is approximate: Flite scales voice duration and
actual pace depends on the text. Changes apply to the next text chunk.

Narration follows source text offsets on screen and skips illustration markers.
The visible text may lead audible speech by the playback lookahead. Normal progress
saves retain the visible reading position. Leaving Reader or changing chapter
stops narration. Completion drains queued audio and leaves the book open; Start
here can replay from the visible position. Playback does not automatically
continue into the next chapter.

The voice is linked into the executable. Narration needs no network connection,
account, voice download, or runtime speech-data directory. This is an English
voice; pronunciation of other languages is not guaranteed.

## Implementation and resource lifetime

| Component | Responsibility |
| --- | --- |
| [speech.cpp](../src/speech.cpp) | Immutable text/illustration snapshot, lazy segmentation, synthesis worker, progress, and cancellation |
| [slt_engine.cpp](../src/slt_engine.cpp) | Serialized access to the process-global SLT voice and streaming callback configuration |
| [speech_buffer.cpp](../src/speech_buffer.cpp) | Bounded producer/consumer PCM queue and playback worker |
| [speech_audio.cpp](../src/speech_audio.cpp) | Vita device output and streaming resampling |
| [FliteDependencies.cmake](../cmake/FliteDependencies.cmake) | Core, US English support, CMU lexicon, and SLT model build |

Chunks are bounded to 180 UTF-8 bytes. The synthesis worker retains each utterance
until streaming returns, then frees it before the next chunk. Its stack budget
is 512 KiB; the playback worker uses 64 KiB. At SLT's 16 kHz rate, the queue starts
with four 1,024-sample blocks (256 ms) and holds at most eight queued blocks
(512 ms), plus producer/consumer blocks. Vita output resamples to 48 kHz mono and
alternates two device buffers to avoid overwriting audio still playing.

Pause preserves queued audio and gates future writes after the active device
write. Stop wakes blocked producers, discards queued speech, and joins the worker.
Synthesis analysis may run until its next streaming callback, so stopping can
briefly block the UI. Natural completion drains playback before publishing the
finished state. Callback pointers are cleared after utterances are released;
the voice stays registered until process shutdown.

Flite networking, mmap, and desktop audio are disabled. Source provenance and the
empty-track copy fix are described in [third-party notices](../third_party/README.md).
Licenses and acknowledgements are packaged under `app0:/licenses/`.

## Verification

Use the [host test commands](building.md#build-and-run-host-tests). Speech tests
synthesize non-silent SLT PCM and check UTF-8 boundaries, skipped illustrations,
speed, pause/resume, cancellation, output failures, and repeated completion/restart.
The [audio tests](../tests/speech_audio_tests.cpp) retain submitted buffer pointers
to detect overwrites; [frontend tests](../tests/frontend_tests.cpp) exercise the
narration menu and completion state. Host output goes to test sinks.

Real speaker/headphone output, responsiveness during page rendering, pause/stop
latency, and repeated narration sessions need Vita hardware validation. Historical
standalone-probe timings are not guarantees for current books or rendering loads.

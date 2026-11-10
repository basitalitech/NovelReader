# Offline narration

In Reader, press **Cross → Read aloud → Start here**. English (US) speech starts
at the current text offset and continues through the current chapter. The panel
offers pause/resume, stop, and slower/faster controls. The default is 175 words
per minute; 100–350 is supported, in steps of 25. Speed is saved in settings and
applies at the next sentence. Older settings files retain the default speed.

The reading view follows sentence starts, with a Speaking/Paused indicator.
Illustration placeholders are skipped. Normal progress saves retain the visible
text position, so reopening resumes near the last spoken sentence. Narration
stops when leaving the reading screen or changing chapters. It does not run
through the next chapter or provide playback while the app is suspended.
The first release bundles only English; other text is still read using English
pronunciation rules. Names and abbreviations may sound mechanical.

## Implementation

[Speech player](../src/speech.cpp) owns a worker thread and an immutable copy of
the current chapter text and illustration ranges. It splits text lazily into
sentence/line chunks, bounded to 600 UTF-8 bytes, preserving original byte
offsets. It uses synchronous eSpeak callbacks on this worker; the eSpeak engine
is serialized because its state is global. UI code never calls eSpeak directly.
Pause and cancellation wake the synthesis and playback condition variables;
shutdown joins both workers before their audio and engine resources are destroyed.

The speech worker is created with pthread attributes requesting a **256 KiB
stack**, on both host and Vita. Vita's default 32 KiB stack is too small:
the release ARM compiler reports 32,392 bytes for `MakePhonemeList`, called
inside `TranslateClauseWithTerminator` with its own 7,024-byte frame. Using
the default worker stack can close the app at **Start here** before any audio
plays. This allocation belongs only to the speech worker; online worker defaults
are unchanged. Linux tests inspect the actual running worker's stack size.

[Vita audio](../src/speech_audio.cpp) converts the engine's 22,050 Hz mono PCM to
48,000 Hz using a streaming linear interpolator and alternating 1,024-sample
output buffers. A submitted buffer is left intact while the driver plays it;
the next PCM samples go into its partner. Reusing the same buffer immediately
after output submission can corrupt audio that is still in flight.

[Playback queue](../src/speech_buffer.cpp) separates eSpeak's approximately
40 ms callbacks from the MAIN port's output pacing. It aggregates callbacks
into 1,024-sample native-rate blocks without padding between them. Playback
prefills four blocks (about 186 ms); the bounded queue holds up to eight
(about 372 ms), with one pending block and one consumer block. A separate
64 KiB-stack playback worker handles device output while synthesis runs ahead.
After an empty queue it prefills again to cover short synthesis/rendering delays.
The displayed sentence position can lead audible speech by this short lookahead.

Pause retains queued audio and prevents further output after the current device
write. Resume continues from that queue. Stop wakes a producer blocked on a full
queue and discards queued speech; only the device's in-flight audio drains.
Natural chapter end drains all queued samples before reporting completion.
Device errors, including a failure while draining the final block, are surfaced
to the reader. No full chapter waveform is retained in memory. No network service
is involved, and SSML/file-loading flags are not enabled for book text.

[Build configuration](../cmake/EspeakDependencies.cmake) disables upstream audio,
async threads, MBROLA, Sonic, and speechPlayer. Klatt synthesis is retained.
The Vita endian adapter lives outside the upstream sources, and position
independent code is disabled for Vita's ELF converter. The public eSpeak headers
are kept separate from its internal C compatibility headers.

## Regenerate bundled data

Normal builds use checked-in data. To regenerate with a **native host compiler**:

```sh
cmake -S cmake/espeak-data-tool -B build/espeak-data -DCMAKE_BUILD_TYPE=Release
cmake --build build/espeak-data --target speech-data -j2
```

This produces data under `build/espeak-data/espeak-ng-data/`, using the vendored
phoneme sources and standard English dictionary (`en_list`, `en_emoji`,
`en_rules`). Optional extended dictionaries are not included. On the initial
compilation an absent `en_dict` warning is expected before that file is created.

Copy only `intonations`, `phondata`, `phonindex`, `phontab`, and `en_dict` to
[assets/espeak-ng-data](../assets/espeak-ng-data/), and preserve the English
definitions under its `lang/gmw/` directory. The compiler's diagnostic manifest
and other build outputs are not runtime assets. Review binary differences before
committing. Preserve the vendored source and license notices with distribution;
eSpeak NG's license is GPL-3.0-or-later.

## Verification

```sh
cmake -S . -B build/host -DNOVELREADER_HOST_TESTS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

The tests synthesize real English PCM, check illustration skipping and UTF-8
offsets, pause/resume/cancel/error recovery, and verify the actual Vita audio
adapter against stubbed device calls. Frontend tests check the narration panel
and speed persistence. The device stub retains submitted PCM pointers to detect
overwriting an in-flight buffer. Queue tests cover prefill, tiny callback chunks,
short clips, pause/resume, canceling a blocked producer, and final-block errors.
Host tests do not play through a desktop audio device.

Build the Vita package using the existing **Build Vita package** task. Physical
hardware checks still needed: speaker/headphone playback, smooth audio while
drawing illustrations, touch/controller access to pause and stop, long chapters,
and suspend/resume behavior. A successful package build and stubbed audio tests
cannot establish real device sound quality or timing.

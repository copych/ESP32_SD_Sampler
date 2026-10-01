# Sample looping

Looping is enabled only by `LOOP_TYPE` in `sampler.ini`. WAV metadata alone
never enables it. Parameters work in `[SAMPLESET]`, `[NOTE]` and `[RANGE]`.
Put global settings before the note/range sections which override them.

```ini
[SAMPLESET]
TYPE=MELODIC
LOOP_TYPE=SUSTAIN
LOOP_START=0
LOOP_END=0
ATTACK_TIME=0.005
DECAY_TIME=0.1
SUSTAIN_LEVEL=1
RELEASE_TIME=0.5

[FILENAME]
TEMPLATE=<MIDI>
```

For this template use MIDI-note filenames, e.g. `60.wav`, `61.wav`. Each sample
set is a directory under the SD root containing its WAVs and `sampler.ini`.
`SUSTAIN_LEVEL=1` keeps a held note audible: looping does not override ADSR.

## Endpoints and sources

All positions are **PCM frames**, not bytes or individual channel samples.
The internal interval is `[LOOP_START, LOOP_END)`: the end is exclusive.

| INI | Result |
| --- | --- |
| Start 0, end 0 | Entire PCM data |
| Start N, end 0 | N to PCM end |
| Only start specified | Override start; use embedded end, otherwise PCM end |
| Only end specified | Override end; use embedded start, otherwise 0 |
| Neither specified | Embedded interval, otherwise entire PCM data |
| Empty/reversed/out-of-range interval | Loop disabled; play sample once, with a warning |

An omitted value is different from an explicit zero. The zero-end shorthand
applies only to INI values. For example, a WAV `smpl` loop from 0 through 0 is
a **one-frame loop**, not a whole-file loop. Explicit INI positions accept
0 through 2147483647; the zero-end shorthand also handles longer PCM streams.

Source priority:

1. Each explicit INI endpoint independently overrides the selected interval.
2. The first usable `smpl` loop (inclusive WAV end converted to exclusive end).
3. Exactly one usable `cue ` + `LIST/adtl/ltxt` region, matched by cue ID.
4. Start 0 / PCM end defaults.

Standalone cue markers do not define a region. Multiple candidate regions are
ambiguous and fall back to INI/defaults, with the source reported in WAV logs.
Arbitrary editor-specific XML, labels, sidecars, playlists and proprietary
region chunks are not interpreted. The parser is separated from playback so
additional formats can be added using representative files. Up to 64 entries
per metadata collection are inspected. `smpl` direction, fraction and play count
are ignored; direction and activation are controlled by INI.

The RIFF parser follows chunk lengths and even-byte padding throughout the
file, including metadata after `data` and headers beyond the first sector.
It accepts mono/stereo integer PCM at 8, 16, 24 and 32 bits, including PCM
WAVE_FORMAT_EXTENSIBLE with valid bits equal to container bits. Output remains
16-bit stereo; 24/32-bit input is reduced to its most significant 16 bits.
Float, compressed, RF64, multiple `data` chunks and `wavl` are unsupported.
Malformed/truncated files are skipped rather than played using guessed offsets.

## Modes

- `NONE`: play once.
- `FORWARD`: play the intro, then repeat `[L,R)` until ADSR ends the voice.
- `SUSTAIN`: repeat while the key or sustain pedal holds the note. On release,
  retain queued audio, finish the producer's current traversal, then play the
  tail from R to PCM end. ADSR release starts immediately and may silence the
  voice before the tail finishes. Raising the pedal again does not undo an
  already accepted release.
- `PINGPONG`: `… L,L+1,…,R-1,R-2,…,L,L+1 …`; turning points are not duplicated.
  One-frame loops repeat a constant frame; two-frame loops alternate.

FORWARD and PINGPONG do not switch to the tail on note-off. They keep looping
under the releasing envelope. `NOTE_OFF=FALSE` still suppresses ordinary
note-off for that key. Exclusive groups use their forced release regardless
of the pedal. MIDI All Sound Off stops immediately; All Notes Off releases
keys and respects the pedal.

## Streaming and timing

`PcmCursor` and `PcmProducer` build a virtual stream on Core1. The same path
handles non-looped audio. Each of two output buffers contains decoded stereo
frames plus its own interpolation neighbour. That neighbour is carried into
the next buffer. Core0 sees neither sector positions nor loop direction.

Each buffer holds `READ_BUF_SECTORS * 512 / 4` frames: 640 on S3 and 1024 on P4
with the current configuration. A shared DMA-capable SD window is used only
by Core1. Fragmented FAT extents, unaligned starts, sector crossings and reverse
spans use the same bounded reader. Read-ahead is limited to the required span.
The shared window fits a complete refill even for stereo 32-bit PCM plus
lookahead and sector alignment (12 sectors on S3, 18 on P4). Linear playback
from a contiguous FAT extent takes one SD transaction per refill. PCM frames
are decoded directly from the window without a per-frame copy.

A per-voice prefix cache holds at most one output buffer of frames, allocated
lazily in PSRAM, with internal RAM fallback. It is populated during the initial
traversal. Short loops then need no SD reads at all; long loops still stream.
Allocation failure disables only this optional cache. Cache contents and queued
frames are reset on retrigger; sample-set changes retire all voices first.

The prefix size provides one buffer's source-frame coverage. At speed S its
coverage is `VOICE_BUFFER_FRAMES / (44100 * S)` seconds. At the configured
maximum speed 8 this is about 1.8 ms (S3) or 2.9 ms (P4). This is a memory budget,
**not a guaranteed SD-latency bound**. Simultaneous voices and 24/32-bit input
increase SD demand; actual maximum polyphony needs hardware measurements.
`MAX_PLAYBACK_SPEED` clamps the combined sample-rate/tuning/pitch-bend ratio.

Core0 never waits for Core1 or performs file I/O/allocation. Buffers are published
with acquire/release atomics. Core1 briefly disables a voice and waits for its
current render call when changing lifecycle or ADSR; Core0 returns silence for
that voice during such changes. On underrun the last output fades over one DMA
block and the voice retires safely. Failed SD reads terminate the virtual stream.
This prevents memory overreads; it does not hide an overloaded SD card.

Enable `DEBUG_STREAM_STATS` for cumulative underruns, maximum refill time,
allocated voices and stealing counters (polyphony, repeated note, immediate).
Core1 reports at most every five seconds, only when no voices are active;
blocking Serial output during playback can itself cause underruns.
`DEBUG_CORE_TIME` measures generation,
mixing and I2S separately (its per-block logging can itself disturb timing).
Enable an Arduino Core Debug Level that displays informational logs.

## Validation

Run host tests against the actual parser, sector reader, producer and Voice code:

```sh
python tests/run_tests.py --cxx g++
# Or with a local Zig compiler:
python tests/run_tests.py --cxx path/to/zig.exe --zig
```

Tests exercise exhaustive short intervals, fractional speeds, PCM conversion,
lookahead across turns, delayed sustain release, exact/partial EOF buffers,
fragmented reads, I/O failure, starvation, cache reuse and concurrent control.
They use undefined-behaviour sanitization. Test outputs are under the system
temporary directory, in `esp32-sampler-tests`.
The original source snapshot is `.work/before-looping.zip`.

Arduino Core 3.3.7 compile targets used for validation (not flashed):

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=default,PSRAM=opi --libraries <libraries> --build-path <temporary-directory>/esp32-sampler-build-s3 .
arduino-cli compile --fqbn esp32:esp32:esp32p4:USBMode=default,PSRAM=enabled --libraries <libraries> --build-path <temporary-directory>/esp32-sampler-build-p4 .
```

Keep compiler installations, caches and Arduino build directories **outside the
sketch folder**: Arduino CLI copies hidden subdirectories too. `.work/` in this
project holds only the source backup, validation logs, firmware and SD fixtures.

Generate optional SD fixtures with `python tests/generate_loop_fixtures.py`.
Copy the generated directories from `.work/loop-fixtures` to a FAT32 SD card.
They cover explicit whole-file points, `smpl` after PCM, region metadata,
ping-pong, sustain tail and a non-looped reference. Hold MIDI note 60, release
with and without the pedal, change sets while playing, and repeat under load.
Compare underruns/refill time at several pitches and voice counts. Listening,
SD timing, RAM headroom and maximum polyphony on physical S3/P4 boards remain
hardware checks; host tests cannot certify them. No automatic crossfade is
applied to discontinuous musical loop endpoints.

Region layout reference: [Microsoft RIFF/WAVE specification, archived excerpt](https://www.mcternan.me.uk/MCS/Downloads/wave.pdf).

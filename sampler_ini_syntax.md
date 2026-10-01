# `sampler.ini`

Each sample set is one directory at the root of the SD card.  A directory is
recognised as a sample set when it contains `sampler.ini`.  WAV files in that
same directory are mapped by the rules below.

Keys and section names are case-insensitive.  Use `=` or `:` between a key and
its value.  Empty lines and lines beginning with `;` or `#` are comments.

The supported sections are `[sampleset]`, `[filename]`, `[note]`, `[range]`
and `[group]`.  Older `[envelope]` and `enveloped=` settings are not used.

## Small melodic example

```ini
[sampleset]
title = Salamander Grand Piano
type = melodic
normalized = true
amplify = 1.2
max_voices = 15
attack_time = 0
decay_time = 12
sustain_level = 0
release_time = 0.10

[filename]
template = <NUMBER>_<NAME><OCTAVE>_<VELO>
velo_variants = P,M,F
velo_limits = 36,96,127
```

With this template, `001_C4_M.wav` maps to MIDI C4 (60), middle velocity
layer.  `NUMBER` is consumed from the filename but does not affect mapping.

## `[sampleset]`

| Key | Meaning |
| --- | --- |
| `title` | Display name. |
| `type` | `melodic` or `percussive`; default is `melodic`. Melodic sets fill nearby missing notes by pitch-shifting. Percussive sets only fill missing velocity layers for an already mapped note. |
| `normalized` | `true` applies MIDI velocity to amplitude in addition to choosing a velocity layer. Use it when source layers have similar loudness. |
| `amplify` or `amp` | Overall sample-set gain. |
| `max_voices` / `max_polyphony` | Maximum voices for this set. It cannot exceed the compiled and currently available voice count. Adaptive polyphony may temporarily reduce it under CPU or SD pressure. |
| `attack_time`, `decay_time`, `sustain_level`, `release_time` | Default ADSR values in seconds, except `sustain_level` which is `0.0` to `1.0`. A `[note]` or `[range]` value overrides the default for its keys. |
| `limit_same_notes` | Maximum simultaneous voices for one MIDI note. `0` means the current polyphony limit. |
| `loop_type`, `loop_start`, `loop_end` | Default loop settings for all notes; see [Loops](#loops). |

## `[filename]`

`template` describes each WAV filename, without `.wav`.  Text outside angle
brackets is matched as a literal separator.  The available elements are:

| Element | Reads from the filename |
| --- | --- |
| `<NAME>` | Note name: `C`, `C#`, `Db`, etc. |
| `<OCTAVE>` | One octave digit. MIDI C4 is 60. |
| `<MIDINOTE>` | MIDI note number, `0` to `127`. |
| `<VELO>` | A name listed by `velo_variants`. |
| `<INSTR>` | An instrument name declared by a `[note]` or `[range]` section. |
| `<NUMBER>` | Decimal digits; parsed only to skip a filename prefix. |

Use either `<NAME><OCTAVE>` or `<MIDINOTE>` for melodic filenames.  `<INSTR>`
maps a WAV to every note covered by the matching `[note]` or `[range]`.

`velo_variants` is a comma-separated list ordered from quiet to loud.  It
defines the accepted text for `<VELO>`, for example `P,M,F`.

`velo_limits` is optional.  It is a comma-separated list of inclusive MIDI
velocity upper bounds for the layers, for example `36,96,127`.  Without it,
the sampler divides the 0–127 range evenly among the discovered layers.

## `[note]` and `[range]`

`[note]` maps one MIDI note with `name`; `[range]` maps all notes from `first`
through `last`, inclusive.  Note names accept sharp and flat spellings, such
as `F#3` or `Gb3`.

```ini
[note]
name = C4
instr = kick
note_off = true
limit_same_notes = 1

[range]
first = C1
last = B3
instr = strings
speed = 1.0
attack_time = 0.02
decay_time = 2.0
sustain_level = 0.7
release_time = 0.4
```

| Key | Meaning |
| --- | --- |
| `name` | Note for a `[note]` section. |
| `first`, `last` | Inclusive range endpoints for `[range]`. |
| `instr` | Text matched by `<INSTR>` in the filename template. |
| `note_off` | `true` releases the note on MIDI Note Off; `false` ignores Note Off. |
| `speed` | Playback speed multiplier. `1.0` is original pitch. |
| `limit_same_notes` | Per-note voice limit. |
| `attack_time`, `decay_time`, `sustain_level`, `release_time` | ADSR overrides for this note or range. |
| `loop_type`, `loop_start`, `loop_end` | Loop override for this note or range. |

## Loops

```ini
[note]
name = D4
instr = beat_90bpm
loop_type = forward
loop_start = 0
loop_end = 127659
sustain_level = 1
note_off = true
release_time = 0.02
```

`loop_start` and `loop_end` are PCM **frame** positions, not bytes and not
output-sample positions.  `loop_end` is exclusive: the loop contains frames
from `loop_start` through `loop_end - 1`.

| `loop_type` | Behaviour |
| --- | --- |
| `none` | Play once. |
| `forward` | Repeat the interval continuously. |
| `sustain` | Repeat while the MIDI note is held; continue after the loop when released. |
| `pingpong` | Play alternately forward and backward within the interval. |

If INI endpoints are omitted, valid loop points from the WAV `smpl` chunk, or
an unambiguous `cue` + `LIST/adtl ltxt` region, are used.  Explicit INI points
override WAV metadata.  Invalid endpoints disable looping for that mapping.

## `[group]`

```ini
[group]
notes = F#1,G#1,A#1
```

Starting one note in a group stops the other mapped notes in that group.  This
is useful for closed, pedal and open hi-hat samples.  Up to three notes are
supported in one group.

## Drum-loop example

```ini
[sampleset]
title = Beat loops
type = melodic
max_voices = 1

[filename]
template = <INSTR>

[note]
name = D4
instr = beat_90bpm
speed = 1
loop_type = forward
loop_start = 0
loop_end = 127659
attack_time = 0
decay_time = 0
sustain_level = 1
release_time = 0.02
note_off = true
limit_same_notes = 1
```

The supported WAV input is RIFF/WAVE PCM or PCM-in-WAVE-extensible: mono or
stereo, 8/16/24/32-bit integer PCM.  The engine reads the `data` chunk by its
declared offset, so normal non-audio WAV chunks before or after audio are safe.

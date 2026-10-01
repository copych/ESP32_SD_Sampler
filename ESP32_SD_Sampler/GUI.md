# Minimal sampler GUI

Enabled by default on ESP32-S3 (`SAMPLER_GUI=1` in `config.h`).
SH1106, 128x64, I2C address `0x3C`, 400 kHz. This is the four-pin OLED
configuration from [RDX](https://github.com/copych/RDX-Reface-DX-emu/blob/c37c7f79e8ec3d22102da2eb265e3fefa534e6c4/RDX/config.h).
An SSD1306 or SPI display needs a different driver; it is not selected by this version.

## Connections (S3)

| Signal | GPIO |
| --- | ---: |
| OLED SDA | 8 |
| OLED SCL | 9 |
| Encoder A / B | 15 / 16 |
| Encoder switch | 14 |
| Previous / next selection | 41 / 42 |
| Back / cancel | 2 |
| All notes off | 1 |

Use common GND and 3.3 V compatible OLED power/signals. Buttons and encoder
contacts connect to GND; inputs use internal pull-ups. The existing BOOT/GPIO0
button keeps its next-sample-set action. Change `GUI_ENCODER_DIRECTION` to `-1`
if rotation is reversed. Optional buttons can be left disconnected.

P4 remains headless by default: the RDX P4 button pins conflict with its I2C
pin assignment, and need a separate confirmed wiring profile. The S3 default
I2C SCL also conflicts with this sampler's optional serial MIDI TX (GPIO9);
the configuration rejects that combination. USB MIDI is unchanged.

## Controls

- Main screen: turn encoder to adjust master volume (2% steps, 0–100%).
- Click encoder: browse sample-set directories; turning previews selection.
- Click again: load selected set and return to the main screen. Loading stops
  current voices, as the existing folder button does. Merely browsing does not.
- Previous/next buttons: enter browsing and move the selection.
- Back button: cancel browsing without loading.
- Hold encoder for 700 ms, or use All notes off: release pedal state and stop
  all sampler voices. The reverb tail can still decay.

The main screen shows the INI title (directory name if no title), directory,
volume, active/allowed voices, cumulative SD underruns and sustain-pedal state.
Names are clipped to 21 characters; this first version uses an ASCII UI and
substitutes `?` for non-ASCII text. No settings are written to SD.

Master volume is an independent output control including the reverb return.
It starts at 100%, persists across set changes for the current boot, and is
smoothed on the audio core. It does not modify the INI `AMPLIFY` value.

## Scheduling and diagnostics

`guiBegin()` and `guiPoll()` run exclusively in the existing ControlTask on
Core1. There is no additional GUI task and no shared mutable UI state.
Buttons/encoder are polled about every millisecond, subject to SD/MIDI work.
The framebuffer is 1024 bytes and the font is constant flash data; there are
no dynamic UI allocations during playback. Wire allocates its bus buffers
once on startup. There is no new GUI library dependency.

Display state is sampled at most 10 times per second, and only changed state
starts a new frame. A frame is sent in 64 fragments of 16 data bytes. Each
fragment repositions the SH1106 page/column, takes roughly 0.52 ms on the wire
at 400 kHz (software/driver overhead is additional), and is allowed only when
no voice needs a refill. Drawing and sending happen in separate loop passes.
Input handling continues even when OLED updates are deferred by SD load.

The I2C driver timeout is 10 ms. The original 1 ms timeout proved too short on
the actual S3: the driver returned `ESP_ERR_INVALID_STATE` during the first
frame. After a transfer failure, regular writes stop; at most once per five
seconds, and only with zero active voices, Core1 resets Wire and reinitializes
the panel. Startup/recovery keep the panel off until a complete frame has been
sent, avoiding uninitialized display RAM. Controls keep working while offline.
A failed/busy bus is not a guarantee of glitch-free audio.

With `DEBUG_STREAM_STATS`, the silence-only log includes
`[GUI] online=... max_chunk_us=... errors=... wire_error=... recoveries=...`
and input counters `enc_steps`, `btn_events`, plus browse/selection state.
On hardware, compare `underruns` while
playing the same dense MIDI with `SAMPLER_GUI=1` and `0`, rotate the encoder,
and verify the SH1106 orientation and encoder direction. Host tests do not
establish real-board I2C timing or maximum polyphony.

## Reference and validation

Wiring and SH1106 initialization follow copych/RDX-Reface-DX-emu, commit
`c37c7f79e8ec3d22102da2eb265e3fefa534e6c4`. `gui/font_mo.h` is copied from
its `RDX/src/GUI/font_mo.h`. The incremental transport, sampler screen and
controls are implemented here specifically for the streaming sampler.

`tests/test_gui.cpp` checks button debounce/hold/release and timer rollover,
encoder quadrature, SH1106 addressing, bounded transfer sizes, and I/O failure.
It writes `.work/gui-preview.pgm` using the same framebuffer/font as firmware.
Run it with the other tests via `tests/run_tests.py`.

`tests/test_gui_controller.cpp` runs the actual GUI controller with fake GPIO,
clock, sampler and I2C: browse/cancel/confirm, panic, refill priority, a single
fragment per pass, stopping writes after disconnect, and external folder changes.
It also checks that recovery is deferred while voices are active, retries are
rate-limited, a recovered screen starts with a complete new frame, and display
output stays off until that frame is fully transmitted.

Validation on 2026-09-25: all host tests passed with Zig C++17 and undefined
behaviour sanitizer. Arduino ESP32 Core 3.3.7 builds passed for S3 with GUI
(509190 bytes flash, 222052 bytes static RAM) and P4 with GUI disabled
(508934 bytes flash, 231568 bytes static RAM). Dynamic buffers are additional.
The framebuffer preview was inspected. Flashing, physical input/display tests
and concurrent SD/I2C timing tests have not been performed.

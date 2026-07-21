# Clover Frontend Runtime

## Boundary

The frontend is intentionally separate from SNES hardware emulation:

- `core/` owns the emulated machine and produces completed video/audio data.
- `frontend/` defines system-neutral media, input, video, and audio contracts.
- `platform/sdl/` owns SDL lifecycle, physical devices, presentation, audio
  buffering, capture files, and wall-clock pacing.

The current factory exposes one `system_id_t`, `snes`, and constructs
`snes_emulator_core_t`. Adding a future core should require another adapter and
factory case, not SDL dependencies in that core.

## Frontend Contract

`frontend::emulator_core_t` currently provides:

- `load_media(...)`
- `power_on()` and `reset()`
- semantic `gamepad_state_t` input on logical ports
- `run_frame()`
- `display_info()`
- a read-only ARGB8888 `video_frame()`
- read-only interleaved signed-16-bit `audio_frame()` data

The SNES adapter maps semantic buttons into the SNES serial joypad bit layout
and supports ports 0 and 1. The SDL app currently drives port 0.

The core reports a 256x240 framebuffer, 8:7 pixel aspect ratio, and nominal
NTSC refresh rate of 60.098812 Hz. Audio is stereo at the core's native output
rate. The platform owns resampling/device negotiation and queue policy.

## SDL Runtime

Build with the checked-in preset:

```bash
cmake --preset sdl-release
cmake --build --preset sdl-release
```

Run with:

```bash
./cmake-build-sdl-release/clover_sdl [rom-path] \
  [--frames count] [--capture new-directory]
```

If `rom-path` is omitted, the executable uses the configured
`CLOVER_SDL_DEFAULT_ROM_PATH`. The source-tree default is the local Final
Fantasy III ROM.

The shell:

- creates an aspect-correct window and streams the core framebuffer to a texture
- opens the first available SDL gamepad and handles hotplug
- combines keyboard and gamepad state into one semantic gamepad
- queues core audio to an SDL audio stream
- paces frames from core-provided refresh metadata rather than display vsync
- can stop deterministically after `--frames`

Wall-clock pacing exists only in the platform layer. Headless tools can run the
same core as fast as the host permits.

## Controls

| SNES control | Keyboard | SDL gamepad |
|---|---|---|
| D-pad | Arrow keys | D-pad or left stick |
| B | X | South face button |
| A | Z | East face button |
| Y | A | West face button |
| X | S | North face button |
| L | Q | Left shoulder |
| R | W | Right shoulder |
| Select | Right Shift | Back |
| Start | Return | Start |
| Capture marker | F8 | — |

F8 marks the next emulated frame only while a capture is active.

## Version-3 Capture Format

`--capture` creates a new investigation directory from power-on. Refusing an
existing directory prevents accidental overwrite.

Files:

- `manifest.txt` records format version, ROM path/size/CRC32, frame numbering,
  audio format, total audio samples, discontinuities, and marker frames.
- `joypad1.script` stores nonzero controller spans as inclusive frame ranges.
- `audio.wav` stores signed 16-bit stereo core output.
- `frames.csv` correlates every emulated frame with controller state, audio
  sample range, discontinuity, marker, host interval, SDL queue depth, audio
  startup state, core runtime, presentation runtime, and queue runtime.
- `marker_frame_XXXXXXXX.ppm` stores the exact rendered frame associated with
  each F8 marker.

Frame numbering begins at 1 for the first `run_frame()` after power-on. An F8
press marks the next frame because input events are sampled before that frame
runs. Investigation should inspect a window around the marker rather than
assuming human input landed on the first visibly or audibly faulty frame.

The controller movie is accepted by both headless runners through:

```bash
CLOVER_JOYPAD1_SCRIPT_FILE=/path/to/joypad1.script
```

That makes an input-gated path repeatable under Clover and bsnes while the WAV
and CSV preserve audio and host-performance evidence.

## Deliberate Limitations

The current frontend has one active core, one presented logical controller,
fixed keyboard bindings, and no file picker, save-state UI, remapping UI,
rumble, mouse, multitap, or light-gun support. These are frontend capabilities,
not reasons to mix platform concepts into the emulator core.

The next frontend work should be driven by an actual use case—such as a second
core, renderer, or input device—and should extend the existing seam without
making it SNES- or SDL-shaped.

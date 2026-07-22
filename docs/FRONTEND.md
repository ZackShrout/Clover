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
- optional typed capabilities, currently `video_plane_control_t`

The SNES adapter maps semantic buttons into the SNES serial joypad bit layout
and supports ports 0 and 1. The SDL app currently drives port 0.

The core reports a 256x240 framebuffer and region-correct presentation data:
8:7 pixel aspect with approximately 60.0988 Hz for NTSC, or 55:43 with
approximately 50.0070 Hz for PAL. Audio is stereo at the core's native output
rate. The platform owns resampling/device negotiation and queue policy.
A core may optionally advertise named presentation planes without making the
base contract SNES-shaped. The SNES adapter reports BG1–BG4 and Objects; a
future Genesis adapter can report its own planes through the same capability.

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

If `rom-path` is omitted, the executable opens without media, displays a
frontend-owned test pattern, and waits for a ROM to be selected through
**File → Import ROM to Library…** or **File → Open ROM Library…**. The pattern
is replaced immediately when media loads. An explicit path still loads that ROM
at startup without importing it for command-line and deterministic test runs.

The shell:

- creates an aspect-correct window and streams the core framebuffer to a texture
- provides an SDL-rendered menu bar and native ROM file picker
- opens the first available SDL gamepad and handles hotplug
- combines keyboard and gamepad state into one semantic gamepad
- queues core audio to an SDL audio stream
- paces frames from core-provided refresh metadata rather than display vsync
- can stop deterministically after `--frames`

Wall-clock pacing exists only in the platform layer. Headless tools can run the
same core as fast as the host permits.

## ROM Library, Menu, and Save RAM

The menu bar exposes:

- **Import ROM to Library…** validates the ROM, canonicalizes an optional SNES
  copier header, copies it atomically into managed storage, deduplicates it by
  SHA-256, records SQLite metadata, and opens it
- **Open ROM Library…** (`Cmd/Ctrl+O`) displays the indexed library; use arrow
  keys and Enter or double-click an entry
- **Open ROM Temporarily…** opens external media without adding it to the library
- **Quit** (`Cmd/Ctrl+Q`) flushes dirty save RAM and exits
- **Emulation → Pause** (`Space`) stops host-driven frame advancement
- **Emulation → Frame Advance** (`.`) enters pause and advances one exact frame
- **Emulation → Reset** (`Cmd/Ctrl+R`) models the console reset button
- **Emulation → Speed** selects 0.5×, 1×, 2×, 4×, or unlimited pacing
- **Video** is populated from the active core's optional plane capability

Loading is transactional at the app level: a replacement core and its save RAM
must load successfully, and the current cartridge's dirty save RAM must flush,
before the active core is replaced. Explicit command-line and temporary ROMs
still use central hash-keyed saves; they simply are not copied or indexed.

The frontend contract exposes persistent memory as a system-neutral byte span
with dirty-state acknowledgement. The SNES adapter maps that contract to
battery-backed cartridge SRAM. Filesystem ownership remains in the host layer:

- SDL resolves the platform-standard application data directory
- canonical ROMs are stored as `library/roms/snes/<sha256>.sfc`
- the SQLite index is `library/library.sqlite3`
- saves are stored as `saves/snes/<sha256>/battery.srm`
- a same-sized sibling `.srm` is migrated only when no central save exists
- the file size must exactly match the cartridge header's SRAM size
- an existing save is loaded before the console is powered on
- changed SRAM is written through a temporary file and atomically renamed
- dirty SRAM is checked periodically and flushed on reset, ROM replacement,
  and shutdown

SNES reset preserves SRAM. Audio queued from the previous machine state is
cleared after reset or ROM replacement.

Menu reset, library browsing, importing, ROM loading, pause/frame advance,
speed selection, and video-plane overrides are deliberately unavailable while
a deterministic capture is active because version 4 controller movies do not
encode those operations.

Pause and speed are host scheduling policy: they do not alter emulated clocks.
Frame Advance invokes one normal `run_frame()` and then returns to pause. The
0.5×, 2×, 4×, and unlimited modes currently mute presentation audio rather than
play native-rate samples at an incorrect wall-clock rate. Returning to 1×
restarts the SDL audio queue cleanly. Faster modes batch multiple fully emulated
frames between host presentations so a refresh-limited window does not impose
a false emulation-speed ceiling; unlimited uses bounded batches to keep input
and menus responsive.

SNES video-plane overrides are presentation-only. They recomposite the
already-emulated BG/OBJ candidates without writing SNES registers, changing
timing, or modifying the canonical all-layers framebuffer path.

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
| Pause | Space | — |
| Frame advance | Period (`.`) | — |

F8 marks the next emulated frame only while a capture is active.

## Version-3 Capture Format

`--capture` creates a new investigation directory from power-on. Refusing an
existing directory prevents accidental overwrite.

Files:

- `manifest.txt` records format version, ROM path/size/CRC32, frame numbering,
  audio format, total audio samples, discontinuities, and marker frames.
- `joypad1.script` stores nonzero controller spans as inclusive frame ranges.
- `initial_save_ram.srm` preserves battery RAM exactly as it existed before the
  first captured frame, allowing save-based paths to replay reproducibly.
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
fixed keyboard bindings, muted non-1× audio, and no save-state UI, remapping
UI, rumble, mouse, multitap, or light-gun support. Battery-backed cartridge
saves are supported; save states are a different, unimplemented capability.
These are frontend capabilities, not reasons to mix platform concepts into the
emulator core.

The next frontend work should be driven by an actual use case—such as a second
core, renderer, or input device—and should extend the existing seam without
making it SNES- or SDL-shaped.

# Clover

Clover is a performance-minded Super Nintendo Entertainment System emulator
focused on hardware-faithful behavior, clean subsystem boundaries, and a stable
real-time runtime.

Hardware behavior is the guiding star. ROM-specific workarounds are not an
acceptable substitute for modeling the machine correctly.

## Current State

Clover currently includes:

- a headless SNES core with 65C816 CPU, PPU, SPC700/DSP audio, DMA/HDMA,
  interrupts, controller input, LoROM/HiROM cartridge mapping, and NTSC timing
- a system-neutral frontend seam around the SNES core
- an SDL3 desktop app with video, audio, keyboard/gamepad input, frame pacing,
  and deterministic investigation captures
- headless Clover and bsnes bringup tools, exact frame comparison tools, and
  hardware-focused regression tests

The emulator is not yet universally compatible. Enhancement chips and less
common hardware edge cases remain active work; see
[`docs/KNOWN_SIMPLIFICATIONS.md`](docs/KNOWN_SIMPLIFICATIONS.md).

## Repository Layout

- `src/clover/core/` — emulation, hardware state, and timing
- `src/clover/frontend/` — system-neutral media, input, video, and audio contracts
- `src/clover/platform/` — SDL and operating-system integration
- `tests/` — headless tests, bringup runners, and comparison tools
- `scripts/` — repeatable validation workflows
- `docs/` — architecture, accuracy status, frontend use, and coding standards
- `roms/local/` — optional local test ROMs; commercial ROMs are not committed

## Build and Test

Clover requires CMake 3.20 or newer and a C++23 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The default build is headless and does not require SDL3.

## SDL Desktop App

The checked-in `SDL Release` CMake preset builds the optimized `clover_sdl`
target. In CLion, select that profile and target to build and run in one step.
From a shell:

```bash
cmake --preset sdl-release
cmake --build --preset sdl-release
./cmake-build-sdl-release/clover_sdl "/path/to/game.sfc"
```

If no ROM argument is supplied, the build-time `CLOVER_SDL_DEFAULT_ROM_PATH`
is used. It currently defaults to the local Final Fantasy III ROM. Override it
while configuring with:

```bash
cmake --preset sdl-release \
  -DCLOVER_SDL_DEFAULT_ROM_PATH="/absolute/path/to/game.sfc"
```

The app also accepts `--frames <count>` to stop after a fixed number of frames.
Controls and capture details are documented in
[`docs/FRONTEND.md`](docs/FRONTEND.md).

## Interactive Investigation Captures

Start a new capture from power-on with:

```bash
./cmake-build-sdl-release/clover_sdl "/path/to/game.sfc" \
  --capture /private/tmp/clover-capture-name
```

The destination must not already exist. Press F8 near a visible or audible
problem; the next emulated frame is marked. Exact human reflex timing is not
required because the bundle records the surrounding continuous run.

A version-3 capture contains:

- `joypad1.script` — run-length-compressed SNES controller input
- `audio.wav` — raw 16-bit stereo core audio
- `frames.csv` — per-frame input, audio ranges, marker state, host timing, core
  runtime, presentation time, and SDL audio-queue telemetry
- `marker_frame_XXXXXXXX.ppm` — a screenshot for each F8 marker
- `manifest.txt` — ROM identity, audio format, frame count, discontinuities,
  and marker frames

Replay the controller movie headlessly with a comfortably high step limit:

```bash
CLOVER_JOYPAD1_SCRIPT_FILE=/path/to/capture/joypad1.script \
  ./build/clover_rom_bringup "/path/to/game.sfc" 1000 2000000000
```

`clover_bsnes_bringup` accepts the same environment variable, allowing the
same input sequence to be compared against bsnes.

## Core Validation

Run the low-noise regression loop with:

```bash
python3 scripts/run_core_validation.py
```

The current script runs the hardware loop test plus local and bsnes checkpoint
comparisons for its configured ROM set. It is a regression suite, not proof of
universal compatibility or an exhaustive all-frame sweep.

For a targeted exact comparison:

```bash
python3 scripts/run_reference_sweep.py "/path/to/game.sfc" \
  --frames 300,600,1000 \
  --step-limit 2000000000 \
  --compare-profile exact \
  --output-dir /private/tmp/clover-reference-sweep
```

Exact pixel comparison is the closure criterion. The sweep tool's legacy
`bsnes-libretro-bottom-corner-artifact` profile remains available for older
capture investigations, but masked comparisons must not be used to declare a
hardware issue solved.

Use `clover_rom_bringup` directly for targeted investigation. Its default mode
is summary-only; `CLOVER_BRINGUP_VERBOSE=1` and the specific
`CLOVER_CAPTURE_*` / `CLOVER_TRACE_*` variables enable intrusive diagnostics.
Keep those diagnostics off during performance and timing validation.

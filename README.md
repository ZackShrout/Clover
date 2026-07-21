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

The ROM argument is optional. With no argument, Clover displays its frontend
test pattern; choose **File → Load ROM…** when ready. The pattern immediately
clears when media loads, before the game produces its first visible frame.
Supplying a ROM path remains useful for command-line and deterministic test
runs.

The app also accepts `--frames <count>` to stop after a fixed number of frames.
Controls and capture details are documented in
[`docs/FRONTEND.md`](docs/FRONTEND.md).

### Save RAM and app menu

Battery-backed cartridge RAM is stored beside the ROM with an `.srm`
extension. For example, `Final Fantasy 3 (USA).smc` uses
`Final Fantasy 3 (USA).srm`. A matching save is restored before power-on;
changed save RAM is written atomically during play and again when resetting,
loading another ROM, or quitting.

The SDL menu bar provides:

- **File → Load ROM…** (`Cmd+O` on macOS, `Ctrl+O` elsewhere)
- **Emulation → Reset** (`Cmd+R` or `Ctrl+R`)

Reset models the console reset button and preserves cartridge save RAM. Reset
and ROM switching are disabled during deterministic capture because the
capture format does not encode those actions.

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

## Accuracy Fence

The primary regression ratchet is manifest-driven. Its default `full` suite
runs CTest, captures every frame through frame 1000 for the four milestone ROMs
under Clover and bsnes, replays the validated Final Fantasy III Mode 7 and
combat paths, enforces the bringup guardrails, and compares all requested
frames exactly:

```bash
python3 scripts/run_accuracy_fence.py
```

Use `--build-dir cmake-build-sdl-release` when the tools are in the CLion/SDL
release tree. The runner auto-detects common build directories, verifies local
ROM SHA-256 identities, uses deterministic startup, stores temporary frames
under `/private/tmp`, and removes them after a complete pass. A failure keeps
the frames and logs for investigation.

The suites can also be run separately:

```bash
python3 scripts/run_accuracy_fence.py --suite baseline
python3 scripts/run_accuracy_fence.py --suite interactive
```

Scenario definitions live in `validation/accuracy_fence.json`; captured
controller movies live in `validation/input/`. List or select scenarios with
`--list` and repeatable `--scenario <id>` options. Use `--keep-artifacts` or a
new `--output-dir` when passing output should be retained intentionally.

`scripts/run_core_validation.py` remains available as the older, faster
three-ROM checkpoint loop. It is useful during iteration but is not the full
accuracy fence.

The checked-in Zelda player-selection movie is classified as an
`investigation` scenario. It reproduces the path, but frame-indexed input does
not yet maintain one exact shared trajectory after the title transition. Run
it explicitly with `--suite investigation`; its mismatch is preserved as a
failure, not accepted or masked by the fence.

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

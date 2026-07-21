# Clover

Clover is a performance-minded Super Nintendo Entertainment System emulator focused on clean architecture, faithful behavior, and a stable real-time runtime.

This repository starts from a few non-negotiable project rules:

- `core/` owns emulation and timing.
- `frontend/` owns presentation and user-facing debug views.
- `platform/` owns OS, windowing, input, audio, and runtime shell work.
- Hot paths stay free of permanent debug instrumentation.
- Optional visibility features such as layer masking must not distort the default emulation path.

## Current State

This scaffold establishes the project shape and architectural guardrails before major CPU, PPU, APU, and scheduler implementation work begins.

## Layout

- `src/clover/core/`
- `src/clover/frontend/`
- `src/clover/platform/`
- `src/clover/debugger/`
- `src/clover/utils/`
- `tests/`
- `docs/`

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

### SDL desktop app

CLion can import the checked-in **SDL Release** CMake preset. Select its
`clover_sdl` target and Run; CLion will build the optimized executable first.
With no program arguments it loads the configured
`CLOVER_SDL_DEFAULT_ROM_PATH`, which currently defaults to the local Mortal Kombat
ROM. A different ROM can still be passed as the first program argument.

Add `--capture <new-directory>` to record a deterministic investigation
bundle from power-on. The directory must not already exist. It contains the
compressed SNES joypad movie (`joypad1.script`), raw core audio (`audio.wav`),
per-frame audio sample ranges (`frames.csv`), and a ROM/audio manifest. Press
F8 while playing to mark the next emulated frame and save its screenshot.

Replay a captured input movie in either headless runner with:

```bash
CLOVER_JOYPAD1_SCRIPT_FILE=/path/to/capture/joypad1.script \
    ./clover_rom_bringup /path/to/game.sfc 1000 10000000
```

The same environment variable is accepted by `clover_bsnes_bringup`.

## Core Validation

Use the low-noise validation path for emulator core work. This keeps Clover and bsnes comparisons symmetric by default and leaves heavier bringup diagnostics opt-in.

```bash
python3 scripts/run_core_validation.py
```

That standard loop runs:

- `clover_hardware_loop_test`
- `clover_local_rom_regression_test`
- `scripts/run_reference_sweep.py`

The reference sweep now defaults to the bsnes libretro comparison profile so
known bottom-corner capture artifacts do not show up as emulator-core
regressions. Use `--compare-profile exact` when you intentionally want a raw
buffer compare.

Use `clover_rom_bringup` directly only for targeted investigation. Its default path is now summary-only; set `CLOVER_BRINGUP_VERBOSE=1` or the specific `CLOVER_CAPTURE_*` / `CLOVER_TRACE_*` knobs when you intentionally want intrusive diagnostics.

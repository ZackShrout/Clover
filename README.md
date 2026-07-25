# Clover

Clover is a performance-minded Super Nintendo Entertainment System emulator
focused on hardware-faithful behavior, clean subsystem boundaries, and a stable
real-time runtime.

Hardware behavior is the guiding star. ROM-specific workarounds are not an
acceptable substitute for modeling the machine correctly.

## Current State

Clover currently includes:

- a headless SNES core with 65C816 CPU, PPU, SPC700/DSP audio, DMA/HDMA,
  interrupts, controller input, LoROM/HiROM cartridge mapping, CX4 cartridge
  enhancement hardware, DSP-1B through DSP-4 cartridge processors, Super FX
  cartridge processing, and automatic NTSC/PAL timing
- a system-neutral frontend seam around the SNES core
- an SDL3 desktop app with video, audio, keyboard/gamepad input, frame pacing,
  and deterministic investigation captures
- headless Clover and bsnes bringup tools, exact frame comparison tools, and
  hardware-focused regression tests

The emulator is not yet universally compatible. Enhancement chips and less
common hardware edge cases remain active work; see
[`docs/KNOWN_SIMPLIFICATIONS.md`](docs/KNOWN_SIMPLIFICATIONS.md).
The normative late 3-chip target and revision policy are defined in
[`docs/SNES_HARDWARE_MODEL.md`](docs/SNES_HARDWARE_MODEL.md).

The first base-console accuracy milestone was established on July 22, 2026.
The complete curated validation battery is green for the canonical NTSC late
3-chip profile, including the 65C816 and SPC700 conformance lanes, the five-ROM
2000-frame accuracy fence, deterministic interactive regressions, and the
hires/interlace closure set. Its exact scope and non-claims are recorded in
 [`docs/SNES_BASE_ACCURACY_MILESTONE.md`](docs/SNES_BASE_ACCURACY_MILESTONE.md).

CX4 support was added after that milestone. Mega Man X2 now participates in the
retail accuracy fence and compares exactly with bsnes across its 800-frame
power-on sequence. CX4 is modeled at its command interface; instruction-level
HG51BS169 execution and command latency remain future accuracy work.

DSP-1B and DSP-2 support were added after CX4. DSP-1B has deterministic
command, protocol, status-register, raster-streaming, and LoROM/HiROM mapper
coverage. A deterministic Pilotwings replay reaches active light-plane flight,
where frames 6200-6500 are compared exactly against bsnes; manual play also
completed the first challenge and reached the second. DSP-2 implements commands
`$01`, `$03`, `$05`, `$06`, `$09`, and `$0d` plus the Dungeon Master mapper;
unsupported commands return no output. Dungeon Master ran for 9,000 scripted
frames with sampled animated output matching bsnes exactly, followed by a
successful manual in-game session through hero resurrection.

DSP-3 and DSP-4 support now covers their cartridge maps, byte/word transfer
protocols, fixed data, and complete command state machines. DSP-3 has
deterministic command and mapper coverage. SD Gundam GX now provides a stable
11,000-frame retail intro/menu lane exercising ROM identification/test/dump,
Shannon-Fano decode, coordinate streaming, and bitmap conversion. DSP-4 has
deterministic arithmetic, lookup, OAM, streaming-projection, termination, and
mapper coverage. Top Gear 3000 completed a 4,887-frame interactive run with a
captured active-race frame at 4,732 and clean audio-queue telemetry. Its
checked-in
[`controller movie`](validation/input/top-gear-3000-active-race.joypad1.script)
reproduces that marker exactly through both frame-at-a-time and hardware-step
replay.

Super FX support now implements the GSU instruction set, register interface,
instruction cache, ROM/RAM buffering and contention, IRQ signaling, PLOT/RPIX
pixel caches, MARIO/GSU-1/GSU-2 cartridge maps, and volatile versus
battery-backed expansion RAM. Deterministic tests cover execution, flags,
bitplane output, delayed buffer completion, bus arbitration, mapper variants,
and IRQ acknowledgement.
Star Fox, Stunt Race FX, Doom, and Yoshi's Island all complete 600-frame
bringup runs without a terminal PC or placeholder CPU opcode; Star Fox, Stunt
Race FX, and Yoshi's Island also match bsnes exactly at frame 120. Star Fox's
full 32 KiB GSU RAM matched bsnes at the first graphics upload. Stunt Race FX
and Yoshi's Island now match bsnes exactly at frame 600 as well. The fixes
needed for those late lanes also hardened base-console HDMA: per-frame
completion reset now includes disabled channels, and HDMA setup/transfers
preempt and resume an in-progress general DMA at byte boundaries. Star Fox's
late attract-mode scene is now free of the former active-display palette
corruption, but its simulation state is not pixel-exact at frame 600. Doom's
frame-600 title composition is visually intact but also remains non-exact.

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
test pattern; choose **File → Import ROM to Library…** when ready. The pattern
immediately clears when media loads, before the game produces its first visible frame.
Supplying a ROM path remains useful for command-line and deterministic test
runs and does not add that ROM to the library.

The app also accepts `--frames <count>` to stop after a fixed number of frames.
Controls and capture details are documented in
[`docs/FRONTEND.md`](docs/FRONTEND.md).

### Downloadable beta builds

GitHub Actions builds portable Clover packages for Windows x64, universal
macOS, and Linux x86-64 on every push to `main`. Version tags such as
`v0.1.0-beta.1` publish those packages together as a GitHub prerelease.
The complete workflow, signing-secret setup, and release commands are in
[`docs/RELEASING.md`](docs/RELEASING.md).

### ROM library, Save RAM, and app menu

Imported ROMs and battery saves live under Clover's SDL-resolved application
data directory rather than beside source ROMs. SNES media is identified by the
SHA-256 of its canonical cartridge bytes, ignoring an optional 512-byte copier
header. Importing the same cartridge from multiple locations therefore creates
one library entry and one save identity.

The SQLite-backed library stores immutable canonical ROM copies under
`library/roms/snes/<sha256>.sfc`. Battery RAM is stored separately under
`saves/snes/<sha256>/battery.srm`. If no central save exists, a matching sibling
`.srm` from the selected source ROM is copied into central storage once. Changed
save RAM is written during play and again when resetting, switching ROMs, or
quitting.

The SDL menu bar provides:

- **File → Import ROM to Library…** copies, deduplicates, indexes, and opens a ROM
- **File → Open ROM Library…** (`Cmd+O` or `Ctrl+O`) opens an imported game
- **File → Open ROM Temporarily…** opens without importing
- **File → Quit** (`Cmd+Q` or `Ctrl+Q`) exits after flushing dirty save RAM
- **Emulation → Pause** (`Space`) stops host-driven frame advancement
- **Emulation → Frame Advance** (`.`) pauses and advances exactly one frame
- **Emulation → Reset** (`Cmd+R` or `Ctrl+R`)
- **Emulation → Speed** selects 0.5×, 1×, 2×, 4×, or unlimited host pacing
- **Video** exposes the active core's optional presentation planes; SNES provides
  BG1–BG4 and Objects

Reset models the console reset button and preserves cartridge save RAM. Reset
and ROM switching, pause/frame advance, speed changes, and video-plane overrides
are disabled during deterministic capture because the capture format does not
encode those actions. Non-1× speed modes currently mute presentation audio so
the host never plays native-rate samples at the wrong wall-clock rate. Faster
modes batch hardware frames between window presentations, keeping display
refresh from silently pinning emulation to 1×.

## Interactive Investigation Captures

Start a new capture from power-on with:

```bash
./cmake-build-sdl-release/clover_sdl "/path/to/game.sfc" \
  --capture /private/tmp/clover-capture-name
```

The destination must not already exist. Press F8 near a visible or audible
problem; the next emulated frame is marked. Exact human reflex timing is not
required because the bundle records the surrounding continuous run.

A version-5 capture contains:

- `joypad1.script` — run-length-compressed SNES controller input
- `joypad2.script` — the same input movie for controller port 2
- `initial_save_ram.srm` — immutable battery RAM as it existed before frame 1,
  when the cartridge provides persistent memory
- `audio.wav` — raw 16-bit stereo core audio
- `frames.csv` — per-frame input, audio ranges, marker state, host timing, core
  runtime, presentation time, and SDL audio-queue telemetry
- `marker_frame_XXXXXXXX.ppm` — a screenshot for each F8 marker
- `manifest.txt` — ROM identity, audio format, frame count, discontinuities,
  and marker frames

Replay the controller movie headlessly with a comfortably high step limit:

```bash
CLOVER_JOYPAD1_SCRIPT_FILE=/path/to/capture/joypad1.script \
  CLOVER_JOYPAD2_SCRIPT_FILE=/path/to/capture/joypad2.script \
  CLOVER_SAVE_RAM_FILE=/path/to/capture/initial_save_ram.srm \
  ./build/clover_rom_bringup "/path/to/game.sfc" 1000 2000000000
```

Set `CLOVER_RUN_FRAME_REPLAY=1` when reproducing an SDL marker or measuring the
frontend-equivalent frame path. The default diagnostic mode advances individual
hardware steps so it can report timing, DMA, and failure state.

`clover_bsnes_bringup` accepts the same environment variable, allowing the
same input sequence to be compared against bsnes.

## Accuracy Fence

The primary regression ratchet is manifest-driven. Its default `full` suite
runs CTest, captures every frame through frame 2000 for the five milestone ROMs,
replays the 800-frame Mega Man X2 CX4 lane, runs Mega Man X3's eight-part CX4
self-test through controller port 2, compares both under Clover and bsnes, and
replays the validated save-RAM-backed Final Fantasy III
world-map path, enforces the bringup guardrails, and compares all requested
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

## Hardware Test Laboratory

Self-reporting and hardware-characterization ROMs have their own manifest-driven
runner. It inventories 95 curated scenarios from six provenance groups,
verifies their hashes, decodes supported terminal pass/fail banners, records
hardware-revision scope, and preserves JSON, Markdown, frame, and log artifacts:

```bash
python3 scripts/run_hardware_validation.py --suite smoke
python3 scripts/run_hardware_validation.py --suite all
python3 scripts/run_hardware_validation.py --suite all --regression
```

bsnes is retained as useful differential evidence, but agreement with it is not
labeled hardware correctness. Only provenance-rich real-SNES references can
produce a `VERIFIED` result. The evidence hierarchy, status vocabulary, reference
format, and subsystem suites are documented in
[`docs/HARDWARE_VALIDATION.md`](docs/HARDWARE_VALIDATION.md).
Corpus selection, source revisions, and deferred hardware lanes are documented
in [`docs/SNES_TEST_CORPUS.md`](docs/SNES_TEST_CORPUS.md).

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

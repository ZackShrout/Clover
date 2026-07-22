# Clover Accuracy Baseline

## Purpose

This document records what Clover has actually demonstrated. It is not a
compatibility promise. A passing ROM remains evidence about the hardware paths
that ROM exercised, not permission to add ROM-specific behavior.

The project standard is hardware-faithful emulation. Exact comparison against
bsnes is a powerful reference, while hardware documentation and tests remain
authoritative when reference behavior needs reconciliation.

## Validation Modes

### Deterministic comparison

Deterministic startup is the default for Clover-versus-bsnes work:

- Clover startup entropy is disabled by default in the bringup harness.
- The bsnes harness also requests deterministic startup by default.
- Input movies can be replayed in both runners through
  `CLOVER_JOYPAD1_SCRIPT_FILE`.

Relevant controls:

- `CLOVER_STARTUP_ENTROPY=none|low|high`
- `CLOVER_STARTUP_ENTROPY_SEED=<integer>`
- `CLOVER_STARTUP_ENTROPY_SEQUENCE=<integer>`
- `CLOVER_BSNES_ENTROPY=None|Low|High`

The older PPU-only entropy variables remain as compatibility aliases:

- `CLOVER_PPU_ENTROPY`
- `CLOVER_PPU_ENTROPY_SEED`
- `CLOVER_PPU_ENTROPY_SEQUENCE`

### Entropy mode

Clover can model undefined cold-boot state for WRAM, VRAM, CGRAM, and related
startup-visible state, including warm-reset preservation where modeled.
Entropy mode is for startup realism and undefined-state investigations; it is
not the default for deterministic frame comparison.

## Comparison Contract

An exact visual match means:

- identical 256x240 rendered pixels at the same emulated frame boundary
- equivalent power-on and controller input sequences
- comparable capture timing relative to the completed frame

When memory or event dumps are part of an investigation, their observation
points must also be equivalent. Similar screenshots or matching frame rates are
not sufficient.

The `exact` frame-compare profile is the closure criterion. A legacy profile
can mask known extreme-corner differences in older bsnes libretro captures, but
masked output is diagnostic only and cannot establish pixel perfection.

## Automated Milestone Set

This retail fence forms one part of the July 22, 2026 base-console milestone.
The complete evidence statement, including the conformance laboratory and
remaining capability boundaries, is recorded in
[`SNES_BASE_ACCURACY_MILESTONE.md`](SNES_BASE_ACCURACY_MILESTONE.md).

The established deterministic no-input milestone set is:

- Super Mario World (USA)
- The Legend of Zelda: A Link to the Past (USA)
- Final Fantasy III (USA)
- Chrono Trigger (USA)
- Mortal Kombat (USA)

The most recently completed continuous sweeps compared every rendered frame
through frame 2000 exactly against bsnes for all five ROMs. Commercial ROM
images and generated sweep directories are deliberately not stored in the
repository; they must be reproduced from local ROM copies.

This 2000-frame result supersedes the old four-ROM, 1000-frame baseline. It is
encoded in `validation/accuracy_fence.json` and reproduced by
`scripts/run_accuracy_fence.py`. The older `run_core_validation.py` command is
still a smaller regression checkpoint loop and should not be confused with the
full milestone sweep.

## Interactive Validation Record

Deterministic power-on sweeps do not reach input-gated paths. SDL capture and
shared input replay were subsequently used to investigate and correct:

- Chrono Trigger post-title corruption and CGRAM color-zero borders/flashes
- Zelda player-name entry and the stuck state after selecting a player
- Final Fantasy III sound-effect corruption, host-time startup/stutter
  regressions, the Mode 7 snow-march transition, and a combat scanline defect
- Mortal Kombat audio queue failure and CGRAM color-zero border behavior

For the earlier Final Fantasy III rendering pass:

- the Mode 7 transition comparison was exact for frames 8300 through 8425
- the affected combat scanline matched at the checked marker frames
- live playtesting found no remaining issue in the previously reported paths

Those original power-on controller movies no longer maintain one shared
Clover/bsnes gameplay trajectory after the dungeon input sequence. They remain
checked in under the `investigation` suite rather than being counted as green
coverage. The save-RAM-backed `ff3-world-map-color-math` scenario replaces the
fragile multi-thousand-frame preamble for the current deep-game regression and
matches exactly for frames 1880 through 1930.

These statements describe the paths tested. They do not imply that every later
game state, PPU edge case, or audio sequence has been exhausted.

## Bringup Guardrails

The Clover bringup summary reports `terminal_pc` and
`cpu_placeholder_opcodes`. `cpu_placeholder_opcodes` must remain zero in a
known-good run. A nonzero value means execution reached the fallback path for a
CPU opcode without an explicit Clover execution/timing implementation.

Timing and performance investigations must be run without optional trace and
capture probes unless those probes are the subject of the test. Always-on
diagnostics have previously produced host-time slowdown without changing the
number of emulated frames, so wall-clock behavior and emulated-frame behavior
must be measured separately.

## Reproducing Comparisons

Run the complete passing fence—five-ROM baseline plus the validated save-based
FF3 interactive window—with:

```bash
python3 scripts/run_accuracy_fence.py
```

Run only the five-ROM baseline or passing Final Fantasy III input movie with:

```bash
python3 scripts/run_accuracy_fence.py --suite baseline
python3 scripts/run_accuracy_fence.py --suite interactive
```

The runner uses frame-only Clover dumps so routine sweeps do not write the
larger APU/VRAM/OAM/CGRAM investigation payload for every frame. It removes
passing temporary artifacts and preserves failures. Use
`scripts/run_reference_sweep.py` for smaller ad hoc checkpoints and SDL
version-4 captures when a new input, save-RAM, or audio path must first be recorded.

Interactive scenarios may declare a bsnes input-frame offset. The SDL movie
records input as applied before Clover's `run_frame()`, while the bsnes libretro
callback associates the same newly visible input response with the preceding
presented-frame boundary. The checked-in offset aligns those harness
observation points; it does not permit a video-frame comparison offset, which
remains fixed at zero.

The Zelda player-selection and original long FF3 rendering movies remain in the
manifest under the `investigation` suite. Their later frame-indexed trajectories
diverge. The fence therefore reproduces and fails those scenarios explicitly
instead of treating a visual offset or partial outcome as an accuracy pass.

The closed low-level reference investigations are preserved as historical
context in [`archive/REFERENCE_RECONCILIATION.md`](archive/REFERENCE_RECONCILIATION.md).

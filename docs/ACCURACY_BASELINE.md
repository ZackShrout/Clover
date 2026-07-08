# Clover Accuracy Baseline

## Purpose

This document records the current hardware-accuracy baseline for Clover so we can
tell the difference between:

- a regression against a known-good checkpoint
- a newly exposed inaccuracy in an area we already know is simplified
- a harness mismatch between Clover and bsnes

## Current Validation Modes

### Deterministic bringup mode

The default Clover-vs-bsnes comparison mode is deterministic.

- Clover startup entropy is disabled by default in the validation harness.
- bsnes entropy is also forced to deterministic startup by default in the
  bsnes bringup harness.
- This is the mode to use for exact frame-by-frame and memory-dump comparisons.

Relevant knobs:

- `CLOVER_STARTUP_ENTROPY=none|low|high`
- `CLOVER_STARTUP_ENTROPY_SEED=<integer>`
- `CLOVER_STARTUP_ENTROPY_SEQUENCE=<integer>`
- `CLOVER_BSNES_ENTROPY=None|Low|High`

Backward-compatible fallback env vars still exist for the older PPU-only path:

- `CLOVER_PPU_ENTROPY`
- `CLOVER_PPU_ENTROPY_SEED`
- `CLOVER_PPU_ENTROPY_SEQUENCE`

### Startup entropy mode

Clover can also model bsnes-style undefined cold-boot state.

Currently modeled:

- WRAM cold-boot entropy
- PPU VRAM cold-boot entropy
- PPU CGRAM and related startup-visible PPU state
- warm-reset preservation where bsnes preserves state

The intended workflow is:

- use deterministic mode for exact Clover-vs-bsnes debugging
- use entropy mode for realism / undefined-startup investigations

## Comparison Contract

Unless a test says otherwise, a "match" currently means:

- identical rendered frame output at the checked frame
- analogous frame capture point relative to vblank entry
- identical Clover-side dump timing for VRAM/CGRAM/OAM/WRAM when requested
- comparable CPU/PPU/APU event summaries in the bringup harness

The standard reference sweep uses the
`bsnes-libretro-bottom-corner-artifact` compare profile. This masks a verified
bsnes libretro capture artifact in the extreme bottom corners of the 256x240
output buffer, while leaving the active picture comparison exact.

## Milestone ROMs

These ROMs are our current real-world baseline set:

- `roms/local/Super Mario World (USA).sfc`
- `roms/local/Legend of Zelda, The - A Link to the Past (USA).sfc`
- `roms/local/Final Fantasy 3 (USA).smc`

These were chosen because together they exercised:

- CPU / PPU / APU bringup sequencing
- DMA / HDMA behavior
- OBJ and active-display PPU behavior
- APU handshakes and timer-driven cadence
- reset / startup-state interactions

## Archived Deterministic Sweep

Post-milestone archival deterministic sweeps are stored under:

- `reference-sweep-zelda-300-deterministic-20260708`
- `reference-sweep-smw-300-deterministic-20260708`
- `reference-sweep-ff3-300-deterministic-20260708`

Bulk one-pass capture archives for the same 300-frame window are stored under:

- `bulk-zelda-bsnes-300`
- `bulk-zelda-clover-300`
- `bulk-smw-bsnes-300`
- `bulk-smw-clover-300`
- `bulk-ff3-bsnes-300`
- `bulk-ff3-clover-300`

These 300-frame sweeps are the authoritative post-startup-semantics baseline,
and the current harness interpretation is:

- Zelda matches through frame 300 in the active picture
- SMW matches through frame 300
- FF3 matches through frame 300

## Known Strong Checkpoints

The following deterministic checkpoints were explicitly re-verified after the
startup entropy and reset-semantics work:

- Zelda frame 83
- SMW frame 86
- FF3 frame 117

These were the critical comparison points we had previously used while
investigating drift, so they remain useful spot-check frames even when the full
300-frame sweeps are also available.

## Current Real-World Status

Under the deterministic Clover-vs-bsnes harness:

- `Legend of Zelda, The - A Link to the Past (USA).sfc`
  matches through frame 300 in the active picture; raw exact-buffer compares
  still expose a known bsnes libretro bottom-corner artifact
- `Super Mario World (USA).sfc`
  matches through frame 300
- `Final Fantasy 3 (USA).smc`
  matches through frame 300 in the current archived sweep

## Bringup Guardrails

The bringup harness now reports:

- `terminal_pc`
- `cpu_placeholder_opcodes`

`cpu_placeholder_opcodes` should remain `0` on known-good bringup sweeps. A
non-zero value means execution hit the CPU fallback path for an opcode that does
not yet have an explicit Clover timing/execution model.

Notably, the milestone ROM bringup path now reaches the current 300-frame
baseline with `cpu_placeholder_opcodes=0`, so the CPU fallback opcode path is
not responsible for the previously investigated Zelda/SMW drift work.

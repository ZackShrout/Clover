# SNES Base Accuracy Milestone v1

Established: July 22, 2026  
Milestone key: `snes-base-accuracy-v1`

## Meaning

This is Clover's first stable accuracy milestone for the base SNES motherboard.
It records a green validation state for the canonical NTSC `late-3chip` profile
without ROM-specific compatibility behavior.

The milestone means that every applicable automated test in the curated battery
reaches its accepted, profile-scoped result and that all exact differential
regressions included in the green suites close at the same emulated observation
boundary. It is a regression ratchet: later work must preserve this behavior
unless stronger physical-hardware evidence demonstrates that the model itself
must change.

## Evidence Included

- all seven compiled CTest targets pass, including focused hardware-loop tests;
- gilyon's comprehensive 65C816 and SPC700 functional suites reach their stable
  passing terminal results;
- the independent Peter Lemon instruction corpus agrees exactly with the
  canonical differential reference;
- the established blargg, Sour, undisbeliever, and community PPU procedures
  produce their accepted self-report or characterization results;
- Super Mario World, The Legend of Zelda: A Link to the Past, Final Fantasy III,
  Chrono Trigger, and Mortal Kombat compare exactly for every rendered frame
  through frame 2000;
- the deterministic save-RAM-backed Final Fantasy III world-map color-math path
  is an exact interactive regression, while retained capture movies reproduce
  the previously investigated Zelda, Chrono Trigger, Final Fantasy III, and
  Mortal Kombat paths without counting divergent long trajectories as passes;
- the Mode 6 offset-per-tile, PPU bus-activity, split-screen, and Mode 5
  interlace observations compare exactly at frame 300;
- validated bringup runs report zero placeholder CPU opcodes and do not terminate
  at an invalid program counter.

The manifests remain the machine-readable authority:

- [`../validation/accuracy_fence.json`](../validation/accuracy_fence.json)
- [`../validation/hardware_tests.json`](../validation/hardware_tests.json)

## What This Does Not Claim

This milestone is not a claim that every possible SNES state has been tested or
that every cartridge is supported. In particular, it does not include:

- enhancement hardware such as CX4, Super FX, SA-1, or cartridge DSP families;
- ExLoROM, ExHiROM, and other unusual cartridge configurations;
- mouse, multitap, Super Scope, and other specialized input devices;
- equivalent evidence depth for PAL or for early 3-chip, S-CPU-A, and 1CHIP
  motherboard revisions;
- broad provenance-rich captures from a physical late 3-chip console.

Self-reporting conformance ROMs provide behavioral verdicts. Exact bsnes
agreement provides strong differential evidence. Only an approved reference
captured from the declared physical hardware profile earns the validation status
`VERIFIED`.

The active risk and capability list remains
[`KNOWN_SIMPLIFICATIONS.md`](KNOWN_SIMPLIFICATIONS.md). The hardware evidence
rules remain [`HARDWARE_VALIDATION.md`](HARDWARE_VALIDATION.md).

## Reproducing the Milestone

From a configured build containing the local hash-pinned ROM corpus:

```bash
ctest --test-dir cmake-build-sdl-release --output-on-failure
python3 scripts/run_accuracy_fence.py --build-dir cmake-build-sdl-release
python3 scripts/run_hardware_validation.py --suite all \
  --build-dir cmake-build-sdl-release
```

The hardware runner intentionally preserves the distinction between
self-reporting passes, characterization observations, bsnes differential
matches, profile exclusions, and real-hardware verification.

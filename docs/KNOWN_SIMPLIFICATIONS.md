# Known Simplifications

This list is for active hardware-faithfulness gaps that are understood and
should not be mistaken for already-solved behavior.

## CPU

### Placeholder opcode fallback still exists

Location:

- `src/clover/core/snes/Cpu.cpp`

Current behavior:

- if an opcode is not handled by any explicit execute path, Clover retires it as
  one fetched opcode plus one trailing idle cycle

Why it matters:

- this is not a real hardware-timing model
- it is only acceptable as a guardrail for unimplemented opcodes, not as a
  tuning mechanism

Current mitigation:

- bringup now reports `cpu_placeholder_opcodes`
- milestone sweeps should keep this at `0`

Next step:

- replace fallback timing with explicit opcode coverage until the fallback is no
  longer reachable for any intended software path

## PPU

### Coverage is not exhaustive

The current milestone coverage does not imply full PPU closure. We should still
expect future work in:

- window / mosaic / Mode 7 edge cases
- mid-scanline register effects
- less common active-display access quirks

## APU / DSP

### Reset semantics are stronger than runtime-internal proof

Cold-boot and warm-reset state handling are covered for:

- APURAM preservation across warm reset
- DSP `FLG` soft-reset behavior

What is not yet implied by that:

- full DSP internal-state equivalence
- full SPC/DSP pipeline equivalence under all test ROMs

## Harness

### Deterministic comparison is the default for validation

This is intentional. It means:

- exact Clover-vs-bsnes comparisons should happen in deterministic mode
- entropy mode should be treated as a realism mode, not the default regression mode

Any new comparison tool or test should preserve that distinction.

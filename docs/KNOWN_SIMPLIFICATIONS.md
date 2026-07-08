# Known Simplifications

This list is for active hardware-faithfulness gaps that are understood and
should not be mistaken for already-solved behavior.

## CPU

### Placeholder opcode fallback still exists

Location:

- `src/clover/core/Cpu.cpp`

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

### CPU timing still needs broader systematic proof

Even though the current ROM milestone is strong, CPU timing work is not "done."
Areas that still deserve targeted validation include:

- exhaustive opcode timing coverage
- interrupt entry / observation edge cases beyond the current tests
- auto-joypad / controller latch edge timing
- additional DMA / HDMA race scenarios

Current evidence that this remains unfinished:

- Zelda stays exact through frame 167 and diverges at 168
- SMW stays exact through frame 193 and diverges at 194
- both divergences occur with `cpu_placeholder_opcodes=0`

That means the active-path timing mismatch is elsewhere than the explicit
fallback opcode placeholder, even though the placeholder still needs eventual removal.

## PPU

### Coverage is stronger than before, but not exhaustive

The OBJ path and active-display behavior are much closer to bsnes now, but we
should still expect future work in:

- window / mosaic / Mode 7 edge cases
- mid-scanline register effects
- less common active-display access quirks

## APU / DSP

### Reset semantics are aligned more closely than runtime internals

Cold-boot and warm-reset state handling are now more bsnes-like for:

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

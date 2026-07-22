# Known Simplifications and Unsupported Hardware

This is the active list of understood gaps. A ROM appearing to work does not
close these areas; a gap is closed only by implementing and validating the
hardware behavior.

## Cartridge Hardware

Clover currently maps ordinary LoROM and HiROM cartridges with SRAM. It does
not implement enhancement coprocessors or their mapping/timing behavior,
including CX4, Super FX, SA-1, and cartridge DSP families.

Consequences:

- Mega Man X2 and other CX4 software are unsupported even if they boot or avoid
  crashing.
- ExLoROM/ExHiROM and unusual cartridge layouts have not been established as
  supported.
- Header scoring recognizes a base mapping; it is not a substitute for parsing
  and constructing the complete cartridge hardware configuration.

## CPU

An explicit fallback remains in `Cpu.cpp` for an opcode that reaches no
implemented execution path. It retires the fetched opcode with a trailing idle
cycle and is a guardrail, not a hardware model.

`clover_rom_bringup` reports this as `cpu_placeholder_opcodes`. It must remain
zero for every validated run. Future work should make the fallback unreachable
for all 65C816 opcodes and modes, then remove it.

## PPU

The validated ROM paths cover substantial background, object, window, color
math, DMA/HDMA, and Mode 7 behavior, but PPU coverage is not exhaustive.
Remaining risk is highest around:

- rare mid-scanline register changes and exact dot effects
- less common window, mosaic, interlace, overscan, and pseudo-hires cases
- active-display access restrictions and latch/open-bus edge cases
- object overflow/range behavior and unusual priority interactions

These must be solved as hardware timing/composition behavior, never as
per-title rendering exceptions.

## APU and DSP

Clover implements SPC700 execution, timers, ports, DSP state, BRR/voice output,
echo, and real-time stereo delivery. The corrected Final Fantasy III sound
effects are strong coverage, not proof of complete internal equivalence.

Full SPC/DSP pipeline timing, reset state, voice edge cases, echo behavior, and
all CPU/APU synchronization patterns remain open to additional reference and
hardware tests.

## Video Standard and Input Devices

- NTSC has substantially deeper retail regression evidence than PAL. PAL timing,
  region reporting, and automatic cartridge selection are active, but uncommon
  PAL raster/interlace edge cases still need hardware characterization.
- Early 3-chip, S-CPU-A, and 1CHIP behavior is cataloged but not selectable until
  each profile's observable differences are implemented and validated.
- The core/frontend seam currently models standard gamepad state on two logical
  ports.
- The SDL app presents one keyboard/gamepad-controlled port. Mouse, multitap,
  Super Scope, and other devices are not implemented.

## Validation Scope

Deterministic Clover-versus-bsnes comparison is the default regression mode.
Entropy mode exists for undefined cold-boot-state investigations.

The five-ROM 2000-frame exact baseline and later interactive captures cover
only the paths executed. They do not imply universal compatibility. Exact
pixels, audio evidence, hardware state, and equivalent observation points are
required according to the fault being investigated; a game merely reaching a
screen is not sufficient.

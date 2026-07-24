# Known Simplifications and Unsupported Hardware

This is the active list of understood gaps. A ROM appearing to work does not
close these areas; a gap is closed only by implementing and validating the
hardware behavior.

## Cartridge Hardware

Clover currently maps ordinary LoROM and HiROM cartridges with SRAM and
supports CX4, DSP-1B, and DSP-2 command devices.

| Cartridge hardware | Status | Current evidence and boundary |
|---|---|---|
| CX4 | Supported at command level | Mega Man X2 has an 800-frame exact retail lane; Mega Man X3's eight-part CX4 self-test is replayed through controller port 2. |
| DSP-1B | Supported at command level | Deterministic arithmetic, projection, data-ROM, status, bidirectional-register, continuous-raster, termination, and LoROM/HiROM mapping tests. Pilotwings has a replayable active-flight exact retail lane and manual play through its first challenge. DSP-1/DSP-1A silicon differences and instruction timing are not modeled separately. |
| DSP-2 | Supported at command level | Commands `$01`, `$03`, `$05`, `$06`, `$09`, and `$0d` plus every mapper window have deterministic coverage; unsupported commands produce no output. Dungeon Master completed a 9,000-frame scripted run with 25 sampled frames matching bsnes exactly, plus manual play through hero resurrection. |
| DSP-3 | Unsupported | Detection exists, but there is no command processor or mapper implementation. |
| DSP-4 | Unsupported | Detection exists, but there is no command processor or mapper implementation. |
| Super FX / SA-1 | Unsupported | No processor or cartridge mapping implementation. |

Consequences:

- CX4 currently uses synchronous command-level emulation. Its 3 KiB RAM,
  register interface, data transfer, arithmetic, sprite construction, and
  graphics commands are implemented, but HG51BS169 instruction timing, command
  latency, and contention are not yet modeled.
- DSP-1B and DSP-2 are high-level command models. Their internal NEC uPD77C25
  instruction execution, command latency, and host contention are not modeled.
- DSP-1B's automated commercial-game path covers Pilotwings power-on, menus,
  and an active light-plane flight. Manual play completed the first challenge
  and reached the second. This does not exercise every vehicle, viewpoint, or
  DSP-1 command sequence used later in the game.
- DSP-2's automated commercial-game path covers power-on and the long animated
  introduction. The later in-game check was manual rather than a replayable
  accuracy-fence scenario.
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

The five-ROM 2000-frame base-console baseline, the CX4 lanes, DSP-1B
command-level tests and Pilotwings flight lane, DSP-2's Dungeon Master
evidence, and later interactive captures cover only the paths executed. They
do not imply universal compatibility. Exact pixels, audio evidence, hardware
state, and equivalent observation points are required according to the fault
being investigated; a game merely reaching a screen is not sufficient.

# SNES Hardware Model Contract

Clover's canonical SNES is a **late 3-chip console**. This is a normative core
contract, not a description of whichever behavior happens to satisfy a ROM or
match another emulator.

## Canonical Profile

The profile key is `late-3chip` and identifies:

- S-CPU B, reporting CPU version 2
- discrete S-PPU1, reporting version 1
- discrete S-PPU2 B/C, reporting version 3
- late integrated S-APU behavior
- NTSC or PAL console timing as a separate region axis

The S-CPU B target intentionally excludes defects unique to the original S-CPU
and S-CPU A. A test designed to exhibit one of those defects is not a failure of
the canonical profile when that defect is absent.

`src/clover/core/snes/HardwareProfile.h` is the executable catalog. Compile-time
defaults, CPU/PPU status-register versions, timing selection, validation labels,
and documentation must agree with it.

## Region Selection

Automatic selection reads the cartridge destination code. Codes `$02-$0c` and
`$11` select PAL timing; other codes select NTSC timing, including PAL-M Brazil,
whose console timing is 60 Hz. Callers may explicitly override the region for a
bad header or a deliberate experiment.

The active region controls frame geometry and master-clock frequency, the
CPU/PPU raster counters, the CPU/APU frequency ratio, frontend refresh reporting,
and the PAL bit in `STAT78`. PAL support is now active but has not yet earned the
same retail and real-hardware evidence depth as NTSC; that evidence gap must
remain visible in validation reports.

## Other Physical Revisions

The catalog reserves these profile keys:

- `early-3chip`
- `scpu-a-3chip`
- `1chip`

They are recognized but unavailable until their observable differences are
implemented and validated. Selecting one must fail clearly. Clover must never
rename late-3chip behavior and present it as another revision.

Revision differences belong in typed hardware traits: DMA/HDMA behavior,
refresh and setup positions, INIDISP behavior, status versions, APU timer
behavior, and other observable effects. They must not be selected by ROM title,
hash, program counter, or compatibility list.

## Validation Meaning

Every hardware test declares the profiles to which it applies. The canonical
validation command selects `late-3chip` unless explicitly overridden. Tests
scoped only to another revision are `NOT_APPLICABLE`, not failures and not
passes.

Result meanings are deliberately distinct:

- `VERIFIED`: exact agreement with an approved reference from the selected
  physical profile
- `HARDWARE_DIFFERENCE`: disagreement with that matching reference
- `NOT_APPLICABLE`: the experiment targets another revision
- self-report results: the test ROM's own verdict, interpreted within its stated
  hardware scope
- bsnes comparison: differential evidence, never hardware proof

Analog composite/RGB character—blur, encoder color, ordinary DAC appearance,
and similar presentation effects—belongs outside the emulation core. A physical
effect stays in the core only when it changes observable raster behavior, such
as revision-specific mid-scanline INIDISP response.

## Compatibility Policy

Cartridge coprocessors and peripherals are orthogonal to motherboard revision.
Supporting every licensed ROM means implementing its cartridge hardware and the
correct regional timing, not mutating the console model per title. Future
revision profiles may be user-selectable for preservation and testing; the SDL
application default remains `late-3chip`.

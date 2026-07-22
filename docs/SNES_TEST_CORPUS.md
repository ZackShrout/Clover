# SNES Test Corpus

Clover's local SNES laboratory is intentionally a curated corpus, not a dump of every ROM that could be found. A test earns a lane when it adds an independently useful hardware claim, has identifiable provenance, and can eventually produce a deterministic verdict or artifact.

The binary ROMs live under `roms/local/` and remain untracked. Their paths and SHA-256 identities are pinned in `validation/hardware_tests.json`. This keeps copyrighted or redistributability-unclear binaries out of Git while making local results reproducible.

## Conformance backbone

| Source | Coverage | Role |
|---|---|---|
| gilyon `snes-tests` | Every 65C816 opcode except WAI/STP; every SPC700 opcode except SLEEP/STOP; addressing modes, flags, edge cases, and wrapping | Primary CPU/SPC functional conformance |
| Peter Lemon `SNES` | Independent CPU and SPC700 instruction examples, including miscellaneous WAI/STP/reset coverage | Independent cross-check and gap coverage |
| Sour `SnesTests` | Nearly all 65C816 instruction cycle counts in all M/X combinations; DMA interrupt-boundary behavior | CPU and DMA timing conformance |
| blargg | SPC700, DSP, timers, controller I/O, and CPU I/O behavior | Established subsystem self-tests |
| undisbeliever | Real-hardware-focused INIDISP, HDMA, and revision-specific edge cases | PPU/DMA characterization by hardware profile |

## PPU breadth lane

The community PPU lane adds focused tests for Mode 2/6 offset-per-tile, Mode 3, Mode 5/interlace, Mode 7 EXTBG, color math, HBlank VRAM DMA, mid-frame HDMA, split-screen timing, and PPU bus activity. These are characterization tests until a result is tied to a cited real-hardware capture. An exact bsnes comparison is useful differential evidence, but is never promoted to hardware proof.

The original PPU bus Sigrok capture and cooked CSV are evidence inputs, not executable ROM tests. They should be retained with their provenance until Clover has a bus-trace comparator capable of consuming them.

## Deliberately deferred lanes

- Mouse, multitap, XBand keyboard, Turbo File Twin, rumble, and similar peripheral tests wait for the corresponding device model and deterministic input automation.
- GSU, SA-1, MSU-1, and other enhancement-chip tests belong in separate capability lanes. A cartridge is not marked as base-SNES incompatible merely because its enhancement chip is not implemented.
- Mid-frame human-input experiments such as `tellinglys` wait for sub-frame input scripting; frame-boundary scripts would answer a different hardware question.
- Sour's general `timing_test` remains excluded because its upstream author labels it work in progress.
- Large tutorial/demo collections are retained as source material only when they do not add a precise, automatable hardware assertion.

## Imported source revisions

- PeterLemon/SNES: `350b394e86ec5d62f600b5cbf64cdce3721bb6ef`
- bbbradsmith/SNES_stuff: `3143bc8cf2b56dc28c6ffad7fe42fbc0eccb8f85`
- higan `snes-test-roms` archive: `26e8aa91e0d3b90b901e7e693bc4a8244b391d7b`

Every promoted ROM also has its own hash in the manifest. Repository revisions describe origin; the ROM hash is the executable identity.

## Verdict hierarchy

1. Exact match to a provenance-pinned real-hardware capture.
2. A test ROM's own stable pass/fail report.
3. Exact differential match to bsnes.
4. Stable Clover-only characterization artifact.

Only the first two are conformance verdicts. The last two are evidence and triage aids.

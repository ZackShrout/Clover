# Archived Reference Reconciliation

> Historical record: these investigations were closed during the earlier
> three-ROM, 300-frame milestone. References below to the "current" or "green"
> path describe that point in project history, not Clover's present accuracy
> baseline. The active baseline is maintained in
> [`../ACCURACY_BASELINE.md`](../ACCURACY_BASELINE.md).

## Purpose

This document tracks low-level observable-behavior questions that are **not**
allowed to gate Clover's main green-path validation unless they have been
checked directly against bsnes or hardware.

The distinction matters:

- the main validation contract is the deterministic Clover-vs-bsnes ROM sweep
- the items below are micro-observation questions about exact scheduler-step
  visibility, register-side effects, or bus residue
- until they are reconciled against a trusted reference, they are
  `investigation targets`, not `core regressions`

## Historical Green Path

The authoritative green path at the time was:

- `ctest --test-dir build --output-on-failure`
- deterministic 300-frame Clover-vs-bsnes sweeps for:
  - `Super Mario World (USA).sfc`
  - `Legend of Zelda, The - A Link to the Past (USA).sfc`
  - `Final Fantasy 3 (USA).smc`

That path was the project-wide validation level used to close the observations
recorded in this archive. It has since been superseded.

## Removed From Green Path

The following `HardwareLoopTest` assumptions were removed from the hard-fail
path because they contradicted the validated ROM sweep behavior and were not
yet directly reconciled against bsnes:

### 1. CLI / HIRQ deferral micro-shape

Old assumption:

- after executing `CLI` in a timed HIRQ scenario, IRQ delivery must defer by
  one additional returned hardware step before vectoring

Current Clover observation:

- IRQ is already observable as pending after the `CLI` step, and the next
  returned hardware step vectors into the IRQ handler

Status:

- resolved against bsnes
- Clover now matches bsnes in the seeded `HIRQ` microcase:
  - `PC = 0007` at `dot = 100`
  - IRQ vector entry at `dot = 154`
- the root cause was CPU executor behavior, not APU or raster drift:
  Clover was missing bsnes-style `idleIRQ()` handling on implied 2-cycle
  opcode tails
- the old `HardwareLoopTest` assumption remains removed from the green path
  because it was an over-specific host-observation claim, not the real CPU
  semantic

### 2. General DMA must surface as a later DMA-owned outer step

Old assumption:

- after startup MDMA activation or a CPU write to `$420B`, a later returned
  `step_hardware()` result must report `slot_owner == dma`

Current Clover observation:

- the validated path may absorb the MDMA batch inside a returned CPU-owned
  hardware step, leaving no later externally visible DMA-owned outer step

Status:

- resolved against the shared cartridge-coded Clover-vs-bsnes microcase
- the old assumption was wrong: bsnes also does not expose a required later
  standalone DMA-owned outer step for this startup MDMA case
- Clover's dedicated host-seeded microcase shows the same shape:
  `general_dma_pending` can retire from `1 -> 0` inside a CPU-owned returned
  step
- DMA result bytes and ROM-vs-bsnes frame output remain authoritative

### 3. HDMA setup / transfer must surface as a later DMA-owned outer step

Old assumption:

- scanline-0 HDMA setup and related `HDMAEN` enable paths must leave a
  separate later DMA-owned returned hardware step or observable pending state

Current Clover observation:

- setup/transfer can trigger and retire inside the same returned CPU-owned
  hardware step that crosses the event point

Status:

- resolved as the same outer-step visibility class of issue
- in the dedicated Clover microcase, scanline-0 HDMA setup and the first
  transfer can trigger and retire inside CPU-owned returned steps
- the shared cartridge-coded Clover-vs-bsnes case likewise does not require a
  separately visible later DMA-owned outer step to match bsnes behavior
- final register/result state and ROM-vs-bsnes frame output remain
  authoritative

### 4. HDMA table-address visibility at the trigger boundary

Old assumption:

- after the scanline-0 HDMA trigger, `$4308/$4309` must still read `$2003`

Current Clover observation:

- in the shared cartridge-coded observation case, Clover now matches bsnes:
  `$4308 = ff`, `$4309 = ff`, `$430A = fe` at the post-trigger readback point

Status:

- resolved against the shared cartridge-coded Clover-vs-bsnes microcase
- the earlier mismatch came from Clover not servicing a newly-pending DMA edge
  after CPU access time advanced but before the actual bus read/write
- once that service point was added, Clover matched the bsnes-observed
  post-trigger table-address / line-counter state without disturbing the
  300-frame ROM sweep

### 5. Open-bus residue around opcode boundaries

Old assumption:

- `open_bus()` must expose specific bytes such as `0x5a`, `0x00`, `0x6a`,
  `0x7c` after particular instruction-boundary experiments

Current Clover observation:

- the bsnes-guided model is now understood:
  - plain implied tails such as `NOP` leave the fetched opcode byte as residue
    unless the tail is converted into a bus read by pending IRQ handling
  - internal CPU/DMA MMIO reads in `$00-3f,80-bf:$4000-$43ff` return register
    values without refreshing CPU MDR / open bus

Status:

- resolved
- the old opcode-boundary expectation was wrong for the no-interrupt `NOP`
  class of cases; bsnes' `idleIRQ()` falls back to `idle()` there
- Clover did have one real hardware gap here: internal CPU/DMA MMIO reads were
  incorrectly overwriting open bus even though bsnes suppresses MDR updates for
  `$4000-$43ff`
- Clover now preserves open bus across those internal MMIO reads, and the
  hardware loop test covers both the MMIO-preserve case and the corrected
  `NOP` residue case

## Reconciliation Order

Recommended order:

- no active reconciliation items

This order is intentional:

- `CLI` / IRQ deferral is now closed and should remain documented history,
  not an active target
- DMA / HDMA outer-step visibility is now closed against a shared
  cartridge-coded Clover-vs-bsnes microcase
- HDMA table-address visibility is now also closed against a shared
  cartridge-coded Clover-vs-bsnes microcase
- open-bus residue is now also closed against the bsnes-guided CPU MDR model
- new backlog items should only be opened from discrepancies independently
  confirmed against bsnes or hardware

## Tooling Gap

Current bsnes-side tooling is strong for:

- deterministic frame dumps
- single-frame trace capture tied to a frame dump

Current bsnes-side tooling is weak for:

- tiny CPU microprogram bringup with step-by-step CPU-visible state output
- exact per-returned-step scheduler ownership comparison
- direct `open_bus` / register-side-effect micro-observation logging

So the next technical step is not "change Clover until the old microtests
pass." The next step is:

1. build a tiny bsnes-backed micro-reference path for one case
2. compare Clover and bsnes using the same setup
3. only then decide whether Clover behavior or the test assumption is wrong

## Closed: Seeded bsnes `CLI` / `HIRQ`

The bsnes-side seeded microcase path proved good enough to close the
`CLI` / `HIRQ` investigation once Clover exposed the hidden poll phase and the
CPU executor bug was fixed.

What is now confirmed:

- the seeded serializer patch lands the intended CPU I/O fields at the correct
  absolute offsets
- the patched bsnes blob really does contain:
  - `PC = 0000`
  - `SP = 01ff`
  - `irq_lock = 1`
  - `hirq = 1`
  - `irq_en = 1`
  - `htime = 68`
  - `vtime = 511`
- after `retro_unserialize()`, bsnes preserves those IRQ-related CPU I/O bytes
  exactly

What is also now confirmed:

- a fresh `retro_serialize()` snapshot is **not** a raw "paused exactly at the
  patched blob" view
- bsnes synchronizes to a later live CPU point before the next observable save
  state
- for the current seeded `CLI` / HIRQ microcase, that live comparable point is:
  - `PC = 0003`
  - `dot = 42`
  - `irq_lock = 0`
  - `hirq = 1`
  - `irq_en = 1`
  - `htime = 68`

This matters because Clover reaches the same shape after three returned
hardware steps:

- step 0: `PC = 0000`, `dot = 0`, `irq_lock = 1`
- step 1: `PC = 0001`, `dot = 14`, `irq_lock = 0`
- step 2: `PC = 0002`, `dot = 28`
- step 3: `PC = 0003`, `dot = 42`

So the correct apples-to-apples seeded comparison point is now:

1. patch bsnes state
2. `retro_unserialize()`
3. capture the post-unserialize live state
4. compare Clover from its matching post-step state, not from the raw patched
   blob

What mattered:

- the seeded serializer patch landed the intended CPU I/O fields correctly
- bsnes did not resume from the raw patched blob, but from a later live point
  at `PC = 0003`, `dot = 42`
- hidden CPU state still mattered, especially the internal IRQ poll cadence

What we added on the Clover side:

- a test-only hook to expose and seed the CPU interrupt poll phase
- a seeded microcase harness that can reproduce the `CLI` / `HIRQ` setup

What we found:

- poll phase alone was not enough
- the remaining discrepancy came from CPU instruction-tail semantics
- bsnes uses `idleIRQ()` for implied 2-cycle opcodes such as `NOP`, `CLI`,
  flag ops, transfers, and accumulator implied modifies
- Clover had been retiring those ops through a generic trailing internal idle

What changed:

- Clover now matches bsnes in the seeded microcase window:
  - `PC = 0003` at `dot = 42`
  - `PC = 0004` at `dot = 56`
  - `PC = 0005` at `dot = 70`
  - `PC = 0006` at `dot = 84`
  - `PC = 0007` at `dot = 100`
  - IRQ vector entry at `dot = 154`

Outcome:

- the seeded interrupt-drift item is closed
- the remaining backlog items are scheduler-visibility / observation-point
  questions, not confirmed emulator-core regressions

## Closed: Shared HDMA table-address visibility microcase

The cartridge-coded shared observation ROM closed the HDMA table-address
visibility question directly against bsnes.

What the shared post-trigger readback now shows in both Clover and bsnes:

- `$4308 = ff`
- `$4309 = ff`
- `$430A = fe`

What this proved:

- the old host-side assumption about a separately visible later DMA-owned outer
  step was not the right correctness criterion
- the actual correctness criterion is the CPU-visible register state at the
  same shared readback point
- Clover had been late to service a DMA edge that became pending during the
  elapsed clocks of a CPU bus access

What changed in Clover:

- DMA / HDMA channel defaults were aligned with the bsnes-observed internal
  state for this case
- HDMA transfer eligibility now follows the channel-enabled / not-completed
  condition used by the shared observation case
- the CPU executor now services DMA again after bus-access time advances and
  before the actual bus read or write completes

Outcome:

- the HDMA table-address visibility item is closed
- Clover matches the shared bsnes observation case
- the deterministic 300-frame ROM sweep remains green after the fix

## Closed: MDMA / HDMA outer-step visibility

The dedicated DMA visibility investigation was closed with two complementary
reference paths:

- a host-seeded Clover microcase in `tests/DmaVisibilityBringup.cpp`
- a shared cartridge-coded startup program run under both
  `clover_rom_bringup` and `clover_bsnes_bringup`

What is now confirmed:

- Clover's host-seeded microcase retires startup MDMA inside a CPU-owned
  returned step:
  - `before_general=1 after_general=0`
  - `slot=cpu`
- the same microcase retires the first HDMA transfer on scanline 0 inside a
  CPU-owned returned step:
  - `entered_hblank=1`
  - `hdma_transfer=1`
  - `slot=cpu`
- in the shared cartridge-coded startup case, Clover reports:
  - `$420b` write at frame 0 scanline 1 dot 84
  - an immediate DMA transition `idle/0/0 -> idle/1/0`
  - VRAM data-port writes at scanline 1 dots 116 and 124 without any later
    required DMA-owned outer scheduler step
- the same startup program under bsnes shows the CPU held at the loop PC while
  time advances across the DMA window rather than exposing a separately
  required later outer "DMA step"

Outcome:

- the old `HardwareLoopTest` assumption was not hardware-faithful
- this item is closed as an observation-point / scheduler-visibility issue,
  not an emulator-core regression

## Closed: Open-bus / CPU MDR residue

The last remaining open-bus backlog item resolved into one stale expectation
and one real bus-model bug.

What bsnes makes explicit:

- `WDC65816::idleIRQ()` only converts an implied tail into a bus read when an
  interrupt is pending; otherwise it falls back to `idle()`
- CPU reads in `$00-3f,80-bf:$4000-$43ff` return their internal CPU-complex
  register values but do not update CPU MDR

What that means:

- the old host-side expectation that `NOP` should normally leave the next
  program byte on open bus was incorrect
- Clover's `NOP`-style opcode residue was already following the bsnes model
- Clover did have a real divergence on internal MMIO reads: CPU/DMA register
  reads in `$4000-$43ff` were incorrectly overwriting open bus

What changed in Clover:

- bus reads from internal CPU / DMA MMIO in `$4000-$43ff` now preserve open
  bus while still returning the correct register value
- `HardwareLoopTest` now asserts both:
  - internal CPU / DMA MMIO reads preserve open bus
  - a plain `NOP` leaves the fetched opcode byte as residue

Outcome:

- the open-bus backlog item is closed
- Clover matches the bsnes-guided CPU MDR model for these cases
- the full 300-frame Clover-vs-bsnes sweep remains green for SMW, Zelda, and
  FF3 after the fix

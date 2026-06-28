# Clover Architecture

## Purpose

Clover is a performance-minded Super Nintendo Entertainment System emulator. This document defines the architectural boundaries of the codebase so the project remains fast, understandable, and maintainable as features are added.

## Comparative Reference Philosophy

CloverV2 should remain unmistakably Clover:

- no bsnes code reuse
- no bsnes-clone goal
- no pressure to mirror every internal implementation choice

At the same time, CloverV2 should be architected so highly-regarded SNES emulators, especially bsnes, remain useful **comparative references** during development and debugging.

That means the subsystem boundaries should be conceptually analogous where it materially helps correctness work:

- scheduler ownership
- CPU / PPU / APU advancement boundaries
- bus-master relationships
- interrupt latching and observation points
- DMA / HDMA sequencing
- PPU composition flow
- separation between core emulation and presentation/platform work

The test is practical rather than ideological: when a hard timing or rendering bug appears, Clover should be debuggable against bsnes at a meaningful subsystem and event-boundary level without contorting Clover’s own design.

Useful conceptual takeaways from bsnes that CloverV2 should preserve at the architecture level:

- the system facade owns outer run/power/frame boundaries, but not inner subsystem logic
- CPU/PPU/APU advancement is synchronized through a shared timing model
- raster timing is a first-class reference point for DMA, HDMA, IRQ, and frame events
- frame presentation happens at a clear boundary after emulation-side frame completion
- PPU composition is organized around raster work and pixel candidate composition, not persistent per-layer frame copies

## Project Laws

- `core/` must not depend on SDL, OpenGL, or any frontend library.
- The default runtime path must be performance-first and suitable for stable real-time frame pacing.
- CloverV2 currently targets NTSC timing as the active hardware model.
- Hot paths must not contain permanent debug instrumentation.
- Debug output formatting belongs outside hot emulation loops.
- Optional inspection and layer-visibility features must be isolated from the default presentation path as much as practical.
- The core must remain usable in headless environments.

## Primary Layers

### `core/`

Owns:

- system facade
- scheduler
- bus and memory map
- cartridge and mapping
- cpu
- ppu
- apu
- controller state
- serialization-safe state containers
- typed trace snapshots only at explicit, non-hot boundaries

Does not own:

- windows
- graphics APIs
- audio devices
- file dialogs
- text formatting for debug summaries

### `frontend/`

Owns:

- presentation
- user-facing layer visibility controls
- palette, tile, BG map, and OBJ inspection views
- formatting of debug information

### `platform/`

Owns:

- application shell
- timers
- input device discovery
- audio output devices
- window creation
- renderer backend bootstrapping

## Core Shape

The core should revolve around a slim console facade that coordinates subsystems without becoming a dumping ground for tools, formatting, and diagnostics.

Recommended top-level core types:

- `console_t`
- `scheduler_t`
- `bus_t`
- `cartridge_t`
- `cpu_t`
- `ppu_t`
- `apu_t`
- `controller_port_t`

## Core Seams

The initial core contracts should lock ownership and stepping policy before detailed emulation behavior is added.

### `console_t`

Owns the concrete subsystem instances and exposes the stable outer control surface:

- power on
- reset
- run one frame
- expose the presented framebuffer

`console_t` is the composition root for the core. It may connect subsystems together, but it should not absorb CPU, PPU, or bus behavior.

### `scheduler_t`

Owns the top-level stepping loop and authoritative master-clock progression for the default runtime path.

Responsibilities:

- call `cpu_t::step()`
- ask CPU-owned timing logic whether the CPU or DMA owns the next bus-master slot
- accumulate elapsed master clocks
- advance PPU and APU from the same elapsed master-clock delta
- stop exactly at the frame boundary requested by the caller

Non-responsibilities:

- memory mapping
- debug formatting
- frontend pacing

The scheduler contract should stay simple and branch-light on the default path. Optional diagnostics must wrap around this loop, not live inside it permanently.

The scheduler is the primary place where Clover should stay conceptually comparable to bsnes-style subsystem advancement, even if the concrete implementation stays Clover-specific. It should remain a coordinator, not the long-term home of CPU timing edge behavior.

For Clover, the important compatibility target is **comparable event boundaries**, not identical implementation mechanics. A debugger should be able to answer questions like:

- which unit owned the bus next?
- what were the raster coordinates?
- did HDMA trigger before or after this CPU-visible event?
- was this IRQ/NMI latched, observed, or consumed yet?

### `bus_t`

Owns the CPU-visible memory map and device register routing.

Responsibilities:

- WRAM storage
- cartridge-visible address decoding later
- routing CPU register reads/writes to attached devices
- open-bus behavior state as needed
- exposing DMA-visible memory transactions without turning the CPU into the owner of DMA flow

`bus_t` should not own scheduling policy or presentation logic. It is the transaction surface between the CPU and the rest of the machine.

The initial CPU-visible MMIO split should stay explicit:

- `$2100-$213f` route to the PPU register block
- `$4200-$421f` route to CPU-owned control and status registers
- `$420b-$420c` and `$4300-$437f` route to DMA/HDMA state

That split matters for comparative debugging because it keeps register ownership aligned with subsystem ownership instead of letting a generic MMIO layer hide timing-sensitive behavior.

### `cpu_t`

Owns only CPU architectural state and instruction execution.

Contract:

- `step(bus_t&)` executes exactly one instruction worth of work
- owns CPU-visible timing edges such as interrupt polling cadence and DMA-servicing decisions
- samples latched interrupt state at CPU-owned boundaries
- returns the number of master clocks consumed by that instruction
- does not drive the PPU directly
- does not own global frame pacing
- should remain the primary comparative reference point for DMA/HDMA servicing order and interrupt observation timing

This keeps the CPU hot path tight and makes timing composition explicit in the scheduler.

Even before large opcode coverage exists, the CPU step path should be shaped like
hardware work rather than a single opaque "advance N clocks" blob. In practice
that means Clover should prefer an explicit per-step bus-phase executor:

- opcode fetch
- internal or operand bus phases
- interrupt/vector entry phases

The point is not to freeze Clover into a copied implementation, but to make
instruction boundaries, interrupt observation, and future DMA-visible sequencing
line up with a bsnes-comparable mental model from the start.

Whether interrupt and DMA helper code physically lives inside `cpu_t` or in tightly-coupled sibling modules, the externally visible sequencing should remain easy to compare against a bsnes-style CPU timing model.

Initial CPU MMIO responsibilities should include:

- timer/NMI enable and target registers
- CPU-visible NMI and IRQ status latches
- CPU-visible blanking status

The important architectural rule is that these are CPU-observed hardware views. The PPU and DMA may produce the underlying events, but the CPU register surface is where the core exposes their latched visibility to software.

The CPU should also maintain its own timing view for timer and status purposes.
In the near term, Clover may derive that view from the shared active timing profile
while keeping it as CPU-owned state. Longer term, that view should be able to
model hardware-facing divergence points without turning the PPU object into the
owner of CPU timer behavior.

For timing-sensitive status and interrupt work, Clover should prefer explicit
counter-edge sampling over broad frame-event shortcuts. In practice that means
the CPU may sample delayed counter views for things like:

- NMI visibility timing
- HTIME / VTIME IRQ matching
- CPU-visible HBlank / VBlank status reads

That approach keeps the comparative model closer to bsnes and closer to the
hardware story than simply asking "did the PPU enter VBlank this step?"

CPU-visible timing should also be allowed to diverge from the PPU's simpler
presentation-facing boundaries when hardware behavior calls for it. Two concrete
examples:

- HVBJOY HBlank visibility should follow CPU-visible counter semantics rather
  than the earliest internal PPU rendering boundary.
- HIRQ matching should compare against an internal dot target derived from the
  raw HTIME register value, not the raw register bits directly.

CPU-owned MMIO writes that affect interrupt visibility should also be allowed to
re-poll or transition CPU-visible interrupt state immediately, rather than
waiting for a later unrelated scheduler step. In practice that especially
matters for:

- `$4200` enable changes
- `$4207-$420a` timer target writes

### `ppu_t`

Owns raster timing state, video register state, and the default composed frame state.

Contract:

- `step(master_clock_delta)` advances raster timing only
- reports frame and raster boundary events to the scheduler
- exposes a timing snapshot containing scanline / dot / blanking state
- `present(framebuffer_t&, options)` materializes the caller-visible framebuffer

Default rendering should target one composed frame. User-facing layer masking and debug views belong in explicit presentation or inspection paths, not in the baseline stepping loop.

Before full rendering is deepened, Clover should make the PPU register surface
feel like hardware in its own right. High-value early examples include:

- OAM address and data latch behavior
- VRAM address, increment, and read-buffer behavior
- CGRAM address and paired data writes/reads
- counter latch and status-register read sequencing

That work improves correctness and comparative debugging value without forcing
the project into "get pixels on screen first" architecture drift.

As Clover begins preparing render-facing PPU state, it should prefer compact,
typed per-layer configuration snapshots over ad hoc staging containers. The
goal of that intermediate state is to feed a future fixed-slot compositor
cleanly, not to introduce vector-based per-pixel candidate collection.

When Clover reaches the composition seam, the baseline contract should be a
small fixed-slot working surface:

- one `above` candidate per producing layer
- one `below` candidate per producing layer
- compact compositor control state for color math and presentation mode

Those slots may remain empty until real pixel generation is implemented, but
the contract should exist before the hot path does so later work has a clear,
performance-shaped target.

### `apu_t`

Owns audio-side timing and DSP/SPC-facing state.

Initial contract:

- advances from the same elapsed master-clock source used by the scheduler
- remains independent from frontend audio device concerns
- does not own host pacing

### `dma_t`

Owns DMA and HDMA channel state and bus-master sequencing outside normal CPU instruction execution.

Contract:

- DMA is not hidden inside the CPU core
- CPU-owned timing decides whether CPU or DMA owns the next advancement slot
- HDMA reacts to raster events through CPU-observed timing boundaries

### `interrupt_controller_t`

Owns latched interrupt visibility between producers and the CPU.

Contract:

- PPU and later other producers raise events here
- CPU observes line state, latches pending interrupts, and consumes them at CPU-defined boundaries
- interrupt state is not smeared across unrelated subsystems

The split between line state and CPU-visible status should remain intentional:

- line assertions are internal synchronization inputs to CPU timing
- CPU-visible MMIO status flags are latched observations
- reading status registers may acknowledge those CPU-visible flags without redefining subsystem ownership

## Shared Timing Contract

The core should use a single shared timing vocabulary:

- video timing should be described by explicit profile data, with NTSC as the default active profile today
- CPU instructions report elapsed **master clocks**
- the scheduler accumulates the authoritative master clock count
- the PPU advances from master clock deltas and owns raster position tracking
- other timing-sensitive units observe timing through explicit snapshots or events rather than hidden side effects
- frame completion is reported by the PPU and observed by the scheduler

This avoids hidden ownership of time and keeps synchronization rules obvious.

The intended comparative-debugging reference points are:

- active video standard
- master clock
- scanline
- dot / horizontal position
- HBlank / VBlank entry
- DMA / HDMA pending and execution boundaries
- interrupt latch / consume boundaries

When explicit comparison support is needed, Clover should prefer opt-in typed
timing snapshots over permanent logging in hot paths. A useful minimal snapshot
surface includes:

- CPU timing view
- delayed CPU timing views used for edge-sensitive tests
- PPU timing view
- DMA / HDMA activity state
- interrupt controller state

For now, CloverV2 should assume NTSC behavior in its timing-sensitive implementation work. If PAL support is added later, it should arrive as an alternate timing profile and explicit behavioral audit, not as silent conditional drift spread throughout the hot path.

The NTSC profile should explicitly define scanline-phase reference points that the rest of the hardware unit can consume:

- HBlank entry dot
- HDMA setup point
- HDMA per-visible-scanline trigger point
- VBlank entry scanline

Those phase boundaries should be produced once by timing code and then observed by DMA, interrupt, and scheduling logic rather than recomputed ad hoc in each subsystem.

CPU timer IRQ mode should also stay explicit at the contract level:

- no timer IRQ
- H counter only
- V counter only
- HV counter combined

That rule keeps the CPU-visible IRQ model understandable and makes comparison against reference emulators much easier when timer bugs show up.

Initial stepping rules:

1. `console_t` asks `scheduler_t` to run one frame.
2. CPU-owned timing logic decides whether a pending DMA/HDMA unit or the CPU owns the next bus-master slot, and `scheduler_t` executes that choice.
3. CPU execution occurs as `cpu_t::step(bus_t&, interrupts)` and returns a `master_clock_delta`.
4. DMA work, when active, also returns elapsed master clocks through its own step contract.
5. `scheduler_t` forwards the elapsed clock delta to both `ppu_t` and `apu_t`.
6. `ppu_t` returns raster/frame events such as HBlank, VBlank, NMI, IRQ, and frame completion.
7. CPU timing logic observes those events, updates DMA/HDMA state, and latches interrupt state.
8. When the PPU reports frame completion, the scheduler stops.
9. `console_t` asks the PPU to present the finished frame into the shared framebuffer.

This is the default fast path. Any optional trace, layer preview, or inspection mode should enter through explicit non-default APIs so the hot path remains clean.

## Alignment Backlog

The current architecture is intentionally staged. The most important remaining
alignment work, in priority order, is:

1. CPU-owned raster/counter view for timer IRQ, HVBJOY, and CPU-visible timing decisions.
2. DMA / HDMA execution behavior that reflects channel transfer modes, reload rules, and bus restrictions more faithfully.
3. NTSC timing refinements, especially the short scanline and counter-edge behavior that matter for difficult timing bugs.

These are not cleanup tasks. They are the next pieces most likely to reduce
comparative-debugging friction against bsnes and against documented SNES hardware behavior.

## Performance Rules

- No dynamic allocation in frame or pixel hot loops.
- No string creation in core stepping paths.
- No virtual debug hooks in per-pixel or per-instruction execution.
- Fixed-size buffers are preferred in hot paths.
- Extra debug work is allowed only when explicitly entering a non-default inspection path.

## PPU Direction

The PPU should support:

- faithful default composition
- user-facing layer masking
- debug layer views
- tileset and palette inspection

without requiring four persistent full-frame layer buffers.

The intended model is:

- per-layer current-pixel candidates are produced during normal composition work
- masking and debug-view selection happen at composition or presentation boundaries
- alternate inspection rendering may do extra work only when explicitly enabled

## Immediate Next Steps

1. Lock core interfaces before implementing major subsystem behavior.
2. Define scheduler ownership and stepping contracts.
3. Define framebuffer and sample output contracts.
4. Add a headless smoke test harness.
5. Add placeholder SDL3 shell only after the core seams are fixed.

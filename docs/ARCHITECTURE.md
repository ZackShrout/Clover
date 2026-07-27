# Clover Architecture

## Purpose

Clover's architecture exists to make hardware behavior explicit without
coupling the emulator to one app, renderer, or future emulated system. The
current implementation contains one SNES core, but both the headless tools and
SDL app consume it through boundaries intended to remain useful when more cores
or presentation targets arrive.

bsnes is a comparative reference, not a source tree to copy. Similar subsystem
and event boundaries make difficult timing behavior easier to compare while
leaving Clover's implementation its own.

## Project Laws

- Hardware-faithful behavior takes priority over ROM-specific workarounds.
- `core/` never depends on SDL, a graphics API, or an application shell.
- Host pacing, windowing, physical input, and audio devices stay outside the
  emulator core.
- The core remains usable headlessly.
- CPU, PPU, APU, DMA, and interrupt timing share one master-clock vocabulary.
- Optional tracing and inspection must be opt-in and absent from normal hot
  paths.
- Exact behavior should be testable at meaningful hardware observation points.

## Layers

### `src/clover/core/`

Owns emulated hardware and time:

- `console_t`, the SNES composition root and outer control surface
- `scheduler_t`, master-clock and frame advancement
- `cpu_t`, 65C816 architectural state and bus-phase execution
- `bus_t`, address decoding, WRAM, MMIO routing, and open-bus state
- `cartridge_t`, ROM/SRAM storage and mapping
- `ppu_t`, raster state, PPU registers, rendering, and composition
- `apu_t`, SPC700 execution, timers, DSP state, and generated samples
- `dma_t`, general DMA and HDMA channel state and transfer sequencing
- `interrupt_controller_t`, interrupt line and latch state
- framebuffer, startup entropy, timing profiles, and typed diagnostic snapshots

The core does not own windows, SDL events, physical controller discovery,
speaker devices, or real-time sleep policy.

### `src/clover/frontend/`

Owns the system-neutral contract used by presentation targets:

- media loading, power, reset, and frame advancement
- semantic gamepad state for two logical ports
- display metadata
- read-only video-frame and audio-frame views
- persistent-memory views, restore, dirty-state query, and flush acknowledgement
- optional typed capabilities such as named presentation-plane control
- an optional system-neutral debug-target capability describing execution
  domains and address spaces
- the factory that currently constructs the SNES implementation

`snes_emulator_core_t` adapts `core::console_t` to this interface. The seam is
generic enough for another core, but it does not pretend multiple systems are
implemented today.

The debug-target capability currently provides the first read-only Workbench
substrate: canonical-media and safe live-memory inspection plus CPU-bus to
canonical-media translation. Volatile MMIO is explicitly unavailable through
the side-effect-free CPU-bus inspection path. Its optional execution-control
capability can step the main CPU to an explicit instruction, interrupt, wait,
or stopped boundary while the scheduler continues advancing the whole machine,
including intervening DMA work. Declared domains without stepping support
report that limitation explicitly.

The optional observation-control capability currently exposes masked main-CPU
boundary and CPU-bus read/write events through a fixed-capacity buffer. Native
core events are typed and written into caller-provided storage; the core
performs no allocation, formatting, I/O, or virtual callback. A disabled memory
probe is one inline branch in the CPU bus path. The frontend allocates storage
only when observation is enabled, translates native events into system-neutral
records, and reports overflow through an explicit dropped-event count. Player
frontends do not query these capabilities during ordinary execution.

The debug target also publishes register descriptors and caller-owned live
register snapshots per execution domain. Workbench derives the 65C816 decode
context from `E`, `P`, `D`, and `DB`; generic frontend clients do not depend on
the native `cpu_state_t` layout.

Debugger session state is also frontend policy. The optional session-control
capability reports not-running, running, or paused and owns pause/resume
transitions. While debugger-paused, the SNES frontend suppresses host
`run_frame()` advancement but still permits explicit domain stepping through
the execution-control capability. Reset and media replacement preserve the
debugger pause. No pause flag or wall-clock policy enters the hardware core,
and the Player remains on the ordinary running path unless a debug client
actively requests the capability.

### `src/clover/platform/`

Owns host integration. The SDL implementation creates the window, renderer,
texture, audio stream, keyboard/gamepad mapping, capture files, event loop, and
wall-clock frame pacing, pause/frame advance, and speed selection. The platform
layer also owns the SQLite ROM library, managed ROM copies, application-data
paths, and save files. It sends semantic input into `emulator_core_t` and pulls
completed video/audio views after each `run_frame()`.

No SDL type crosses into `core/` or the frontend contract.

The SDL build produces two distinct applications. `clover_sdl` is the focused
Player and does not link the Workbench project or analysis libraries.
`clover_workbench` is the analysis host: it composes the debug-target byte
source, Stage 1 decoder/listing services, the Workbench-owned run controller,
and the SQLite-backed project service.
Workbench persistence itself lives under `src/clover/workbench/`, is UI
independent, and is included in the default headless build for testing.

The future checkpoint boundary is specified by
`docs/SNES_MACHINE_STATE_AUDIT.md`. Checkpoints preserve causal hardware state
and validate immutable media/configuration identity, while pointers, platform
objects, observation buffers, and legacy traces remain outside the payload.
Restore is transactional: decode and validate into a temporary console,
reconnect wiring locally, and replace the live machine only after all
cross-subsystem invariants pass. The in-memory causal snapshot types cover the
scheduler, S-CPU, DMA/HDMA, interrupt controller, CPU bus, cartridge, PPU, and
APU. Alongside WRAM, open bus, pending device writes, bootstrap memory, and
SRAM continuity, PPU state retains video memory, register/latch state, raster
timing, live render pipelines, compositor samples, and partial frame pixels.
APU state retains SPC700 execution and replay state, APURAM, ports, timers,
exact S-DSP state, and in-progress audio output.
Cartridge restore also validates immutable header, mapper, ROM-size, and
RAM-topology fields; canonical SHA-256 validation belongs to the outer
checkpoint envelope. CX4, DSP-1 through DSP-4, and Super FX each own a
versioned, explicit little-endian causal-state blob; cartridge state selects
the device implied by the immutable loaded-media topology. They now compose
into one transactional in-memory console state for powered bootstrap, base,
and enhancement-cartridge machines. Restore builds and wires an independent
candidate, validates every subsystem and shared clock/configuration invariant,
and only then performs non-failing field commits into the live address-stable
console. The caller must validate canonical SHA-256 before invoking this core
restore; topology checks alone cannot distinguish two same-layout ROM
revisions.

`frontend/SnesCheckpoint` is that portable caller boundary. Version 1 uses a
fixed 128-byte little-endian envelope followed by an explicit field-wise
payload; it never copies C++ object representations. The envelope records the
format, system and core-state versions, canonical media length and SHA-256,
hardware and cartridge topology, every subsystem schema version, payload
length, and CRC-32. Restore bounds the payload and dynamic cartridge RAM before
allocation, verifies the complete envelope and checksum, compares the canonical
ROM identity, decodes into temporary state, and only then invokes the core
transaction. Unknown required versions, invalid scalar encodings, malformed
geometry, truncation, trailing bytes, and identity mismatches therefore cannot
mutate the running machine.

The optional `checkpoint_control_t` debug-target capability exposes this
portable boundary without adding checkpoint operations to the ordinary player
contract. Capture and restore require a running, paused debug session. A
successful restore retains debugger pause and presentation policy while
clearing observations from the abandoned timeline.

The checkpoint boundary is guarded by a deterministic replay matrix. Tests
capture a portable checkpoint, execute scripted controller and cartridge I/O
through a completed frame, restore, and replay. Exact hardware-step
observations, causal state, re-encoded checkpoint bytes, framebuffer, audio,
and device responses must match. Coverage includes NTSC/PAL mid-frame state,
SRAM, reset, WAI/IRQ entry, general DMA, HDMA, APU-port synchronization, CX4,
DSP-1 through DSP-4, and Super FX.

Battery-backed SRAM bytes and dirty state are emulated cartridge state. Save
identity, filenames, filesystem access, migration, temporary-file replacement,
and flush cadence are platform policy. Canonical SHA-256 identities decouple a
game's save from its source filename or location. Canonicalization and hashing
live in a small shared frontend service so the ROM library and Workbench use
the same identity. This lets a headless tool or future host persist the same
core memory without teaching the SNES cartridge about files.

## Core Runtime

`console_t` owns the concrete SNES subsystems. Its stable outer operations are
media load, power/reset, single hardware step, scanline run, frame run,
controller state, completed framebuffer, and per-frame audio output.

For each hardware step:

1. `scheduler_t` asks CPU-owned timing which bus master can advance.
2. CPU execution or DMA/HDMA work consumes an explicit master-clock delta.
3. PPU and APU advance from that same elapsed time.
4. Raster events update DMA/HDMA and interrupt visibility at defined edges.
5. Frame execution stops at the PPU frame boundary.
6. The completed PPU image is materialized into the shared framebuffer; audio
   samples generated during the frame remain available to the caller.

The scheduler coordinates time; it does not own instruction semantics, memory
mapping, rendering, or frontend pacing.

## Hardware Ownership

### CPU and bus

The CPU owns architectural registers, opcode execution, CPU-visible interrupt
polling, and bus-phase timing. The bus owns address decoding and routes:

- `$2100-$213f` to PPU registers
- `$4200-$421f` to CPU/system status and control
- `$420b-$420c` and `$4300-$437f` to DMA/HDMA
- `$2140-$217f` mirrored APU communication ports
- cartridge and WRAM regions to their storage owners

Open-bus residue is part of the hardware contract, including cases where an
internal MMIO read returns a value without refreshing CPU MDR.

### PPU

The PPU owns raster progression, register/latch behavior, VRAM/CGRAM/OAM,
background and object evaluation, windows, color math, Mode 7, and final pixel
composition. Rendering uses fixed-size state and candidate storage rather than
allocating per pixel.

Some register changes during active display require effects at a defined dot
rather than the next scanline. Those timing decisions belong in the PPU model,
not in game-specific presentation fixes.

Layer masking and alternate debug views enter through explicit presentation
options and do not alter the default composed hardware path.

### APU and DSP

The APU advances from master-clock credit independently of the host audio
device. It owns SPC700 state, APURAM, IPL behavior, timers, CPU/APU ports, DSP
registers, and sample generation. The frontend receives signed 16-bit stereo
samples plus a discontinuity flag; buffering and playback are platform policy.
Its causal snapshot is only available outside the call-scoped CPU/APU I/O
window. The instruction journal and suspension state remain checkpointable,
but the active bus pointer does not. Restore validates the DSP payload in an
isolated instance, reconnects DSP RAM and audio-output pointers locally, and
clears instruction/I/O diagnostics from the abandoned timeline.

### DMA, HDMA, and interrupts

DMA owns channel registers and transfer state. CPU timing determines when DMA
or HDMA takes the bus, while raster edges determine HDMA setup and transfer
opportunities. Interrupt producers and CPU-visible latches remain distinct so
assertion, observation, acknowledgement, and consumption are not collapsed into
one flag.

## Timing Contract

The canonical motherboard profile is late 3-chip: S-CPU B, S-PPU1 version 1,
S-PPU2 version 3, and late S-APU behavior. Region is an orthogonal timing axis;
NTSC or PAL is selected from cartridge metadata unless explicitly overridden.
The shared vocabulary includes:

- master clocks
- scanline and dot
- HBlank and VBlank edges
- frame completion
- DMA/HDMA pending and active state
- NMI/IRQ line, latch, poll, and consume points

CPU-visible counter behavior may intentionally differ from the PPU's internal
render boundary where the hardware does. These differences are represented as
explicit timing views and delayed samples, not broad frame-event shortcuts.

Hardware profiles and region timing are typed configuration, not scattered
conditionals. See [`SNES_HARDWARE_MODEL.md`](SNES_HARDWARE_MODEL.md).

## Presentation and Pacing

`emulator_core_t::run_frame()` advances emulated hardware only. The current SDL
shell normally paces completed frames at the SNES core's reported regional
refresh rate, applies the regional pixel aspect ratio, and manages audio queue latency. Pause,
single-frame advancement, and alternate speeds change only when the host calls
`run_frame()`; headless callers can run without sleeping.

System-specific presentation controls are exposed through optional typed
frontend capabilities. The SNES plane implementation recomposites BG1–BG4 and
OBJ visibility in a separate presentation framebuffer. It does not write PPU
registers or alter the canonical all-layers output, so diagnostic convenience
does not become emulated hardware state.

This separation is important: a host slowdown must not become an emulated
timing change, and a future renderer or offline tool must be able to consume
the same core output without SDL.

## Diagnostics

The core exposes typed snapshots and bounded trace buffers for targeted
bringup. Environment-controlled probes may record CPU/APU/PPU events, memory,
or frames, but they are not part of the normal correctness path.

Rules for diagnostics:

- disabled means no formatting, I/O, or hidden per-instruction work
- bounded buffers are preferred to unbounded logging
- legacy APU, bus-register, OAM, and CGRAM histories require explicit
  diagnostic-host opt-in and are disabled in ordinary Player execution
- comparisons identify equivalent hardware observation points
- performance measurements are repeated with probes disabled
- temporary ROM-specific watches do not become permanent core policy

This opt-in is a performance boundary, not merely a UI preference. Saturated
legacy buffers must never shift records during Player execution. The SDL Player
leaves these histories disabled; `clover_rom_bringup` enables legacy APU/bus
history only on its detailed diagnostic path (or with
`CLOVER_CAPTURE_LEGACY_TRACES=1`) and enables OAM/CGRAM history only through
their existing capture controls.

## Performance Rules

- No dynamic allocation in instruction, pixel, or sample hot loops.
- No string construction or console output in hardware stepping paths.
- Fixed-capacity state is preferred for per-frame/per-scanline work.
- System-neutral virtual dispatch stays at frame/media boundaries, not within
  CPU or PPU hot paths.
- Optional visibility features pay their cost only when enabled.

## Current Scope

Clover currently implements the late 3-chip SNES/SFC hardware model with NTSC
and PAL timing plus LoROM and HiROM mapping. Cartridge-owned enhancement
hardware currently includes CX4, DSP-1B through DSP-4, and Super FX:

- CX4 cartridges expose RAM and command registers through the LoROM bus window.
  The synchronous command model does not yet reproduce HG51BS169 instruction
  timing, command latency, or contention.
- DSP-1-family cartridges use the DSP-1B command model and the appropriate
  LoROM or HiROM data/status layout. DSP-1 and DSP-1A silicon differences are
  not separately modeled.
- DSP-2 uses its byte-command protocol and Dungeon Master mapping across banks
  `$20-$3f` and `$a0-$bf`.
- DSP-3 exposes its word-oriented data/status protocol across banks `$20-$3f`
  and `$a0-$bf`, backed by its fixed data ROM and resumable command processor.
- DSP-4 exposes its word-oriented data/status protocol across banks `$30-$3f`
  and `$b0-$bf`; projection commands retain per-cartridge state across host
  transfers, and multiple DSP-4 instances do not share command state.
- Super FX cartridges own a cycle-stepped GSU processor with 16 registers,
  instruction and pixel caches, delayed buffered ROM/RAM access, bus
  arbitration, and IRQ output. Cartridge detection selects MARIO/GSU-1/GSU-2
  ROM and expansion RAM layouts from the header, including volatile and
  battery-backed variants. The scheduler advances the GSU alongside CPU bus
  phases so register polling and DMA observe coprocessor progress at the
  subsystem boundary. The shared DMA controller resets every HDMA completion
  latch at frame setup and can suspend/resume MDMA at byte boundaries when an
  HDMA setup or transfer becomes pending.

SA-1 remains unsupported. Current unresolved accuracy areas are tracked in
[`KNOWN_SIMPLIFICATIONS.md`](KNOWN_SIMPLIFICATIONS.md).

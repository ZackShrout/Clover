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
- the factory that currently constructs the SNES implementation

`snes_emulator_core_t` adapts `core::console_t` to this interface. The seam is
generic enough for another core, but it does not pretend multiple systems are
implemented today.

### `src/clover/platform/`

Owns host integration. The SDL implementation creates the window, renderer,
texture, audio stream, keyboard/gamepad mapping, capture files, event loop, and
wall-clock frame pacing, pause/frame advance, and speed selection. The platform
layer also owns the SQLite ROM library,
canonical media hashing, managed ROM copies, application-data paths, and save
files. It sends semantic input into `emulator_core_t` and pulls completed
video/audio views after each `run_frame()`.

No SDL type crosses into `core/` or the frontend contract.

Battery-backed SRAM bytes and dirty state are emulated cartridge state. Save
identity, filenames, filesystem access, migration, temporary-file replacement,
and flush cadence are platform policy. Canonical SHA-256 identities decouple a
game's save from its source filename or location. This lets a headless tool or
future host persist the same core memory without teaching the SNES cartridge
about files.

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
- comparisons identify equivalent hardware observation points
- performance measurements are repeated with probes disabled
- temporary ROM-specific watches do not become permanent core policy

## Performance Rules

- No dynamic allocation in instruction, pixel, or sample hot loops.
- No string construction or console output in hardware stepping paths.
- Fixed-capacity state is preferred for per-frame/per-scanline work.
- System-neutral virtual dispatch stays at frame/media boundaries, not within
  CPU or PPU hot paths.
- Optional visibility features pay their cost only when enabled.

## Current Scope

Clover currently implements the late 3-chip SNES/SFC hardware model with NTSC
and PAL timing plus LoROM and HiROM mapping. Cartridge-owned command devices
currently include CX4, DSP-1B, and DSP-2:

- CX4 cartridges expose RAM and command registers through the LoROM bus window.
  The synchronous command model does not yet reproduce HG51BS169 instruction
  timing, command latency, or contention.
- DSP-1-family cartridges use the DSP-1B command model and the appropriate
  LoROM or HiROM data/status layout. DSP-1 and DSP-1A silicon differences are
  not separately modeled.
- DSP-2 uses its byte-command protocol and Dungeon Master mapping across banks
  `$20-$3f` and `$a0-$bf`.

Super FX, SA-1, DSP-3, and DSP-4 remain unsupported. Current unresolved
accuracy areas are tracked in
[`KNOWN_SIMPLIFICATIONS.md`](KNOWN_SIMPLIFICATIONS.md).

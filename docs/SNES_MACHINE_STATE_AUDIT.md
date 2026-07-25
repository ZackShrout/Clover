# SNES Machine-State Ownership and Serialization Audit

Status: Stage 0 design deliverable
Audited against the Clover worktree on July 25, 2026

## Purpose

This document defines the SNES state that an exact Workbench checkpoint must
capture, the state it must deliberately exclude, and the order in which it must
be restored. It is an ownership map, not a claim that checkpoint capture and
restore are implemented.

The audit uses this behavioral definition:

> After restoring a checkpoint, the same subsequent controller inputs and host
> commands must produce the same emulated bus activity, boundaries, video,
> audio, persistent-memory bytes, and interrupt behavior.

That definition is stronger than saving visible CPU registers. Hidden timing,
pending transfers, device protocols, render pipelines, and audio synthesis
history are causal state too.

## State classes

Every field belongs to one of these classes:

| Class | Checkpoint treatment |
| --- | --- |
| Causal machine state | Required. A difference can change later emulation. |
| Immutable identity/configuration | Record a stable identifier and validate it before restore; do not serialize pointers or duplicate ROM bytes. |
| Observable continuity state | Preserve for a transparent in-process restore, although it does not affect later hardware behavior. |
| Reconstructable derived state | Prefer rebuilding through an explicit function; serialize it when rebuilding cannot yet be proved exact. |
| Wiring | Never serialize. Reconnect references, spans, and pointers after owned storage exists. |
| Diagnostic state | Exclude by default. Observation and legacy trace buffers must not influence the restored machine. |
| Host policy | Exclude from the core payload. A session envelope may preserve it separately. |

The first checkpoint implementation should favor correctness over payload size:
if a cache or partial output cannot be proven reconstructable, preserve it.
Optimization can follow equivalence tests.

## Capture boundary

Version 1 checkpoints should be captured only when all of the following are
true:

- the frontend debugger session is paused;
- no `console_t` step is executing;
- the S-CPU has returned an explicit boundary result;
- the observation sink is detached or quiescent for the copy;
- the media and hardware configuration cannot change during the copy.

An S-CPU instruction boundary is not a globally quiescent hardware boundary.
The APU may be partway through an SPC700 instruction, DMA may be suspended, and
bus writes may still be queued by clock offset. Those states remain required.
The boundary restriction prevents copying C++ stack-local executor state; it
does not permit dropping device-internal work.

## Composition and identity

### Console and hardware configuration

Required:

- powered/not-powered lifecycle state;
- selected hardware model and requested region;
- resolved video standard;
- resolved CPU, PPU1, and PPU2 version identifiers.

Record as configuration:

- startup-entropy mode, seed-override state, seed, and sequence.

Validate before restoring:

- system identifier (`snes`);
- checkpoint schema version;
- core state version;
- canonical media SHA-256 and length;
- cartridge mapping mode, detected header values, and enhancement hardware.

Do not serialize `snes_hardware_identity_t::profile`, which is a pointer into a
compiled profile table. Restore the stable hardware-model identifier and
resolve the pointer locally.

### Media

Canonical ROM and CX4 program ROM are immutable identity inputs. A checkpoint
references them by hash and length; it does not embed or overwrite them.
Bootstrap program bytes are mutable test-machine storage and must be included
when the bootstrap cartridge mode is active.

Cartridge RAM bytes are causal mutable state. Persistent status and the dirty
flag are session-continuity metadata: preserving them makes an in-process
restore transparent and avoids accidental save acknowledgements, but portable
checkpoints must never perform filesystem I/O themselves.

## Subsystem inventory

### Scheduler

Required:

- master clock;
- frame index.

Excluded wiring/diagnostics:

- observation-sink pointer.

The restored scheduler clock must agree with the CPU, PPU, and APU clock
domains. Restore must reject a payload whose redundant clocks violate declared
invariants.

The console-owned materialized framebuffer is observable-continuity state. It
does not feed hardware, but a transparent in-process restore must preserve what
the frontend reads before the next completed frame.

### S-CPU

Required architectural state:

- PC, PB, DB, SP, D, A, X, Y, P, and emulation mode.

Required hidden and I/O state:

- CPU master clock, free-running DMA divider clock, and raster counter;
- current and delayed timing snapshots;
- interrupt poll phase and poll-valid flags;
- NMI/IRQ enable, flags, hold clocks, and H/V timer registers;
- HBlank/VBlank visibility and IRQ condition validity;
- reset, WAI, wake-idle, STP, and DMA-active state;
- DRAM refresh and HDMA setup dots and pending flags;
- multiplication/division operands, counters, shift state, and results;
- FastROM state, PIO, WRAM address;
- auto-joypad poll state, latches, busy clocks, shift counts, and results;
- both live controller input words;
- interlace/timing configuration and visible scanline count.

The placeholder-opcode count is diagnostic accounting, not hardware state. It
may be stored in a debugging-session envelope but is excluded from the portable
machine payload.

Excluded wiring:

- bus, interrupt-controller, and PPU pointers.

### CPU bus

Required:

- 128 KiB WRAM;
- open-bus value;
- pending CPU, PPU, and APU write arrays, their counts, values, destinations,
  and relative apply clocks;
- APU-progressed CPU-clock offset;
- startup-entropy mode, override, seed, and sequence.

Excluded wiring:

- APU, cartridge, CPU, DMA, and PPU pointers.

Excluded diagnostics:

- PPU-register, system-register, watched-write, and APU-port trace buffers and
  counts;
- APU-port trace enablement.

Restore must validate every pending-write count against its fixed capacity
before copying any entries.

### DMA and HDMA

Required:

- all eight channel register blocks;
- DMA/HDMA enable, active, completion, and transfer flags;
- pending general-DMA, HDMA-setup, and HDMA-transfer masks;
- active activity kind, channel, substep, and alignment state;
- source, target, table, indirect, line-counter, transfer-size, and transfer
  index state;
- DMA counter and CPU bus-cycle clocks;
- batch-started and reload-pending state;
- suspended general-DMA channel, substep, alignment, batch, remaining-unit,
  and transfer-index state.

No DMA state is safely reconstructable from the public channel registers once a
transfer has begun.

### Interrupt controller

Required:

- NMI and IRQ lines, holds, transitions, pending latches, and IRQ lock;
- separate CPU and cartridge IRQ source lines;
- NMI and IRQ transition clocks.

After restore, the combined IRQ line must be recomputed or validated from the
two source lines without creating a new transition.

### PPU

Required register and memory state:

- PPU register file, VRAM, OAM, and CGRAM;
- PPU1 and PPU2 memory-data-bus latches;
- VRAM address, increment/mapping policy, and read latch;
- OAM base/current/latched address, priority, and write latch;
- CGRAM current/latched address, byte phases, and write latch;
- scroll and Mode 7 latches;
- counter-latch values and high-byte read phases;
- external counter-latch enablement.

Required timing/configuration state:

- video timing and PPU version values;
- raster counter, field, interlace/overscan timing latches, and frame counter;
- entropy mode, override, seed, and sequence.

Required render-causal state:

- display, display-write history, background, object, mosaic, window,
  color-math, screen, compositor, and pipeline structures;
- decoded objects, evaluated-object double buffers, fetched tile buffers,
  background cycle-fetch state, pixel counters, and mosaic samples;
- all partial composed/presented frame pixels and active geometry.

Partial frame pixels are required because a mid-frame restore must eventually
produce the same completed frame; they cannot be regenerated without replaying
earlier scanlines. The presentation copies are observable-continuity state and
should be retained for a seamless in-process restore.

Excluded host/debug policy:

- presentation layer mask;
- frame-capture enable;
- completed-frame queue enable and queued frames.

Excluded diagnostics:

- CGRAM/OAM write trace buffers, counts, and trace start frames.

On restore, diagnostic queues and trace counts start empty. Restoring them
would conflate observations from two timelines.

### APU, SPC700, and S-DSP

Required SPC700 state:

- APU master clock, SMP clock credit, and master-clock frequency;
- SPC700 PC, A, X, Y, SP, PSW, current opcode PC, and last opcode;
- IPL enable, halt, wait, and stop state;
- complete I/O control state, auxiliary registers, DSP address, and wait
  states;
- all 64 KiB APU RAM;
- CPU-to-APU and APU-to-CPU ports;
- all stages, lines, enables, and targets for the three timers;
- DSP clock remainder and initialization state.

Required synchronization/protocol state:

- active SPC700 instruction journal, starting registers/opcode state, recorded
  accesses, replay cursor, and abort flag;
- SMP-suspended-for-CPU state;
- CPU I/O window target and consumed-clock numerator.

The CPU I/O window bus pointer is wiring and must not be serialized. Restore it
as null outside an active core call; version 1 capture is forbidden while the
call stack owns an active CPU I/O window.

Required DSP state:

- the exact 640-byte `SPC_DSP::copy_state` representation, including voices,
  BRR history, echo history, envelopes, noise, counters, phase, KON state, and
  between-clock temporaries;
- current per-frame audio samples, write position, and overflow state for
  observable continuity.

The DSP RAM, echo, voice-register, echo-history, and output pointers are wiring.
`SPC_DSP::copy_state` reconstructs its internal relative pointers. Output
continuity is captured separately as primary/emergency sample counts, primary
output enablement, overflow state, and the fixed emergency buffer. Restore
rebases those values onto the APU's current audio array; no pointer enters the
snapshot. `apu_audio_output_state_t` couples that descriptor with the
caller-owned primary samples so a transparent restore retains the current audio
frame.

Excluded diagnostics:

- SPC700 instruction and I/O trace buffers and counts.

### Cartridge base state

Required:

- mutable bootstrap program when active;
- all cartridge RAM/expansion RAM bytes;
- the active enhancement device state;
- loaded state and persistent/dirty continuity metadata.

Validated identity:

- ROM hash and length;
- detected header, mapper, RAM length, and enhancement hardware.

Excluded/reconstructed:

- ROM byte vector (provided by validated media);
- `unique_ptr` object addresses;
- ROM/RAM spans inside enhancement devices.

Restore must construct the detected enhancement device before loading its state.

### CX4

Required:

- 3 KiB data RAM and 256-byte register file;
- internal transform coordinates, distance, and scale.

Rewire the program-ROM span from the validated cartridge ROM.

### DSP-1

Required:

- parameter, result, and matrix arrays;
- command phase, command, word/byte indexes, data register, status byte phase,
  frozen state, and raster-output flag;
- all mutable projection/reference variables used by the DSP-1 algorithms.

The projection/reference variables formerly declared in
`Dsp1Reference.inc` now live in the instance-owned
`dsp1_projection_state_t`. Reference operations receive that state explicitly,
so two consoles no longer share camera/projection state. The state is ready to
join the future DSP-1 snapshot value.

### DSP-2

Required:

- input and output buffers;
- input stage, command, transparent color;
- expected/input/output lengths and all indexes.

### DSP-3

Required:

- data/status registers, operation, and protocol index;
- geometry, bitmap conversion, entropy-code, LZ, and bit-reader state;
- terrain, cost, weight, coordinate, and path-cell buffers;
- path counts and indexes.

Vector lengths must be encoded explicitly and bounded to their expected 0x2000
entries before allocation.

### DSP-4

Required:

- the complete legacy protocol and variables structures;
- input/output buffers and indexes and command/wait/resume positions.

The legacy `dsp4_byte` and unused `dsp4_address` process globals have been
removed. Byte input/output is now passed directly across the wrapper boundary;
all persistent protocol and algorithm state remains in `dsp4_t::state_t`.
Legacy thread-local binding pointers are wiring: they are rebound for every
operation and after restore, and their values must never enter the payload.

### Super FX

Required:

- all sixteen registers and status/control/bank registers;
- instruction pipeline and source/destination register selection;
- ROM and RAM buffers, addresses, clock counters, and bus-wait state;
- instruction clock credit;
- cache bytes and valid bits;
- both pixel caches, including pending pixels;
- register-modified flags, RAM-written latch, and IRQ state encoded in SFR.

Rewire ROM and RAM spans after cartridge storage is restored. The RAM-written
latch affects persistent dirty propagation and is therefore session-causal.

## Presentation, diagnostics, and frontend session state

The core machine payload excludes:

- observation sinks, masks, buffered events, drain cursors, and drop counts;
- all legacy trace buffers and trace enable flags;
- SDL pause, frame-advance, speed, pacing, audio-device, window, renderer, and
  controller-discovery state;
- ROM-library records, file paths, managed copies, and save flush timing;
- Workbench panels, selections, text-editor buffers, and analysis database
  state.

A session envelope may separately preserve:

- debugger paused/running state;
- presentation-plane selections;
- the last materialized frontend framebuffer;
- current audio-frame samples and cursor;
- persistent-memory dirty acknowledgement;
- Workbench navigation state.

Restoring the envelope must not alter the causal core payload or inject
observations.

## Serialization format requirements

Use an explicit, field-wise, little-endian format. Do not `memcpy` C++ objects:
they contain padding, pointers, spans, `unique_ptr`, vectors, deques, and
implementation-dependent enum and `size_t` widths.

The top-level header must contain:

- magic and checkpoint-format version;
- system ID and core-state version;
- canonical media hash and length;
- hardware model, requested region, and resolved video standard;
- mapper, enhancement hardware, and mutable-memory sizes;
- per-subsystem schema versions;
- payload length and checksum.

Each subsystem decoder must:

- reject unknown required versions;
- bounds-check counts and lengths before allocation or copying;
- decode into a temporary value;
- validate cross-subsystem invariants;
- commit only after the entire checkpoint is valid.

A failed restore leaves the running machine unchanged.

## Restore order

1. Parse and validate the entire envelope without mutating the console.
2. Pause frontend advancement and detach diagnostics.
3. Validate system, media hash/length, hardware model, region, mapper,
   enhancement hardware, and mutable-memory sizes.
4. Build a temporary console with the requested configuration and validated
   media.
5. Construct the correct cartridge enhancement device and allocate owned RAM.
6. Restore cartridge mutable storage and coprocessor-owned state.
7. Restore PPU/APU memories and all subsystem value state.
8. Restore bus pending queues, DMA/HDMA sequencers, and interrupt latches.
9. Restore CPU, PPU, APU, and scheduler clocks and validate their relationships.
10. Reconnect all bus/device pointers and cartridge ROM/RAM spans.
11. Restore partial video and optional observable-continuity output.
12. Validate invariants again, then atomically replace the live console.
13. Clear diagnostic buffers, reconnect the selected observation sink, and
    restore frontend session policy.

Building a temporary console is the required validation boundary. After that
candidate succeeds, implementations may either swap an owning indirection or
perform a non-failing field-wise commit into an address-stable live console.
Fallible in-place decoding or validation cannot satisfy the rule that a
malformed checkpoint leaves the live machine unchanged.

## Required invariants

At minimum, capture and restore tests must check:

- scheduler, CPU, PPU, and APU clock relationships are identical before and
  after restore;
- all raster counters and frame indexes agree with their pre-capture values;
- pending-write and trace-independent behavior resumes at the same clock;
- DMA/HDMA resumes from every substep, including suspended general DMA;
- an APU mid-instruction journal resumes without duplicating a port write;
- DSP audio and echo histories produce identical subsequent samples;
- open bus, MDR latches, and counter read phases survive;
- each cartridge device resumes from every byte/word protocol phase;
- a mid-frame PPU checkpoint produces a byte-identical completed frame;
- restored observation begins a new timeline with empty buffers;
- a rejected payload does not change any live state.

The principal equivalence test is:

1. run to a selected boundary;
2. capture checkpoint `C`;
3. run scripted inputs for `N` boundaries and record causal observations plus
   video/audio hashes;
4. restore `C`;
5. replay the same inputs for `N` boundaries;
6. require exact equality.

The matrix must include NTSC/PAL, reset and interrupt entry, WAI, active
DMA/HDMA, APU port synchronization, mid-frame rendering, SRAM, and every
implemented enhancement device.

## Implementation sequence

1. Add private per-subsystem causal snapshot value types and field-wise
   `capture_state`/`restore_state` functions.
2. Add transactional console capture/restore at a paused S-CPU boundary.
3. Implement versioned binary encoding independently of the in-memory snapshot
   types.
4. Expose checkpoint capability through the optional frontend debug target.
5. Add deterministic replay equivalence tests before reverse execution.

The process-global coprocessor and DSP output-continuity blockers are resolved.
The versioned in-memory causal snapshot types now cover the scheduler, S-CPU,
DMA/HDMA, interrupt controller, CPU bus, base cartridge, PPU, and APU. Bus state
includes WRAM, startup entropy configuration, open bus, all pending
CPU/PPU/APU writes, and APU progress within the current CPU interval.
Base-cartridge state includes bootstrap memory, SRAM bytes, and SRAM dirty
continuity. Its restore validates the loaded state, header, mapper, ROM size,
RAM size, and persistence topology before mutation. Canonical ROM SHA-256
remains an outer checkpoint-envelope precondition. Enhancement cartridges are
explicitly unsupported until each device has a complete causal snapshot,
preventing partial checkpoints from being accepted.

PPU state includes all video memories and register latches, hardware and raster
timing, entropy continuity, active background/object fetch pipelines,
compositor samples, and partial composed and presentation framebuffers. Restore
validates timing, raster, address-policy, buffer-count, pipeline-index, palette,
and geometry invariants before mutation. Presentation/debug policy is retained,
while completed-frame and trace observations from the abandoned timeline are
cleared.

APU state includes SPC700 clocks and registers, APURAM, IPL and I/O control,
CPU/APU ports, timers, DSP phase, the suspended-instruction access journal,
exact S-DSP state, and audio-output continuity. Capture and restore refuse an
active call-scoped CPU I/O window, leaving its bus pointer outside the payload.
Restore validates timer, wait-state, journal, DSP, and audio-output invariants
before mutation, then reconstructs DSP RAM and output pointers against the
destination APU. Instruction and I/O trace histories are cleared. Round-trip
coverage includes an active journal and deterministic continuation between two
independently restored APUs.

Restore paths validate before mutation, preserve existing device wiring and
trace policy, and clear trace contents from the abandoned timeline. Round-trip,
pending-write continuation, and atomic-rejection coverage is in place.
The subsystem states now compose into a transactional in-memory console state
for powered bootstrap and base-cartridge machines. Restore validates a
separately allocated and wired console before a non-failing field commit into
the live address-stable instance. Cross-subsystem checks cover requested and
resolved hardware configuration, CPU/PPU/APU/scheduler clocks, raster counters,
frame indexes, revision values, entropy configuration, and interrupt clock
bounds. Whole-console tests cover PAL base media, bootstrap memory, WRAM, SRAM,
independent-console deterministic continuation, topology mismatch, malformed
subsystem state, clock disagreement, and enhancement refusal.

Portable binary encoding and outer-envelope canonical SHA-256 validation are
now implemented in `frontend/SnesCheckpoint`. The version-1 format has a fixed
little-endian header, explicit field-wise payload encoding, per-subsystem schema
versions, bounded payload and cartridge-RAM decoding, and CRC-32 payload
integrity. It rejects wrong media before invoking the transactional core
restore. Exact round-trip coverage compares the complete in-memory causal state
and verifies that corrupt, truncated, oversized, wrong-version, wrong-ROM, and
cross-subsystem-invalid checkpoints leave the live console unchanged.

Enhancement-device state and optional debug-target exposure remain. The
existing inspection, stepping, observation, and debugger-session contracts do
not need to change to perform that work.

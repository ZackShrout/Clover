# Clover Workbench Multi-System Architecture

## Status

This document is the architectural contract implemented by the Workbench
multi-system refactor.
It turns the multi-system direction in the
[Workbench Blueprint](CLOVER_WORKBENCH_DISASSEMBLER_BLUEPRINT.md#24-multi-system-design)
into concrete ownership boundaries and a behavior-preserving migration plan.

The refactor prepares the existing SNES Workbench for additional emulator
cores. The next planned system is the Sega Genesis/Mega Drive, with Motorola
68000 and Z80 execution domains. This work does not implement that core,
Genesis inspectors, source editing, or IDE language support.

### Implementation status

The behavior-preserving implementation passes now provide:

- a generic tool and command registry with one authoritative active-tool
  identity;
- a target-support factory that owns emulator/debugger construction, project
  preparation, and toolkit registration;
- an SNES toolkit that registers the current output, palette, tile, tile-map,
  OAM, and DMA surfaces using stable identities;
- a system-neutral debugger contract and model for execution control,
  breakpoints, watchpoints, registers, memory, and stops; and
- a 65C816 instruction service that derives decode context from generic
  register metadata and injects instruction length plus call/return semantics
  into debugger stepping policy;
- SNES-owned analysis services for reset-vector discovery, static listings,
  hybrid-analysis composition, fingerprints, and project publication;
- an SNES presentation model that owns palette, tile, tile-map, live-PPU,
  object, and DMA selection/capture state plus tool navigation outside the SDL
  shell;
- an SNES SDL application and presentation adapter that own all SNES command
  bindings, inspection assembly, and palette, tile, tile-map, rendered-plane,
  OAM, and DMA drawing, leaving the shared shell responsible only for media
  loading and application dispatch;
- all SNES graphics analysis (palette, tile, tile-map, and OAM) located in the
  SNES analyzer module rather than presented as system-neutral concepts; and
- SNES-only tile-layer, object/OAM, and DMA diagnostic contracts located in a
  system namespace and compiled into `snes_emulator_core_t` only for the
  specialized Workbench build; and
- a deliberately non-SNES boundary test using a 32-bit Motorola 68000-shaped
  CPU domain, address space, register set, instruction encoding, execution
  step, and registered diagnostic tool without SNES headers.

The only SNES references outside the toolkit/application module are in the
compile-time composition roots that select a registered system implementation.
The second-system proof validates the generic contracts; it is intentionally
not a placeholder Genesis emulator.

## Product direction

Workbench is intended to become Clover's emulator laboratory: a trusted,
hardware-aware environment for developing, debugging, analyzing, and
eventually authoring software for every system Clover supports. It should make
it unnecessary to depend permanently on a separate reference emulator's
debugger while preserving reference emulators as useful comparison tools.

The long-term product has two cooperating surfaces:

- **Clover Workbench** owns execution control, machine inspection, traces,
  graphics and audio diagnostics, analysis evidence, projects, and temporal
  investigation.
- **JetBrains CLion** is the planned first-class source workspace for editing,
  navigation, completion, diagnostics, building, and source control.

Workbench remains the authoritative debugging backend. A future CLion plugin
or language-server-style bridge consumes a narrow, versioned protocol rather
than reaching into an emulator core. Clover's assembly intelligence remains
editor-neutral so the same services can support Workbench, CLion, and possible
later editor integrations.

## Design laws

1. **The host is system-neutral.** Windowing, layout, navigation, commands,
   project lifecycle, and generic debugger interaction must not depend on SNES
   analysis or hardware types.
2. **Architectures and systems are distinct.** A processor module describes an
   instruction set. A system module composes processors with memory maps,
   hardware symbols, devices, media rules, and specialized tools.
3. **Specialized tools remain first-class.** Multi-system support must not
   reduce Workbench to the lowest common denominator.
4. **Capabilities are optional.** A target advertises what it supports instead
   of implementing an ever-growing interface shaped by every console.
5. **Presented hardware truth comes from execution.** A diagnostic labeled as
   authoritative must be captured from the core's actual execution state.
   Later reconstruction from backing memory must be labeled explicitly.
6. **Player performance and behavior are invariant.** Workbench diagnostics
   must not impose execution, storage, binary, or dependency cost on Player.
7. **Projects use stable identities.** Persisted facts name their system,
   execution domain, address space, and producing service versions rather than
   relying on process-local enums or bare addresses.
8. **The refactor preserves behavior.** Existing SNES controls, projects,
   analysis results, and inspectors remain functional throughout migration.
9. **A binary plugin ABI is premature.** Compile-time module registration is
   sufficient until at least two real systems validate the boundary.

## Target architecture

```text
SDL Workbench presentation
    |
Workbench host and controller
    |-- command registry
    |-- tool registry and active view
    |-- navigation and project lifecycle
    |
Generic debug session
    |-- execution control
    |-- breakpoints and watchpoints
    |-- registers and memory
    |-- observations and checkpoints
    |
System support toolkit selected for the loaded core
    |-- processor architecture services
    |-- system analysis services and symbols
    |-- optional system-specific diagnostic tools
    |
Frontend DebugTarget and optional diagnostic capabilities
    |
Release emulator core / specialized Workbench core variant
```

### Workbench host

The generic host owns:

- SDL application lifecycle and presentation;
- panel layout and rendering orchestration;
- input routing and a command registry;
- one active tool/view identity instead of a matrix of mutually exclusive
  booleans;
- navigation history, selection coordination, and status messages;
- project opening and target-support selection; and
- generic register, memory, disassembly, breakpoint, and watchpoint surfaces.

The host must not include `analysis/snes` headers, instantiate an SNES core
directly, import SNES symbols, assume `snes.cpu-bus`, or bind fixed keys to PPU
concepts. It asks the selected system toolkit to register its commands and
tools.

### Generic debug session

The debugger owns system-neutral policy:

- running, pausing, and stepping;
- execution breakpoints and memory watchpoints;
- run-to and navigation history;
- register and safe-memory snapshots;
- bounded observation consumption; and
- checkpoint and replay coordination.

Instruction-specific behavior is injected. Step-over and step-out cannot
remain embedded in an `snes_debugger_t`, because recognizing calls, returns,
branches, targets, and instruction lengths belongs to a processor
architecture.

### Processor architecture services

An architecture service supplies:

- structured instruction decoding;
- instruction length and operand descriptions;
- formatting profiles;
- control-flow classification and direct targets;
- call, return, branch, and trap semantics;
- architectural context propagation; and
- architecture-specific source-language services when those are implemented.

Planned modules include:

```text
65C816       SNES main CPU
SPC700       SNES audio CPU
GSU          SNES Super FX, when exposed
Motorola 68000  Genesis main CPU
Z80          Genesis audio and compatibility domain
```

Architecture services do not own a console memory map or names such as
`INIDISP` or `VDP_CTRL`.

### System support toolkit

A toolkit composes one or more processor architectures with a system. It may
provide:

- execution-domain and address-space conventions;
- reset, interrupt, and other analysis seeds;
- cartridge or media mapping rules;
- built-in hardware symbols and documentation;
- system-specific analysis packages;
- graphics, audio, transfer, and device inspectors; and
- commands that open those tools.

The SNES toolkit initially wraps the current 65C816 decoder, analyzer, symbol
package, PPU views, OAM view, and DMA/HDMA provenance. A future Genesis toolkit
will compose 68000 and Z80 services with Genesis memory maps and VDP, CRAM,
VRAM, VSRAM, plane, window, sprite, audio, and DMA tools.

### Tools and commands

Each Workbench tool has a stable target-local identity, title, availability
requirements, commands, state, and presentation model. The host owns generic
activation and routing. A system toolkit registers specialized tools without
adding system branches to the host.

Generic tools can include:

- disassembly;
- registers and memory;
- breakpoints and watchpoints;
- symbols, functions, graphs, and cross-references;
- typed data; and
- timelines and project facts.

System tools can include SNES PPU/OAM/HDMA views or Genesis VDP/CRAM/DMA
views. Shared presentation components such as grids, timelines, image
surfaces, inspectors, and selection models may be reused without pretending
the underlying hardware schemas are identical.

### Optional diagnostic capabilities

`emulator_core_t` and `debug_target_t` must not acquire one new virtual method
for every console-specific inspector. Diagnostics should be grouped behind
optional, narrowly scoped providers at tooling boundaries, such as rendered
layers, backing graphics memory, objects, transfers, or audio state.

A common event envelope may eventually contain stable source/destination
addresses, timing, size, engine, and initiator information. Native details
remain owned by the system provider and are translated by its toolkit. The
current SNES tile, OAM, and DMA contracts therefore live under
`frontend::snes`; A-bus/B-bus and HDMA terminology are not imposed on future
Genesis VDP transfers.

System-neutral virtual dispatch belongs at media, frame, session, and tooling
boundaries—not in instruction, pixel, transfer-byte, or audio-sample hot
loops.

## Project and analysis identity

A Workbench project records:

- the emulated system;
- canonical media identity;
- stable execution-domain and address-space identifiers;
- target-support module version;
- processor decoder/analyzer versions; and
- provenance for imported and derived facts.

An execution domain identifies an independently executing processor. An
address space identifies where bytes or device state reside. Neither may be
substituted for the other, and Workbench addresses must never be persisted as
bare integers.

The current SNES project remains compatible. The refactor may migrate internal
ownership, but it must not silently reinterpret existing facts or manufacture
generic identities that lose their SNES meaning.

The existing palette, tile, tile-map, and related SQLite records remain an
SNES compatibility schema for this refactor. Their models and interpretation
live in the SNES analysis toolkit. A future system may add its own native asset
schema or motivate a genuinely shared abstraction; this refactor does not
rename SNES concepts into misleading universal ones.

## Specialized Workbench cores

Player and Workbench use the same hardware implementation. Workbench may build
a separate release-optimized variant that includes additional observation
storage or capture boundaries. This is analogous to a visibility-oriented
build, not a second emulator implementation.

To keep core code readable and Player fast:

- instrumentation is grouped into a small diagnostics policy/facade or a
  dedicated source set rather than scattered UI-aware conditionals;
- compile-time selection supplies a real Workbench implementation or removes
  it from Player;
- hot paths perform no allocation, formatting, I/O, locking, or callbacks;
- capture is bounded and exposes overflow explicitly;
- Player does not link Workbench projects, analysis, or diagnostic storage;
- ordinary core changes require the normal correctness and performance review;
  and
- instrumented boundaries require proof that the normal Player build retains
  unchanged behavior and no measurable cost, using object-code comparisons
  where practical and repeatable performance tests where necessary.

The core exposes observations. It does not own tool state, labels, comments,
projects, UI models, or editor integration.

## Future source editing and CLion integration

Source editing is a later authoring phase. This refactor creates compatible
boundaries but does not implement a JetBrains plugin or language server.

Future editor-neutral language services should provide:

- parsing and syntax trees;
- semantic highlighting and completion;
- instruction and addressing-mode validation;
- symbol resolution, references, and documentation;
- control-flow semantics;
- source-to-media and source-to-runtime mappings; and
- structured diagnostics.

The system toolkit selects and enriches architecture services:

```text
SNES toolkit
    |-- 65C816 language support
    |-- SPC700 language support
    `-- SNES hardware symbols and memory semantics

Genesis toolkit
    |-- Motorola 68000 language support
    |-- Z80 language support
    `-- Genesis hardware symbols and memory semantics
```

A future CLion integration may synchronize source breakpoints, the live PC,
labels, comments, runtime cross-references, coverage, and hardware-aware hover
information with Workbench. Generated reconstruction and user-authored source
remain separate; authored files are never silently regenerated.

## Refactor sequence

### Slice 1: Mechanical application seams

- Extract Workbench state, controller responsibilities, and SDL rendering from
  the monolithic application shell.
- Introduce a command registry, tool registry, and single active-view model.
- Preserve every existing SNES command and visible behavior.

### Slice 2: Generic debugger

- Move breakpoint, watchpoint, run-state, and navigation policy out of
  `SnesDebugger`.
- Introduce an instruction-services contract.
- Adapt the current 65C816 implementation without changing its results.

Implemented for the instruction-semantics boundary. The existing SNES
debugger retains SNES runtime-evidence extensions while generic run policy no
longer recognizes 65C816 opcodes directly.

### Slice 3: SNES system toolkit

- Move SNES core creation, reset-vector policy, hardware symbols, analyzer
  composition, and specialized tools out of the generic host.
- Register palette, VRAM, tile, BG, OAM, raw-rendered-layer, and DMA tools from
  SNES support.
- Keep the existing project and controls working.

Implemented. Core creation, symbols, reset-vector and analyzer policy,
graphics selection state, live BG capture, DMA selection, SDL command routing,
inspection assembly, and system-specific drawing now live in the SNES toolkit
and application adapter.

### Slice 4: Optional capability cleanup

- Split console-specific inspection methods away from an expanding base core
  interface.
- Preserve the specialized release-optimized Workbench instrumentation model.
- Re-run correctness and Player non-interference checks.

Implemented. The ordinary `emulator_core_t` has no tile, OAM, or DMA inspection
methods. SNES owns those optional capability contracts, and
`snes_emulator_core_t` inherits and compiles them only when
`CLOVER_WORKBENCH_DIAGNOSTICS` selects the separately built
`clover_core_workbench` variant. The normal Player core contains no diagnostic
interfaces, methods, storage, or capture branches.

### Slice 5: Multi-system boundary proof

- Add a small test target with different execution-domain, processor, and
  address-space identities.
- Verify that it can expose registers, memory, disassembly, and stepping
  without SNES includes or branches in the host.
- Do not implement a placeholder Genesis emulator merely to satisfy this gate.

Implemented by `clover_workbench_target_boundary_test`. Its fake target uses
different domain and address-space identities, a 32-bit 68000 processor shape,
generic memory/register inspection, injected six-byte call semantics, generic
execution stepping, and a non-SNES registered tool.

## Acceptance gates

The completed implementation satisfies these gates:

- the generic Workbench host contains no `analysis/snes` dependency;
- shared host policy does not hard-code SNES address spaces, symbols, PPU
  terminology, or specialized shortcuts; system references are confined to
  compile-time composition roots and the SNES implementation;
- generic debugging is driven by execution-domain and address-space
  descriptors;
- instruction-aware debugger operations use injected architecture services;
- SNES-specific tools are registered by an SNES toolkit;
- a test target proves a second system shape can be added without modifying
  the generic host;
- all existing SNES Workbench tests and project migrations pass;
- existing SNES workflows remain behaviorally equivalent; and
- Player remains free of Workbench UI, analysis dependencies, instrumentation
  storage, and measurable diagnostic overhead.

## Explicit non-goals for this refactor

This work does not yet:

- implement a Genesis emulator core;
- implement 68000, Z80, SPC700, or GSU debugging unless required by an
  independent scheduled feature;
- build a CLion plugin, language server, source editor, assembler, or patch
  workflow;
- define a stable third-party binary plugin ABI;
- redesign the analysis database beyond migrations required for stable target
  identity; or
- generalize SNES hardware concepts into misleading universal schemas.

These are future consumers of the boundary, not prerequisites for extracting
it safely.

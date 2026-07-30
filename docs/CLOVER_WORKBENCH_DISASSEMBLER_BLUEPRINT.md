# Clover Workbench
## A World-Class SNES Disassembler, Debugger, Program-Analysis Environment, and Game-Authoring Platform

## Purpose

Clover is intended to be more than an emulator used only to play games.

It is also intended to become a serious environment for understanding, analyzing, modifying, designing, and eventually building software for the systems it emulates. For that purpose, a basic disassembler is necessary, but a basic disassembler is not sufficient.

A conventional disassembler converts bytes into assembly instructions:

```asm
LDA #$20
STA $2100
```

A world-class implementation must answer substantially deeper questions:

- Which processor mode caused the immediate operand to be decoded as eight bits?
- Is the byte sequence actually executable code, or is it data that merely resembles code?
- What control-flow paths reach this instruction?
- Which bank, memory map, cartridge configuration, and address space apply?
- What hardware register does `$2100` represent?
- What side effects occur when that register is written?
- Which exact execution caused the instruction to run?
- Which game state, graphics asset, sound effect, script, or subsystem does the code influence?
- Was the code copied to WRAM before execution?
- Was it modified after being copied?
- How did the instruction interact with DMA, HDMA, interrupts, PPU timing, APU communication, or a cartridge coprocessor?
- Which user annotations, imported symbols, inferred facts, and runtime observations support the current interpretation?
- Can the user rename, annotate, classify, export, patch, revisit, and reproduce the analysis?

The correct long-term goal is therefore not merely a disassembler.

The goal is an integrated, hardware-aware, trace-assisted, deterministic program-analysis and game-authoring environment built around Clover’s emulation cores.

This document refers to that environment as **Clover Workbench**.

Clover Workbench should be understood as a family of connected capabilities:

- instruction decoding;
- static disassembly;
- recursive control-flow analysis;
- trace-assisted analysis;
- symbols and annotations;
- typed data interpretation;
- live debugging;
- temporal queries;
- deterministic replay;
- reverse debugging;
- graphics and audio provenance;
- patching and assembly;
- asset inspection and replacement;
- project persistence;
- eventual source reconstruction.

The disassembler is the first major view into that system, but it is only one view.

## Document Status and Planning Horizons

This document is a product north star and architectural direction. Section 28
also serves as the authoritative delivery ledger. As of July 30, 2026, Stages
0 through 4 and the Stage 5.1-5.2 typed-data and palette foundations are
complete: Clover has the inspection and checkpoint substrate, the structured
decoder, the persistent Workbench, live debugger integration, the hybrid
analyzer, durable typed definitions and object bindings, and persistent
palette inspection. Descriptions outside Section 28 define the target
experience unless they explicitly describe the current implementation.

The capabilities described here fall into four planning horizons:

```text
Established
    Side-effect-free inspection, address translation, observation contracts,
    exact checkpoints, decoder infrastructure, persistent projects,
    live stepping, breakpoints, watchpoints, durable annotations, functions,
    blocks, cross-references, runtime coverage, provenance, and conflicts

Next
    Typed data, graphics and audio views, DMA provenance, and hardware-aware
    inspectors

Medium term
    Reverse-debugging workflows built on the checkpoint substrate and temporal
    investigation

Long term
    Reverse-debugging workflows built on the checkpoint substrate, asset
    authoring, patching, and reassemblable reconstruction
```

Later sections intentionally describe all four horizons so that early
interfaces do not prevent the long-term product. The implementation sequence
in Section 28 is the authoritative ordering; feature lists elsewhere describe
the target experience, not the first release.

---

# 1. Core Product Vision

A world-class Clover analysis environment should connect several different forms of truth:

```text
What the bytes could mean
What static analysis believes they mean
What the emulated hardware actually executed
What the user has explicitly established
What game resource the code affects
When the machine state changed
Why the machine state changed
How the user can intentionally modify the result
```

The ideal experience is not simply scrolling through assembly.

The ideal experience is navigating a connected machine model.

For example:

```text
Rendered framebuffer pixel
    → winning PPU layer candidate
    → tile-map entry
    → tile graphics in VRAM
    → palette entry in CGRAM
    → DMA transfer that uploaded the graphics
    → WRAM staging buffer
    → decompression routine
    → compressed ROM asset
    → function that initiated the transfer
    → callers of that function
    → patch or asset replacement authored by the user
```

Likewise, for audio:

```text
Audible sound effect
    → active DSP voice
    → BRR sample
    → SPC700 sequence command
    → APU-side dispatch routine
    → CPU-to-APU port write
    → game-side sound-effect function
    → gameplay event that triggered it
```

This is the actual end state that makes Clover useful not only for emulation, but for reverse engineering, preservation, game design, ROM hacking, debugging, education, and eventually original development.

---

# 2. Architectural Placement

## 2.1 The analysis environment must not live inside the emulated hardware core

Clover’s existing architecture already establishes the correct separation.

The emulation core should continue to own:

- emulated hardware state;
- hardware timing;
- CPU execution;
- bus behavior;
- PPU behavior;
- APU and DSP behavior;
- DMA and HDMA;
- interrupts;
- cartridge mapping;
- cartridge coprocessors;
- exact hardware-visible side effects.

The analysis and authoring environment should live above that core.

A useful conceptual architecture is:

```text
┌──────────────────────────────────────────────┐
│ Clover Workbench UI                         │
│                                              │
│ Disassembly, graphs, inspectors, timelines, │
│ asset tools, patch tools, project tools     │
└──────────────────────────┬───────────────────┘
                           │
┌──────────────────────────▼───────────────────┐
│ Analysis and Debug Services                 │
│                                              │
│ Symbol database, CFG, xrefs, types, traces, │
│ provenance, replay, breakpoints, queries    │
└──────────────────────────┬───────────────────┘
                           │ typed observations
┌──────────────────────────▼───────────────────┐
│ Frontend / Debug Adapter                    │
│                                              │
│ System-neutral session control with         │
│ optional system-specific capabilities       │
└──────────────────────────┬───────────────────┘
                           │
┌──────────────────────────▼───────────────────┐
│ Emulator Core                              │
│                                              │
│ Hardware state, clocks, execution, buses,   │
│ raster, audio, DMA, interrupts, devices     │
└──────────────────────────────────────────────┘
```

The emulator core should expose precise observations.

It should not own the user’s analysis project.

## 2.2 What the core may expose

The core may expose typed, opt-in events or state snapshots such as:

- instruction boundaries;
- opcode fetches;
- architectural CPU state at observation boundaries;
- executed CPU addresses;
- effective addresses;
- memory reads and writes;
- call and return observations;
- interrupt assertion, latch, poll, acknowledge, and consume events;
- DMA and HDMA setup and transfer events;
- PPU register reads and writes;
- raster position;
- APU port communication;
- SPC700 execution events;
- DSP register events;
- cartridge-coprocessor commands and data transactions;
- completed video frames;
- per-pixel or per-candidate provenance when explicitly enabled;
- audio voice state;
- memory snapshots;
- deterministic checkpoints.

These observations should remain:

- typed;
- bounded where possible;
- disabled by default;
- free of formatting work in hot paths;
- free of disk I/O in the core;
- absent from normal execution cost when not enabled.

## 2.3 What the core must not own

The core should not know about:

- user-defined labels;
- symbol names;
- function names;
- comments;
- bookmarks;
- graph layouts;
- syntax highlighting;
- project files;
- decompiler-style rendering;
- editable structures;
- source exports;
- patch files;
- assembler project organization;
- GUI panel state;
- user preferences.

Those belong to Workbench.

This preserves the distinction between emulated hardware truth and user-facing interpretation.

## 2.4 Current Clover substrate and remaining work

Clover now provides the foundations and first end-to-end Workbench slices:

- a headless `console_t`;
- explicit CPU, bus, PPU, APU, DMA, interrupt, and cartridge ownership;
- master-clock, raster, and frame coordinates;
- hardware-step, frame-step, and main-CPU instruction-step execution surfaces;
- side-effect-free live memory inspection and typed processor-register state;
- public, pure cartridge-aware CPU-bus address translation;
- masked, bounded, opt-in instruction-boundary and CPU-memory observations;
- frontend debug-session, execution-control, observation, and checkpoint
  capabilities;
- exact, portable, schema-versioned machine capture and transactional restore;
- deterministic replay-equivalence coverage across base and implemented
  enhancement hardware;
- deterministic-capable startup through explicit entropy configuration;
- deterministic controller input capture and replay;
- shared canonical-media identity outside the SDL platform layer;
- save-RAM restoration;
- a headless structured 65C816 decoder and listing service;
- a transactional SQLite analysis project keyed by canonical media identity;
- a separate SDL Workbench with durable annotations and navigation; and
- a Workbench-owned live run controller with persistent execution breakpoints,
  CPU-bus watchpoints, stepping, runtime-correct disassembly, registers, and
  safe memory views.

The remaining work is no longer the creation of a debugger substrate. It is
the expansion of that substrate into richer analysis and investigation
services:

- typed DMA, interrupt, PPU, APU, and cartridge-device observations where a
  later feature has a concrete consumer;
- SPC700 and optional cartridge-processor debug stepping;
- typed-data and hardware-aware asset inspection;
- reverse-debugging UI and temporal queries built on the verified checkpoint
  primitive; and
- authoring, patching, and reconstructed-source workflows.

This distinction is important when describing current readiness:

> Clover has a tested inspection, observation, control, checkpoint, decoder,
> project, live-debugger, and hybrid-analysis substrate. The next challenge is
> applying that connected model to typed game data and hardware-aware assets.

## 2.5 Player separation and non-interference

Clover Player and Clover Workbench should be separate applications built on the
same hardware implementation:

```text
Clover hardware cores
    ├── Clover Player
    │   └── video, audio, input, saves, and media library
    └── Clover Workbench
        └── debugging, analysis, projects, traces, and authoring
```

The Player must not become a reduced Workbench interface with disabled panels.
It should remain a focused way to load and play software. Workbench may present
the denser interface appropriate to professional analysis without exposing that
complexity to Player users.

Heavy analysis services should remain outside the Player link graph. This
includes project databases, control-flow analysis, graph layout, trace-history
storage, source tooling, and target-specific inspector UI. Both applications
must nevertheless use the same CPU, PPU, APU, DMA, cartridge, and timing
implementations so that a separate "debug core" cannot drift from the core
players use.

The core may contain dormant inspection and observation boundaries. When they
are unused, they must not allocate, format strings, lock, perform I/O, retain
trace history, or change scheduling. Expensive event payloads must only be
constructed after their event class has been enabled.

The governing contract is:

> Every Workbench feature must be absent from the Player user experience,
> dormant unless explicitly activated, and incapable of changing emulated
> results.

---

# 3. The First Foundation: A Correct Structured Instruction Decoder

## 3.1 The decoder must return structure, not text

The most basic component is a correct instruction decoder.

The decoder should not primarily return a formatted assembly string.

It should return a structured representation from which multiple consumers can derive their own output.

For example:

```cpp
struct decoded_instruction_t
{
    cpu_address_t address{};
    uint8_t opcode{};

    instruction_id_t instruction{};
    addressing_mode_t addressing_mode{};

    uint8_t encoded_size{};
    uint8_t operand_size{};

    std::array<uint8_t, 4> bytes{};
    uint8_t byte_count{};

    std::optional<int32_t> relative_displacement{};
    std::optional<cpu_address_t> direct_target{};
    std::optional<address_expression_t> address_expression{};

    cpu_decode_context_t context{};
    decode_certainty_t certainty{};
};
```

The decoder should represent:

- opcode identity;
- instruction identity;
- addressing mode;
- encoded length;
- operand length;
- operand bytes;
- immediate width;
- branch displacement;
- direct target where statically resolvable;
- unresolved address expressions where not resolvable;
- processor context;
- certainty;
- errors or ambiguity.

Formatting should be a separate operation:

```cpp
formatted_instruction_t format_instruction(
    const decoded_instruction_t& instruction,
    const formatting_context_t& context);
```

This separation allows the same decoder to support:

- textual assembly views;
- debugger stepping;
- static analysis;
- control-flow graph generation;
- syntax highlighting;
- instruction search;
- trace decoding;
- machine-readable exports;
- tests;
- source generation;
- future non-graphical tools.

## 3.2 Shared opcode metadata

The CPU implementation and disassembler should share authoritative instruction metadata where doing so is clean and safe.

For example:

```cpp
struct opcode_descriptor_t
{
    instruction_id_t instruction{};
    addressing_mode_t addressing_mode{};
    width_rule_t operand_width{};
    flag_effects_t flag_effects{};
    control_flow_kind_t control_flow{};
};
```

The same opcode table may describe:

- mnemonic identity;
- addressing mode;
- operand-width rule;
- control-flow category;
- static flag effects.

However, the following responsibilities should remain distinct:

- instruction decoding;
- execution semantics;
- exact bus-cycle behavior;
- formatting;
- static analysis;
- dynamic tracing.

A shared metadata table must not turn into a giant abstraction that tangles execution and analysis.

The first shared table should be limited to architectural metadata that can be
made authoritative immediately:

- instruction identity;
- addressing mode;
- encoded operand rule;
- control-flow category;
- architectural flag effects.

Cycle descriptions should not be labeled authoritative merely because they
appear in the table. Clover’s current execution semantics are organized across
opcode-family implementations, and some opcodes can still reach placeholder
execution. Cycle metadata should become shared only when execution consumes it
or tests mechanically prove it agrees with the executor.

## 3.3 Byte sources must be side-effect-free

Offline decoding should consume immutable canonical ROM bytes or an explicit
memory image. Live decoding should consume a debugger inspection interface.

Neither path should decode by issuing ordinary reads through the emulated bus.
Hardware reads may change latches, acknowledge state, advance device protocols,
or update open-bus residue. A read that preserves open-bus state is not
necessarily side-effect-free for every mapped device.

A suitable boundary might be:

```cpp
class analysis_memory_view_t
{
public:
    virtual ~analysis_memory_view_t() = default;

    [[nodiscard]] virtual inspected_byte_t inspect(
        address_identity_t address) const noexcept = 0;
};
```

The result should distinguish a present byte from an unmapped address, device
space, unavailable runtime memory, or a versioned runtime-code region.

---

# 4. The Central 65C816 Problem: Context-Dependent Instruction Width

## 4.1 The byte stream is not always self-describing

The 65C816 cannot always be disassembled correctly from bytes alone.

Immediate operand width depends on processor state.

For example:

- accumulator-width immediates depend on `M`;
- index-width immediates depend on `X`;
- emulation mode constrains processor state;
- `REP` may clear `M` or `X`;
- `SEP` may set `M` or `X`;
- `PLP` restores status from the stack;
- `RTI` restores status from an interrupt frame;
- `XCE` can change emulation mode;
- interrupt entry changes execution context;
- dynamically restored state can make static inference uncertain.

Consider:

```text
A9 34 12
```

Under one context:

```asm
LDA #$1234
```

Under another:

```asm
LDA #$34
ORA ($12)
```

A poor disassembler silently chooses one.

A world-class disassembler represents the context and, where necessary, the uncertainty.

## 4.2 Required disassembly modes

Clover Workbench should support at least three explicit disassembly modes.

### Static contextual disassembly

The analyzer propagates possible `E`, `M`, and `X` states through control flow.

Each instruction may have:

- one known decode context;
- several possible decode contexts;
- an unknown context;
- contradictory incoming contexts.

### Trace-backed disassembly

The decoder uses the actual CPU state observed when the instruction executed.

This is the strongest way to decode executed instructions because Clover owns the exact state at the instruction boundary.

A trace-backed instruction can record:

```cpp
struct observed_decode_context_t
{
    bool emulation_mode{};
    bool accumulator_is_8_bit{};
    bool index_is_8_bit{};
    uint16_t direct_page{};
    uint8_t data_bank{};
    uint8_t program_bank{};
};
```

### Manual or raw disassembly

The user chooses the assumed state.

This is necessary for:

- regions not yet executed;
- imported source assumptions;
- isolated bank inspection;
- manually classified routines;
- experiments;
- damaged or partially understood binaries.

## 4.3 Context propagation

Static analysis should propagate an abstract processor state.

A simplified abstract state might be:

```cpp
enum class ternary_bit_t
{
    clear,
    set,
    unknown
};

struct abstract_cpu_context_t
{
    ternary_bit_t emulation{};
    ternary_bit_t m{};
    ternary_bit_t x{};
    ternary_bit_t carry{};

    abstract_value16_t direct_page{};
    abstract_value8_t data_bank{};
};
```

Carry is required because `XCE` exchanges carry and emulation mode. An
`E/M/X`-only state is sufficient to decode a known instruction boundary, but it
is not sufficient to propagate mode through `CLC` or `SEC` followed by `XCE`.

The analyzer should understand direct effects from instructions such as:

- `REP`;
- `SEP`;
- `CLC` / `SEC` before `XCE`;
- `XCE`;
- `PLP`;
- `RTI`;
- status-changing interrupt transitions.

Interrupt entry does not itself arbitrarily redefine `E/M/X`: it selects a
vector according to emulation state and preserves the width context while
changing other status. `RTI` may restore `M/X` from the saved status in native
mode. The analyzer should model those exact transitions rather than treating an
interrupt as a generic unknown-state boundary.

It must also understand when state becomes unknown.

For example, after `PLP`, static analysis in native mode may not know `M` and
`X` unless stack contents are known. In emulation mode, the architectural
constraints still force both width flags to the eight-bit state.

The correct response is not to guess.

The correct response is to preserve ambiguity.

---

# 5. Address Identity and Memory Mapping

## 5.1 An SNES address is not merely a ROM offset

A high-quality analysis model must distinguish several forms of identity:

- CPU bus address;
- cartridge-visible address;
- physical ROM offset;
- SRAM offset;
- WRAM offset;
- PPU register;
- CPU I/O register;
- APU port;
- DMA register;
- mirrored address;
- cartridge-coprocessor aperture;
- copied or generated executable RAM;
- memory-mapped device space.

A single displayed number is not enough.

## 5.2 Suggested address model

```cpp
enum class address_space_t
{
    cpu_bus,
    rom,
    sram,
    wram,
    ppu_mmio,
    cpu_mmio,
    dma_mmio,
    apu_port,
    cartridge_device,
    apu_ram,
    vram,
    cgram,
    oam
};

struct address_identity_t
{
    address_space_t space{};
    uint32_t logical_address{};

    std::optional<uint32_t> physical_offset{};
    std::optional<uint32_t> canonical_storage_offset{};

    mapping_kind_t mapping{};
    mirror_class_t mirror{};
    region_identity_t region{};
};
```

The UI should allow movement among equivalent identities:

```text
CPU address:  $C2:A410
ROM offset:   $212410
Symbol:       Player_Update
Source label: player_update
```

These may refer to the same byte, but they are not the same concept.

## 5.3 Mapping must be cartridge-aware

The analysis system must use the active cartridge configuration.

It should not assume that all ROMs use the same mapping.

It must eventually support:

- LoROM;
- HiROM;
- ExLoROM;
- ExHiROM;
- unusual boards;
- enhancement-chip mappings;
- cartridge RAM;
- coprocessor windows;
- bank aliases;
- ROM mirrors.

The current SNES implementation may initially support only the mappings Clover already emulates, but the data model should not hard-code a single map.

## 5.4 Canonical byte identity

Mirrors create an important analytical problem.

Several CPU addresses may refer to the same underlying ROM byte.

Workbench should distinguish:

- logical reference identity;
- canonical stored-byte identity.

This makes it possible to answer both:

- “Which bus addresses can reach this byte?”
- “Which instructions refer to this physical byte?”

---

# 6. Code Versus Data

## 6.1 Linear sweep is not enough

A ROM intermixes many kinds of content:

- executable instructions;
- interrupt vectors;
- pointer tables;
- jump tables;
- graphics;
- tile maps;
- palettes;
- strings;
- scripts;
- music sequences;
- compressed data;
- lookup tables;
- constants;
- structures;
- alignment bytes;
- unused space;
- embedded binary payloads.

A linear sweep disassembler will decode all of these as instructions.

That output may look impressive while being deeply wrong.

## 6.2 Recursive traversal

The static analyzer should use recursive traversal as its primary code-discovery strategy.

A basic algorithm is:

1. Seed a work queue with known entry points.
2. Decode an instruction under the current abstract context.
3. Record the instruction and basic block.
4. Add direct branch targets.
5. Add direct call targets.
6. Continue fallthrough where control flow allows.
7. Stop at unconditional transfer, return, trap, unresolved ambiguity, or known data.
8. Avoid re-decoding already resolved bytes under the same relevant context.
9. Record conflicts where bytes are reached under incompatible interpretations.

Initial entry points include:

- the reset vector;
- native and emulation COP, ABORT, and NMI vectors;
- the native BRK vector;
- the native IRQ vector;
- the shared emulation IRQ/BRK vector;
- known startup routines;
- runtime-observed execution addresses;
- user-defined function entries;
- imported symbol entries;
- pointer-table targets;
- cartridge-device handlers.

## 6.3 Recursive traversal is still incomplete

Static traversal cannot resolve all SNES software patterns.

Common complications include:

- indirect calls;
- computed jumps;
- dispatch tables;
- bank-relative function tables;
- script interpreters;
- copied-to-WRAM code;
- decompressed executable overlays;
- self-modifying code;
- dynamically generated code;
- state-dependent dispatch;
- code reached only after long gameplay paths.

This is why runtime observation is not an optional luxury.

It is a core differentiator.

---

# 7. Hybrid Static and Dynamic Analysis

## 7.1 Clover owns the executing machine

Most static reverse-engineering tools analyze a binary from outside the machine.

Clover executes the machine.

That gives Workbench access to facts that a conventional disassembler must infer or leave unresolved.

The emulator can directly observe:

- actual program counters;
- actual `E`, `M`, and `X` state;
- actual branch outcomes;
- actual indirect targets;
- actual effective addresses;
- actual bank registers;
- actual stack behavior;
- actual interrupt entry;
- actual DMA activity;
- actual MMIO accesses;
- actual copied code;
- actual self-modification.

This should be treated as a first-class analysis source.

## 7.2 Instruction observation model

A rich observation might include:

```cpp
struct instruction_observation_t
{
    uint64_t master_clock{};

    cpu_address_t pc{};
    uint8_t opcode{};

    cpu_state_summary_t before{};
    cpu_state_summary_t after{};

    std::optional<effective_address_t> effective_address{};
    std::optional<cpu_address_t> branch_target{};

    control_flow_event_t control_flow{};
    memory_access_summary_t memory_accesses{};

    std::optional<interrupt_context_t> interrupt{};
};
```

It is not necessary to permanently store every full event.

Workbench can fold raw observations into durable facts.

Instruction observations should remain bounded. Multi-byte read-modify-write
instructions, block moves, interrupt entry, and device interactions may produce
several accesses, while DMA and HDMA are not naturally owned by one CPU
instruction. The event contract should therefore support:

- a bounded list or span of instruction-owned accesses;
- an explicit truncation or overflow marker;
- independent DMA, HDMA, interrupt, PPU, APU, and cartridge-device events;
- shared master-clock and causal identifiers for correlating those events.

The core should report raw architectural and hardware facts. Workbench should
apply the structured decoder and attach instruction meaning outside the
execution path.

Examples:

- address executed;
- instruction executed under a particular mode;
- branch seen taken;
- branch seen not taken;
- direct or indirect call target observed;
- return target observed;
- memory range read;
- memory range written;
- MMIO register accessed;
- code byte modified;
- WRAM region executed;
- function reached from caller;
- data table index values observed;
- DMA source and destination observed.

## 7.3 Example: resolving an indirect dispatch

Static analysis may reach:

```asm
JSR ($1234,X)
```

and produce:

```text
Indirect call target unresolved.
```

Runtime analysis may later observe:

```text
Observed targets:
    $84:9210
    $84:9348
    $84:9472
```

The analyzer should then:

1. record those targets as runtime-observed;
2. classify them as candidate function entries;
3. analyze them statically;
4. add call-graph edges;
5. preserve the fact that the target set may not yet be complete.

This last point matters.

Observed targets are evidence, not proof that every possible target has been seen.

## 7.4 Confidence and provenance

Every analytical conclusion should record where it came from.

```cpp
enum class evidence_source_t
{
    hardware_definition,
    architecture_rule,
    imported_symbol,
    imported_debug_info,
    static_analysis,
    runtime_observation,
    user_defined,
    generated_export
};
```

A fact should also carry confidence or status:

```cpp
enum class analysis_certainty_t
{
    confirmed,
    observed,
    inferred,
    probable,
    ambiguous,
    conflicting,
    unknown
};
```

This allows Workbench to say:

```text
Function entry at $84:9210
Status: observed
Evidence:
    - indirect call target observed 1,482 times
    - valid recursive decode
    - user named function
```

The tool should never hide uncertainty merely to produce clean-looking output.

---

# 8. Persistent Analysis Database

## 8.1 Analysis must outlive the current UI session

Annotations and discovered facts should not be stored as incidental GUI state.

Workbench needs a real persistent analysis project.

The project should contain:

- ROM identity;
- system identity;
- hardware profile;
- mapping configuration;
- address classifications;
- symbols;
- functions;
- labels;
- comments;
- bookmarks;
- basic blocks;
- control-flow edges;
- call-graph edges;
- cross-references;
- observed processor contexts;
- execution coverage;
- data types;
- structures;
- enums;
- bitfields;
- pointer tables;
- string encodings;
- assets;
- patch definitions;
- imported symbol packages;
- analyzer settings;
- project schema version;
- analyzer version and analysis generation;
- user assertions;
- analyzer-generated assertions;
- conflicts;
- evidence provenance;
- trace summaries;
- checkpoint references;
- UI navigation history where useful.

## 8.2 Layered facts

A useful model is to keep facts in layers:

```text
Hardware facts
Imported symbols and metadata
Static-analysis results
Runtime observations
User annotations
Generated patch/export facts
```

A user-defined label should not be silently replaced by an inferred label.

A runtime observation should not silently overwrite an imported source symbol.

Conflicts should be visible.

## 8.3 ROM identity

Projects should be keyed by canonical ROM identity, ideally the same canonical SHA-256 identity Clover already uses for its ROM library.

The project may also record:

- original source path;
- copier-header presence;
- original file hash;
- canonical cartridge hash;
- patch stack;
- effective modified-image hash;
- mapping selection;
- region;
- hardware profile;
- enhancement chips.

## 8.4 Storage format

A practical design may use SQLite because the project will contain connected, query-heavy data.

Possible major tables include:

```text
project
images
address_regions
symbols
functions
basic_blocks
instructions
control_flow_edges
cross_references
data_definitions
type_definitions
comments
bookmarks
runtime_contexts
coverage
memory_access_summaries
assets
patches
evidence
conflicts
```

Large traces and checkpoints may live in separate binary files referenced by the database.

## 8.5 Derived facts, evidence, and invalidation

Persistent does not mean permanently valid.

Instructions, basic blocks, cross-references, inferred types, confidence values,
and graph edges are derived from a particular image, decoder, analyzer version,
configuration, and set of evidence. When any of those inputs changes,
Workbench must be able to invalidate and regenerate derived results without
damaging user-authored knowledge.

Every derived fact should therefore be attributable to:

- canonical input-image identity;
- effective patched-image identity where applicable;
- analyzer and decoder version;
- analysis generation;
- relevant analyzer settings;
- source session or sessions for runtime evidence;
- one or more supporting evidence records.

Evidence should be modeled as relationships rather than as one enum stored on a
fact. A function entry, for example, may simultaneously be supported by an
imported symbol, recursive decoding, several runtime sessions, and a user
assertion.

The storage model should distinguish:

```text
Authoritative project input
    User names, comments, explicit classifications, imported source facts

Regenerable derived state
    Decoded instructions, inferred blocks, generated xrefs, confidence scores

Session evidence
    Observations tied to an execution trajectory and time coordinate
```

Runtime facts may also need temporal validity. Versioned WRAM code, mutable
tables, overlays, and patched images can make the same logical address mean
different things at different times.

Schema migrations must be explicit. Opening an older project should either
migrate it transactionally or preserve it read-only; it should never silently
reinterpret stale derived rows as current truth.

---

# 9. Symbols and Semantic Hardware Names

## 9.1 Hardware registers should not remain anonymous numbers

A SNES-focused disassembler should not force users to read this indefinitely:

```asm
STA $2116
STA $2117
STA $2118
```

It should display symbolic names:

```asm
STA VMADDL
STA VMADDH
STA VMDATAL
```

or, under a more descriptive formatting profile:

```asm
STA PPU.VRAM_ADDRESS_LOW
STA PPU.VRAM_ADDRESS_HIGH
STA PPU.VRAM_DATA_LOW
```

## 9.2 Built-in symbol packages

Clover Workbench should ship with authoritative symbol packages for:

- PPU registers;
- CPU I/O registers;
- DMA and HDMA channel registers;
- APU communication ports;
- controller registers;
- interrupt vectors;
- WRAM regions;
- cartridge header fields;
- cartridge coprocessor registers;
- common hardware bitfields.

Each register definition can include:

- canonical name;
- aliases;
- address;
- access width;
- read/write direction;
- bitfield definitions;
- side effects;
- open-bus behavior;
- applicable hardware profiles;
- timing notes;
- documentation links;
- current live value;
- recent accesses.

## 9.3 Navigation from symbols to hardware inspectors

Clicking a hardware symbol should open the relevant live inspector.

For example, selecting `VMDATAL` could show:

- current register state;
- current VRAM address;
- increment mode;
- last writer;
- recent write history;
- written data;
- affected VRAM range;
- raster position at access;
- instruction that performed the write;
- related DMA events;
- register documentation.

This is one of the ways Workbench becomes more than a text disassembler.

---

# 10. Functions, Basic Blocks, and Control-Flow Graphs

## 10.1 Basic blocks

The analyzer should divide code into basic blocks.

A basic block begins at:

- a function entry;
- a branch target;
- a fallthrough after a conditional branch;
- an interrupt entry;
- a user-defined entry;
- a runtime-observed entry;
- an address after an unconditional control transfer if separately reachable.

A block ends at:

- conditional branch;
- unconditional branch;
- jump;
- call, depending on graph representation;
- return;
- interrupt return;
- stop or wait instruction;
- unresolved decode boundary;
- data conflict.

## 10.2 Control-flow edges

Edges should preserve their meaning:

```cpp
enum class control_flow_edge_kind_t
{
    fallthrough,
    conditional_taken,
    conditional_not_taken,
    direct_jump,
    indirect_jump_observed,
    direct_call,
    indirect_call_observed,
    return_edge_observed,
    interrupt_entry,
    interrupt_return,
    unknown
};
```

The graph should distinguish:

- statically proven edges;
- runtime-observed edges;
- inferred edges;
- unresolved edges.

## 10.3 Function summaries

A function view should eventually provide information such as:

```text
Player_Update

Address
    $82:9410

Aliases
    player_update
    sub_829410

Called by
    Main_Game_Loop
    Pause_Resume

Calls
    Read_Controller
    Update_Player_Physics
    Resolve_Player_Collision
    Build_Player_Sprites

Reads
    WRAM $7E:1200-$7E:123F
    Controller state
    Level collision table

Writes
    WRAM $7E:1400
    WRAM player position fields
    OAM staging buffer

Hardware effects
    Initiates OAM DMA indirectly
    Reads joypad state

Runtime observations
    18,412 calls
    97.4% discovered block coverage
    Observed in M=8/X=16 mode
```

## 10.4 Call-stack reconstruction

The debugger should reconstruct call stacks where possible.

The 65C816 complicates this because code may use:

- `JSR`;
- `JSL`;
- indirect calls;
- manually manipulated return addresses;
- interrupt frames;
- tail calls;
- custom dispatch mechanisms.

Workbench should distinguish:

- structurally inferred stack frames;
- runtime-observed call frames;
- uncertain frames;
- interrupt frames.

It should not pretend the stack is always perfectly recoverable.

---

# 11. Typed Data Analysis

## 11.1 Bytes need richer interpretations

For game design and reverse engineering, data typing is as important as code disassembly.

The user should be able to define regions as:

- unsigned 8-bit integers;
- signed 8-bit integers;
- unsigned 16-bit integers;
- signed 16-bit integers;
- unsigned 24-bit integers;
- fixed-point values;
- pointers;
- banked pointers;
- arrays;
- structures;
- unions;
- enums;
- bitfields;
- strings;
- encoded text;
- pointer tables;
- jump tables;
- tile data;
- tile maps;
- palettes;
- OAM records;
- DMA tables;
- HDMA tables;
- music sequences;
- script bytecode;
- compressed assets;
- opaque binary blobs.

## 11.2 Custom structures

A game-specific structure might be defined as:

```cpp
struct enemy_definition
{
    uint16_t hp;
    uint8_t attack;
    uint8_t defense;
    uint24_t ai_script;
    uint16_t sprite_id;
};
```

The user should then be able to apply that type to a table.

Instead of seeing:

```text
5A 00 10 08 34 A2 C1 07 00 ...
```

the user should see:

```text
Enemy 0
    hp          = 90
    attack      = 16
    defense     = 8
    ai_script   = $C1:A234
    sprite_id   = 7
```

## 11.3 Pointer semantics

SNES software uses many pointer forms:

- 16-bit pointer in current data bank;
- 24-bit long pointer;
- bank byte plus offset table;
- program-bank-relative function pointer;
- WRAM pointer;
- ROM pointer;
- indirect jump table;
- split low/high/bank arrays.

Workbench should allow custom pointer encodings and resolver rules.

## 11.4 Script engines

Many games implement their own bytecode for:

- events;
- dialogue;
- AI;
- cutscenes;
- map logic;
- battle actions;
- music;
- animation.

A world-class analysis environment should allow users to define custom instruction sets for these script languages.

A script definition might describe:

- opcode;
- operand layout;
- branch semantics;
- call semantics;
- text references;
- asset references;
- display formatting.

That makes Workbench extensible beyond CPU assembly.

---

# 12. Cross-References

## 12.1 Everything should be navigable

A strong reverse-engineering tool is a connected graph of references.

From:

```asm
LDA.w EnemyHpTable,X
```

the user should be able to ask:

- Where is `EnemyHpTable` defined?
- Which functions read it?
- Which instructions write it?
- Which indexes were observed?
- Which typed structure covers it?
- Which runtime values were loaded?
- Which game entities correspond to those entries?

From a function, the user should be able to ask:

- Who calls it?
- What does it call?
- Which interrupts reach it?
- Which memory locations does it read?
- Which memory locations does it write?
- Which hardware registers does it touch?
- Which DMA transfers does it initiate?
- Which assets does it reference?
- Which frames executed it?

From a graphics asset:

- Which ROM bytes store it?
- Which code decompresses it?
- Which function uploads it?
- Which tile-map entries use it?
- Which scenes display it?

## 12.2 Cross-reference categories

Cross-references should be typed:

- code call;
- code jump;
- code branch;
- data read;
- data write;
- address load;
- pointer reference;
- table membership;
- asset reference;
- hardware-register access;
- runtime-observed reference;
- user-defined relationship.

---

# 13. Live Debugger Integration

## 13.1 The disassembler and debugger should be one coherent system

The disassembly view should not be a disconnected offline utility.

It should be the primary code-navigation surface for live debugging.

Core debugger capabilities should include:

- pause;
- resume;
- instruction step;
- step over;
- step out;
- run to cursor;
- run until return;
- run until interrupt;
- software breakpoints;
- conditional breakpoints;
- execute watchpoints;
- read watchpoints;
- write watchpoints;
- read/write watchpoints;
- memory-region watches;
- event breakpoints;
- register-change breakpoints;
- tracepoints;
- logging actions;
- breakpoint hit counters;
- temporary breakpoints.

## 13.2 Hardware-aware breakpoint conditions

Workbench should support conditions that understand emulated hardware.

Examples:

```text
Break when CPU writes INIDISP during active display.
```

```text
Break when HDMA channel 3 transfers on scanline 104.
```

```text
Break when VRAM address $4000 is written.
```

```text
Break when PPU enters VBlank after UploadSprites executed.
```

```text
Break when DSP-1 receives command $0A.
```

```text
Break when NMI is asserted while DMA is active.
```

```text
Break when APU port 0 receives value $37.
```

```text
Break when a write changes the player HP field below 10.
```

## 13.3 Breakpoint expression language

A world-class debugger benefits from a small expression language.

Example:

```text
cpu.pc == $82:9410 &&
cpu.a == $0020 &&
ppu.scanline >= 224
```

or:

```text
write.address in WRAM[$7E:1200..$7E:123F] &&
write.old_value != write.new_value
```

The language should expose typed namespaces:

- `cpu`;
- `ppu`;
- `apu`;
- `dma`;
- `hdma`;
- `bus`;
- `cartridge`;
- `frame`;
- `input`;
- named symbols;
- typed user variables.

## 13.4 Live code display

The disassembly view should show:

- current PC;
- upcoming instructions;
- executed-path highlighting;
- breakpoint markers;
- coverage;
- branch outcomes;
- current mode;
- current bank;
- symbols;
- comments;
- effective addresses;
- live operand values;
- last execution count;
- recent callers.

---

# 14. Temporal Queries and Reverse Debugging

## 14.1 A normal debugger answers “what is happening now?”

A world-class emulator debugger should also answer:

- Who last wrote this byte?
- What changed this register?
- Which instruction uploaded this tile?
- When did this palette entry change?
- Which function created this OAM entry?
- Where did this DMA source pointer come from?
- Which code caused this sound effect?
- What sequence of events led to this corrupted frame?
- What was the machine state immediately before the failure?

These are causal and temporal questions.

## 14.2 Deterministic replay foundation

Clover already has a strong conceptual foundation for deterministic replay:

- deterministic-capable startup under explicit entropy configuration;
- deterministic controller movies;
- explicit frame boundaries;
- hardware-owned timing;
- reproducible save-RAM state;
- headless execution;
- exact observation points.

Workbench should build reverse debugging around that determinism.

This is a conceptual foundation, not a completed checkpoint system. Clover does
not yet expose complete serialization and restoration of all machine-owned
state. Reverse-debugging work must begin only after Stage 0 identifies and
tests the full state boundary.

## 14.3 Checkpoint-and-replay model

The first practical reverse-debugging implementation does not need to store every complete machine state.

It can use:

1. periodic checkpoints;
2. recorded controller inputs;
3. recorded external deterministic events;
4. deterministic replay;
5. targeted event tracing.

To move backward:

1. find the nearest checkpoint before the desired time;
2. restore it;
3. replay deterministically;
4. stop at the requested instruction, frame, scanline, or master clock.

## 14.4 Time coordinate system

Workbench should support several time coordinates:

- session-relative master clock;
- frame number;
- scanline;
- dot;
- CPU instruction count;
- APU instruction count;
- event index;
- wall-clock time only as diagnostic metadata.

The authoritative temporal coordinate should be emulated hardware time.

## 14.5 Last-writer queries

A last-writer query might return:

```text
WRAM $7E:1432 last changed at:

Frame
    1281

Raster
    scanline 227, dot 64

CPU
    PC $82:9A40
    instruction STA $1432,X

Function
    Update_Player_Position

Old value
    $7A

New value
    $7B

Call chain
    Main_Game_Loop
    Player_Update
    Update_Player_Position
```

## 14.6 Reverse stepping

Long-term capabilities may include:

- reverse instruction step;
- reverse step over;
- reverse step out;
- reverse continue;
- rewind to last write;
- rewind to last register access;
- rewind to previous interrupt;
- rewind to previous DMA transfer;
- rewind to previous frame change.

---

# 15. Graphics Provenance

## 15.1 Graphics analysis must connect pixels to code and data

For game-design use, a graphics inspector should not merely display VRAM.

It should explain how graphics arrived there and how the PPU used them.

A pixel-inspection workflow should expose:

- final pixel color;
- source layer;
- tile;
- tile-map entry;
- palette;
- priority;
- window result;
- color-math result;
- source VRAM bytes;
- source CGRAM entry;
- source OAM entry;
- DMA or CPU upload history;
- ROM or WRAM source;
- initiating instruction;
- initiating function.

## 15.2 Example pixel provenance

```text
Final pixel at (118, 92)

Winning source
    OBJ sprite 17

Tile
    index $02A4
    base $6000
    format 4bpp

Palette
    OBJ palette 5
    color index 11
    CGRAM entry $BB

Priority
    OBJ priority 2

OAM source
    main OAM entry 17
    generated from WRAM $7E:1800

Tile upload
    frame 1281
    scanline 225
    DMA channel 1
    source $C4:9200
    destination VRAM $6000

Initiating code
    function Upload_Player_Graphics
    call site $82:A104
```

## 15.3 VRAM timeline

Workbench should allow a user to select a VRAM region and inspect:

- current bytes;
- decoded tiles;
- write history;
- source transfers;
- code references;
- frames in which it changed;
- assets that occupied the region over time.

## 15.4 Tile-map and sprite tooling

Useful views include:

- tile viewer;
- tile-map viewer;
- palette viewer;
- OAM viewer;
- BG layer viewer;
- OBJ layer viewer;
- window mask viewer;
- priority viewer;
- color-math viewer;
- Mode 7 map viewer;
- DMA upload viewer;
- atlas reconstruction view.

---

# 16. Audio Provenance

## 16.1 Audio should receive equivalent treatment

A world-class Workbench should connect an audible result back to its origin.

For an active voice, it should show:

- DSP voice number;
- sample directory entry;
- BRR sample address;
- loop point;
- pitch;
- ADSR or gain mode;
- volume;
- envelope;
- echo state;
- key-on and key-off history;
- SPC700 routine that configured it;
- CPU-side command that requested it;
- gameplay event that triggered it.

## 16.2 Audio timeline

Useful views include:

- CPU/APU port event timeline;
- SPC700 instruction trace;
- DSP register timeline;
- voice activity lanes;
- BRR sample browser;
- sequence-command browser;
- echo-buffer inspector;
- timer inspector;
- waveform and spectrogram views;
- sound-effect trigger cross-references.

## 16.3 Sound-effect provenance example

```text
Audible event
    explosion sound effect

CPU trigger
    function Spawn_Explosion
    wrote command $2D to APUIO0

APU dispatch
    command handler $03:1840

Sequence
    sound-effect script $02:9200

DSP voices
    voices 4 and 5

BRR samples
    sample 27
    sample 31
```

---

# 17. Copied Code, Self-Modifying Code, and Dynamic Overlays

## 17.1 ROM-only assumptions are insufficient

SNES software may copy code into WRAM and execute it there.

The analysis environment should detect:

- writes into executable RAM;
- later execution of written bytes;
- source ROM region;
- copy routine;
- modifications after copy;
- multiple versions of code at the same RAM address;
- overlays loaded at different times.

## 17.2 Versioned runtime code identity

A runtime code block may need an identity such as:

```cpp
struct runtime_code_identity_t
{
    address_space_t space{};
    uint32_t address{};
    uint64_t content_hash{};
    uint64_t first_seen_master_clock{};
};
```

This distinguishes two different programs that occupied the same WRAM range at different times.

## 17.3 Self-modification warnings

Workbench should visibly mark when:

- an executed instruction’s bytes changed;
- a decoded region was overwritten;
- static disassembly no longer matches current memory;
- a breakpoint refers to an older code version.

---

# 18. Search and Query System

A world-class environment should provide structural searches, not just byte searches.

Examples:

```text
Find every function that writes INIDISP.
```

```text
Find every function that reads controller input.
```

```text
Find every branch never observed taken.
```

```text
Find all indirect-call targets observed during battle.
```

```text
Find every DMA transfer sourced from bank $C4.
```

```text
Find every function that writes the player HP field.
```

```text
Find all executed code with unknown M/X context.
```

```text
Find all ROM regions classified both as code and graphics.
```

```text
Find all palette writes during frame 1200.
```

```text
Find all functions not executed by the current test corpus.
```

This suggests a query engine over the analysis database.

---

# 19. Assembler and Patch Workflow

## 19.1 Analysis eventually leads to modification

For game design, users will eventually want to intentionally change code and data.

Workbench should ultimately support:

- assembly editing;
- assembling patches;
- free-space discovery;
- free-space allocation;
- code relocation;
- data relocation;
- pointer updates;
- bank-aware placement;
- ROM expansion where valid;
- checksum and header updates;
- patch import and export;
- hot reload;
- before/after comparison;
- deterministic regression after modification.

## 19.2 Do not begin with direct in-place editing

The initial disassembly listing should probably not be directly editable.

A better first workflow is to generate or manage a patch project:

```text
project/
    clover-project.json
    symbols.asm
    generated/
        bank_80.asm
        bank_81.asm
    patches/
        player_speed.asm
        battle_changes.asm
    assets/
        player_tiles.png
        enemy_table.json
    linker/
        memory_map.json
        free_space.json
```

The analysis database remains the record of discovered facts.

The patch project contains deliberate authored changes.

## 19.3 Free-space management

A free-space manager should distinguish:

- confirmed unused bytes;
- likely padding;
- unreachable code;
- unreferenced data;
- runtime-observed unused regions;
- user-reserved regions;
- unsafe or unknown regions.

It must not treat “not yet observed” as “safe to overwrite.”

## 19.4 Patch formats

Workbench may support:

- native project patches;
- BPS;
- IPS;
- IPS32;
- assembled binary diffs;
- source-level patch exports.

---

# 20. Asset Authoring and Replacement

The long-term game-design workflow should allow users to work with assets at a semantic level.

Examples:

- export tiles as PNG;
- edit and re-import palettes;
- edit tile maps;
- replace sprites;
- edit text;
- modify enemy tables;
- edit scripts;
- replace music data;
- adjust sound effects;
- inspect compressed assets;
- recompress replacements;
- patch pointers and sizes;
- validate runtime behavior.

This should be built on the same analysis graph.

An asset is not merely a file.

It has:

- source ROM region;
- encoding;
- decompressor;
- runtime destination;
- references;
- palette;
- scene usage;
- patch constraints.

---

# 21. Event and Hardware Timeline

A timeline view could become one of Workbench’s most powerful tools.

Possible lanes include:

- CPU instruction execution;
- interrupts;
- DMA;
- HDMA;
- PPU register writes;
- APU port communication;
- SPC700 execution;
- DSP key-on/key-off;
- input changes;
- frame boundaries;
- scanline boundaries;
- user markers;
- breakpoints;
- function entry and exit;
- asset uploads.

The user should be able to zoom from:

```text
entire 10-minute play session
```

down to:

```text
one scanline
```

or:

```text
a few master-clock events
```

Selecting an event should synchronize all views:

- disassembly;
- registers;
- memory;
- framebuffer;
- audio;
- PPU state;
- APU state;
- call stack;
- annotations.

---

# 22. Project and Session Concepts

## 22.1 Project

A Workbench project is the persistent understanding of a game.

It includes:

- canonical ROM;
- symbols;
- functions;
- types;
- assets;
- comments;
- patch definitions;
- analysis results;
- imported knowledge;
- user decisions.

## 22.2 Session

A session is one execution trajectory.

It includes:

- startup state;
- save RAM;
- controller input;
- timing profile;
- hardware profile;
- checkpoints;
- trace summaries;
- markers;
- coverage;
- dynamic observations.

Multiple sessions can contribute evidence to one project.

Examples:

```text
Power-on title-screen session
New-game session
Battle session
World-map session
Final-boss session
PAL session
DSP-1 test session
```

The project should combine observations while preserving session provenance.

---

# 23. UI Expectations for a World-Class Implementation

The quality of the analysis engine matters more than visual polish, but the UI must make the connected model usable.

Core views may include:

- disassembly;
- hex and typed memory;
- symbols;
- functions;
- cross-references;
- control-flow graph;
- call graph;
- registers;
- call stack;
- breakpoints;
- watch expressions;
- timeline;
- coverage;
- source/patch editor;
- PPU registers;
- tile viewer;
- tile-map viewer;
- OAM viewer;
- palette viewer;
- framebuffer inspector;
- pixel provenance;
- VRAM history;
- DMA/HDMA inspector;
- APU registers;
- DSP voice inspector;
- BRR browser;
- audio timeline;
- script viewer;
- asset browser;
- project evidence view;
- conflict view.

All views should support linked navigation.

Selecting an address in one view should update the relevant context in others.

## 23.1 Disassembly, structured facts, and source editing

The disassembly surface may provide editor-like navigation, selection, search,
copying, and syntax coloring, but it is not an editable text file. A listing is
a rendering of bytes plus analysis facts. Labels, comments, types, code/data
classifications, operand representations, and function boundaries should be
changed through structured project operations and then rendered back into the
listing.

This prevents a text edit from ambiguously meaning any of the following:

- rename an analysis symbol;
- change presentation syntax;
- reclassify bytes;
- patch the canonical media;
- author assembly that should be assembled elsewhere.

A general source editor becomes appropriate with the Stage 7 patch and
game-authoring workflow. It should support assembly syntax, source navigation,
diagnostics, source-level breakpoints, project builds, and mappings among source
locations, canonical byte ranges, runtime addresses, and observations.

Workbench should embed a mature editing component rather than implement text
selection, undo, Unicode behavior, multi-cursor editing, and document rendering
itself. Clover's assembly intelligence should remain editor-neutral so that the
same services can support both an embedded editor and external editors through
a language-server-style integration.

Generated source and user-authored source must remain distinct in the
interactive UI as well as in exported projects. Generated text is disposable
and reproducible from the analysis database. Authored files are user property
and must never be silently regenerated or overwritten. Promoting generated
content into an authored overlay must be an explicit operation.

---

# 24. Multi-System Design

Clover currently has an SNES core, but Workbench should be architected so that
additional systems can contribute system-specific analysis capabilities. The
SNES implementation is the first target of this architecture; it must not
become the implicit shape of every future target.

The generic layer can define concepts such as:

- machines;
- execution domains;
- address spaces;
- decoded instructions;
- code blocks;
- symbols;
- memory accesses;
- execution traces;
- breakpoints;
- frames;
- audio;
- assets;
- checkpoints.

A machine must not be assumed to contain one CPU. The SNES alone can expose
several execution domains:

```text
snes.main-cpu       65C816
snes.audio-cpu      SPC700
snes.super-fx       GSU, when present
```

An execution domain identifies an independently executing processor or device.
An address space identifies where a value is located. They are related but not
interchangeable. Instruction stepping, execution breakpoints, and register
views identify an execution domain; memory operations identify an address
space.

Addresses must therefore carry address-space identity rather than travel
through Workbench as bare integers:

```cpp
struct debug_address_t
{
    address_space_id_t space{};
    uint64_t value{};
};
```

Examples include:

```text
snes.cpu-bus:$c0/8123
snes.apu-ram:$0200
snes.vram:$4a00
media.canonical:$018123
```

A system-specific target module or capability can define:

- memory map;
- hardware symbols;
- vector rules;
- graphics formats;
- audio formats;
- device inspectors;
- cartridge mapping;
- script conventions.

Processor architecture modules should separately define:

- instruction decoding;
- operand and register descriptions;
- control-flow classification;
- architectural context propagation;
- formatting profiles.

This permits a 65C816, SPC700, GSU, 68000, Z80, or other processor module to be
reused without embedding one console's memory map or hardware policy in its
decoder.

For example:

```cpp
class analysis_target_t
{
public:
    virtual ~analysis_target_t() = default;

    [[nodiscard]] virtual system_id_t system_id() const noexcept = 0;
    [[nodiscard]] virtual std::span<const execution_domain_descriptor_t>
        execution_domains() const noexcept = 0;
    [[nodiscard]] virtual std::span<const address_space_descriptor_t>
        address_spaces() const noexcept = 0;
    [[nodiscard]] virtual debug_capabilities_t capabilities() const noexcept = 0;
};
```

Targets should advertise optional capabilities rather than implement one
ever-growing interface. Generic Workbench views consume machine, execution
domain, address-space, register, memory, and event descriptors. A target module
may additionally register specialized views such as the SNES tile viewer, PPU
compositor inspector, DSP voice inspector, or cartridge-coprocessor panels.

Stable target-local identifiers should be used instead of global enums that
must grow whenever a system or device is added. Project data and derived facts
must record the system, target-module version, processor-analyzer versions, and
the stable execution-domain and address-space identities they reference.

The first target modules may be registered at compile time. Clover should defer
a stable binary plugin ABI until multiple real systems have validated the
extension boundary. Architectural modularity is required now; separately
distributed binary modules are not.

Hot-path execution should still avoid unnecessary virtual dispatch.

The system-neutral boundaries should exist at appropriate tooling boundaries, not per instruction inside the emulator core.

---

# 25. Testing the Disassembler and Analyzer

## 25.1 Decoder tests

The decoder should have exhaustive tests for:

- every opcode;
- every addressing mode;
- every immediate-width rule;
- all `E/M/X` combinations;
- branch target calculation;
- bank wrapping;
- instruction length;
- malformed or incomplete buffers;
- formatting profiles.

## 25.2 Differential tests

Where useful, instruction text can be compared against trusted tools, but Clover should retain its own structured truth.

The important closure criteria are:

- correct opcode identity;
- correct operand width;
- correct target calculation;
- correct context handling;
- correct address mapping.

## 25.3 Static-analysis tests

Synthetic test ROMs should cover:

- direct branches;
- calls;
- returns;
- interrupt vectors;
- mixed code and data;
- jump tables;
- indirect calls;
- conflicting decode contexts;
- copied code;
- self-modifying code;
- user overrides;
- runtime-assisted target discovery.

## 25.4 Deterministic integration tests

A checked-in synthetic ROM can be executed under Clover and used to verify:

- runtime contexts;
- execution coverage;
- call graph;
- memory access summaries;
- last-writer queries;
- checkpoint replay;
- reverse stepping;
- graphics provenance;
- audio trigger provenance.

## 25.5 Target conformance and non-interference tests

Every system target should pass a reusable conformance suite covering:

- declared execution-domain and address-space identity;
- side-effect-free inspection;
- address-space bounds and unsupported operations;
- clean handling of absent optional capabilities;
- domain-specific instruction stepping;
- pause and resume determinism;
- breakpoint and watchpoint observation boundaries;
- exact checkpoint capture and restoration;
- stable canonical-media and project identity.

Deterministic scenarios must also run in three configurations:

```text
No observer attached
Observer attached with all event masks disabled
Observer attached with selected events enabled
```

All three must produce identical machine state, master-clock position, video,
audio, persistent memory, and replay results. Observation may change host
performance when enabled; it must never change emulated behavior.

---

# 26. Performance Requirements

A world-class tool must remain usable during real execution.

## 26.1 Disabled diagnostics must remain cheap

When analysis features are disabled:

- no string formatting;
- no disk I/O;
- no unbounded event allocation;
- no hidden per-instruction virtual callbacks;
- no global tracing overhead;
- no accidental host-time pacing changes.

## 26.2 Tiered instrumentation

Workbench should support instrumentation tiers.

### Tier 0: no analysis

Normal emulator execution.

### Tier 1: coverage summary

Record executed addresses and basic context.

### Tier 2: event summaries

Record calls, branches, memory ranges, and hardware accesses.

### Tier 3: detailed trace

Record instruction and event detail.

### Tier 4: provenance mode

Record expensive graphics, audio, and temporal provenance.

### Tier 5: investigation mode

Record highly detailed bounded or streamed traces for a narrow period.

This prevents the world-class tooling goal from making ordinary emulation slow.

## 26.3 Player budgets and dependency boundaries

Performance and product separation should be measurable rather than
aspirational. Regression automation should track:

- Tier 0 execution performance;
- allocations during ordinary Player execution;
- Player startup time;
- Player binary size;
- event-mask disabled overhead;
- accidental linkage of Workbench-only dependencies.

Reasonable thresholds may evolve by platform, but a regression beyond an
established budget must be reviewed explicitly. The Player must not initialize
analysis databases, target-specific inspector UI, trace storage, graph engines,
or source tooling.

---

# 27. Source Reconstruction and Reassemblable Disassembly

## 27.1 Aspirational end state

The highest-level static-analysis goal is a reassemblable disassembly.

That means generating a reconstructed assembly-source project that can rebuild
the canonical ROM byte-for-byte.

The generated project is equivalent source, not recovered historical source.
Workbench cannot infer the original:

- filenames or number of translation units;
- file boundaries;
- local or global naming choices;
- comments;
- macros;
- include structure;
- assembler and linker scripts;
- conditional-build organization;
- formatting;
- author intent.

Workbench should choose deterministic replacements for those missing details
and clearly identify which content was generated, imported, inferred, or
authored by the user.

This document’s source-reconstruction scope is assembly. High-level-language
decompilation, including C generation, is a separate future product direction
and is not part of the Workbench blueprint defined here.

The output should:

- preserve bank layout;
- preserve addresses;
- preserve vectors;
- preserve unknown bytes;
- label code and data;
- express pointers;
- represent structures;
- include binary regions when not understood;
- survive incremental improvement.

## 27.2 Unknown data must remain representable

The tool should never invent semantic meaning simply to make the output attractive.

Unknown regions can be emitted as:

```asm
.incbin "unknown/bank_c4_9200_97ff.bin"
```

or:

```asm
.byte $12, $34, $56, $78
```

## 27.3 Confidence classifications

Useful classifications include:

```text
Confirmed code
Observed code
Probable code
Ambiguous code
Confirmed structured data
Probable pointer table
Known asset
Unknown bytes
Conflicting interpretation
```

## 27.4 Byte-identical rebuild

A successful reconstruction pipeline should verify:

1. assemble generated source;
2. produce canonical ROM bytes;
3. hash the output;
4. compare byte-for-byte with the original;
5. report every mismatch;
6. preserve exact unchanged binary regions.

This is difficult to automate universally, but Workbench should make the process progressively attainable.

---

# 28. Recommended Implementation Sequence

The correct implementation strategy is incremental.

The final vision is large, but the early stages are independently valuable.

## Stage 0: Inspection, observation, and state substrate

Implement:

- system-neutral machine, execution-domain, and address-space descriptors;
- stable target-local identifiers and optional debug capabilities;
- immutable canonical-media byte views;
- side-effect-free live-memory inspection;
- a public cartridge-aware address translator;
- explicit instruction-boundary results;
- a masked, typed, bounded observation sink;
- debugger session-control capability boundaries;
- shared media identity outside the SDL platform library;
- a complete machine-state ownership and serialization audit;
- analyzer, project-schema, and derived-fact versioning rules.

The state audit does not need to deliver reverse debugging yet. It must identify
every value that a future checkpoint must preserve, including pending bus
writes, timing counters, device protocol state, startup configuration,
cartridge coprocessors, and buffered APU/DSP state.

Deliverable:

A tested analysis boundary that can inspect and observe the machine without
changing it, and a documented path to exact state restoration.

### Stage 0 implementation status

**Status: complete.**

This section retains the implementation chronology because it records why the
checkpoint and observation boundaries have their present shape. Statements
about work remaining "at that point" describe the named historical slice, not
the current repository.

The first Stage 0 slice was established on July 24, 2026. It provides:

- system-neutral execution-domain and address-space descriptors;
- an optional frontend debug-target capability;
- immutable canonical cartridge media after copier-header removal;
- shared canonical-media SHA-256 identity outside the SDL ROM library;
- pure LoROM, HiROM, cartridge-RAM, enhancement-device, and Super FX address
  classification in the cartridge;
- CPU-bus to canonical-media translation through the SNES debug target;
- side-effect-free inspection of safe CPU-bus storage, WRAM, APU RAM, and
  canonical media;
- explicit refusal to inspect volatile MMIO through the safe CPU-bus path;
- explicit CPU boundary results for instruction retirement, reset completion,
  interrupt entry, WAI waiting, and STP stopped states;
- optional domain-aware frontend execution control with coherent main-CPU
  stepping across intervening DMA slots;
- explicit `unsupported` results for declared domains that do not yet provide
  stepping, including the SPC700;
- a masked, typed SNES observation sink with fixed caller-provided storage and
  no core allocation or virtual callback;
- an optional system-neutral observation-control capability with explicit
  available/current masks, bounded draining, clearing, and dropped-event
  accounting;
- main-CPU boundary observations carrying domain, machine clock, frame index,
  boundary kind, and before/after CPU-bus addresses;
- lazy frontend event-buffer allocation only when observation is enabled;
- an optional debugger-session capability with explicit not-running, running,
  and paused states plus pause/resume transition results;
- frontend-owned pause policy that suppresses host frame advancement without
  introducing paused state into the hardware core;
- domain stepping restricted to paused sessions, with explicit `not_paused`
  results and pause preservation across reset and media replacement;
- focused tests for mirror translation, pre-power static inspection, open-bus
  non-interference, descriptors, media identity, unavailable addresses,
  boundary equivalence, WAI/STP behavior, execution-control lifecycle,
  observation ordering, mask rejection, buffer overflow, drain behavior, and
  no-observer/disabled/enabled equivalence, session lifecycle, paused frame
  suppression, resume, reset, and media replacement.

At that point, this first slice intentionally did not claim Stage 0 completion.
The remaining substrate work at that time was:

- SPC700 and optional cartridge-processor instruction stepping;
- additional typed event families for memory, DMA/HDMA, interrupts, PPU, APU,
  and cartridge devices;
- migration of useful legacy bring-up traces onto the observation substrate;
- exact capture and restoration;
- analyzer, project-schema, and derived-fact versioning rules.

The SNES machine-state ownership and serialization audit is complete as a
Stage 0 design deliverable in `docs/SNES_MACHINE_STATE_AUDIT.md`. It classifies
causal, identity, continuity, derived, wiring, diagnostic, and host-policy
state; defines a transactional restore order and equivalence-test matrix; and
identifies three blockers that exact capture must resolve first: instance
ownership for DSP-1 reference state, instance ownership for DSP-4 byte/address
latches, and explicit S-DSP audio-output cursor/overflow restoration.

The next checkpoint-preparation slice removed both process-global coprocessor
blockers. DSP-1 projection/reference state is now owned by each `dsp1_t`, DSP-4
passes its byte latch directly through the wrapper while keeping persistent
protocol state per instance, and regression coverage interleaves independent
devices. Explicit S-DSP audio-output cursor/overflow restoration remains before
transparent checkpoint capture.

The S-DSP output-continuity blocker is now resolved as well. Its pointer-free
descriptor preserves whether primary output is enabled, primary and emergency
sample positions, overflow state, and the fixed emergency buffer; the APU pairs
that descriptor with its primary audio array. Restore validates all counts,
rebases pointers onto current storage, and rejects malformed state without
changing the visible audio frame. Coverage exercises disabled output,
non-overflow continuation, overflow restoration, APU sample preservation, and
invalid-state rejection.

The first broader causal-state slice now provides schema-versioned in-memory
snapshots for the scheduler, S-CPU, DMA/HDMA, and interrupt controller. Capture
is field-wise and excludes pointers, observation sinks, and diagnostic-only
accounting. Restore validates enums, channel indexes, IRQ source consistency,
SNES video timing, and raster bounds before mutating live state. Dedicated
tests fill hidden timing and protocol fields, require exact round trips, and
verify rejected state leaves each subsystem unchanged. Bus, cartridge, PPU,
and causal APU snapshots were identified as the remaining ownership layers
before these pieces could form a transactional console checkpoint.

The CPU-bus causal snapshot is now implemented too. It owns the full 128 KiB
WRAM image, startup-entropy configuration, open-bus byte, fixed pending
CPU/PPU/APU write queues, and the APU-progress offset within the active CPU
interval. Restore validates queue counts and entropy mode before mutation,
leaves all connected-device pointers intact, retains trace enablement, and
clears trace contents so observations from the abandoned timeline cannot leak
past a checkpoint. Tests restore delayed writes and require them to reach the
same PPU, APU, CPU, and DMA instances at their original relative clocks.

The base-cartridge envelope now captures bootstrap memory, SRAM bytes, and SRAM
dirty continuity. Restore validates the loaded state, header, mapper, ROM size,
RAM size, and persistence topology before mutation. Canonical ROM SHA-256 stays
in the outer checkpoint identity envelope shared with the ROM library. CX4,
DSP, and Super FX cartridges return an explicit unsupported-hardware result
until each enhancement device has a complete causal snapshot; a checkpoint can
therefore never appear successful while silently omitting coprocessor state.

The PPU causal snapshot now retains its register file, VRAM, OAM, CGRAM,
memory-data-bus latches, address/read/write phases, video configuration, raster
counter, entropy configuration, decoded render state, active background and
object pipelines, compositor samples, four partial/presentation framebuffers,
and geometry continuity. Validation covers timing, raster bounds, enum and
mask ranges, buffer counts, pipeline indexes, VRAM policy, palette values, and
framebuffer geometry before mutation. Presentation controls, completed-frame
queues, and diagnostic traces remain outside the payload; restore keeps policy
but clears queued observations from the abandoned timeline.

The causal APU snapshot now retains the SPC700 clock credit and registers,
APURAM, IPL and I/O control, CPU/APU ports, all timer stages, DSP clock phase,
the suspended-instruction access journal, the exact S-DSP state blob, and
in-progress audio samples/output continuity. Capture and restore explicitly
refuse an active call-scoped CPU/APU I/O window, so no bus pointer enters the
payload. Restore validates wait states, timers, journal indexes, the DSP blob
through an isolated DSP instance, and audio-output bounds before mutation; DSP
RAM and output pointers are then rebound to the destination APU. Deterministic
continuation tests compare two independently restored APUs.

The subsystem states are now composed into a versioned transactional console
state. Capture supports powered bootstrap, base-cartridge, and enhancement-
cartridge machines while rejecting active CPU/APU call windows. CX4, DSP-1
through DSP-4, and Super FX own versioned causal-state blobs encoded without
copying C++ object representations. Restore first validates requested hardware,
resolved region, media topology, subsystem payloads, CPU/PPU/APU/scheduler
clock agreement, frame indexes, revision values, raster state, entropy
configuration, and interrupt clock bounds in an independently allocated and
wired console. Only after that candidate succeeds are non-failing field
restores committed to the live address-stable console, preserving observation
and presentation policy while clearing abandoned diagnostics. Tests cover PAL
base media, bootstrap memory, WRAM, SRAM, deterministic continuation in a
second console, malformed subsystems, cross-clock mismatches, different media
topology, and all implemented enhancement families.

The core causal transaction now has a portable frontend envelope and an
optional Workbench-facing `checkpoint_control_t` capability.
`frontend/SnesCheckpoint` writes a fixed versioned little-endian header and
explicit field-wise payload, including all subsystem schema versions, bounded
lengths, CRC-32 payload integrity, hardware topology, and canonical media
length/SHA-256. Restore validates those fields and fully decodes temporary state
before calling the core transaction, so a wrong ROM or malformed checkpoint
cannot reach live mutation. Capability capture and restore require a paused
debug session, and restore clears observations from the abandoned timeline.

The deterministic replay-equivalence gate is also complete. Its scenario
matrix restores the portable envelope and replays scripted input through NTSC
and PAL mid-frame rendering, SRAM, reset, WAI/IRQ entry, general DMA, HDMA,
APU-port synchronization, and all implemented enhancement devices. It compares
the full hardware-step transcript, final causal state, re-encoded checkpoint,
framebuffer, audio, and device I/O byte-for-byte. Reverse-execution work can
therefore treat checkpoint restore as a verified primitive rather than adding
another state format.

The Stage 0 performance boundary is now enforced for the older bring-up
instrumentation too. APU instruction/I/O history, CPU-bus register/write
history, and PPU OAM/CGRAM history are disabled during ordinary Player
execution and require explicit diagnostic-host opt-in. Saturated fixed buffers
therefore cannot shift thousands of records in the emulation loop while the
focused bring-up tools remain available.

Together, the audited state ownership, exact transactional checkpoint,
portable frontend envelope, replay-equivalence gate, side-effect-free
inspection, explicit stepping, and opt-in observation boundary complete the
Stage 0 deliverable. Later stages may add observation families and execution
domains for concrete features without reopening Stage 0.

## Stage 1: Correct decoder and basic disassembly

**Status: complete.**

Implement:

- structured 65C816 decoder;
- explicit `E/M/X` context;
- formatter separation;
- shared architectural opcode metadata;
- CPU address to canonical-byte mapping through the Stage 0 translator;
- built-in hardware symbols;
- command-line decoder and machine-readable output;
- read-only static listing;
- exhaustive decoder tests.

Deliverable:

A reliable decoder and read-only listing that never hide context dependence and
can be tested independently of the desktop UI.

### Stage 1 implementation status

Stage 1 was completed on July 27, 2026. It provides:

- a headless `clover_analysis` library separate from the hardware executor and
  SDL Player;
- one complete 256-entry W65C816 architectural opcode table covering
  instruction identity, addressing mode, operand-width rule, control-flow
  category, and architectural status-flag effects;
- structured decoding with explicit `E`, `M`, and `X` state plus optional
  direct-page and data-bank context;
- explicit `ambiguous_context` results with minimum and maximum encoded sizes
  when immediate width cannot be established, plus rejection of contradictory
  emulation-mode width flags;
- separate text and JSON Lines formatting;
- statically resolved branch, jump, and call targets where the encoding makes
  them knowable;
- built-in SNES PPU, APU-port, WRAM-port, CPU-I/O, controller, and DMA register
  symbols, applied only when the effective bank is known to reach hardware
  space;
- a side-effect-free byte source backed by the Stage 0 CPU-bus-to-canonical-
  media translator, with safe live-storage inspection as its fallback;
- a read-only linear listing that refuses to advance through an ambiguous
  instruction boundary;
- the `clover_workbench_disasm` command-line tool with human-readable and
  machine-readable output; and
- an independent golden opcode matrix, kept outside production metadata, that
  verifies all 256 mnemonic, addressing-mode, width-rule, control-flow, and
  encoded-size results under emulation, native 8-bit, and native 16-bit
  contexts;
- a real-executor drift gate that runs every opcode through the S-CPU,
  rejects placeholder dispatch, and verifies linear instructions consume the
  golden operand length; and
- focused ambiguity, contradiction, wrapping, target, formatting,
  hardware-symbol, JSON, listing, and Stage 0 translation tests.

For example:

```bash
./build/clover_workbench_disasm game.sfc \
  --address 00:8000 --count 64 --e 1 --m 1 --x 1 --db 00

./build/clover_workbench_disasm game.sfc \
  --address C0:8000 --count 64 --e 0 --m 0 --x 0 --jsonl
```

The command requires an explicit CPU-bus start address. Width context defaults
to unknown, so omitting `--e`, `--m`, and `--x` cannot silently impose an
eight- or sixteen-bit interpretation.

## Stage 2: First persistent Workbench slice

**Status: complete.**

Implemented:

- project creation keyed by canonical media identity;
- transactional schema migrations;
- labels;
- comments;
- bookmarks;
- explicit code and data classifications;
- imported hardware symbols;
- separation of user facts from regenerable derived facts;
- basic navigation history;
- a minimal desktop Workbench surface.

Deliverable:

A small but durable analysis project in which user knowledge survives decoder,
analyzer, and UI changes.

The Stage 2 project service stores one SQLite database at
`<project-root>/<system>/<canonical-sha256>/project.sqlite3`. Schema upgrades
run in immediate transactions and roll back as a unit. User facts, imported
facts, and regenerable derived facts have explicit layers; starting a new
analysis generation invalidates only the derived layer. A separate
`clover_workbench` SDL application presents the Stage 1 listing with persistent
annotations, classifications, symbols, and back/forward navigation. The Player
target remains independent of all Workbench project and analysis libraries.

`clover_workbench_project_test` verifies canonical copier-header identity,
per-media isolation, successful migration, failed-migration rollback, durable
user facts, derived-fact invalidation, hardware symbol import, and branched
navigation history.

## Stage 3: Live debugger integration

**Status: complete.**

Implemented:

- pause and resume;
- instruction step;
- step over;
- step out;
- run to cursor;
- breakpoints;
- watchpoints;
- live registers;
- live memory;
- current instruction highlighting;
- runtime mode-correct disassembly;
- call and return observation;
- hardware-register breakpoints.

Deliverable:

A strong emulator debugger with a first-class disassembly surface.

Stage 3 extends the system-neutral debug-target contract with opt-in CPU memory
access observations and live processor-register snapshots. The Workbench-owned
SNES debugger pumps exact whole-machine instruction boundaries while keeping
run-control policy outside the hardware core. It provides persistent execution
breakpoints and CPU-bus range watchpoints, temporary run operations, runtime
65C816 decode context, bounded call/return observations, and explicit stop
reasons. Read/write watchpoints over `$2100-$43ff` provide hardware-register
breakpoints without reading volatile MMIO from the UI.

The SDL Workbench now powers the target on, follows the live PC, highlights the
current instruction, displays registers and side-effect-free memory, and
provides pause/continue, step, step-over, step-out, run-to-cursor, breakpoint,
and watchpoint controls. Player does not enable the new probes and retains its
ordinary frame-running path.

`clover_workbench_debugger_test` executes a synthetic cartridge through
instruction stepping, `$2100` write observation, step-over/out, run-to-cursor,
execute and read watchpoints, live memory, runtime context, and JSR/RTS
observation. `clover_analysis_boundary_test` additionally guards register
metadata, probe opt-in, bounded observation delivery, and disabled-probe
non-interference.

## Stage 4: Hybrid analyzer

**Status: complete.**

Goal:

Turn the Stage 1 decoder, Stage 2 durable project, and Stage 3 runtime evidence
into a deterministic program model. Stage 4 must improve what Workbench knows
without converting an inference into a user fact or hiding ambiguity.

### Stage 4.1: Analysis model and ownership

Implement a headless analyzer service in the analysis/Workbench service layer,
never in the emulated hardware core or SDL shell. Its inputs are:

- canonical media identity and cartridge mapping;
- an immutable byte source and explicit 65C816 decode context;
- user code/data classifications and user-selected entry points;
- imported symbols;
- optional runtime observations from identified sessions; and
- analyzer, decoder, schema, and analysis-generation versions.

Its output is a candidate analysis generation containing decoded instructions,
basic blocks, functions, edges, cross-references, coverage summaries,
unresolved targets, conflicts, and evidence links. Analysis must be
deterministic for identical inputs. A failed or cancelled run must not publish
a partial generation.

User and imported facts remain authoritative inputs. Analyzer output is
regenerable derived data. The analyzer may report that user facts conflict with
bytes or runtime evidence, but it must not rewrite those facts.

### Stage 4.2: Deterministic static traversal

Seed a stable worklist from:

- reset and interrupt vectors that map to inspectable code;
- explicit user entry points and code classifications;
- statically known direct branch, jump, and call targets; and
- runtime-executed addresses supplied as evidence.

Decode recursively with an explicit `E/M/X/D/DB` context. Split basic blocks at
entry points, control transfers, calls, returns, stops, context conflicts,
user-data boundaries, and already-owned byte ranges. Record fallthrough and
known targets without sweeping through ambiguous widths or classified data.

Traversal order and identifiers must be stable. Reaching the same byte under
incompatible width contexts, overlapping an existing instruction, entering
user-classified data, reading an unavailable byte, or encountering an
unresolved indirect transfer produces an explicit conflict or unresolved fact;
it must never be silently guessed through.

### Stage 4.3: Graphs, functions, and cross-references

Represent:

- basic-block ranges and their entry contexts;
- typed control-flow edges for fallthrough, conditional branch, jump, call,
  return, interrupt entry, and unresolved transfer;
- provisional functions rooted at vectors, explicit user entries, and direct
  call targets;
- call-graph edges without assuming that every shared block belongs to exactly
  one function; and
- code, data, pointer, hardware-register, and runtime-observed
  cross-references with source address, target identity, and evidence.

Function discovery must tolerate tail calls, shared epilogues, non-returning
routines, multiple entry points, and unresolved indirect calls. Those cases
remain represented rather than forced into a conventional compiler model.

### Stage 4.4: Runtime evidence and coverage

Extend observation capture only as required by analyzer consumers. Aggregate
instruction and edge coverage without retaining an unbounded trace. Runtime
evidence may:

- confirm that an instruction or edge executed;
- provide an observed decode context;
- add an observed target for an indirect jump or call;
- identify code executing from writable memory; and
- increase confidence while retaining its session provenance.

Runtime execution is evidence, not permanent truth about every session.
Coverage and observed targets must name their source session and media
identity. Player keeps all analyzer probes disabled, and Workbench uses bounded
buffers with dropped-event accounting.

### Stage 4.5: Confidence, provenance, and conflicts

Every derived fact records:

- its analysis generation and producer version;
- the evidence kind and source identity that support it;
- a confidence/status classification such as confirmed, strongly inferred,
  weakly inferred, unresolved, or conflicting; and
- any competing interpretation that prevents promotion.

Confidence follows explicit, tested rules. Multiple independent evidence
sources may strengthen a fact; absence of runtime coverage must not weaken
otherwise valid static evidence. User confirmation creates or updates a user
fact rather than mutating the derived record in place.

### Stage 4.6: Persistence and invalidation

Add a transactional project-schema migration beyond the current Stage 3
schema. Persist published analysis generations and their derived instructions,
blocks, functions, edges, cross-references, coverage, evidence, and conflicts.
Foreign keys and uniqueness constraints must prevent facts from crossing media
identities or generations.

Starting a generation leaves the previously published generation readable.
Publishing atomically switches the current generation only after validation.
Decoder, analyzer, mapping, or relevant input-fact changes invalidate derived
results without deleting labels, comments, bookmarks, breakpoints,
watchpoints, or other user-authored facts.

### Stage 4.7: First analysis UI

The first UI slice should add:

- analyze/reanalyze status and generation identity;
- function and basic-block navigation;
- incoming and outgoing cross-references;
- coverage markers distinct from current-PC and breakpoint markers;
- unresolved-target and conflict views; and
- provenance/confidence inspection for the selected derived fact.

Graph rendering may begin as a simple navigable block/edge view. Layout is UI
state, not analyzer truth, and must not be required for headless tests.

### Stage 4 implementation slices

1. **Persistent model:** schema migration, generation publication, evidence,
   conflicts, and invalidation tests.
2. **Static core:** deterministic seeds, recursive traversal, block splitting,
   direct edges, provisional functions, and cross-references.
3. **Dynamic merge:** bounded coverage and observed indirect targets with
   session provenance and writable-code identity.
4. **Workbench surface:** analysis commands, function/xref navigation,
   coverage, conflicts, provenance, and a first graph view.

Each slice must remain independently testable and leave Player free of
Workbench and analyzer dependencies.

### Stage 4 acceptance gates

Stage 4 is complete only when:

- synthetic cartridges cover branches, calls, returns, vectors, loops,
  context changes, shared blocks, tail calls, indirect transfers, overlaps,
  ambiguous widths, and code/data conflicts;
- repeated analysis of identical inputs produces byte-for-byte equivalent
  derived facts and stable identifiers;
- a failed or cancelled analysis preserves the prior published generation;
- user facts survive reanalysis and derived-fact invalidation;
- runtime coverage and indirect targets merge without losing session
  provenance or fabricating unobserved targets;
- writable-memory execution is distinguished from canonical ROM identity;
- headless tests exercise the analyzer without SDL;
- Player has no Workbench/analyzer link dependency and disabled observation
  remains behaviorally equivalent; and
- a real-ROM smoke test can navigate from an executed instruction to its
  function, callers/callees, cross-references, evidence, and conflicts.

Non-goals for Stage 4 are decompilation, rich typed-data inference, asset
viewers, reverse execution, and complete resolution of arbitrary indirect
control flow. Those remain later-stage work.

### Stage 4 implementation status

Stage 4 was completed on July 27, 2026. It provides:

- a system-neutral `ProgramModel` owned by `clover_analysis`;
- a deterministic SNES hybrid analyzer with vector, user-code, and runtime
  seeds; conservative `E/M/X/D/DB` propagation; recursive traversal; stable
  instruction and block identities; provisional functions; many-to-many
  function/block membership; typed graph edges; code, data, hardware, and
  runtime cross-references; explicit unresolved and conflicting facts; and
  deterministic input fingerprints;
- bounded, aggregated debugger runtime edges with decode contexts, session
  identity, hit counts, and dropped-record accounting;
- confirmed runtime indirect targets and coverage without retaining an
  unbounded trace;
- explicit canonical-media versus writable-memory code identity;
- transactional schema-v4 analysis generations containing instructions,
  blocks, function ownership, functions, edges, cross-references, evidence,
  conflicts, and coverage;
- atomic generation publication: prior generations remain readable until a
  candidate validates and commits, while failed publication rolls back without
  changing the current generation or user facts;
- a headless `clover_workbench_analyze` command that can combine vector-led
  static analysis with a bounded live execution sample and optionally publish
  the result;
- Workbench analysis controls, coverage markers, function and conflict
  navigation, incoming/outgoing cross-reference navigation, confidence and
  provenance inspection, writable-code identity, and a compact navigable block
  edge view; and
- Player separation: the Player neither links the Workbench project service
  nor enables the analyzer’s observations.

`clover_hybrid_analyzer_test` covers deterministic results, width-context
changes and ambiguity, incompatible contexts, overlaps, branches, calls,
returns, indirect targets, shared blocks, tail jumps, user-data conflicts,
runtime provenance, and writable code. `clover_workbench_project_test` covers
schema migration, full model round trips, historical generation reads, atomic
publication rollback, and user-fact preservation.
`clover_workbench_analysis_integration_test` executes a cartridge and follows
the resulting chain from coverage to instruction, block, function, graph,
evidence, conflicts, and durable reload. A 100,000-instruction Final Fantasy
III smoke run produced 127 covered addresses and 130 bounded runtime edges with
zero dropped records, then published and reloaded the hybrid model.

Deliverable:

A deterministic, persistent, evidence-backed program-analysis environment
rather than a listing.

## Stage 5: Typed data and assets

**Status: in progress; typed-data and palette foundations complete.**

### Stage 5.1: Typed-data foundation

Define system-neutral, stable representations for unsigned and signed
integers, arrays, structures, pointers, enums, bitfields, and fixed-size
strings. Types own byte size, byte order, element relationships, structure
members, named values, pointer address spaces, and string encodings. Typed
objects bind a definition to an explicit address-space range without erasing
the underlying bytes or analyzer evidence.

Persist user and imported types, members, named values, and object bindings in
the per-media project. Schema migration and updates are transactional.
Definitions reject missing references, inconsistent array sizes, out-of-bounds
or overlapping structure members, duplicate identities, address overflow, and
overlapping objects. A typed object creates a user data classification so
hybrid analysis reports any code/data disagreement explicitly.

The headless decoder formats scalar, aggregate, enum, bitfield, string, and
pointer values from a side-effect-free byte reader. It bounds recursion and
display expansion, reports unavailable bytes and pointer targets, and retains
pointer targets as navigable addresses.

The first Workbench surface marks typed ranges, shows type and decoded value in
the inspector, provides byte and fixed-ASCII quick bindings, navigates between
objects, and follows decoded pointers.

**Implementation status:** complete on July 30, 2026. Schema v5 stores the
typed model and four imported primitive definitions. `clover_typed_data_test`
covers all first-slice type kinds, aggregate decoding, validation, unavailable
bytes, and pointer targets. `clover_workbench_project_test` covers migration,
full definition/object round trips, reanalysis survival, and atomic rejection
of overlapping bindings.

### Stage 5.2: Palette assets and inspection

Add a system-neutral palette asset whose stable identity binds a name, format,
color count, and address-space source. The first system format is SNES
little-endian BGR555. Decoding retains the raw 15-bit value, individual
five-bit channels, expanded eight-bit RGB, and explicit conflicts for invalid,
misaligned, unavailable, truncated, or out-of-range sources.

Expose live SNES CGRAM as a 512-byte read-only debug address space. Inspection
reads the PPU-owned array directly without touching `$213B` latches, changing
PPU state, or enabling legacy write traces. ROM, WRAM, canonical-media, and
CGRAM palettes all consume the same injected side-effect-free byte reader.

Persist palette assets in the per-media project. CPU-bus-backed palettes create
an authoritative data classification; live CGRAM palettes remain device-space
facts. The Workbench provides quick 16-color CPU palette and full-CGRAM
bindings, a navigable 16-column swatch view, raw and RGB color inspection, and
explicit decode conflict display.

**Implementation status:** complete on July 30, 2026. Schema v6 stores palette
assets transactionally. `clover_palette_test` covers BGR555 conversion,
alignment, truncation, and CGRAM bounds. The project test covers durable
palette definitions, data classification, migration, and rejected invalid
assets. `clover_analysis_boundary_test` guards the read-only CGRAM descriptor
and bounds. `clover_workbench_palette_integration_test` executes a real CPU
upload through `$2121/$2122` and follows it through side-effect-free CGRAM
inspection to the decoded RGB value.

### Remaining Stage 5 work

Implement:

- graphics viewers;
- tile-map viewers;
- OAM inspection;
- DMA source tracking;
- asset definitions;
- script definitions;
- APU and DSP inspection.

Deliverable:

A game-oriented reverse-engineering environment.

## Stage 6: Deterministic reverse debugging

**Status: planned; checkpoint and replay primitives are established.**

Implement:

- checkpoint scheduling, indexing, retention, and storage policy built on the
  established capture/restore envelope;
- deterministic replay orchestration from a checkpoint plus recorded input;
- timeline;
- last-writer queries;
- event history;
- reverse step;
- rewind to event;
- session comparison;
- marker-based investigation.

Deliverable:

A causal debugger capable of explaining how state arose.

## Stage 7: Game-authoring workflow

**Status: planned.**

Implement:

- assembler integration;
- an editor-neutral source-document and source-location model;
- source-to-canonical-byte and source-to-runtime-address mappings;
- an embedded mature editing component or external-editor integration;
- assembly navigation, completion, diagnostics, and source breakpoints;
- patch projects;
- free-space management;
- relocation;
- pointer repair;
- asset import/export;
- ROM expansion policy;
- hot reload;
- patch export;
- deterministic regression for modified builds.

Deliverable:

A practical game modification and authoring environment.

## Stage 8: Reconstructed assembly-source export

**Status: planned.**

Stage 8 turns the analysis project into a maintainable assembly-source project.
It does not claim to recover the historical source tree. It emits a
deterministic representation of the bytes and the best-supported structure
known to Workbench.

### Stage 8.1: Export inputs

Every export should pin:

- canonical base-image hash;
- effective patched-image hash when applicable;
- cartridge mapping and hardware profile;
- analysis generation;
- decoder and analyzer versions;
- selected symbol and annotation layers;
- source-layout policy;
- assembler dialect and version;
- linker or placement-tool version;
- export-format version.

The export must be reproducible from those inputs. If a required input has
changed, Workbench should regenerate deliberately rather than silently mixing
facts from different analysis generations.

### Stage 8.2: Source meaning and non-goals

“Source” in this stage means assembly and data declarations capable of
rebuilding the ROM.

The export may contain:

- assembly instructions;
- labels and aliases;
- constants and hardware-register symbols;
- vectors;
- typed data declarations;
- pointer and jump tables;
- structures, enums, and bitfields where supported by the selected dialect;
- asset declarations;
- raw byte directives;
- included binary regions;
- placement and linker directives;
- analyzer-generated explanatory comments.

It must not imply that generated names, comments, file boundaries, macros, or
module organization came from the original developers. High-level-language
output, including C, is explicitly outside this document.

### Stage 8.3: Deterministic project organization

Workbench should provide several layout policies:

```text
Lossless bank layout
    One primary generated unit per canonical ROM bank or storage region.

Semantic layout
    Generated units grouped by discovered functions, data, scripts, and assets.

User-directed layout
    Stable user assignments place analysis entities into named source units.
```

The lossless bank layout should always remain available because it requires the
fewest semantic assumptions. Semantic layout is more readable but must not be a
prerequisite for byte-identical reconstruction.

Generated filenames and labels should be deterministic, dialect-safe, and
collision-resistant. Stable analysis-entity identifiers should preserve
placement decisions when a label is renamed or analysis improves.

One possible export is:

```text
source-project/
    clover-export.json
    build/
        build-config.json
    linker/
        memory-map.json
        layout.script
    include/
        hardware.inc
        symbols.inc
        types.inc
    generated/
        vectors.asm
        bank_80.asm
        bank_81.asm
        data/
            pointer_tables.asm
        assets/
            palettes.asm
    unknown/
        bank_83_9000_97ff.bin
    patches/
        user_changes.asm
    assets/
        replacements/
```

The exact names may vary by assembler profile, but ownership must remain
unambiguous.

### Stage 8.4: Generated and authored ownership

Regeneration must never overwrite authored work.

The export contract should distinguish:

```text
Workbench-owned generated files
    Disposable and safe to regenerate from the analysis database.

User-owned authored files
    Patches, replacement assets, build additions, and handwritten assembly.

Shared declarative placement
    User decisions stored as project facts and consumed during regeneration.
```

User names, types, comments, classifications, and file-placement decisions
belong in the persistent analysis project, not only in generated text. A user
may edit generated output for experimentation, but Workbench must warn that
those edits are not durable unless promoted into an authored overlay or project
fact.

### Stage 8.5: Lossless emission rules

Every canonical input byte must have exactly one rebuild representation.

Workbench should emit understood regions using the richest representation that
is safe:

- instructions for sufficiently established code when the selected assembler
  can be constrained to reproduce the exact opcode and addressing-mode
  encoding;
- typed declarations for established data;
- symbolic expressions for resolvable pointers;
- asset source where a lossless encoder is available;
- raw byte directives for small opaque or ambiguous regions;
- included binary files for large opaque regions.

Uncertainty must not block export. Conflicting or ambiguous regions should fall
back to a lossless byte representation and retain comments or manifest entries
describing the unresolved interpretations.

Mirrored CPU addresses must not cause the same canonical stored byte to be
emitted multiple times. The source project should emit canonical storage once
and represent valid bus aliases through symbols or address expressions.

The exporter must preserve:

- canonical byte order and size;
- bank and section placement;
- vectors;
- cartridge header bytes;
- checksum bytes in byte-identical mode;
- padding and alignment;
- intentionally overlapping logical interpretations;
- unclassified and unreachable bytes.

A separate modified-build mode may recompute checksums, expand ROM, or relocate
content. Those transformations must not occur during byte-identical base-image
reconstruction.

Readable assembly is subordinate to losslessness. If a dialect cannot force an
immediate width, direct-versus-absolute form, long-versus-short address,
instruction alias, or other exact encoding, the exporter should use an
encoding-forcing directive or raw bytes for that instruction and record why.

### Stage 8.6: Assembler and placement profiles

Source generation must target an explicit assembler profile. A profile defines:

- accepted 65C816 syntax;
- immediate-width directives and mode assumptions;
- label and identifier rules;
- section and bank syntax;
- binary-include syntax;
- structure, enum, and macro capabilities;
- syntax for forcing exact opcode and operand encodings;
- object format;
- linker or placement behavior;
- command-line build invocation.

The analysis model must remain assembler-neutral. Formatting and project
emission translate that model into a chosen profile.

The initial implementation should support one well-tested assembler and
placement toolchain rather than several incomplete dialects. Additional
profiles can be added after the source model and verification contract are
stable.

### Stage 8.7: Incremental regeneration

Regeneration should be deterministic and reviewable:

1. load a pinned analysis generation;
2. resolve the selected layout and assembler profiles;
3. generate into a staging directory;
4. verify that all canonical bytes have a representation;
5. build and validate the staged project;
6. compare generated files with the prior generated tree;
7. replace only Workbench-owned output;
8. leave authored files untouched;
9. report moved, renamed, reclassified, or downgraded entities.

Analysis improvement may convert an `.incbin` region into instructions or typed
data. That change should appear as an explicit generated diff. It must not
require the user to manually recover patches or annotations from the old file.

### Stage 8.8: Patch and authored-source composition

The unmodified generated base should build successfully before authored changes
are applied.

Authored assembly and assets should be composed as explicit overlays:

```text
Canonical base reconstruction
    → authored assembly patches
    → replacement assets
    → relocation and free-space allocation
    → modified-image checksum policy
    → final patched image
```

This separation allows Workbench to distinguish:

- a failure to reconstruct the original bytes;
- a failure in generated source;
- an invalid user patch;
- a relocation conflict;
- an asset-encoding failure;
- an expected final-image difference.

### Stage 8.9: Verification contract

A byte-identical export is successful only when:

1. the generated project builds from a clean directory;
2. every canonical input byte is represented;
3. the output has the expected canonical size;
4. the output hash matches the canonical base-image hash;
5. a byte-for-byte comparison reports no mismatch;
6. a second generation with unchanged inputs produces no generated-file diff;
7. authored files remain untouched;
8. the manifest records the exact toolchain and analysis inputs.

When verification fails, Workbench should map each mismatch back to:

- canonical ROM offset;
- logical address aliases;
- generated source file and line;
- analysis entity;
- emitted region kind;
- expected and actual bytes.

Verification should produce a machine-readable report so deterministic
regression tests can enforce reconstruction closure.

### Stage 8.10: Acceptance tests

Stage 8 should include fixtures covering:

- pure code regions;
- mixed code and data;
- all 65C816 immediate-width contexts;
- bank-boundary placement;
- mirrored ROM addresses;
- vectors and headers;
- unknown byte ranges;
- ambiguous classifications;
- included binary regions;
- typed pointer tables;
- ROM regions identified as sources for copied-to-WRAM code;
- user-defined source-unit placement;
- regeneration after label and type changes;
- authored overlays that survive regeneration;
- expected failure diagnostics for mismatched output.

Deliverable:

A deterministic, regenerable assembly-source project that reconstructs the
canonical ROM byte-for-byte, preserves all unknown bytes, clearly separates
generated interpretation from authored changes, and never presents its chosen
organization as the unrecoverable historical source tree.

---

# 29. What “World Class” Means

A world-class implementation is not defined by having the most colorful assembly listing.

It is defined by correctness, honesty, connectedness, and usefulness.

It should:

- decode instructions correctly;
- represent processor context explicitly;
- preserve uncertainty;
- distinguish code from data;
- understand address spaces;
- combine static and runtime evidence;
- record provenance;
- preserve user knowledge;
- expose hardware semantics;
- connect code to memory;
- connect memory to graphics and audio;
- connect execution to time;
- answer causal questions;
- support deterministic replay;
- support intentional modification;
- enable progressive source reconstruction.

A world-class environment should allow the user to move seamlessly among:

```text
Code
Data
Hardware
Time
Assets
Execution
Evidence
Authored changes
```

The most important principle is this:

> The tool must never fabricate certainty merely to make the analysis look complete.

That principle fits Clover’s broader engineering philosophy.

A passing game path is evidence about the path exercised.

A runtime-observed target is evidence that the target occurred.

A static inference is an inference.

A user classification is an explicit assertion.

A physical hardware observation is hardware evidence.

These things should remain distinguishable.

---

# 30. Why Clover Is Well Positioned

Clover already contains several architectural prerequisites that make this feasible:

- a headless core;
- explicit subsystem ownership;
- a common master-clock vocabulary;
- deterministic-capable startup with explicit entropy configuration;
- deterministic input replay;
- frame-accurate observation;
- explicit frontend boundaries;
- targeted typed diagnostics;
- bounded and opt-in diagnostic mechanisms;
- ROM hashing and identity in the platform ROM library;
- save-RAM restoration;
- reproducible captures;
- exact regression workflows;
- a policy of hardware correctness over ROM-specific workarounds.

These foundations have now produced an explicit inspection and observation
boundary, exact portable checkpoints, a structured decoder, a persistent
Workbench project, and a live debugger. Sections 2.4 and 28 distinguish that
implemented substrate from the hybrid analysis, typed-data, reverse-debugging,
and authoring services that remain.

Many emulator projects attempt to add an advanced debugger after years of accumulating tightly coupled state and ad hoc logging.

Clover can extend its existing clean boundaries instead of first having to
untangle presentation policy from emulated hardware.

The disassembler itself is not the frightening part.

A correct 65C816 decoder is finite and manageable.

The harder and more valuable work is building the connected model around it:

- context;
- address identity;
- control flow;
- code/data classification;
- runtime evidence;
- symbols;
- types;
- provenance;
- debugging;
- replay;
- graphics;
- audio;
- assets;
- patching;
- reconstruction.

That work is substantial, but it is also a natural extension of Clover’s existing philosophy.

---

# 31. Final Direction

Clover should absolutely gain a disassembler.

However, it should be designed from the beginning as the first major component of a broader **Clover Workbench**.

The disassembler should not become:

- a string formatter attached to the CPU;
- a one-off debugger panel;
- a linear ROM dump;
- a collection of ROM-specific annotations;
- an analysis system that treats guesses as facts.

It should become the code-facing view of an integrated hardware-analysis platform.

The long-term vision is:

```text
Clover Emulator
    +
Clover Debugger
    +
Clover Disassembler
    +
Clover Static Analyzer
    +
Clover Trace Analyzer
    +
Clover Graphics Inspector
    +
Clover Audio Inspector
    +
Clover Reverse Debugger
    +
Clover Patch and Asset Toolchain
    =
Clover Workbench
```

At that point, Clover is not only a way to run SNES software.

It is a way to understand the machine, understand the game, explain how the game works, modify it safely, validate the modification, and eventually build new work with the same level of hardware awareness.

That is what a world-class implementation looks like.

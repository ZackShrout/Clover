# Clover Workbench

Clover Workbench is a separate desktop analysis application built on the same
SNES core as Clover Player. It does not add analysis UI or project dependencies
to the Player.

## Build and launch

```bash
cmake --preset sdl-release
cmake --build --preset sdl-release
./cmake-build-sdl-release/clover_workbench "/path/to/game.sfc"
```

Workbench starts at the native reset vector unless an address is supplied:

```bash
./cmake-build-sdl-release/clover_workbench "/path/to/game.sfc" \
  --address 00:8000
```

Use `--project-root <path>` to override the platform application-data location.

## Workbench controls

| Control | Action |
|---|---|
| F5 | Continue or pause the live target |
| F9 | Add or remove a persistent execution breakpoint |
| F10 | Step over a call, otherwise step one instruction |
| F11 | Step one instruction |
| Shift+F11 | Step out of the current call |
| T | Run to the selected instruction |
| M | Add a persistent read/write CPU-bus watchpoint by address |
| A | Analyze and atomically publish a new hybrid-analysis generation |
| N | Navigate to the next discovered function |
| K | Navigate to the next analysis conflict |
| X / Shift+X | Follow an outgoing / incoming cross-reference |
| Up / Down | Select an instruction |
| Enter | Follow a statically known direct target |
| Page Up / Page Down | Move the linear listing |
| L | Add or replace the selected address label |
| Semicolon | Add or replace the selected address comment |
| B | Add a bookmark |
| C / D | Classify the selected bytes as code or data |
| Alt+Left / Alt+Right | Move backward or forward through navigation history |
| H | Refresh the built-in SNES hardware symbol package |
| Escape | Cancel an edit or quit |

Text edits are committed with Enter. The first desktop slice deliberately uses
the native reset state. Once the live debugger attaches, disassembly width and
effective addresses use the current `E`, `M`, `X`, `D`, and `DB` register
values. The current PC is marked with `>` and execution breakpoints with `B`.
Analyzed runtime-covered instructions are marked with `+`. The inspector shows
live registers, a side-effect-free memory window, analysis confidence,
canonical-ROM versus writable-code identity, evidence provenance, block
ownership and outgoing edges, cross-reference counts, and conflicts.

Workbench runs debugger continue operations in bounded batches of exact
whole-machine instruction steps. This keeps the desktop responsive and allows
breakpoints and watchpoints to stop on an instruction boundary without placing
UI policy or callbacks in the emulation core.

An `M` watchpoint accepts a CPU-bus address such as `00:2100`. CPU read and
write events are observed inside the real bus access path, so hardware-register
writes can stop the debugger without the UI reading volatile MMIO.

## Hybrid analysis

`A` combines deterministic recursive traversal with the bounded execution
evidence collected by the current debugger session. Reset and interrupt
vectors, user code/data classifications, direct targets, and runtime-observed
addresses seed the traversal. The analyzer propagates explicit 65C816
`E/M/X/D/DB` context and stops with a durable conflict when width, ownership,
availability, or code/data interpretation is ambiguous.

Runtime evidence is aggregated by edge and decode context rather than retained
as an unbounded trace. Each record carries a session identity and hit count,
and the debugger reports records dropped after its fixed distinct-edge limit.
Observed indirect targets become confirmed graph edges. Executed code also
retains whether its bytes came from canonical cartridge media or writable
memory.

The same path is available without SDL:

```bash
./build/clover_workbench_analyze "/path/to/game.sfc" \
  --project-root "/path/to/projects" \
  --run-instructions 100000
```

Omit `--project-root` for a read-only report and omit `--run-instructions` for
vector-led static analysis only.

## Project storage

Each project is keyed by the canonical media identity shared with the ROM
library:

```text
<project-root>/<system>/<canonical-sha256>/project.sqlite3
```

A 512-byte copier header therefore does not create a second project for the
same cartridge image. A recorded project identity is checked whenever the
database opens.

SQLite schema changes use an immediate transaction. Either every statement and
the schema-version update commits, or the migration rolls back.

Facts carry one of three layers:

- user: labels, comments, bookmarks, and explicit code/data classifications;
- imported: the built-in SNES hardware register symbol package;
- derived: analyzer output tied to an analysis generation.

Analysis generations record analyzer and decoder versions plus a deterministic
input fingerprint. Instructions, blocks, many-to-many function ownership,
edges, cross-references, confidence, provenance, conflicts, and coverage are
written as one transaction. The prior published generation remains readable
until the candidate validates and commits; a failed publication leaves it
current. Publishing invalidates legacy derived facts without deleting labels,
comments, bookmarks, classifications, breakpoints, watchpoints, or imported
symbols. Navigation history and its cursor are also persisted, with a
512-entry bound and ordinary forward-history truncation after branching.

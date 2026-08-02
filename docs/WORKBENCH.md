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
| Tab | Toggle the live game-output preview |
| F5 | Fast continue or pause the live target; breakpoints and watchpoints remain exact |
| Shift+F5 | Traced continue or pause; collects per-instruction runtime analysis evidence |
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
| Y / Shift+Y | Bind the selected address as an unsigned byte / ASCII string[16] |
| J | Navigate to the next typed object |
| P | Follow the first pointer decoded from the selected typed object |
| Q / Shift+Q | Bind a 16-color CPU-bus palette / the live 256-color CGRAM palette |
| V / Shift+V | Open or cycle palette assets / return to disassembly |
| Arrow keys in palette view | Select a palette color |
| 2 / 4 / 8 | Bind CPU-bus tiles in the selected SNES planar format |
| Shift+2 / Shift+4 / Shift+8 | Bind live VRAM tiles in that format |
| G / Shift+G | Open or cycle tile assets / return to disassembly |
| Arrow keys in tile view | Select a tile |
| Shift+arrow keys in tile view | Select a pixel within the tile |
| Ctrl+1 / Ctrl+2 / Ctrl+3 / Ctrl+4 | Open the corresponding active raw rendered BG frame |
| R in a live BG view | Toggle raw rendered frame / coherent backing tilemap |
| H / Shift+H | Open or cycle tile maps / return to disassembly |
| F in a live tile-map view | Toggle the current scrolled viewport / full backing map |
| Arrow keys in tile-map view | Select a map entry |
| O | Open or close the live OAM object viewer |
| Arrow keys in OAM view | Select one of the 128 object entries |
| I / Shift+I | Open or close the live DMA/HDMA history / clear its history |
| Arrow keys in DMA view | Select a captured transfer |
| Enter / Shift+Enter in DMA view | Follow the initiating instruction / A-bus source |
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
Analyzed runtime-covered instructions are marked with `+`; typed ranges use
`T`. The inspector shows
live registers, a side-effect-free memory window, analysis confidence,
canonical-ROM versus writable-code identity, evidence provenance, block
ownership and outgoing edges, cross-reference counts, conflicts, and decoded
typed values.

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

## Typed data

The schema-v5 typed-data model supports unsigned and signed integers, arrays,
structures, pointers, enums, bitfields, and fixed-size ASCII or UTF-8 strings.
Definitions record stable identities, explicit sizes and byte order, aggregate
relationships, structure members, named values, pointer address spaces, and
string encodings. Object bindings name an address-space range and persist as
user facts.

`Y` is the fastest way to bind the selected byte; `Shift+Y` creates or reuses a
16-byte ASCII definition and binds it. Binding also creates a user data
classification, so reanalysis reports a conflict if execution evidence treats
the same bytes as code. `J` cycles through bindings, while `P` follows a
decoded pointer. The inspector reports unavailable bytes and uninspectable
pointer targets instead of manufacturing a value.

## Palette inspection

Palette assets persist a stable name, source address space, color count, and
format. Stage 5.2 supports SNES little-endian BGR555 sources in inspectable
CPU-bus memory, WRAM, canonical media, or the live PPU CGRAM address space.
The decoder retains the raw 15-bit word, five-bit channels, and expanded
eight-bit RGB values.

`Q` binds 16 colors at the selected CPU-bus address and classifies the 32-byte
range as data. `Shift+Q` binds all 256 live CGRAM colors without reading the
stateful `$213B` register. Press `V` to open the palette view or cycle among
saved palettes; `Shift+V` returns to disassembly. Arrow keys select a swatch,
and the inspector shows its index, raw BGR555 word, RGB channels, source, and
any alignment, availability, truncation, or bounds conflict.

## Tile-graphics inspection

Tile assets persist a stable name, source, tile count, native SNES planar
format, and an optional palette/base-color link. Stage 5.3 decodes 2bpp, 4bpp,
and 8bpp 8x8 tiles through the same side-effect-free byte-reader boundary.

Press `2`, `4`, or `8` to bind 64 tiles at the aligned selected CPU-bus
address; hold Shift to bind 256 tiles from live VRAM. `G` opens the 16-column
tile grid or cycles saved tile assets, and `Shift+G` returns to disassembly.
Arrow keys select a tile. Shift+arrow keys select one of its pixels, whose
decoded color index and linked palette are shown in the inspector. Live VRAM
inspection reads the PPU-owned 64 KiB array directly and never touches the
stateful `$2139/$213A` ports.

## Background tile-map inspection

Tile-map assets persist their SNES screen geometry, tile size, source, and
links to tile and palette assets. Stage 5.4 decodes all four 32/64-screen
quadrant layouts and retains each entry's character, palette group, priority,
and horizontal/vertical flips. Both 8x8 and 16x16 entries are assembled in the
viewer.

Press `Tab` to watch the live game output while the debugger runs, then pause
on the scene to inspect. Workbench's specialized core keeps normal composed
frame production active during instruction-domain debugger execution, and the
UI publishes the latest completed frame only while the output view is open.
This makes the preview current without adding framebuffer copies to the other
views or changing the Player loop. Press `Ctrl+1` through `Ctrl+4` to bind the
corresponding active BG layer. The binding reads the PPU's already-decoded render configuration and
automatically creates or updates its live CGRAM palette, VRAM tile set, and
tile map, opens the completed raw rendered layer frame, and never reads stateful
PPU ports. The raw frame records the PPU state that actually applied on every
scanline, including raster-time scroll and map changes, main-screen enable, and
the layer's main-screen window mask. Press `R` to switch to
the coherent backing tilemap snapshot containing the layer configuration, all
VRAM, and all CGRAM from the same execution boundary. This second view is useful
for inspecting backing memory but deliberately does not claim to reconstruct
mid-frame effects.
Press `H` to
cycle saved maps and `Shift+H` to return to disassembly. Arrow keys select an entry, and
the inspector shows its raw word, position, character, palette group, priority,
and flips. Live bindings open on the PPU's current 256x224 scrolled viewport;
press `F` to inspect the complete 32/64-screen backing map. Transparent
color-zero pixels use a checkerboard so partially uploaded or sparse maps
remain legible. Selecting a BG layer that is inactive in the current PPU mode closes
the previous map instead of leaving stale layer contents visible. Mode 7 is
intentionally deferred to an affine-specific viewer.

The raw-layer capture is compiled only into the release-optimized Workbench
core variant. The ordinary Player core is built from the same sources without
the instrumentation definition and therefore contains no capture branch,
frame storage, or diagnostic symbols.

## OAM and sprite inspection

Press `O` to inspect all 128 live SNES OAM entries. The grid distinguishes
entries intersecting the 256x224 viewport from entries positioned outside it;
arrow keys select an object. The selected object is assembled from its live
4bpp VRAM tiles and OBJ CGRAM palette with transparent color zero shown as a
checkerboard. The inspector reports signed screen position, dimensions,
character and name table, palette, priority, flips, size selection, first
sprite, and range/time overflow flags. SNES OAM has no enable bit, so a fully
transparent in-range entry is shown accurately rather than labeled disabled.
The read-only `snes.oam` debug space does not access `$2138` or disturb its
address latch.

## DMA and HDMA provenance

Press `I` to inspect the specialized Workbench core's bounded live transfer
history. Each row identifies MDMA or HDMA, channel, byte count, A-bus address
or range, every B-bus register selected by the transfer mode, frame, and
raster position. The inspector also retains the exact CPU instruction that
wrote the enabling `$420B` or `$420C`, the channel mask, direction, first and
last values, and whether the B-bus access was valid. Arrow keys select a row;
Enter follows its initiating instruction and Shift+Enter follows the A-bus
source. Shift+I clears the diagnostic history.

Capture occurs directly at the existing DMA byte-transfer boundary, so the
reported addresses and values are observations of transfers the core actually
performed rather than a reconstruction from later PPU memory. Consecutive
MDMA bytes are coalesced, while HDMA records remain separated by scanline.
Storage is a fixed 512-record ring with explicit dropped-record accounting;
the hot path does not allocate, log, perform I/O, or invoke UI callbacks.
`CLOVER_WORKBENCH_DMA_PROVENANCE` is defined only for
`clover_core_workbench`. The normal Player target contains neither the ring nor
capture instructions.

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
- imported: the built-in SNES hardware register symbol and primitive-type
  packages;
- derived: analyzer output tied to an analysis generation.

Typed definitions, members, enum/bitfield values, and object bindings are
stored separately from regenerable analysis generations. They survive
reanalysis and are validated before a transaction commits; invalid references
or overlapping objects leave the prior model intact.

Schema v8 stores palette, tile-graphics, and tile-map assets. CPU-bus bindings
participate in code/data conflict analysis, while device-space CGRAM and VRAM
assets remain durable facts. Maps retain validated tile/palette links, screen
geometry, and palette base. Invalid definitions never replace the previously
stored asset.

Analysis generations record analyzer and decoder versions plus a deterministic
input fingerprint. Instructions, blocks, many-to-many function ownership,
edges, cross-references, confidence, provenance, conflicts, and coverage are
written as one transaction. The prior published generation remains readable
until the candidate validates and commits; a failed publication leaves it
current. Publishing invalidates legacy derived facts without deleting labels,
comments, bookmarks, classifications, breakpoints, watchpoints, or imported
symbols. Navigation history and its cursor are also persisted, with a
512-entry bound and ordinary forward-history truncation after branching.

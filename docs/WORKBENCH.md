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
The inspector shows live registers and a side-effect-free memory window.

Workbench runs debugger continue operations in bounded batches of exact
whole-machine instruction steps. This keeps the desktop responsive and allows
breakpoints and watchpoints to stop on an instruction boundary without placing
UI policy or callbacks in the emulation core.

An `M` watchpoint accepts a CPU-bus address such as `00:2100`. CPU read and
write events are observed inside the real bus access path, so hardware-register
writes can stop the debugger without the UI reading volatile MMIO.

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

Starting a new analysis generation records analyzer and decoder versions and
removes only derived facts. User-authored and imported knowledge remains
unchanged. Navigation history and its cursor are also persisted, with a
512-entry bound and ordinary forward-history truncation after branching.
Execution breakpoints and CPU-bus watchpoint ranges are persistent project
facts and are restored when the same canonical cartridge project reopens.

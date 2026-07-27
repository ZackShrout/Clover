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

## Stage 2 controls

| Control | Action |
|---|---|
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
an explicit reset decode context (`E=1`, `M=1`, `X=1`, `D=$0000`, `DB=$00`);
runtime mode-correct disassembly belongs to Stage 3.

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

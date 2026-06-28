# Clover Architecture

## Purpose

Clover is a performance-minded Super Nintendo Entertainment System emulator. This document defines the architectural boundaries of the codebase so the project remains fast, understandable, and maintainable as features are added.

## Project Laws

- `core/` must not depend on SDL, OpenGL, or any frontend library.
- The default runtime path must be performance-first and suitable for stable real-time frame pacing.
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

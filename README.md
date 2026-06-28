# Clover

Clover is a performance-minded Super Nintendo Entertainment System emulator focused on clean architecture, faithful behavior, and a stable real-time runtime.

This repository starts from a few non-negotiable project rules:

- `core/` owns emulation and timing.
- `frontend/` owns presentation and user-facing debug views.
- `platform/` owns OS, windowing, input, audio, and runtime shell work.
- Hot paths stay free of permanent debug instrumentation.
- Optional visibility features such as layer masking must not distort the default emulation path.

## Current State

This scaffold establishes the project shape and architectural guardrails before major CPU, PPU, APU, and scheduler implementation work begins.

## Layout

- `src/clover/core/`
- `src/clover/frontend/`
- `src/clover/platform/`
- `src/clover/debugger/`
- `src/clover/utils/`
- `tests/`
- `docs/`

## Build

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

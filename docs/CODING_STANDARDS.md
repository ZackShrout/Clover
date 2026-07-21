# Clover C++ Coding Standards

These rules keep the emulator hardware-focused, readable, and fast. Existing
local style wins when a rule is not stated here.

## Language and Build

- C++23 is required.
- CMake is the build system.
- Warnings introduced by a change should be fixed, not suppressed globally.
- Platform dependencies remain optional; the default headless build must not
  require SDL.

## Architecture First

- Emulated hardware and timing belong in `src/clover/core/`.
- System-neutral media/input/output contracts belong in
  `src/clover/frontend/`.
- SDL and operating-system behavior belong in `src/clover/platform/`.
- Do not repair a ROM by recognizing the ROM, program counter, scene, or data
  pattern. Model the hardware behavior that makes the ROM correct.
- Keep the core callable by tests and non-SDL renderers.
- Add an abstraction at a demonstrated subsystem boundary, not for a
  hypothetical feature with no consumer.

## Naming

The codebase uses snake case:

- namespaces: `clover::core`, `clover::frontend`, `clover::platform`
- types and enums: `console_t`, `hardware_step_result_t`, `pixel_format_t`
- functions and variables: `run_frame()`, `master_clock`, `frame_index`
- compile-time constants: `k_audio_sample_rate_hz`
- private data members: `_console`, `_audio_stream`

Use fixed-width integer types for hardware-visible values and clocks. Include
units in names when ambiguity is possible: `_hz`, `_ns`, `_bytes`,
`_master_clocks`.

## Formatting

Follow the established source style:

```cpp
namespace clover::core
{
    struct example_t
    {
    public:
        [[nodiscard]] uint8_t read_u8(uint32_t address) const noexcept;

    private:
        uint8_t _value{ 0 };
    };
}
```

- Four spaces; no tabs.
- Braces on their own line for namespaces, types, functions, and control flow.
- One declaration per line.
- Use brace initialization by default.
- Keep headers self-contained and use `#pragma once`.
- Order includes as the project header, other Clover headers, standard library,
  then external libraries, with blank lines between groups.
- Prefer early returns when they make hardware conditions clearer.

## Interfaces and Types

- Mark discarded-result bugs with `[[nodiscard]]`.
- Mark non-throwing hardware and frame-boundary operations `noexcept`.
- Prefer `std::span` for borrowed contiguous data and views for frontend output.
- Prefer `enum class` to unscoped constants.
- Use `const` wherever it communicates ownership or immutable hardware state.
- Avoid owning raw pointers; use references, values, or RAII ownership.
- Avoid implicit narrowing and signed/unsigned mixing in address and timing math.
- Keep public interfaces small and based on responsibilities, not debugging
  convenience.

## Hardware Code

- Express bus phases, latch points, and clock domains explicitly.
- Do not collapse assertion, latching, observation, acknowledgement, and
  consumption of an interrupt into one vague flag.
- A timing constant should identify its clock domain or derive from the shared
  timing profile.
- Preserve open-bus and register side effects; a read is not automatically a
  pure lookup.
- Prefer fixed-size arrays and bounded traces in PPU/CPU/APU hot state.
- No allocations, strings, file I/O, console output, or virtual debug callbacks
  inside instruction, pixel, or sample hot loops.
- Comments should explain the hardware reason or observation point, not restate
  the code.

## Diagnostics

- Diagnostic work is opt-in and disabled by default.
- Use typed snapshots or bounded trace entries in the core; format and write
  them in tests or platform code.
- Environment-controlled probes must not impose hidden work when disabled.
- Remove temporary one-ROM watches after the investigation unless they become
  a generally useful, explicitly enabled diagnostic.
- Repeat timing/performance measurements with all probes disabled.

## Error Handling

- Core load/setup APIs may return `bool` when failure is expected and the
  caller owns reporting.
- Platform and tool code should report actionable context to `stderr` and
  return a nonzero exit code.
- Do not print from ordinary core execution.
- Validate external sizes, formats, indices, and paths before use.
- Refuse destructive overwrite for investigation artifacts unless explicitly
  requested; SDL capture directories currently must be new.

## Testing

Changes to emulated behavior should be checked at the narrowest useful level
and then against real software:

1. Add or update a focused hardware test when the behavior can be isolated.
2. Run the headless CTest suite.
3. Compare equivalent Clover and bsnes frames/state with deterministic startup.
4. Use exact pixel comparison for visual closure.
5. Use a shared input movie plus audio/frame telemetry for input-gated or audio
   failures.
6. Re-run a representative ROM set to catch cross-title regressions.

Tests and reference emulators can contain wrong assumptions. When a microtest
and a validated real path disagree, reconcile both at the same hardware
observation point before changing the core.

## Documentation and Commits

- Update architecture, limitations, commands, and accuracy claims in the same
  change that makes them stale.
- State what was actually validated, including frame ranges and comparison
  mode; do not generalize a checkpoint into universal support.
- Keep generated captures, commercial ROMs, build trees, and local diagnostic
  archives out of source control.
- Keep commits focused enough that the hardware change and its evidence can be
  reviewed together.

# Clover Frontend Implementation Plan

## Purpose

This document turns the current frontend discussion into a concrete build plan.
The immediate goal is a live SDL3 desktop frontend for the current SNES core.
The longer-term goal is a frontend/core boundary that can support additional
systems later without forcing Clover to pretend it is fully multi-system today.

This plan intentionally borrows the **boundary shape** from bsnes while keeping
the implementation Clover-specific:

- the core exposes system metadata and runtime control through an emulator-core interface
- the frontend implements a host interface that receives video/audio output and supplies input
- SDL3 handles windowing, rendering, audio, and input device discovery
- SNES remains the only active core for now, wired through the factory

We want bsnes-style framebuffer and input handoffs without importing bsnes's
Ruby/Hiro application stack or its global-singleton structure.

## Background

bsnes separates these responsibilities cleanly:

- system-facing interface:
  - display metadata
  - load/save/power/reset/run
  - port/device/input enumeration
- host-facing platform contract:
  - `videoFrame(...)`
  - `audioFrame(...)`
  - `inputPoll(...)`
  - `inputRumble(...)`

That ownership model is the part worth preserving.

For Clover, the equivalent high-level rule should be:

- `core/` owns emulation and timing
- `frontend/` owns emulator-facing abstractions and presentation policies
- `platform/` owns SDL3 shell work, device discovery, audio devices, window creation, and renderer setup

## Current Clover State

Today Clover already has a very small abstraction seam:

- `src/clover/frontend/EmulatorCore.h`
- `src/clover/frontend/SnesEmulatorCore.h`

That seam is useful, but it is still too narrow for a real frontend. It only
supports:

- `power_on()`
- `run_frame()`
- `framebuffer()`

That is enough for tests and smoke harnesses, but not enough for:

- ROM loading from the app shell
- audio handoff
- input polling
- controller/device metadata
- future non-SNES cores

Separately, current controller behavior is still too CPU-local. The CPU owns
some placeholder controller-visible register state, but there is not yet a
first-class controller-port abstraction in the core. That should be corrected
before SDL is allowed to drive input deeply into the runtime.

## Design Goals

1. Preserve a headless core.
2. Keep SDL3 out of `core/`.
3. Match bsnes's handoff model conceptually:
   - semantic input polling by `(port, device, input)`
   - frame-complete video delivery from core to host
   - frontend-owned device mapping and presentation
4. Keep SNES hard-wired for now through the factory, not throughout the API.
5. Avoid global singletons where straightforward dependency injection works.
6. Keep the default fast path clean and free of frontend-specific branching.

## Recommended Architecture

Introduce two cooperating interfaces:

1. `frontend::emulator_core_t`
2. `frontend::emulator_host_t`

### `frontend::emulator_core_t`

This is the system-facing contract. It describes what the active emulator core
is and what the frontend can ask it to do.

Recommended responsibilities:

- system identity
- display characteristics
- ROM loading and unload
- power and reset
- frame or run advancement
- port/device/input enumeration
- port-device connection management
- host attachment
- optional framebuffer snapshot access for tests and tools

Recommended initial shape:

```cpp
namespace clover::frontend
{
    enum class system_id_t
    {
        snes
    };

    struct core_information_t
    {
        const char* manufacturer{ "" };
        const char* name{ "" };
        const char* extension{ "" };
        bool resettable{ false };
    };

    struct display_info_t
    {
        uint32_t nominal_width{ 0 };
        uint32_t nominal_height{ 0 };
        uint32_t framebuffer_width{ 0 };
        uint32_t framebuffer_height{ 0 };
        float pixel_aspect_ratio{ 1.f };
        float nominal_refresh_hz{ 0.f };
    };

    struct input_port_info_t
    {
        uint32_t id{ 0 };
        const char* name{ "" };
    };

    struct input_device_info_t
    {
        uint32_t id{ 0 };
        const char* name{ "" };
    };

    struct input_control_info_t
    {
        enum class type_t
        {
            button,
            axis,
            hat,
            trigger,
            rumble
        };

        uint32_t id{ 0 };
        type_t type{ type_t::button };
        const char* name{ "" };
    };

    struct emulator_host_t;

    struct emulator_core_t
    {
        virtual ~emulator_core_t() = default;

        [[nodiscard]] virtual system_id_t system() const noexcept = 0;
        [[nodiscard]] virtual core_information_t information() const noexcept = 0;
        [[nodiscard]] virtual display_info_t display() const noexcept = 0;

        virtual void attach_host(emulator_host_t& host) noexcept = 0;

        [[nodiscard]] virtual bool loaded() const noexcept = 0;
        [[nodiscard]] virtual bool load_rom(std::span<const std::byte> rom_data) noexcept = 0;
        virtual void unload() noexcept = 0;

        virtual void power_on() noexcept = 0;
        virtual void reset() noexcept = 0;
        virtual void run() noexcept = 0;

        [[nodiscard]] virtual std::span<const input_port_info_t> ports() const noexcept = 0;
        [[nodiscard]] virtual std::span<const input_device_info_t> devices(uint32_t port_id) const noexcept = 0;
        [[nodiscard]] virtual std::span<const input_control_info_t> controls(uint32_t device_id) const noexcept = 0;
        [[nodiscard]] virtual uint32_t connected_device(uint32_t port_id) const noexcept = 0;
        virtual void connect_device(uint32_t port_id, uint32_t device_id) noexcept = 0;

        [[nodiscard]] virtual const core::framebuffer_t& framebuffer() const noexcept = 0;
    };
}
```

Notes:

- `run()` is preferred over `run_frame()` for the public abstraction.
- For SNES phase one, `run()` can simply execute one frame.
- Keeping `framebuffer()` is still useful for tests and screenshots.
- `system_id_t` remains hard-wired to `snes` for now.

### `frontend::emulator_host_t`

This is the host-facing contract. The active core calls into it when it needs
to present output or read mapped input state.

Recommended responsibilities:

- video frame presentation handoff
- audio sample handoff
- semantic input polling
- rumble delivery

Recommended initial shape:

```cpp
namespace clover::frontend
{
    struct video_frame_view_t
    {
        const uint32_t* pixels{ nullptr };
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        uint32_t pitch_bytes{ 0 };
    };

    struct audio_sample_view_t
    {
        const float* samples{ nullptr };
        uint32_t frame_count{ 0 };
        uint32_t channel_count{ 0 };
        uint32_t sample_rate{ 0 };
    };

    struct emulator_host_t
    {
        virtual ~emulator_host_t() = default;

        virtual void present_video(const video_frame_view_t& frame) noexcept = 0;
        virtual void push_audio(const audio_sample_view_t& audio) noexcept = 0;
        [[nodiscard]] virtual int16_t poll_input(uint32_t port_id,
                                                 uint32_t device_id,
                                                 uint32_t control_id) noexcept = 0;
        virtual void set_rumble(uint32_t port_id,
                                uint32_t device_id,
                                uint32_t control_id,
                                bool enabled) noexcept = 0;
    };
}
```

Notes:

- This keeps the bsnes-style flow while staying idiomatic for Clover.
- No SDL types appear in the interface.
- The host decides how it maps keyboard/gamepad state onto these semantic controls.

## Runtime Ownership Model

The recommended runtime ownership model is:

1. `platform::sdl_app_shell_t` boots SDL3.
2. It creates a `platform::sdl_emulator_host_t`.
3. It creates the active `frontend::emulator_core_t` through the frontend factory.
4. It attaches the host to the core.
5. It loads a ROM.
6. It runs the app loop:
   - poll SDL events
   - update mapped input state
   - call `core.run()`
   - let the core deliver video/audio through the host

The key rule is:

- SDL drives the app loop
- the core still owns when a frame is complete
- the frontend host owns how that frame reaches the screen

## SNES-Specific Input Plan

Before SDL is connected to the SNES runtime, Clover should add first-class
controller ownership in `core/snes/`.

Recommended new core types:

- `controller_state_t`
- `controller_port_t`
- `snes_gamepad_state_t`
- `controller_data_lines_t`

Phase-one requirements are intentionally small:

- port 1 and port 2 exist
- `none` and `gamepad` devices exist
- gamepad exposes:
  - up
  - down
  - left
  - right
  - B
  - A
  - Y
  - X
  - L
  - R
  - select
  - start

Recommended ownership rule:

- the frontend host returns semantic input values
- the SNES frontend core samples those values into controller state at
  hardware-appropriate boundaries
- the CPU and bus observe controller-port state through SNES-owned interfaces

Do not let SDL directly mutate CPU IO registers.

## Video Handoff Plan

The current Clover PPU already produces a presented RGBA framebuffer, and
`console_t` exposes it. That is a good starting point.

Phase-one video policy:

- keep the current `core::framebuffer_t` as the canonical presented frame
- after `console_t::run_frame()` completes, `snes_emulator_core_t` hands that
  framebuffer to the attached host
- SDL uploads the RGBA buffer to a texture and presents it

This keeps the first frontend simple and avoids redesigning the PPU output path
before a live frontend exists.

Later, we can refine this if needed:

- richer timing metadata per frame
- overscan/crop policies
- layer-masking presentation options
- direct texture-friendly pixel formats

## Audio Handoff Plan

Audio is not yet represented in Clover's frontend seam, so phase one should add
the abstraction even if the initial implementation is minimal.

Recommended policy:

- define the host-side audio callback shape now
- define a core-side audio output buffer contract
- if the current APU/frontend path is not ready for real-time streaming yet,
  phase one may temporarily provide silence or no-op audio while the SDL shell
  is brought up

The important part is to choose the correct ownership boundary now so audio
slots cleanly into place later.

## File Layout Proposal

Recommended additions and changes:

### `src/clover/frontend/`

- `EmulatorCore.h`
- `EmulatorHost.h`
- `FrontendTypes.h`
- `EmulatorFactory.cpp`
- `SnesEmulatorCore.h`
- `SnesEmulatorCore.cpp`

Responsibilities:

- frontend-facing abstractions
- system-neutral metadata types
- active-core factory
- SNES adapter from `console_t` to the generic frontend interface

### `src/clover/platform/sdl/`

- `SdlAppShell.h`
- `SdlAppShell.cpp`

Responsibilities:

- SDL lifecycle
- window and renderer creation
- texture upload and presentation
- audio device management
- SDL keyboard/gamepad discovery
- mapping from SDL inputs to Clover semantic controls

### `src/clover/core/snes/`

Likely additions:

- `Controller.h`
- `Controller.cpp`

Responsibilities:

- hardware-facing SNES controller semantics
- latched controller state
- port/device read behavior
- future auto-joypad support integration

## Factory Direction

Keep the public abstraction generic, but keep the creation path hard-wired.

Recommended initial factory contract:

```cpp
namespace clover::frontend
{
    [[nodiscard]] std::unique_ptr<emulator_core_t> create_default_emulator_core() noexcept;
}
```

Initial implementation:

- always returns `snes_emulator_core_t`

Future-friendly optional extension:

```cpp
namespace clover::frontend
{
    enum class requested_system_t
    {
        snes
    };

    [[nodiscard]] std::unique_ptr<emulator_core_t> create_emulator_core(requested_system_t system) noexcept;
}
```

There is no need to expose non-SNES systems until they exist. The point is only
to keep the abstraction from being SNES-shaped in the wrong places.

## Phase-by-Phase Plan

### Phase 0: Lock the abstraction shape

Deliverables:

- expand `frontend::emulator_core_t`
- add `frontend::emulator_host_t`
- add frontend-neutral metadata types
- keep `create_default_emulator_core()` SNES-only

Exit criteria:

- the public seam can describe a system, connect a host, load a ROM, and run
- no SDL dependency leaks into `core/`

### Phase 1: Add SNES controller abstractions

Deliverables:

- introduce SNES controller-port types in `core/snes/`
- move controller-visible state out of CPU-local placeholders where appropriate
- define SNES port/device/control IDs

Exit criteria:

- `snes_emulator_core_t` can expose ports/devices/controls
- controller state can be sampled without SDL knowledge inside the core

### Phase 2: Adapt the SNES core to the frontend seam

Deliverables:

- `snes_emulator_core_t` owns `console_t`
- `load_rom(...)` routes into `console_t::load_cartridge(...)`
- `run()` executes one frame
- completed frame is pushed to the attached host

Exit criteria:

- a non-SDL test host can receive frames and provide semantic input

### Phase 3: Build the minimal SDL3 platform shell

Deliverables:

- SDL app shell
- window creation
- renderer and texture upload
- keyboard-to-gamepad mapping
- ROM path from command line

Current implementation note:

- the first SDL3 slice lives in `src/clover/platform/sdl/SdlAppShell.*`
- it is enabled by default, and can be disabled with
  `-DCLOVER_BUILD_SDL_FRONTEND=OFF`
- the executable currently expects a ROM path on the command line
- the SDL host now hotplugs the first available SDL gamepad onto SNES port 1
- keyboard and gamepad input both feed the same semantic SNES control IDs
- frame pacing now uses core-provided refresh metadata instead of a fixed
  `16ms` sleep

Exit criteria:

- Clover opens a window and displays live SNES video from a ROM
- at least one controller mapping works for gameplay

### Phase 4: Add real-time audio output

Deliverables:

- SDL audio device management
- core-to-host audio sample flow
- buffering policy appropriate for stable playback

Exit criteria:

- SDL frontend runs video and audio together at stable real-time pacing

### Phase 5: Improve frontend ergonomics

Deliverables:

- gamepad discovery
- configurable bindings
- pause/reset/power hotkeys
- drag-and-drop or file picker
- aspect and integer-scale options

Exit criteria:

- the SDL frontend is comfortable enough for regular bringup use

### Phase 6: Debug and presentation features

Deliverables:

- optional layer masking controls
- screenshots
- frame timing display
- future debugger-oriented overlays if desired

Exit criteria:

- frontend features remain opt-in and do not distort the default emulation path

## Recommended Initial IDs

These are enough for phase one and intentionally mirror the bsnes conceptual
shape without copying names exactly.

### SNES ports

- `controller_port_1 = 0`
- `controller_port_2 = 1`

### SNES devices

- `none = 0`
- `gamepad = 1`

### SNES gamepad controls

- `up = 0`
- `down = 1`
- `left = 2`
- `right = 3`
- `b = 4`
- `a = 5`
- `y = 6`
- `x = 7`
- `l = 8`
- `r = 9`
- `select = 10`
- `start = 11`

This is intentionally enough to support a live frontend first. Mouse,
multitap, Super Scope, and other SNES devices can come later.

## Pacing Recommendation

For the first SDL frontend, keep scheduling simple:

- `core.run()` advances one frame
- SDL app loop presents one completed frame at a time
- real-time pacing initially lives in the platform layer, not in the core
- the core should expose nominal refresh metadata so the platform layer can
  pace correctly without hardcoding extra system timing constants

This matches Clover's current `run_frame()` shape and avoids forcing a larger
timing redesign into the first frontend milestone.

If future systems need a different cadence, we can broaden the abstraction
later, but frame-at-a-time delivery is the fastest path to a working live ROM
frontend right now.

## Risks and Watchpoints

### 1. Making the abstraction too small again

If we only add SDL upload on top of the current `framebuffer()` getter, we will
have to redesign input, ROM loading, and audio immediately afterward.

Mitigation:

- lock the richer frontend/core seam first

### 2. Letting SDL concepts leak into `core/`

This would violate the repo's architecture rules and make future systems harder.

Mitigation:

- keep SDL-specific code entirely under `platform/sdl/`

### 3. Over-generalizing for future systems too early

Trying to solve Genesis, NES, and SNES all at once would slow down the only
frontend we actually need now.

Mitigation:

- generic interface
- SNES-only implementation
- SNES-only factory

### 4. Mixing host input mapping with SNES latch semantics

bsnes keeps this separation clean. Clover should too.

Mitigation:

- host returns semantic control values
- SNES core owns how and when those values are sampled

## Recommended First Implementation Slice

If work begins immediately, the highest-value first slice is:

1. expand `frontend::emulator_core_t`
2. add `frontend::emulator_host_t`
3. adapt `snes_emulator_core_t` to support ROM loading and host attachment
4. create a test host that receives a frame and returns fixed input
5. add SDL3 shell after the seam is proven

That ordering keeps the risk low and gives us a stable foundation for the live
frontend instead of a one-off SDL path.

## Summary

The recommended Clover frontend architecture is:

- bsnes-like in boundary shape
- Clover-specific in implementation
- SDL3-based in the platform layer
- SNES-only in the current factory
- future-friendly in the abstraction

The key decision is to treat the frontend as a host that the core talks to,
rather than treating the emulator as a passive framebuffer object. That is the
part of bsnes worth carrying forward into Clover.

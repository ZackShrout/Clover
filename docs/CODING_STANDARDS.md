# Clover Emulator – Coding Standards

**BunnySoft**
**Project Coding Standards**
**Last Updated: May 2026**

---

## File & Folder Naming

* Headers: `.h`
* Source files: `.cpp`
* Folders under `src/clover/`: lowercase snake_case

    * `core/`
    * `frontend/`
    * `platform/`
    * `debugger/`
    * `utils/`
* Source files: UpperCamelCase

    * `Emulator.h`
    * `Cartridge.h`
    * `Cpu65816.cpp`
    * `SdlAppShell.cpp`

---

## Naming Conventions

* All user-defined types: `snake_case_t`

    * `emulator_t`
    * `cartridge_t`
    * `cpu_65816_t`
    * `ppu_t`
* Functions, variables, parameters, namespaces: `snake_case`
* Private/protected member variables: `_snake_case`
* Compile-time constants / static globals:

    * `k_framebuffer_width`
    * `k_master_clock_hz`
    * `g_trace_cpu`
* Macros: rare, screaming case only when unavoidable

    * `CLOVER_ENABLE_TRACE`
    * `CLOVER_ASSERT`

---

## Namespaces

All Clover code lives under:

```cpp
namespace clover {
}
```

Subsystem namespaces are encouraged:

```cpp
namespace clover::cpu {
}

namespace clover::ppu {
}

namespace clover::frontend {
}

namespace clover::platform {
}
```

Rules:

* Never use `using namespace` in headers.
* Anonymous namespaces are encouraged in `.cpp` files for file-local helpers.
* `using` declarations are allowed in `.cpp` files when they improve readability.

---

## Core Language Style

Use uniform/bracket initialization everywhere:

```cpp
uint32_t cycles{ 0 };
float scale{ 1.f };
bool running{ true };
```

Float literals always use the `f` suffix:

```cpp
float alpha{ 1.f };
float gamma{ 2.2f };
```

Use `auto` only when:

* iterating containers
* the type is obvious and unimportant
* the type name is very long

Do not use `auto` to hide pointers, references, or important emulator types.

Good:

```cpp
for (const auto& breakpoint : breakpoints)
    check_breakpoint(breakpoint);
```

Avoid:

```cpp
auto cpu = emulator.cpu();
```

Prefer:

```cpp
cpu_65816_t* cpu{ emulator.cpu() };
```

---

## Bracing Style

Single-statement `if`, `for`, and `while` may omit braces:

```cpp
if (!is_valid)
    return false;

for (uint32_t i{ 0 }; i < count; ++i)
    step_cycle();
```

Multi-statement blocks always use Allman-style braces:

```cpp
if (loaded)
{
    reset();
    return true;
}
```

---

## Type Discipline

Prefer `struct` over `class`.

Use `class` only when:

* private sections are central to the type
* inheritance is actually needed
* the type represents a strict interface

Free functions in namespaces are preferred when behavior does not require internal object state.

Pointers and references bind to the type:

```cpp
uint8_t* data{ nullptr };
const cartridge_t& cartridge{ get_cartridge() };
```

East-const style is forbidden.

---

## Header Rules

* Use `#pragma once`.
* Prefer forward declarations over unnecessary includes.
* Headers should include only what they need.
* Never include SDL or ImGui from emulator core headers.
* Keep platform/frontend dependencies out of `core/`.

---

## Function Attributes

Use aggressively where legal:

```cpp
[[nodiscard]]
bool load_rom(std::span<const std::byte> rom_data);

[[nodiscard]]
uint8_t read_u8(uint32_t address) const noexcept;

void reset() noexcept;
```

General rules:

* Use `[[nodiscard]]` for functions returning meaningful values.
* Use `noexcept` for functions that cannot throw.
* Use `constexpr` for compile-time constants and simple compile-time helpers.
* Use `const` wherever mutation is not required.

---

## Struct / Class Layout Order

Access sections should appear at most once each, in this order:

1. `public`
2. `protected`
3. `private`

Within each section, place member functions first and member variables second.

Separate the function group from the variable group with a blank line when both are present.

Do not reopen the same access section later in the type just to split functions from data or constants from state. Keep each section contiguous and easy to scan.

Do not add an access label when the entire type uses the language default:

- omit `public:` for an all-public `struct`
- omit `private:` for an all-private `class`

Use this structure:

```cpp
struct example_t
{
public:
    // public methods

    // public data, only if truly appropriate

protected:
    // protected methods

    // protected data

private:
    // private methods

    // private data
};
```

For plain data structs, public data is acceptable:

```cpp
struct cpu_registers_t
{
    uint16_t a{ 0 };
    uint16_t x{ 0 };
    uint16_t y{ 0 };
    uint16_t sp{ 0 };
    uint16_t pc{ 0 };
    uint8_t  db{ 0 };
    uint8_t  pb{ 0 };
    uint8_t  p{ 0 };
};
```

---

## Emulator Core Rules

The emulator core must not depend on:

* SDL
* ImGui
* native OS APIs
* windowing code
* frontend audio APIs

The core may depend on:

* the C++ standard library
* Clover utility code
* plain data structures
* test helpers, in test builds only

The core owns emulation logic:

* CPU
* PPU
* APU
* cartridge mapping
* memory bus
* DMA / HDMA
* timing
* scheduling
* save RAM model

The frontend owns presentation:

* window creation
* framebuffer upload
* UI
* input device mapping
* audio device output

---

## SDL Rules

SDL is the platform shell.

SDL may be used for:

* window creation
* input polling
* gamepad mapping
* audio device output
* file dialog helpers later, if needed

SDL must not appear in emulator core code.

---

## Memory / Ownership

Avoid raw `new` and `delete`.

Prefer:

```cpp
std::unique_ptr<T>
std::vector<T>
std::array<T, N>
std::span<T>
```

Use raw pointers only for:

* non-owning references
* optional references
* C API interop
* tightly controlled low-level memory views

When using raw pointers, make ownership obvious.

---

## Fixed-Width Types

Use fixed-width integer types for emulator logic:

```cpp
uint8_t
uint16_t
uint32_t
uint64_t
int8_t
int16_t
int32_t
int64_t
```

Avoid plain `int` in hardware logic unless the size does not matter.

Addressing should use explicit types.

Examples:

```cpp
using cpu_address_t = uint32_t;
using rom_offset_t = uint32_t;
```

---

## ROM / Binary Data Rules

ROM data should be treated as immutable after loading.

Prefer:

```cpp
std::span<const std::byte>
std::vector<std::byte>
```

Avoid using `char*` for binary ROM data.

Use helper functions for endian reads:

```cpp
[[nodiscard]]
uint16_t read_le_u16(std::span<const std::byte> data, size_t offset) noexcept;
```

---

## Error Handling

For early Clover development:

* use `bool` for simple success/failure
* use small result structs when useful
* avoid exceptions in emulator core
* log errors at the frontend/app boundary

Example:

```cpp
struct rom_load_result_t
{
public:
    bool success{ false };
    std::string error;
};
```

---

## Logging

Logging should be lightweight and frontend-controllable.

Core code may emit logs through Clover logging helpers, but should not print directly to stdout/stderr except in small tools or tests.

Avoid:

```cpp
printf("bad opcode\n");
std::cout << "bad opcode\n";
```

Prefer:

```cpp
clover_log_error("Invalid opcode: 0x%02X", opcode);
```

---

## Testing Rules

Testable code is preferred over clever code.

The core should be designed so tests can directly exercise:

* CPU instructions
* addressing modes
* bus reads/writes
* ROM header parsing
* mapper behavior
* PPU register behavior
* scheduler behavior

Commercial ROMs must not be committed.

Legally redistributable test ROMs may live under:

```text
roms/tests/
```

---

## Example Clover Style

```cpp
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace clover {

struct cartridge_header_t
{
    char     title[22]{};
    uint8_t  mapping_mode{ 0 };
    uint8_t  rom_size{ 0 };
    uint8_t  sram_size{ 0 };
    uint16_t checksum{ 0 };
    uint16_t checksum_complement{ 0 };
};

struct cartridge_t
{
public:
    [[nodiscard]]
    bool load(std::span<const std::byte> rom_data);

    [[nodiscard]]
    uint8_t read_u8(uint32_t address) const noexcept;

    void reset() noexcept;

private:
    [[nodiscard]]
    bool parse_header() noexcept;

    std::vector<std::byte> _rom_data;
    cartridge_header_t    _header{};
    bool                  _loaded{ false };
};

} // namespace clover
```

---

## Design Bias

Clover should be simpler than Carrot.

Do not build an engine inside the emulator.

Do not create a renderer abstraction until there is a real need.

Do not create a platform abstraction beyond what keeps emulator core clean.

Prefer:

```text
simple
explicit
testable
boring
correct
```

over:

```text
generic
abstract
clever
future-proofed
engine-like
```

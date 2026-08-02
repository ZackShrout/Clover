//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/DebugTarget.h"
#include "clover/workbench/AnalysisServices.h"
#include "clover/workbench/InstructionServices.h"
#include "clover/workbench/ToolRegistry.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>

namespace
{
    constexpr clover::frontend::execution_domain_id_t k_main_cpu{ 0x41u };
    constexpr clover::frontend::address_space_id_t k_cpu_bus{ 0x52u };

    class proof_target_t final
        : public clover::frontend::debug_target_t,
          public clover::frontend::execution_control_t
    {
    public:
        proof_target_t()
        {
            _memory[0x100u] = std::byte{ 0x4eu };
            _memory[0x101u] = std::byte{ 0xb9u };
            _memory[0x102u] = std::byte{ 0x00u };
            _memory[0x103u] = std::byte{ 0x00u };
            _memory[0x104u] = std::byte{ 0x02u };
            _memory[0x105u] = std::byte{ 0x00u };
        }

        [[nodiscard]] std::span<const clover::frontend::execution_domain_descriptor_t>
            execution_domains() const noexcept override
        {
            return _domains;
        }

        [[nodiscard]] std::span<const clover::frontend::address_space_descriptor_t>
            address_spaces() const noexcept override
        {
            return _spaces;
        }

        [[nodiscard]] clover::frontend::memory_inspection_result_t inspect_memory(
            clover::frontend::debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept override
        {
            if (address.space != k_cpu_bus)
            {
                return {
                    .status = clover::frontend::memory_inspection_status_t::invalid_address_space
                };
            }
            if (address.value + destination.size() > _memory.size())
            {
                return {
                    .status = clover::frontend::memory_inspection_status_t::out_of_range
                };
            }
            for (size_t index{ 0 }; index < destination.size(); ++index)
                destination[index] = _memory[address.value + index];
            return {
                .status = clover::frontend::memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        [[nodiscard]] clover::frontend::address_translation_result_t
            translate_address(
                clover::frontend::debug_address_t source,
                clover::frontend::address_space_id_t destination_space
            ) const noexcept override
        {
            if (source.space == k_cpu_bus && destination_space == k_cpu_bus)
            {
                return {
                    .status = clover::frontend::address_translation_status_t::complete,
                    .address = source
                };
            }
            return {
                .status = clover::frontend::address_translation_status_t::unsupported
            };
        }

        [[nodiscard]] std::span<const clover::frontend::processor_register_descriptor_t>
            processor_registers(
                clover::frontend::execution_domain_id_t domain
            ) const noexcept override
        {
            return domain == k_main_cpu
                ? std::span<const clover::frontend::processor_register_descriptor_t>{
                    _registers
                }
                : std::span<const clover::frontend::processor_register_descriptor_t>{};
        }

        [[nodiscard]] clover::frontend::processor_state_result_t
            inspect_processor_state(
                clover::frontend::execution_domain_id_t domain,
                std::span<clover::frontend::processor_register_value_t> values
            ) const noexcept override
        {
            if (domain != k_main_cpu)
            {
                return {
                    .status = clover::frontend::processor_state_status_t::invalid_domain,
                    .domain = domain
                };
            }
            if (values.size() < 2u)
            {
                return {
                    .status = clover::frontend::processor_state_status_t::insufficient_storage,
                    .domain = domain
                };
            }
            values[0].value = _pc;
            values[1].value = 0x12345678u;
            return {
                .status = clover::frontend::processor_state_status_t::complete,
                .domain = domain,
                .instruction_address = { k_cpu_bus, _pc },
                .registers_written = 2u
            };
        }

        [[nodiscard]] clover::frontend::execution_control_t*
            execution_control() noexcept override
        {
            return this;
        }

        [[nodiscard]] clover::frontend::execution_step_result_t
            step_execution_domain(
                clover::frontend::execution_domain_id_t domain
            ) noexcept override
        {
            if (domain != k_main_cpu)
            {
                return {
                    .status = clover::frontend::execution_step_status_t::invalid_domain,
                    .domain = domain
                };
            }
            _pc += 6u;
            return {
                .status = clover::frontend::execution_step_status_t::complete,
                .domain = domain,
                .boundary = clover::frontend::execution_boundary_t::instruction,
                .machine_clocks_elapsed = 18u
            };
        }

    private:
        static constexpr std::array _domains{
            clover::frontend::execution_domain_descriptor_t{
                k_main_cpu,
                "proof.m68k",
                "Proof 68000",
                clover::frontend::processor_architecture_t::motorola_68000
            }
        };
        static constexpr std::array _spaces{
            clover::frontend::address_space_descriptor_t{
                k_cpu_bus,
                "proof.cpu-bus",
                "Proof CPU Bus",
                clover::frontend::address_space_kind_t::bus,
                32u,
                0x1000u
            }
        };
        static constexpr std::array _registers{
            clover::frontend::processor_register_descriptor_t{
                "pc", "PC", 32u
            },
            clover::frontend::processor_register_descriptor_t{
                "d0", "D0", 32u
            }
        };
        std::array<std::byte, 0x1000u> _memory{};
        uint64_t _pc{ 0x100u };
    };

    class proof_instruction_services_t final
        : public clover::workbench::instruction_services_t
    {
    public:
        [[nodiscard]] clover::frontend::execution_domain_id_t
            execution_domain() const noexcept override
        {
            return k_main_cpu;
        }

        [[nodiscard]] clover::frontend::address_space_id_t
            instruction_address_space() const noexcept override
        {
            return k_cpu_bus;
        }

        [[nodiscard]] bool valid_instruction_address(
            clover::frontend::debug_address_t address
        ) const noexcept override
        {
            return address.space == k_cpu_bus && address.value < 0x1000u;
        }

        [[nodiscard]] clover::frontend::debug_address_t advance_address(
            clover::frontend::debug_address_t address,
            uint8_t encoded_size
        ) const noexcept override
        {
            address.value += encoded_size;
            return address;
        }

        [[nodiscard]] bool decode_semantics(
            const clover::frontend::debug_target_t& target,
            const clover::workbench::live_processor_state_t& state,
            clover::workbench::instruction_semantics_t& semantics,
            std::string& error
        ) const override
        {
            std::array<std::byte, 2u> opcode{};
            const auto read{ target.inspect_memory(
                state.instruction_address,
                opcode
            ) };
            if (read.status
                    != clover::frontend::memory_inspection_status_t::complete
                || opcode[0] != std::byte{ 0x4eu }
                || opcode[1] != std::byte{ 0xb9u })
            {
                error = "Synthetic instruction is not JSR absolute-long";
                return false;
            }
            semantics = {
                .address = state.instruction_address,
                .encoded_size = 6u,
                .control_flow = clover::workbench::instruction_control_flow_t::call
            };
            error.clear();
            return true;
        }
    };

    class proof_analysis_services_t final
        : public clover::workbench::analysis_services_t
    {
    public:
        [[nodiscard]] clover::frontend::address_space_id_t
            instruction_address_space() const noexcept override
        {
            return k_cpu_bus;
        }

        [[nodiscard]] std::string_view
            instruction_address_space_name() const noexcept override
        {
            return "proof.cpu-bus";
        }

        [[nodiscard]] std::optional<uint64_t>
            default_entry() const override
        {
            return 0x100u;
        }

        [[nodiscard]] clover::workbench::disassembly_listing_t build_listing(
            uint64_t address,
            size_t,
            const clover::workbench::live_processor_state_t&
        ) const override
        {
            return {
                .instructions = {{
                    .address = address,
                    .encoded_size = 6u,
                    .direct_target = 0x200u,
                    .formatted_bytes = "4E B9 00 00 02 00",
                    .formatted_instruction = "JSR $00000200"
                }},
                .next_address = address + 6u
            };
        }

        [[nodiscard]] bool analyze_and_publish(
            clover::workbench::project_t&,
            std::span<const clover::workbench::classification_t>,
            const clover::workbench::debugger_t&,
            clover::workbench::analysis_publication_t&,
            std::string& error
        ) const override
        {
            error = "Proof target intentionally has no analyzer";
            return false;
        }
    };

    [[nodiscard]] bool check(bool condition, const char* message)
    {
        if (!condition)
            std::fprintf(stderr, "%s\n", message);
        return condition;
    }
}

int main()
{
    proof_target_t target{};
    proof_instruction_services_t instructions{};
    proof_analysis_services_t analysis{};
    bool passed{ true };
    passed &= check(
        target.execution_domains().front().architecture
            == clover::frontend::processor_architecture_t::motorola_68000,
        "Second-system processor identity was not preserved"
    );
    passed &= check(
        target.address_spaces().front().address_width_bits == 32u,
        "Second-system address width was not preserved"
    );
    std::array<clover::frontend::processor_register_value_t, 2u> registers{};
    const auto state_result{
        target.inspect_processor_state(k_main_cpu, registers)
    };
    passed &= check(
        state_result.status
            == clover::frontend::processor_state_status_t::complete
            && registers[1].value == 0x12345678u,
        "Architecture-neutral register inspection failed"
    );
    clover::workbench::live_processor_state_t state{
        .instruction_address = state_result.instruction_address,
        .descriptors = {
            target.processor_registers(k_main_cpu).begin(),
            target.processor_registers(k_main_cpu).end()
        },
        .values = { registers.begin(), registers.end() }
    };
    clover::workbench::instruction_semantics_t semantics{};
    std::string error{};
    passed &= check(
        instructions.decode_semantics(target, state, semantics, error)
            && semantics.encoded_size == 6u
            && semantics.control_flow
                == clover::workbench::instruction_control_flow_t::call,
        "Injected non-SNES instruction semantics failed"
    );
    const auto listing{ analysis.build_listing(0x100u, 1u, state) };
    passed &= check(
        analysis.instruction_address_space_name() == "proof.cpu-bus"
            && listing.instructions.size() == 1u
            && listing.instructions.front().encoded_size == 6u
            && listing.instructions.front().formatted_instruction
                == "JSR $00000200",
        "System-neutral non-SNES listing contract failed"
    );
    const auto step{ target.execution_control()->step_execution_domain(
        instructions.execution_domain()
    ) };
    passed &= check(
        step.status == clover::frontend::execution_step_status_t::complete
            && step.machine_clocks_elapsed == 18u,
        "Architecture-neutral execution stepping failed"
    );
    clover::workbench::tool_registry_t tools{};
    passed &= check(
        tools.register_tool({ "proof.vdp", "VDP", "proof-system" })
            && tools.register_command({
                "proof.vdp.toggle",
                "VDP",
                "proof-system",
                clover::workbench::command_activation_t::toggle_tool,
                std::string{ "proof.vdp" }
            }),
        "Second-system tool registration failed"
    );
    clover::workbench::active_tool_t active{};
    passed &= check(
        tools.activate_command("proof.vdp.toggle", active)
            && active.is_active("proof.vdp"),
        "Second-system tool activation failed"
    );
    return passed ? 0 : 1;
}

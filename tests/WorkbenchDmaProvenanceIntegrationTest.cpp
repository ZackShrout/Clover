//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"
#include "clover/frontend/EmulatorCore.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_rom()
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0xeau });
        const uint8_t program[]{
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x21u, 0x21u,      // STA $2121 (CGRAM address)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x00u, 0x43u,      // STA $4300 (mode 0, A -> B)
            0xa9u, 0x22u,             // LDA #$22
            0x8du, 0x01u, 0x43u,      // STA $4301 (CGRAM data)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x02u, 0x43u,      // STA $4302 (source low)
            0xa9u, 0x81u,             // LDA #$81
            0x8du, 0x03u, 0x43u,      // STA $4303 (source high)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x04u, 0x43u,      // STA $4304 (source bank)
            0xa9u, 0x02u,             // LDA #$02
            0x8du, 0x05u, 0x43u,      // STA $4305 (size low)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x06u, 0x43u,      // STA $4306 (size high)
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x0bu, 0x42u,      // STA $420B (start channel 0)

            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x00u, 0x43u,      // STA $4300 (HDMA mode 0, A -> B)
            0xa9u, 0x22u,             // LDA #$22
            0x8du, 0x01u, 0x43u,      // STA $4301 (CGRAM data)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x02u, 0x43u,      // STA $4302 (table low)
            0xa9u, 0x82u,             // LDA #$82
            0x8du, 0x03u, 0x43u,      // STA $4303 (table high)
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x04u, 0x43u,      // STA $4304 (table bank)
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x0cu, 0x42u,      // STA $420C (enable HDMA channel 0)
            0x80u, 0xfeu              // BRA *
        };
        for (size_t index{}; index < std::size(program); ++index)
            rom[index] = static_cast<std::byte>(program[index]);
        rom[0x0100u] = std::byte{ 0x1fu };
        rom[0x0101u] = std::byte{ 0x00u };
        rom[0x0200u] = std::byte{ 0x01u }; // One HDMA line.
        rom[0x0201u] = std::byte{ 0x2au }; // CGRAM payload.
        rom[0x0202u] = std::byte{ 0x00u }; // End table.

        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x02u };
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(
            stderr,
            "WorkbenchDmaProvenanceIntegrationTest failed at %s\n",
            checkpoint
        );
        return 1;
    }
}

int main()
{
    using namespace clover;

    std::unique_ptr<frontend::emulator_core_t> emulator{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    const std::vector<std::byte> rom{ make_rom() };
    if (emulator == nullptr || !emulator->load_media(rom))
        return fail("load");
    auto* const dma_diagnostics{
        dynamic_cast<frontend::snes::dma_transfer_diagnostics_t*>(emulator.get())
    };
    if (dma_diagnostics == nullptr)
        return fail("diagnostics");
    emulator->power_on();
    emulator->run_frame();
    emulator->run_frame();

    std::array<frontend::snes::dma_transfer_record_t, 512u> transfers{};
    const frontend::snes::dma_transfer_inspection_result_t result{
        dma_diagnostics->inspect_dma_transfers(transfers)
    };
    if (result.record_count < 2u || result.records_dropped != 0u)
    {
        std::fprintf(
            stderr,
            "Captured %zu DMA records (%llu dropped)\n",
            result.record_count,
            static_cast<unsigned long long>(result.records_dropped)
        );
        for (size_t index{}; index < result.record_count; ++index)
        {
            const auto& record{ transfers[index] };
            std::fprintf(
                stderr,
                "  %zu: kind=%u initiator=%06X A=%06X-%06X bytes=%u "
                "B=%02X value=%02X-%02X\n",
                index,
                static_cast<unsigned>(record.kind),
                record.initiator_address,
                record.first_a_bus_address,
                record.last_a_bus_address,
                record.byte_count,
                record.b_bus_base,
                record.first_value,
                record.last_value
            );
        }
        return fail("record_count");
    }

    const auto mdma_found{ std::find_if(
        transfers.begin(),
        transfers.begin() + static_cast<std::ptrdiff_t>(result.record_count),
        [](const frontend::snes::dma_transfer_record_t& record)
        {
            return record.kind == frontend::snes::dma_transfer_kind_t::general;
        }
    ) };
    if (mdma_found == transfers.begin()
            + static_cast<std::ptrdiff_t>(result.record_count))
    {
        return fail("mdma_missing");
    }
    const frontend::snes::dma_transfer_record_t& mdma{ *mdma_found };
    if (mdma.kind != frontend::snes::dma_transfer_kind_t::general
        || mdma.channel != 0u
        || mdma.channel_mask != 0x01u
        || mdma.control != 0x00u
        || mdma.initiator_address != 0x00802au
        || mdma.first_a_bus_address != 0x008100u
        || mdma.last_a_bus_address != 0x008101u
        || mdma.byte_count != 2u
        || mdma.b_bus_base != 0x22u
        || mdma.b_bus_offset_mask != 0x01u
        || mdma.first_value != 0x1fu
        || mdma.last_value != 0x00u
        || !mdma.direction_to_b_bus
        || !mdma.b_bus_access_valid)
    {
        return fail("mdma_fields");
    }

    const auto hdma_found{ std::find_if(
        transfers.begin(),
        transfers.begin() + static_cast<std::ptrdiff_t>(result.record_count),
        [](const frontend::snes::dma_transfer_record_t& record)
        {
            return record.kind
                    == frontend::snes::dma_transfer_kind_t::horizontal_blank
                && record.first_a_bus_address == 0x008201u;
        }
    ) };
    if (hdma_found == transfers.begin()
            + static_cast<std::ptrdiff_t>(result.record_count))
    {
        return fail("hdma_missing");
    }
    const frontend::snes::dma_transfer_record_t& hdma{ *hdma_found };
    if (hdma.kind != frontend::snes::dma_transfer_kind_t::horizontal_blank
        || hdma.channel != 0u
        || hdma.channel_mask != 0x01u
        || hdma.control != 0x00u
        || hdma.initiator_address != 0x008048u
        || hdma.first_a_bus_address != 0x008201u
        || hdma.last_a_bus_address != 0x008201u
        || hdma.byte_count != 1u
        || hdma.b_bus_base != 0x22u
        || hdma.b_bus_offset_mask != 0x01u
        || hdma.first_value != 0x2au
        || hdma.last_value != 0x2au
        || !hdma.direction_to_b_bus
        || !hdma.b_bus_access_valid)
    {
        return fail("hdma_fields");
    }

    dma_diagnostics->clear_dma_transfers();
    if (dma_diagnostics->inspect_dma_transfers(transfers).record_count != 0u)
        return fail("clear");

    std::printf(
        "Workbench DMA provenance integration passed: MDMA $00802A -> "
        "$00:8100-$00:8101 -> $2122; HDMA $008048 -> "
        "$00:8201 -> $2122\n"
    );
    return 0;
}

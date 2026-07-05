//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Cartridge.h"
#include "clover/core/Console.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct bringup_summary_t
    {
        uint64_t steps{ 0 };
        uint64_t dma_steps{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t hblank_entries{ 0 };
        uint64_t vblank_entries{ 0 };
        uint64_t nmi_requests{ 0 };
        uint64_t irq_requests{ 0 };
        uint64_t hdma_setup_triggers{ 0 };
        uint64_t hdma_transfer_triggers{ 0 };
    };

    struct cpu_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t state{};
        uint8_t opcode{ 0 };
    };

    struct direct_page_watch_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t cpu{};
        uint8_t opcode{ 0 };
        uint8_t value_00{ 0 };
        uint8_t value_01{ 0 };
        uint8_t value_02{ 0 };
        uint8_t value_03{ 0 };
        uint8_t value_04{ 0 };
        uint8_t value_05{ 0 };
        uint8_t value_59{ 0 };
        uint8_t value_68{ 0 };
        uint8_t value_69{ 0 };
        uint8_t value_6a{ 0 };
        uint8_t value_65{ 0 };
        uint8_t value_66{ 0 };
        uint8_t value_67{ 0 };
        uint32_t pointer_65y{ 0 };
        uint8_t pointer_byte_0{ 0 };
        uint8_t pointer_byte_1{ 0 };
        uint8_t pointer_byte_2{ 0 };
    };

    struct pointer_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_65{ 0 };
        uint8_t old_66{ 0 };
        uint8_t old_67{ 0 };
        uint8_t new_65{ 0 };
        uint8_t new_66{ 0 };
        uint8_t new_67{ 0 };
    };

    struct source_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_68{ 0 };
        uint8_t old_69{ 0 };
        uint8_t old_6a{ 0 };
        uint8_t new_68{ 0 };
        uint8_t new_69{ 0 };
        uint8_t new_6a{ 0 };
    };

    struct lowram_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        clover::core::hardware_slot_owner_t slot_owner{};
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_value{ 0 };
        uint8_t new_value{ 0 };
    };

    struct hot_path_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t active_frame{ 0 };
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t cpu{};
        uint8_t opcode{ 0 };
        uint8_t apu_port_0{ 0 };
        uint8_t apu_port_1{ 0 };
        uint8_t apu_port_2{ 0 };
        uint8_t apu_port_3{ 0 };
        uint8_t dp_00{ 0 };
        uint8_t dp_01{ 0 };
        uint8_t dp_02{ 0 };
        uint8_t dp_03{ 0 };
        uint16_t effective_dp_03_address{ 0 };
        uint8_t effective_dp_03{ 0 };
        uint8_t dp_04{ 0 };
        uint8_t dp_05{ 0 };
        uint8_t dp_65{ 0 };
        uint8_t dp_66{ 0 };
        uint8_t dp_67{ 0 };
        uint8_t dp_68{ 0 };
        uint8_t dp_69{ 0 };
        uint8_t dp_6a{ 0 };
        uint8_t dma_source_bank{ 0 };
        uint32_t dma_source_0d84{ 0 };
        uint32_t dma_source_0d85{ 0 };
        uint32_t dma_source_0d8e{ 0 };
        uint32_t dma_source_0d8f{ 0 };
        uint8_t dma_byte_0d84{ 0 };
        uint8_t dma_byte_0d85{ 0 };
        uint8_t dma_byte_0d8e{ 0 };
        uint8_t dma_byte_0d8f{ 0 };
        uint8_t dma_byte_0d98{ 0 };
        uint8_t dma_byte_0d99{ 0 };
        uint8_t dma_control{ 0 };
        uint8_t dma_bbus{ 0 };
        uint16_t dma_source_address{ 0 };
        uint8_t dma_source_bank_register{ 0 };
        uint16_t dma_transfer_size{ 0 };
    };

    [[nodiscard]] std::string mapping_mode_name(clover::core::cartridge_mapping_mode_t mode)
    {
        using mode_t = clover::core::cartridge_mapping_mode_t;
        switch (mode)
        {
        case mode_t::none:
            return "none";
        case mode_t::bootstrap:
            return "bootstrap";
        case mode_t::lorom:
            return "lorom";
        case mode_t::hirom:
            return "hirom";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] bool is_hot_path_pc(const clover::core::cpu_state_t& cpu) noexcept
    {
        if (cpu.pb != 0x00u)
            return false;

        return (cpu.pc >= 0x809du && cpu.pc <= 0x80a5u)
            || (cpu.pc >= 0x8181u && cpu.pc <= 0x82b6u)
            || (cpu.pc >= 0x85d0u && cpu.pc <= 0x8738u)
            || (cpu.pc >= 0xa32du && cpu.pc <= 0xa386u);
    }

    [[nodiscard]] std::vector<std::byte> read_file_bytes(const std::string& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return {};

        const std::vector<char> raw{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        std::vector<std::byte> bytes(raw.size());
        std::transform(raw.begin(), raw.end(), bytes.begin(), [](char value) noexcept
            {
                return static_cast<std::byte>(static_cast<unsigned char>(value));
            });
        return bytes;
    }

    void print_usage(const char* executable)
    {
        std::fprintf(stderr,
                     "Usage: %s <rom-path> [frames] [step-limit] [dump-dir] [dump-count] [dump-start-frame]\n"
                     "Example: %s roms/local/Super\\ Mario\\ World\\ \\(USA\\).sfc 180 10000000 dumps 3 120\n",
                     executable,
                     executable);
    }

    [[nodiscard]] bool write_framebuffer_ppm(const std::filesystem::path& path,
                                             const clover::core::framebuffer_t& framebuffer)
    {
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output << "P6\n"
               << clover::core::framebuffer_t::k_width << ' ' << clover::core::framebuffer_t::k_height << "\n255\n";

        const uint32_t* const pixels{ framebuffer.data() };
        for (int index{ 0 }; index < clover::core::framebuffer_t::k_pixel_count; ++index)
        {
            const uint32_t rgba8{ pixels[index] };
            const unsigned char red{ static_cast<unsigned char>((rgba8 >> 16u) & 0xffu) };
            const unsigned char green{ static_cast<unsigned char>((rgba8 >> 8u) & 0xffu) };
            const unsigned char blue{ static_cast<unsigned char>(rgba8 & 0xffu) };
            output.write(reinterpret_cast<const char*>(&red), 1);
            output.write(reinterpret_cast<const char*>(&green), 1);
            output.write(reinterpret_cast<const char*>(&blue), 1);
        }

        return static_cast<bool>(output);
    }

    template <typename value_t, size_t size_v>
    [[nodiscard]] bool write_binary_blob(const std::filesystem::path& path,
                                         const std::array<value_t, size_v>& values)
    {
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output.write(reinterpret_cast<const char*>(values.data()),
                     static_cast<std::streamsize>(sizeof(value_t) * values.size()));
        return static_cast<bool>(output);
    }

    void print_cpu_state(const clover::core::cpu_state_t& cpu)
    {
        std::printf("CPU: PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                    cpu.pb,
                    cpu.pc,
                    cpu.a,
                    cpu.x,
                    cpu.y,
                    cpu.sp,
                    cpu.d,
                    cpu.db,
                    cpu.p,
                    cpu.emulation_mode ? 1u : 0u);
    }

    void print_cpu_trace(const std::deque<cpu_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("CPU trace:\n");
        for (const cpu_trace_entry_t& entry : trace)
        {
            std::printf("  step=%llu PB:%02x PC:%04x OP:%02x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.state.pb,
                        entry.state.pc,
                        entry.opcode,
                        entry.state.a,
                        entry.state.x,
                        entry.state.y,
                        entry.state.sp,
                        entry.state.d,
                        entry.state.db,
                        entry.state.p,
                        entry.state.emulation_mode ? 1u : 0u);
        }
    }

    void print_direct_page_watch(const std::deque<direct_page_watch_entry_t>& watch)
    {
        if (watch.empty())
            return;

        std::printf("Direct page watch:\n");
        for (const direct_page_watch_entry_t& entry : watch)
        {
            std::printf("  step=%llu PB:%02x PC:%04x OP:%02x 00:%02x 01:%02x 02:%02x 03:%02x 04:%02x 05:%02x 59:%02x 68:%02x 69:%02x 6a:%02x 65:%02x 66:%02x 67:%02x ptr:%06x [%02x %02x %02x]\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.value_00,
                        entry.value_01,
                        entry.value_02,
                        entry.value_03,
                        entry.value_04,
                        entry.value_05,
                        entry.value_59,
                        entry.value_68,
                        entry.value_69,
                        entry.value_6a,
                        entry.value_65,
                        entry.value_66,
                        entry.value_67,
                        entry.pointer_65y,
                        entry.pointer_byte_0,
                        entry.pointer_byte_1,
                        entry.pointer_byte_2);
        }
    }

    void print_pointer_changes(const std::deque<pointer_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Pointer changes:\n");
        for (const pointer_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x/%02x/%02x -> %02x/%02x/%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_65,
                        entry.old_66,
                        entry.old_67,
                        entry.new_65,
                        entry.new_66,
                        entry.new_67);
        }
    }

    void print_source_changes(const std::deque<source_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Source changes:\n");
        for (const source_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x/%02x/%02x -> %02x/%02x/%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_68,
                        entry.old_69,
                        entry.old_6a,
                        entry.new_68,
                        entry.new_69,
                        entry.new_6a);
        }
    }

    void print_lowram_changes(const std::deque<lowram_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Low WRAM $0003 changes:\n");
        for (const lowram_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu frame=%llu slot=%s scanline=%u dot=%u before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x->%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        entry.slot_owner == clover::core::hardware_slot_owner_t::dma ? "dma" : "cpu",
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_value,
                        entry.new_value);
        }
    }

    void print_hot_path_trace(const std::deque<hot_path_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("Hot path trace:\n");
        for (const hot_path_trace_entry_t& entry : trace)
        {
            std::printf("  step=%llu completed=%llu active_frame=%llu scanline=%u dot=%u PB:%02x PC:%04x OP:%02x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x "
                        "ports=%02x,%02x,%02x,%02x dp00-05=%02x,%02x,%02x,%02x,%02x,%02x effdp03[%04x]=%02x dp65-6a=%02x,%02x,%02x,%02x,%02x,%02x "
                        "dma[ch2 ctl=%02x bbus=%02x src=%02x:%04x size=%04x] "
                        "dma_bank=%02x src[%06x]=%02x src[%06x]=%02x src[%06x]=%02x src[%06x]=%02x src98=%02x src99=%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        static_cast<unsigned long long>(entry.active_frame),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.d,
                        entry.cpu.db,
                        entry.cpu.p,
                        entry.apu_port_0,
                        entry.apu_port_1,
                        entry.apu_port_2,
                        entry.apu_port_3,
                        entry.dp_00,
                        entry.dp_01,
                        entry.dp_02,
                        entry.dp_03,
                        entry.dp_04,
                        entry.dp_05,
                        entry.effective_dp_03_address,
                        entry.effective_dp_03,
                        entry.dp_65,
                        entry.dp_66,
                        entry.dp_67,
                        entry.dp_68,
                        entry.dp_69,
                        entry.dp_6a,
                        entry.dma_control,
                        entry.dma_bbus,
                        entry.dma_source_bank_register,
                        entry.dma_source_address,
                        entry.dma_transfer_size,
                        entry.dma_source_bank,
                        entry.dma_source_0d84,
                        entry.dma_byte_0d84,
                        entry.dma_source_0d85,
                        entry.dma_byte_0d85,
                        entry.dma_source_0d8e,
                        entry.dma_byte_0d8e,
                        entry.dma_source_0d8f,
                        entry.dma_byte_0d8f,
                        entry.dma_byte_0d98,
                        entry.dma_byte_0d99);
        }
    }

    void print_timing(const char* label, const clover::core::timing_snapshot_t& timing)
    {
        std::printf("%s: master=%llu scanline=%u dot=%u hblank=%u vblank=%u\n",
                    label,
                    static_cast<unsigned long long>(timing.master_clock),
                    timing.raster.scanline,
                    timing.raster.dot,
                    timing.in_hblank ? 1u : 0u,
                    timing.in_vblank ? 1u : 0u);
    }

    void print_interrupts(const clover::core::interrupt_state_t& interrupts)
    {
        std::printf("Interrupts: nmi_line=%u nmi_pending=%u irq_line=%u irq_pending=%u irq_lock=%u\n",
                    interrupts.nmi_line ? 1u : 0u,
                    interrupts.nmi_pending ? 1u : 0u,
                    interrupts.irq_line ? 1u : 0u,
                    interrupts.irq_pending ? 1u : 0u,
                    interrupts.irq_lock ? 1u : 0u);
    }

    void print_apu_ports(clover::core::console_t& console)
    {
        std::printf("APU ports: 2140=%02x 2141=%02x 2142=%02x 2143=%02x\n",
                    console.read_u8(0x002140u),
                    console.read_u8(0x002141u),
                    console.read_u8(0x002142u),
                    console.read_u8(0x002143u));
    }

    void print_apu_state(const clover::core::apu_state_t& apu)
    {
        std::printf("APU: PC=%04x A=%02x X=%02x Y=%02x SP=%02x PSW=%02x IPL=%u halted=%u last=%02x waits=%u/%u timers=%u/%u trace=%u io_trace=%u\n",
                    apu.pc,
                    apu.a,
                    apu.x,
                    apu.y,
                    apu.sp,
                    apu.psw,
                    apu.ipl_rom_enabled ? 1u : 0u,
                    apu.halted ? 1u : 0u,
                    apu.last_opcode,
                    apu.external_wait_states,
                    apu.internal_wait_states,
                    apu.timers_enable ? 1u : 0u,
                    apu.timers_disable ? 1u : 0u,
                    apu.instruction_trace_count,
                    apu.io_trace_count);
        std::printf("APU timers: T0 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u | T1 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u | T2 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u\n",
                    apu.timer0.stage0,
                    apu.timer0.stage1,
                    apu.timer0.stage2,
                    apu.timer0.stage3,
                    apu.timer0.line ? 1u : 0u,
                    apu.timer0.enable ? 1u : 0u,
                    apu.timer0.target,
                    apu.timer1.stage0,
                    apu.timer1.stage1,
                    apu.timer1.stage2,
                    apu.timer1.stage3,
                    apu.timer1.line ? 1u : 0u,
                    apu.timer1.enable ? 1u : 0u,
                    apu.timer1.target,
                    apu.timer2.stage0,
                    apu.timer2.stage1,
                    apu.timer2.stage2,
                    apu.timer2.stage3,
                    apu.timer2.line ? 1u : 0u,
                    apu.timer2.enable ? 1u : 0u,
                    apu.timer2.target);
    }

    void print_apu_window(const clover::core::console_t& console, const clover::core::apu_state_t& apu)
    {
        std::printf("APU window:");
        for (int offset{ -8 }; offset <= 8; ++offset)
        {
            const uint16_t address{ static_cast<uint16_t>(apu.pc + offset) };
            std::printf(" %04x=%02x", address, console.apu_peek_ram(address));
        }
        std::printf("\n");
    }

    void print_apu_instruction_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_instruction_trace() };
        const uint8_t trace_count{ console.apu_instruction_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU instruction trace:\n");
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  clk=%llu PC=%04x OP=%02x A=%02x X=%02x Y=%02x SP=%02x PSW=%02x T0=%u/%u ports=%02x,%02x,%02x,%02x\n",
                        static_cast<unsigned long long>(entry.master_clock),
                        entry.pc,
                        entry.opcode,
                        entry.a,
                        entry.x,
                        entry.y,
                        entry.sp,
                        entry.psw,
                        entry.timer0_stage2,
                        entry.timer0_stage3,
                        entry.port0,
                        entry.port1,
                        entry.port2,
                        entry.port3);
        }
    }

    void print_apu_io_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_io_trace() };
        const uint8_t trace_count{ console.apu_io_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU IO trace:\n");
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  clk=%llu %c addr=%04x value=%02x PC=%04x OP=%02x A=%02x X=%02x Y=%02x PSW=%02x\n",
                        static_cast<unsigned long long>(entry.master_clock),
                        entry.is_write ? 'W' : 'R',
                        entry.address,
                        entry.value,
                        entry.pc,
                        entry.opcode,
                        entry.a,
                        entry.x,
                        entry.y,
                        entry.psw);
        }
    }

    void print_direct_page_window(clover::core::console_t& console,
                                  uint16_t start,
                                  uint16_t count)
    {
        std::printf("Direct page:");
        for (uint16_t index{ 0 }; index < count; ++index)
        {
            const uint16_t address{ static_cast<uint16_t>(start + index) };
            std::printf(" %02x:%02x", address & 0x00ffu, console.read_u8(address));
        }
        std::printf("\n");
    }

    void print_wram_window(clover::core::console_t& console,
                           uint32_t start,
                           uint16_t count)
    {
        std::printf("WRAM window:");
        for (uint16_t index{ 0 }; index < count; ++index)
        {
            const uint32_t address{ start + index };
            std::printf(" %06x:%02x", address, console.read_u8(address));
        }
        std::printf("\n");
    }

    void print_ppu_summary(const clover::core::ppu_render_state_snapshot_t& ppu)
    {
        std::printf("PPU: display_disabled=%u brightness=%u bg_mode=%u hires=%u overscan=%u interlace=%u\n",
                    ppu.display_disabled ? 1u : 0u,
                    ppu.brightness,
                    ppu.bg_mode,
                    ppu.hires ? 1u : 0u,
                    ppu.overscan ? 1u : 0u,
                    ppu.interlace ? 1u : 0u);
        std::printf("OBJ: first=%u eval_first=%u eval_count=%u tile_count=%u range_over=%u time_over=%u\n",
                    ppu.objects.first_sprite,
                    ppu.objects.evaluation_first_sprite,
                    ppu.objects.evaluation_count,
                    ppu.objects.tile_count,
                    ppu.objects.range_over ? 1u : 0u,
                    ppu.objects.time_over ? 1u : 0u);
        std::printf("BG1: active=%u tiledata=%04x screen=%04x hoff=%u voff=%u\n",
                    ppu.backgrounds[0].active ? 1u : 0u,
                    ppu.backgrounds[0].tiledata_address,
                    ppu.backgrounds[0].screen_address,
                    ppu.backgrounds[0].hoffset,
                    ppu.backgrounds[0].voffset);
        std::printf("Mode7: A=%04x B=%04x C=%04x D=%04x X=%04x Y=%04x\n",
                    ppu.mode7_a,
                    ppu.mode7_b,
                    ppu.mode7_c,
                    ppu.mode7_d,
                    ppu.mode7_x,
                    ppu.mode7_y);
        if (ppu.display_write_count > 0)
        {
            std::printf("INIDISP writes:\n");
            for (uint8_t index{ 0 }; index < ppu.display_write_count; ++index)
            {
                const auto& write{ ppu.recent_display_writes[index] };
                std::printf("  frame=%llu scanline=%u dot=%u value=%02x disabled=%u brightness=%u\n",
                            static_cast<unsigned long long>(write.frame_index),
                            write.scanline,
                            write.dot,
                            write.value,
                            (write.value & 0x80u) != 0 ? 1u : 0u,
                            write.value & 0x0fu);
            }
        }
    }

    void print_compositor_summary(const clover::core::ppu_compositor_snapshot_t& compositor)
    {
        std::printf("Compositor: above_pri=%u below_pri=%u obj_above_pri=%u obj_below_pri=%u out0=%04x\n",
                    compositor.above.priority,
                    compositor.below.priority,
                    compositor.objects.above.priority,
                    compositor.objects.below.priority,
                    compositor.output_color[0]);
    }

    void print_ppu_register_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.ppu_register_write_trace() };
        const uint8_t trace_count{ console.ppu_register_write_trace_count() };
        bool printed_header{ false };
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            const uint16_t address{ static_cast<uint16_t>(entry.address & 0xffffu) };
            const bool interesting_register{
                address == 0x2100u
                || address == 0x2102u
                || address == 0x2103u
                || address == 0x2104u
                || address == 0x2115u
                || address == 0x2116u
                || address == 0x2117u
                || address == 0x2118u
                || address == 0x2119u
                || address == 0x2121u
                || address == 0x2122u
            };
            if (!interesting_register)
                continue;

            if (!printed_header)
            {
                std::printf("PPU register writes:\n");
                printed_header = true;
            }

            std::printf("  addr=%04x value=%02x scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        address,
                        entry.value,
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }

    void print_watched_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.watched_write_trace() };
        const uint8_t trace_count{ console.watched_write_trace_count() };
        bool printed_header{ false };
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            if (!printed_header)
            {
                std::printf("Watched WRAM writes:\n");
                printed_header = true;
            }

            std::printf("  frame=%llu addr=%06x value=%02x scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        static_cast<unsigned long long>(entry.frame_index),
                        entry.address,
                        entry.value,
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }

    void print_apu_port_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_port_trace() };
        const uint16_t trace_count{ console.apu_port_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU port trace:\n");
        for (uint16_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  %c frame=%llu addr=%04x value=%02x scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        entry.is_write ? 'W' : 'R',
                        static_cast<unsigned long long>(entry.frame_index),
                        static_cast<uint16_t>(entry.address & 0xffffu),
                        entry.value,
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 7)
    {
        print_usage(argv[0]);
        return 1;
    }

    const std::string rom_path{ argv[1] };
    uint64_t target_frames{ 3 };
    if (argc >= 3)
    {
        target_frames = std::strtoull(argv[2], nullptr, 10);
        if (target_frames == 0)
            target_frames = 1;
    }

    uint64_t step_limit{ 2'000'000u };
    if (argc >= 4)
    {
        step_limit = std::strtoull(argv[3], nullptr, 10);
        if (step_limit == 0)
            step_limit = 2'000'000u;
    }

    std::filesystem::path dump_directory{};
    uint64_t dump_count{ 0 };
    uint64_t dump_start_frame{ 1 };
    if (argc >= 5)
        dump_directory = argv[4];

    if (argc >= 6)
        dump_count = std::strtoull(argv[5], nullptr, 10);

    if (argc >= 7)
    {
        dump_start_frame = std::strtoull(argv[6], nullptr, 10);
        if (dump_start_frame == 0)
            dump_start_frame = 1;
    }

    const std::vector<std::byte> rom_bytes{ read_file_bytes(rom_path) };
    if (rom_bytes.empty())
    {
        std::fprintf(stderr, "Failed to read ROM: %s\n", rom_path.c_str());
        return 1;
    }

    clover::core::cartridge_t cartridge_probe{};
    if (!cartridge_probe.load(rom_bytes))
    {
        std::fprintf(stderr, "Cartridge detection failed: %s\n", rom_path.c_str());
        return 1;
    }

    clover::core::console_t console{};
    if (!console.load_cartridge(rom_bytes))
    {
        std::fprintf(stderr, "Console load failed: %s\n", rom_path.c_str());
        return 1;
    }

    console.power_on();
    const bool dump_frames{ !dump_directory.empty() && dump_count > 0 };
    if (dump_frames)
    {
        std::error_code error{};
        std::filesystem::create_directories(dump_directory, error);
        if (error)
        {
            std::fprintf(stderr, "Failed to create dump directory: %s\n", dump_directory.string().c_str());
            return 1;
        }
        console.set_frame_capture_enabled(true);
    }

    bringup_summary_t summary{};
    std::deque<cpu_trace_entry_t> cpu_trace{};
    std::deque<direct_page_watch_entry_t> direct_page_watch{};
    std::deque<pointer_change_entry_t> pointer_changes{};
    std::deque<source_change_entry_t> source_changes{};
    std::deque<lowram_change_entry_t> lowram_03_changes{};
    std::deque<hot_path_trace_entry_t> hot_path_trace{};
    clover::core::cpu_state_t last_recorded_cpu{};
    bool have_last_recorded_cpu{ false };
    direct_page_watch_entry_t last_direct_page_watch{};
    bool have_last_direct_page_watch{ false };
    bool terminal_pc_detected{ false };
    uint64_t dumped_frames{ 0 };
    while (summary.steps < step_limit && summary.frame_completions < target_frames)
    {
        const clover::core::cpu_state_t current_cpu{ console.cpu_state() };
        const uint8_t current_opcode{ console.read_u8((static_cast<uint32_t>(current_cpu.pb) << 16u) | current_cpu.pc) };
        const uint64_t active_frame{ summary.frame_completions + 1u };
        if (is_hot_path_pc(current_cpu))
        {
            const uint8_t dma_source_bank{ static_cast<uint8_t>(current_cpu.y & 0x00ffu) };
            const uint16_t effective_dp_03_address{ static_cast<uint16_t>(current_cpu.d + 0x0003u) };
            const uint32_t dma_source_0d84{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d84u
            };
            const uint32_t dma_source_0d85{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d85u
            };
            const uint32_t dma_source_0d8e{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d8eu
            };
            const uint32_t dma_source_0d8f{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d8fu
            };
            hot_path_trace.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .active_frame = active_frame,
                .timing = console.timing(),
                .cpu = current_cpu,
                .opcode = current_opcode,
                .apu_port_0 = console.read_u8(0x002140u),
                .apu_port_1 = console.read_u8(0x002141u),
                .apu_port_2 = console.read_u8(0x002142u),
                .apu_port_3 = console.read_u8(0x002143u),
                .dp_00 = console.read_u8(0x000000u),
                .dp_01 = console.read_u8(0x000001u),
                .dp_02 = console.read_u8(0x000002u),
                .dp_03 = console.read_u8(0x000003u),
                .effective_dp_03_address = effective_dp_03_address,
                .effective_dp_03 = console.read_u8(effective_dp_03_address),
                .dp_04 = console.read_u8(0x000004u),
                .dp_05 = console.read_u8(0x000005u),
                .dp_65 = console.read_u8(0x000065u),
                .dp_66 = console.read_u8(0x000066u),
                .dp_67 = console.read_u8(0x000067u),
                .dp_68 = console.read_u8(0x000068u),
                .dp_69 = console.read_u8(0x000069u),
                .dp_6a = console.read_u8(0x00006au),
                .dma_source_bank = dma_source_bank,
                .dma_source_0d84 = dma_source_0d84,
                .dma_source_0d85 = dma_source_0d85,
                .dma_source_0d8e = dma_source_0d8e,
                .dma_source_0d8f = dma_source_0d8f,
                .dma_byte_0d84 = console.read_u8(dma_source_0d84),
                .dma_byte_0d85 = console.read_u8(dma_source_0d85),
                .dma_byte_0d8e = console.read_u8(dma_source_0d8e),
                .dma_byte_0d8f = console.read_u8(dma_source_0d8f),
                .dma_byte_0d98 = console.read_u8((static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d98u),
                .dma_byte_0d99 = console.read_u8((static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d99u),
                .dma_control = console.read_u8(0x004320u),
                .dma_bbus = console.read_u8(0x004321u),
                .dma_source_address = static_cast<uint16_t>(
                    console.read_u8(0x004322u) | (console.read_u8(0x004323u) << 8u)
                ),
                .dma_source_bank_register = console.read_u8(0x004324u),
                .dma_transfer_size = static_cast<uint16_t>(
                    console.read_u8(0x004325u) | (console.read_u8(0x004326u) << 8u)
                )
            });
            if (hot_path_trace.size() > 2048)
                hot_path_trace.pop_front();
        }
        if (!have_last_recorded_cpu
            || current_cpu.pc != last_recorded_cpu.pc
            || current_cpu.pb != last_recorded_cpu.pb
            || current_cpu.sp != last_recorded_cpu.sp
            || current_cpu.a != last_recorded_cpu.a
            || current_cpu.x != last_recorded_cpu.x
            || current_cpu.y != last_recorded_cpu.y
            || current_cpu.d != last_recorded_cpu.d
            || current_cpu.db != last_recorded_cpu.db
            || current_cpu.p != last_recorded_cpu.p
            || current_cpu.emulation_mode != last_recorded_cpu.emulation_mode)
        {
            cpu_trace.push_back({
                .hardware_step = summary.steps,
                .state = current_cpu,
                .opcode = current_opcode
            });
            if (cpu_trace.size() > 64)
                cpu_trace.pop_front();
            last_recorded_cpu = current_cpu;
            have_last_recorded_cpu = true;
        }

        const uint8_t value_65{ console.read_u8(0x000065u) };
        const uint8_t value_66{ console.read_u8(0x000066u) };
        const uint8_t value_67{ console.read_u8(0x000067u) };
        const uint8_t value_68{ console.read_u8(0x000068u) };
        const uint8_t value_69{ console.read_u8(0x000069u) };
        const uint8_t value_6a{ console.read_u8(0x00006au) };
        const uint32_t pointer_base_65{
            static_cast<uint32_t>(value_65)
            | (static_cast<uint32_t>(value_66) << 8u)
            | (static_cast<uint32_t>(value_67) << 16u)
        };
        const uint32_t pointer_65y{ (pointer_base_65 + current_cpu.y) & 0x00ffffffu };
        const direct_page_watch_entry_t current_direct_page_watch{
            .hardware_step = summary.steps,
            .cpu = current_cpu,
            .opcode = current_opcode,
            .value_00 = console.read_u8(0x000000u),
            .value_01 = console.read_u8(0x000001u),
            .value_02 = console.read_u8(0x000002u),
            .value_03 = console.read_u8(0x000003u),
            .value_04 = console.read_u8(0x000004u),
            .value_05 = console.read_u8(0x000005u),
            .value_59 = console.read_u8(0x000059u),
            .value_68 = value_68,
            .value_69 = value_69,
            .value_6a = value_6a,
            .value_65 = value_65,
            .value_66 = value_66,
            .value_67 = value_67,
            .pointer_65y = pointer_65y,
            .pointer_byte_0 = console.read_u8(pointer_65y),
            .pointer_byte_1 = console.read_u8((pointer_65y + 1u) & 0x00ffffffu),
            .pointer_byte_2 = console.read_u8((pointer_65y + 2u) & 0x00ffffffu)
        };
        if (!have_last_direct_page_watch
            || current_direct_page_watch.value_00 != last_direct_page_watch.value_00
            || current_direct_page_watch.value_01 != last_direct_page_watch.value_01
            || current_direct_page_watch.value_02 != last_direct_page_watch.value_02
            || current_direct_page_watch.value_03 != last_direct_page_watch.value_03
            || current_direct_page_watch.value_04 != last_direct_page_watch.value_04
            || current_direct_page_watch.value_05 != last_direct_page_watch.value_05
            || current_direct_page_watch.value_59 != last_direct_page_watch.value_59
            || current_direct_page_watch.value_68 != last_direct_page_watch.value_68
            || current_direct_page_watch.value_69 != last_direct_page_watch.value_69
            || current_direct_page_watch.value_6a != last_direct_page_watch.value_6a
            || current_direct_page_watch.value_65 != last_direct_page_watch.value_65
            || current_direct_page_watch.value_66 != last_direct_page_watch.value_66
            || current_direct_page_watch.value_67 != last_direct_page_watch.value_67
            || current_direct_page_watch.pointer_65y != last_direct_page_watch.pointer_65y
            || current_direct_page_watch.pointer_byte_0 != last_direct_page_watch.pointer_byte_0
            || current_direct_page_watch.pointer_byte_1 != last_direct_page_watch.pointer_byte_1
            || current_direct_page_watch.pointer_byte_2 != last_direct_page_watch.pointer_byte_2)
        {
            direct_page_watch.push_back(current_direct_page_watch);
            if (direct_page_watch.size() > 64)
                direct_page_watch.pop_front();
            last_direct_page_watch = current_direct_page_watch;
            have_last_direct_page_watch = true;
        }

        const clover::core::hardware_step_result_t step{ console.step_hardware() };
        ++summary.steps;
        summary.dma_steps += step.slot_owner == clover::core::hardware_slot_owner_t::dma ? 1u : 0u;
        summary.frame_completions += step.ppu.frame_complete ? 1u : 0u;
        summary.hblank_entries += step.ppu.entered_hblank ? 1u : 0u;
        summary.vblank_entries += step.ppu.entered_vblank ? 1u : 0u;
        summary.nmi_requests += step.ppu.nmi_requested ? 1u : 0u;
        summary.irq_requests += step.ppu.irq_requested ? 1u : 0u;
        summary.hdma_setup_triggers += step.ppu.hdma_setup_triggered ? 1u : 0u;
        summary.hdma_transfer_triggers += step.ppu.hdma_transfer_triggered ? 1u : 0u;
        if (step.ppu.frame_complete
            && dump_frames
            && summary.frame_completions >= dump_start_frame
            && dumped_frames < dump_count)
        {
            const std::string frame_basename{ "frame_" + std::to_string(summary.frame_completions) };
            const std::filesystem::path frame_path{
                dump_directory / (frame_basename + ".ppm")
            };
            if (!write_framebuffer_ppm(frame_path, console.framebuffer()))
            {
                std::fprintf(stderr, "Failed to write frame dump: %s\n", frame_path.string().c_str());
                return 1;
            }

            const std::filesystem::path vram_path{ dump_directory / (frame_basename + ".vram.bin") };
            if (!write_binary_blob(vram_path, console.ppu_vram()))
            {
                std::fprintf(stderr, "Failed to write VRAM dump: %s\n", vram_path.string().c_str());
                return 1;
            }

            const std::filesystem::path oam_path{ dump_directory / (frame_basename + ".oam.bin") };
            if (!write_binary_blob(oam_path, console.ppu_oam()))
            {
                std::fprintf(stderr, "Failed to write OAM dump: %s\n", oam_path.string().c_str());
                return 1;
            }

            const std::filesystem::path cgram_path{ dump_directory / (frame_basename + ".cgram.bin") };
            if (!write_binary_blob(cgram_path, console.ppu_cgram()))
            {
                std::fprintf(stderr, "Failed to write CGRAM dump: %s\n", cgram_path.string().c_str());
                return 1;
            }

            ++dumped_frames;
        }

        const uint8_t updated_65{ console.read_u8(0x000065u) };
        const uint8_t updated_66{ console.read_u8(0x000066u) };
        const uint8_t updated_67{ console.read_u8(0x000067u) };
        const uint8_t updated_68{ console.read_u8(0x000068u) };
        const uint8_t updated_69{ console.read_u8(0x000069u) };
        const uint8_t updated_6a{ console.read_u8(0x00006au) };
        const clover::core::cpu_state_t updated_cpu{ console.cpu_state() };
        if (updated_65 != value_65 || updated_66 != value_66 || updated_67 != value_67)
        {
            pointer_changes.push_back({
                .hardware_step = summary.steps,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_65 = value_65,
                .old_66 = value_66,
                .old_67 = value_67,
                .new_65 = updated_65,
                .new_66 = updated_66,
                .new_67 = updated_67
            });
            if (pointer_changes.size() > 64)
                pointer_changes.pop_front();
        }

        if (updated_68 != value_68 || updated_69 != value_69 || updated_6a != value_6a)
        {
            source_changes.push_back({
                .hardware_step = summary.steps,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_68 = value_68,
                .old_69 = value_69,
                .old_6a = value_6a,
                .new_68 = updated_68,
                .new_69 = updated_69,
                .new_6a = updated_6a
            });
            if (source_changes.size() > 64)
                source_changes.pop_front();
        }

        const uint8_t updated_03{ console.read_u8(0x000003u) };
        if (updated_03 != current_direct_page_watch.value_03)
        {
            lowram_03_changes.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .slot_owner = step.slot_owner,
                .timing = step.ppu.timing,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_value = current_direct_page_watch.value_03,
                .new_value = updated_03
            });
            if (lowram_03_changes.size() > 64)
                lowram_03_changes.pop_front();
        }

        const clover::core::cpu_state_t stepped_cpu{ console.cpu_state() };
        if (stepped_cpu.pb == 0x00u && stepped_cpu.pc == 0xffffu)
        {
            terminal_pc_detected = true;
            cpu_trace.push_back({
                .hardware_step = summary.steps,
                .state = stepped_cpu,
                .opcode = console.read_u8(0x00ffffu)
            });
            if (cpu_trace.size() > 64)
                cpu_trace.pop_front();
            break;
        }
    }

    const clover::core::timing_snapshot_t ppu_timing{ console.timing() };
    const clover::core::timing_snapshot_t cpu_timing{ console.cpu_timing() };
    const clover::core::hardware_timing_snapshot_t timing_snapshot{ console.capture_timing_snapshot() };
    const clover::core::ppu_render_state_snapshot_t ppu_state{ console.ppu_render_state() };
    const clover::core::ppu_compositor_snapshot_t compositor_state{ console.ppu_compositor_state() };

    std::printf("ROM: %s\n", rom_path.c_str());
    std::printf("ROM size: %zu bytes\n", rom_bytes.size());
    std::printf("Cartridge: mapping=%s raw_map_mode=%02x reset_vector=%04x\n",
                mapping_mode_name(cartridge_probe.mapping_mode()).c_str(),
                cartridge_probe.header().raw_map_mode,
                cartridge_probe.header().reset_vector);
    std::printf("Run: target_frames=%llu frames_completed=%llu steps=%llu dma_steps=%llu step_limit_hit=%u\n",
                static_cast<unsigned long long>(target_frames),
                static_cast<unsigned long long>(summary.frame_completions),
                static_cast<unsigned long long>(summary.steps),
                static_cast<unsigned long long>(summary.dma_steps),
                summary.steps >= step_limit ? 1u : 0u);
    std::printf("Diagnostics: terminal_pc=%u\n", terminal_pc_detected ? 1u : 0u);
    std::printf("Events: hblank=%llu vblank=%llu nmi=%llu irq=%llu hdma_setup=%llu hdma_transfer=%llu\n",
                static_cast<unsigned long long>(summary.hblank_entries),
                static_cast<unsigned long long>(summary.vblank_entries),
                static_cast<unsigned long long>(summary.nmi_requests),
                static_cast<unsigned long long>(summary.irq_requests),
                static_cast<unsigned long long>(summary.hdma_setup_triggers),
                static_cast<unsigned long long>(summary.hdma_transfer_triggers));

    print_cpu_state(console.cpu_state());
    print_timing("CPU timing", cpu_timing);
    print_timing("PPU timing", ppu_timing);
    print_timing("CPU NMI delay", timing_snapshot.cpu_timing_nmi_delay);
    print_timing("CPU IRQ delay", timing_snapshot.cpu_timing_irq_delay);
    print_interrupts(console.interrupts());
    print_cpu_trace(cpu_trace);
    print_lowram_changes(lowram_03_changes);
    if (terminal_pc_detected)
    {
        print_direct_page_watch(direct_page_watch);
        print_pointer_changes(pointer_changes);
        print_source_changes(source_changes);
        print_direct_page_window(console, 0x0000u, 8u);
    }
    print_hot_path_trace(hot_path_trace);
    print_apu_ports(console);
    print_apu_state(console.apu_state());
    print_apu_window(console, console.apu_state());
    print_apu_instruction_trace(console);
    print_apu_io_trace(console);
    print_apu_port_trace(console);
    std::printf("DMA: activity=%u hdma_pending=%u general_pending=%u open_bus=%02x frame_index=%llu\n",
                static_cast<unsigned>(console.dma_activity()),
                console.hdma_pending() ? 1u : 0u,
                console.general_dma_pending() ? 1u : 0u,
                console.open_bus(),
                static_cast<unsigned long long>(console.frame_index()));
    print_wram_window(console, 0x000000u, 0x06u);
    print_wram_window(console, 0x000d84u, 0x16u);
    print_ppu_summary(ppu_state);
    print_ppu_register_write_trace(console);
    print_watched_write_trace(console);
    print_compositor_summary(compositor_state);
    if (dump_frames)
    {
        std::printf("Frame dumps: directory=%s dumped=%llu start_frame=%llu\n",
                    dump_directory.string().c_str(),
                    static_cast<unsigned long long>(dumped_frames),
                    static_cast<unsigned long long>(dump_start_frame));
    }

    return 0;
}

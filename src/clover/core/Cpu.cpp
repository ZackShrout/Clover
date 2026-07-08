//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Cpu.h"

#include "clover/core/Bus.h"
#include "clover/core/Dma.h"
#include "clover/core/CpuInternal.h"
#include "clover/core/Interrupts.h"
#include "clover/core/Ppu.h"

namespace
{
    constexpr uint16_t k_cpu_hblank_start_dot{ 1096 };

    enum class irq_timer_mode_t : uint8_t
    {
        none,
        h_counter,
        v_counter,
        hv_counter
    };

    [[nodiscard]] bool crossed_irq_point(const clover::core::timing_snapshot_t& previous_timing,
                                         const clover::core::timing_snapshot_t& current_timing,
                                                uint16_t target_scanline,
                                                uint16_t target_dot) noexcept
    {
        if (current_timing.raster.scanline < previous_timing.raster.scanline)
        {
            if (target_scanline >= previous_timing.raster.scanline)
                return true;

            if (target_scanline <= current_timing.raster.scanline && target_dot <= current_timing.raster.dot)
                return true;

            return false;
        }

        if (target_scanline < previous_timing.raster.scanline || target_scanline > current_timing.raster.scanline)
            return false;

        if (target_scanline == previous_timing.raster.scanline && target_dot < previous_timing.raster.dot)
            return false;

        if (target_scanline == current_timing.raster.scanline && target_dot > current_timing.raster.dot)
            return false;

        return previous_timing.master_clock != current_timing.master_clock;
    }

    [[nodiscard]] irq_timer_mode_t irq_timer_mode(const clover::core::cpu_io_t& io) noexcept
    {
        if (io.hirq_enabled && io.virq_enabled)
            return irq_timer_mode_t::hv_counter;

        if (io.hirq_enabled)
            return irq_timer_mode_t::h_counter;

        if (io.virq_enabled)
            return irq_timer_mode_t::v_counter;

        return irq_timer_mode_t::none;
    }

    [[nodiscard]] bool crossed_hirq_point(const clover::core::timing_snapshot_t& previous_timing,
                                          const clover::core::timing_snapshot_t& current_timing,
                                                 uint16_t target_dot) noexcept
    {
        if (crossed_irq_point(previous_timing,
                              current_timing,
                              previous_timing.raster.scanline,
                              target_dot))
        {
            return true;
        }

        if (current_timing.raster.scanline != previous_timing.raster.scanline)
        {
            return crossed_irq_point(previous_timing,
                                     current_timing,
                                     current_timing.raster.scanline,
                                     target_dot);
        }

        return false;
    }

    [[nodiscard]] bool crossed_raster_point(const clover::core::timing_snapshot_t& previous_timing,
                                            const clover::core::timing_snapshot_t& current_timing,
                                            uint16_t target_scanline,
                                            uint16_t target_dot) noexcept
    {
        if (current_timing.raster.scanline < previous_timing.raster.scanline)
        {
            if (target_scanline > previous_timing.raster.scanline)
                return true;

            if (target_scanline == previous_timing.raster.scanline
                && target_dot >= previous_timing.raster.dot)
                return true;

            if (target_scanline < current_timing.raster.scanline)
                return true;

            if (target_scanline == current_timing.raster.scanline && target_dot <= current_timing.raster.dot)
                return true;

            return false;
        }

        if (target_scanline < previous_timing.raster.scanline
            || target_scanline > current_timing.raster.scanline)
            return false;

        if (target_scanline == previous_timing.raster.scanline && target_dot < previous_timing.raster.dot)
            return false;

        if (target_scanline == current_timing.raster.scanline && target_dot > current_timing.raster.dot)
            return false;

        return previous_timing.master_clock != current_timing.master_clock;
    }

    [[nodiscard]] bool crossed_hdma_transfer_point(const clover::core::timing_snapshot_t& previous_timing,
                                                   const clover::core::timing_snapshot_t& current_timing,
                                                   uint16_t visible_scanlines,
                                                   uint16_t target_dot) noexcept
    {
        if (crossed_raster_point(previous_timing,
                                 current_timing,
                                 previous_timing.raster.scanline,
                                 target_dot)
            && previous_timing.raster.scanline < visible_scanlines)
        {
            return true;
        }

        if (current_timing.raster.scanline != previous_timing.raster.scanline)
        {
            return crossed_raster_point(previous_timing,
                                        current_timing,
                                        current_timing.raster.scanline,
                                        target_dot)
                && current_timing.raster.scanline < visible_scanlines;
        }

        return false;
    }

    void decrement_hold(uint8_t& hold_clocks,
                        clover::core::master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (hold_clocks == 0)
            return;

        if (elapsed_master_clocks >= hold_clocks)
        {
            hold_clocks = 0;
            return;
        }

        hold_clocks = static_cast<uint8_t>(hold_clocks - elapsed_master_clocks);
    }

    [[nodiscard]] uint8_t remaining_hold(uint8_t hold_clocks,
                                         clover::core::master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (elapsed_master_clocks >= hold_clocks)
            return 0;

        return static_cast<uint8_t>(hold_clocks - elapsed_master_clocks);
    }

    [[nodiscard]] uint16_t remaining_busy_clocks(uint16_t busy_clocks,
                                                 clover::core::master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (elapsed_master_clocks >= busy_clocks)
            return 0;

        return static_cast<uint16_t>(busy_clocks - elapsed_master_clocks);
    }

    [[nodiscard]] bool cpu_in_hblank(const clover::core::timing_snapshot_t& timing) noexcept
    {
        return timing.raster.dot <= 2u || timing.raster.dot >= k_cpu_hblank_start_dot;
    }

    [[nodiscard]] uint16_t hirq_target_dot(const clover::core::cpu_io_t& io) noexcept
    {
        const uint16_t htime{ static_cast<uint16_t>(io.htime & 0x01ffu) };
        return static_cast<uint16_t>((htime + 1u) << 2u);
    }

    [[nodiscard]] uint16_t scanline_start_dma_phase(const clover::core::timing_snapshot_t& timing) noexcept
    {
        return clover::core::dma_phase_from_master_clock(timing.master_clock - timing.raster.dot);
    }

} // anonymous namespace

namespace clover::core
{
    namespace {
        constexpr uint16_t k_auto_joypad_busy_clocks{ 4224 };
    } // anonymous namespace

    void cpu_t::attach_bus(bus_t& bus) noexcept
    {
        _bus = &bus;
    }

    void cpu_t::attach_interrupt_controller(interrupt_controller_t& interrupts) noexcept
    {
        _interrupts = &interrupts;
    }

    void cpu_t::attach_ppu(ppu_t& ppu) noexcept
    {
        _ppu = &ppu;
    }

    void cpu_t::power_on() noexcept
    {
        _state = {};
        _state.sp = 0x01ffu;
        _state.p = 0x34u;
        _state.emulation_mode = true;
        _io = {};
        _io.in_hblank = true;
        _io.htime = 0x01ffu;
        _io.vtime = 0x01ffu;
        _master_clock = 0;
        _counter.reset();
        _interrupt_poll_phase = 0;
        _visible_scanlines = k_ntsc_video_timing.visible_scanlines;
        _waiting = false;
        _wait_wake_idle_pending = false;
        _stopped = false;
        _interlace = false;
        _last_timing = _counter.snapshot(k_ntsc_video_timing, _visible_scanlines);
        _last_nmi_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 2, _interlace);
        _last_irq_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 10, _interlace);
        _last_irq_gate_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 6, _interlace);
        _irq_condition_valid = false;
        _dma_active = false;
        _dram_refresh_dot = dram_refresh_dot_v2(dma_phase());
        _dram_refresh_pending = true;
        _hdma_setup_dot = hdma_setup_dot_v2(dma_phase());
        _hdma_setup_pending = true;
    }

    void cpu_t::reset() noexcept
    {
        _state.pc = 0;
        _state.sp = 0x01ffu;
        _state.p = 0x34u;
        _state.db = 0;
        _state.pb = 0;
        _state.emulation_mode = true;
        _io = {};
        _io.in_hblank = true;
        _io.htime = 0x01ffu;
        _io.vtime = 0x01ffu;
        _master_clock = 0;
        _counter.reset();
        _interrupt_poll_phase = 0;
        _visible_scanlines = k_ntsc_video_timing.visible_scanlines;
        _waiting = false;
        _wait_wake_idle_pending = false;
        _stopped = false;
        _interlace = false;
        _last_timing = _counter.snapshot(k_ntsc_video_timing, _visible_scanlines);
        _last_nmi_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 2, _interlace);
        _last_irq_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 10, _interlace);
        _last_irq_gate_timing = _counter.snapshot_delayed(k_ntsc_video_timing, _visible_scanlines, 6, _interlace);
        _irq_condition_valid = false;
        _dma_active = false;
        _dram_refresh_dot = dram_refresh_dot_v2(dma_phase());
        _dram_refresh_pending = true;
        _hdma_setup_dot = hdma_setup_dot_v2(dma_phase());
        _hdma_setup_pending = true;
    }

    void cpu_t::load_reset_vector(bus_t& bus) noexcept
    {
        const uint8_t vector_low{ bus.read_u8(0x00fffcu) };
        const uint8_t vector_high{ bus.read_u8(0x00fffdu) };
        _state.pb = 0;
        _state.pc = static_cast<uint16_t>(vector_low | (vector_high << 8u));
    }

    uint8_t cpu_t::dma_phase() const noexcept
    {
        return static_cast<uint8_t>(_master_clock & 7u);
    }

    timing_snapshot_t cpu_t::timing(const video_timing_t& video_timing) const noexcept
    {
        return _counter.snapshot(video_timing, _visible_scanlines);
    }

    timing_snapshot_t cpu_t::delayed_timing(const video_timing_t& video_timing,
                                            master_clock_delta_t delay) const noexcept
    {
        return _counter.snapshot_delayed(video_timing, _visible_scanlines, delay, _interlace);
    }

    uint32_t cpu_t::wram_address() const noexcept
    {
        return _io.wram_address & 0x01ffffu;
    }

    bool cpu_t::irq_condition(const timing_snapshot_t& irq_timing,
                              const timing_snapshot_t& irq_gate_timing) const noexcept
    {
        if (!_io.irq_enabled)
            return false;

        if (irq_gate_timing.raster.scanline == 0 && irq_gate_timing.raster.dot == 0)
            return false;

        if (_io.virq_enabled && irq_timing.raster.scanline != _io.vtime)
            return false;

        if (_io.hirq_enabled && irq_timing.raster.dot != hirq_target_dot(_io))
            return false;

        return true;
    }

    void cpu_t::repoll_irq_on_register_write(interrupt_controller_t& interrupts) noexcept
    {
        if (!_io.irq_enabled)
        {
            interrupts.cancel_irq_delivery();
            _irq_condition_valid = false;
            return;
        }

        const bool current_condition{ irq_condition(_last_irq_timing, _last_irq_gate_timing) };
        if (current_condition && !_irq_condition_valid)
        {
            _io.irq_flag = true;
            _io.irq_hold_clocks = 4;
            interrupts.assert_irq_line();
        }

        _irq_condition_valid = current_condition;
    }

    uint8_t cpu_t::read_register(uint16_t address, master_clock_delta_t elapsed_master_clocks) noexcept
    {
        raster_counter_t timed_counter{ _counter };
        timed_counter.advance(elapsed_master_clocks, k_ntsc_video_timing, _interlace);
        const timing_snapshot_t timed_snapshot{
            timed_counter.snapshot(k_ntsc_video_timing, _visible_scanlines)
        };
        const uint8_t timed_nmi_hold{ remaining_hold(_io.nmi_hold_clocks, elapsed_master_clocks) };
        const uint8_t timed_irq_hold{ remaining_hold(_io.irq_hold_clocks, elapsed_master_clocks) };
        const uint16_t timed_auto_joypad_busy{
            remaining_busy_clocks(_io.auto_joypad_busy_clocks, elapsed_master_clocks)
        };

        switch (address)
        {
        case 0x2180u:
            if (_bus == nullptr)
                return 0;

            return _bus->read_u8(0x7e0000u | (_io.wram_address++ & 0x01ffffu));
        case 0x2181u:
            return static_cast<uint8_t>(_io.wram_address & 0x0000ffu);
        case 0x2182u:
            return static_cast<uint8_t>((_io.wram_address >> 8u) & 0x0000ffu);
        case 0x2183u:
            return static_cast<uint8_t>((_io.wram_address >> 16u) & 0x0001u);
        case 0x4016u:
            return static_cast<uint8_t>(_io.controller_port_1_latch ? 0x01u : 0x00u);
        case 0x4017u:
            return 0x1cu;
        case 0x4200u:
        {
            uint8_t value{ 0 };
            value |= static_cast<uint8_t>(_io.auto_joypad_poll ? 0x01u : 0x00u);
            value |= static_cast<uint8_t>(_io.hirq_enabled ? 0x10u : 0x00u);
            value |= static_cast<uint8_t>(_io.virq_enabled ? 0x20u : 0x00u);
            value |= static_cast<uint8_t>(_io.nmi_enabled ? 0x80u : 0x00u);
            return value;
        }
        case 0x4207u:
            return static_cast<uint8_t>(_io.htime & 0x00ffu);
        case 0x4208u:
            return static_cast<uint8_t>(_io.htime >> 8u);
        case 0x4209u:
            return static_cast<uint8_t>(_io.vtime & 0x00ffu);
        case 0x420au:
            return static_cast<uint8_t>(_io.vtime >> 8u);
        case 0x4210u:
        {
            const uint8_t value{ static_cast<uint8_t>(0x02u | (_io.nmi_flag ? 0x80u : 0x00u)) };
            if (timed_nmi_hold == 0)
            {
                _io.nmi_flag = false;
                _io.nmi_hold_clocks = 0;
                if (_interrupts != nullptr)
                    _interrupts->clear_nmi_line();
            }
            return value;
        }
        case 0x4211u:
        {
            const uint8_t value{ static_cast<uint8_t>(_io.irq_flag ? 0x80u : 0x00u) };
            if (timed_irq_hold == 0)
            {
                _io.irq_flag = false;
                _io.irq_hold_clocks = 0;
                if (_interrupts != nullptr)
                    _interrupts->clear_irq_status_line();
            }
            return value;
        }
        case 0x4212u:
        {
            uint8_t value{ 0 };
            value |= static_cast<uint8_t>(timed_auto_joypad_busy != 0 ? 0x01u : 0x00u);
            value |= static_cast<uint8_t>(cpu_in_hblank(timed_snapshot) ? 0x40u : 0x00u);
            value |= static_cast<uint8_t>(timed_snapshot.in_vblank ? 0x80u : 0x00u);
            return value;
        }
        case 0x4213u:
            return _io.pio;
        case 0x4218u:
            return static_cast<uint8_t>(_io.joy1 & 0x00ffu);
        case 0x4219u:
            return static_cast<uint8_t>(_io.joy1 >> 8u);
        case 0x421au:
            return static_cast<uint8_t>(_io.joy2 & 0x00ffu);
        case 0x421bu:
            return static_cast<uint8_t>(_io.joy2 >> 8u);
        case 0x421cu:
            return static_cast<uint8_t>(_io.joy3 & 0x00ffu);
        case 0x421du:
            return static_cast<uint8_t>(_io.joy3 >> 8u);
        case 0x421eu:
            return static_cast<uint8_t>(_io.joy4 & 0x00ffu);
        case 0x421fu:
            return static_cast<uint8_t>(_io.joy4 >> 8u);
        case 0x420du:
            return static_cast<uint8_t>(_io.fast_rom_enabled ? 0x01u : 0x00u);
        default:
            return 0;
        }
    }

    void cpu_t::write_register(uint16_t address, uint8_t value) noexcept
    {
        switch (address)
        {
        case 0x2180u:
            if (_bus == nullptr)
                return;

            _bus->write_u8(0x7e0000u | (_io.wram_address++ & 0x01ffffu), value);
            return;
        case 0x2181u:
            _io.wram_address = (_io.wram_address & 0x01ff00u) | value;
            return;
        case 0x2182u:
            _io.wram_address = (_io.wram_address & 0x0100ffu) | (static_cast<uint32_t>(value) << 8u);
            return;
        case 0x2183u:
            _io.wram_address = (_io.wram_address & 0x00ffffu) | ((static_cast<uint32_t>(value) & 0x01u) << 16u);
            return;
        case 0x4016u:
            _io.controller_port_1_latch = (value & 0x01u) != 0;
            return;
        case 0x4200u:
            _io.auto_joypad_poll = (value & 0x01u) != 0;
            if (!_io.auto_joypad_poll)
                _io.auto_joypad_busy_clocks = 0;
            if ((value & 0x80u) != 0 && !_io.nmi_enabled && _last_nmi_timing.in_vblank)
            {
                _io.nmi_flag = true;
                _io.nmi_hold_clocks = 4;
                if (_interrupts != nullptr)
                    _interrupts->force_nmi_transition();
            }
            _io.hirq_enabled = (value & 0x10u) != 0;
            _io.virq_enabled = (value & 0x20u) != 0;
            _io.irq_enabled = _io.hirq_enabled || _io.virq_enabled;
            _io.nmi_enabled = (value & 0x80u) != 0;
            if (_interrupts != nullptr)
            {
                if (_io.virq_enabled && !_io.hirq_enabled && _interrupts->sample().irq_line)
                    _interrupts->force_irq_transition();

                if (_io.nmi_enabled && _interrupts->sample().nmi_line)
                    _interrupts->force_nmi_transition();

                repoll_irq_on_register_write(*_interrupts);
                _interrupts->set_irq_lock();
            }
            return;
        case 0x4207u:
            _io.htime = static_cast<uint16_t>((_io.htime & 0x0100u) | value);
            if (_interrupts != nullptr)
                // Timer target writes repoll immediately without the extra
                // $4200-style IRQ lock, matching the bsnes comparison model.
                repoll_irq_on_register_write(*_interrupts);
            return;
        case 0x4208u:
            _io.htime = static_cast<uint16_t>((_io.htime & 0x00ffu) | ((value & 0x01u) << 8u));
            if (_interrupts != nullptr)
                repoll_irq_on_register_write(*_interrupts);
            return;
        case 0x4209u:
            _io.vtime = static_cast<uint16_t>((_io.vtime & 0x0100u) | value);
            if (_interrupts != nullptr)
                repoll_irq_on_register_write(*_interrupts);
            return;
        case 0x420au:
            _io.vtime = static_cast<uint16_t>((_io.vtime & 0x00ffu) | ((value & 0x01u) << 8u));
            if (_interrupts != nullptr)
                repoll_irq_on_register_write(*_interrupts);
            return;
        case 0x4201u:
            if (_ppu != nullptr)
                _ppu->set_external_latch_enabled((value & 0x80u) != 0);
            if ((_io.pio & 0x80u) != 0 && (value & 0x80u) == 0 && _ppu != nullptr)
                _ppu->latch_counters_external();
            _io.pio = value;
            return;
        case 0x420du:
            _io.fast_rom_enabled = (value & 0x01u) != 0;
            return;
        default:
            return;
        }
    }

    hardware_slot_owner_t cpu_t::next_slot_owner(const dma_t& dma) const noexcept
    {
        return _dma_active && dma.has_pending_work()
            ? hardware_slot_owner_t::dma
            : hardware_slot_owner_t::cpu;
    }

    cpu_step_result_t cpu_t::step(bus_t& bus,
                                  const dma_t& dma,
                                  interrupt_controller_t& interrupts) noexcept
    {
        cpu_step_executor_t executor{ bus, interrupts, _io.fast_rom_enabled };

        if (_stopped)
        {
            executor.idle();
            const cpu_step_result_t result{ executor.finish() };
            _master_clock += result.master_clocks;
            return result;
        }

        if (_wait_wake_idle_pending)
        {
            _wait_wake_idle_pending = false;
            executor.idle();
            const cpu_step_result_t result{ executor.finish() };
            _master_clock += result.master_clocks;
            return result;
        }

        if (_waiting)
        {
            const interrupt_state_t interrupt_state{ interrupts.sample() };
            const bool wake_requested{ interrupt_state.nmi_pending
                || interrupt_state.nmi_transition
                || interrupt_state.irq_pending
                || interrupt_state.irq_transition };

            if (!wake_requested)
            {
                executor.idle();
                const cpu_step_result_t result{ executor.finish() };
                _master_clock += result.master_clocks;
                return result;
            }

            _waiting = false;
            _wait_wake_idle_pending = true;
            executor.idle();
            const cpu_step_result_t result{ executor.finish() };
            _master_clock += result.master_clocks;
            return result;
        }

        if (interrupts.irq_lock())
        {
            interrupts.clear_irq_lock();
        }
        else
        {
            if (interrupts.consume_nmi())
            {
                executor.idle();
                enter_interrupt_handler(_state,
                                        executor,
                                        hardware_nmi_vector(_state),
                                        false,
                                        true);

                const cpu_step_result_t result{ executor.finish() };
                _master_clock += result.master_clocks;
                return result;
            }

            if ((_state.p & k_status_irq_disable) == 0
                && interrupts.consume_irq())
            {
                executor.idle();
                enter_interrupt_handler(_state,
                                        executor,
                                        hardware_irq_vector(_state),
                                        false,
                                        true);

                const cpu_step_result_t result{ executor.finish() };
                _master_clock += result.master_clocks;
                return result;
            }
        }

        const uint8_t opcode{ executor.fetch_opcode(_state) };

        if (!execute_load_opcode(opcode, _state, executor)
            && !execute_jump_opcode(opcode, _state, executor)
            && !execute_memory_opcode(opcode, _state, executor)
            && !execute_transfer_opcode(opcode, _state, executor)
            && !execute_stack_opcode(opcode, _state, executor)
            && !execute_alu_opcode(opcode, _state, executor)
            && !execute_modify_opcode(opcode, _state, executor)
            && !execute_system_opcode(opcode, *this, _state, executor)
            && !execute_branch_opcode(opcode, _state, executor))
        {
            // TODO(hardware-timing): This is still a placeholder execution model:
            // one opcode fetch plus one trailing CPU cycle. Zelda/SMW timing work
            // has shown we need real per-opcode CPU timing that stays phase-aligned
            // with bsnes and hardware, so this must be replaced rather than tuned.
            executor.idle();
        }

        if (!executor.retired_instruction())
            executor.retire_instruction();

        const cpu_step_result_t result{ executor.finish() };
        _master_clock += result.master_clocks;
        // DMA requests raised by CPU MMIO writes become visible only after the
        // current opcode retires, so the scheduler hands ownership to DMA on
        // the following hardware slot rather than mid-instruction.
        if (dma.has_pending_work())
            _dma_active = true;
        return result;
    }

    master_clock_delta_t cpu_t::apply_system_timing(master_clock_delta_t elapsed_master_clocks,
                                                    const video_timing_t& video_timing) noexcept
    {
        if (!_dram_refresh_pending || elapsed_master_clocks == 0)
            return elapsed_master_clocks;

        raster_counter_t timed_counter{ _counter };
        timed_counter.advance(elapsed_master_clocks, video_timing, _interlace);
        const timing_snapshot_t current_timing{
            timed_counter.snapshot(video_timing, _visible_scanlines)
        };
        if (!crossed_raster_point(_last_timing,
                                  current_timing,
                                  _last_timing.raster.scanline,
                                  _dram_refresh_dot))
        {
            return elapsed_master_clocks;
        }

        return static_cast<master_clock_delta_t>(
            elapsed_master_clocks + k_cpu_dram_refresh_stall_clocks
        );
    }

    void cpu_t::on_dma_step(const dma_t& dma, interrupt_controller_t& interrupts) noexcept
    {
        interrupts.set_irq_lock();
        _dma_active = dma.has_pending_work();
    }

    void cpu_t::on_ppu_step(master_clock_delta_t elapsed_master_clocks,
                            const video_timing_t& video_timing,
                            const ppu_step_result_t& ppu_step,
                            dma_t& dma,
                            interrupt_controller_t& interrupts) noexcept
    {
        _interrupt_poll_phase = static_cast<master_clock_delta_t>(_interrupt_poll_phase + elapsed_master_clocks);
        _counter.advance(elapsed_master_clocks, video_timing, _interlace);
        _visible_scanlines = ppu_step.visible_scanlines;
        _interlace = ppu_step.interlace;
        const timing_snapshot_t current_timing{ _counter.snapshot(video_timing, _visible_scanlines) };
        const timing_snapshot_t nmi_timing{
            _counter.snapshot_delayed(video_timing, _visible_scanlines, 2, _interlace)
        };
        const timing_snapshot_t irq_timing{
            _counter.snapshot_delayed(video_timing, _visible_scanlines, 10, _interlace)
        };
        const timing_snapshot_t irq_gate_timing{
            _counter.snapshot_delayed(video_timing, _visible_scanlines, 6, _interlace)
        };

        decrement_hold(_io.nmi_hold_clocks, elapsed_master_clocks);
        decrement_hold(_io.irq_hold_clocks, elapsed_master_clocks);

        if (_io.auto_joypad_busy_clocks != 0)
        {
            if (elapsed_master_clocks >= _io.auto_joypad_busy_clocks)
                _io.auto_joypad_busy_clocks = 0;
            else
                _io.auto_joypad_busy_clocks = static_cast<uint16_t>(_io.auto_joypad_busy_clocks - elapsed_master_clocks);
        }

        _io.in_hblank = cpu_in_hblank(current_timing);
        _io.in_vblank = current_timing.in_vblank;

        const bool entered_vblank{
            !_last_timing.in_vblank && current_timing.in_vblank
        };
        const bool exited_vblank_on_nmi_timing{
            _last_nmi_timing.in_vblank && !nmi_timing.in_vblank
        };

        if (ppu_step.entered_scanline)
        {
            const uint16_t dma_phase_at_scanline_start{ scanline_start_dma_phase(current_timing) };
            _dram_refresh_dot = dram_refresh_dot_v2(dma_phase_at_scanline_start);
            _dram_refresh_pending = true;
        }

        if (ppu_step.entered_frame_start)
        {
            const uint16_t dma_phase_at_frame_start{ scanline_start_dma_phase(current_timing) };
            _hdma_setup_dot = hdma_setup_dot_v2(dma_phase_at_frame_start);
            _hdma_setup_pending = true;
        }

        if (exited_vblank_on_nmi_timing)
            interrupts.clear_nmi_line();

        if (!_last_nmi_timing.in_vblank && nmi_timing.in_vblank)
        {
            _io.nmi_flag = true;
            _io.nmi_hold_clocks = 4;
            if (_io.nmi_enabled)
                interrupts.assert_nmi_line();
        }

        if (entered_vblank && _io.auto_joypad_poll)
            _io.auto_joypad_busy_clocks = k_auto_joypad_busy_clocks;

        const bool irq_gate_open{
            irq_gate_timing.raster.scanline != 0 || irq_gate_timing.raster.dot != 0
        };
        const bool irq_condition_now{ irq_condition(irq_timing, irq_gate_timing) };
        bool irq_edge{ false };
        switch (irq_timer_mode(_io))
        {
        case irq_timer_mode_t::none:
            break;
        case irq_timer_mode_t::h_counter:
            irq_edge = _io.irq_enabled
                && irq_gate_open
                && crossed_hirq_point(_last_irq_timing, irq_timing, hirq_target_dot(_io));
            break;
        case irq_timer_mode_t::v_counter:
            irq_edge = _io.irq_enabled
                && irq_gate_open
                && crossed_irq_point(_last_irq_timing, irq_timing, _io.vtime, 0);
            break;
        case irq_timer_mode_t::hv_counter:
            irq_edge = _io.irq_enabled
                && irq_gate_open
                && crossed_irq_point(_last_irq_timing, irq_timing, _io.vtime, hirq_target_dot(_io));
            break;
        }

        if (irq_edge)
        {
            _io.irq_flag = true;
            _io.irq_hold_clocks = 4;
            interrupts.assert_irq_line();
        }
        else
        {
            interrupts.clear_irq_line();
        }

        if (_dram_refresh_pending
            && crossed_raster_point(_last_timing,
                                    current_timing,
                                    _last_timing.raster.scanline,
                                    _dram_refresh_dot))
        {
            _dram_refresh_pending = false;
        }

        if (_hdma_setup_pending
            && crossed_raster_point(_last_timing,
                                    current_timing,
                                    0,
                                    _hdma_setup_dot))
        {
            dma.request_hdma_setup();
            _hdma_setup_pending = false;
        }

        if (crossed_hdma_transfer_point(_last_timing,
                                        current_timing,
                                        _visible_scanlines,
                                        video_timing.hdma_trigger_dot))
            dma.request_hdma_transfer();

        while (_interrupt_poll_phase >= 4)
        {
            interrupts.advance_to_observation_point();
            interrupts.latch_from_lines();
            _interrupt_poll_phase = static_cast<master_clock_delta_t>(_interrupt_poll_phase - 4);
        }

        _last_timing = current_timing;
        _last_nmi_timing = nmi_timing;
        _last_irq_timing = irq_timing;
        _last_irq_gate_timing = irq_gate_timing;
        _irq_condition_valid = irq_condition_now;
    }

    const cpu_state_t& cpu_t::state() const noexcept
    {
        return _state;
    }

    void cpu_t::set_waiting(bool waiting) noexcept
    {
        _waiting = waiting;
    }

    void cpu_t::set_stopped(bool stopped) noexcept
    {
        _stopped = stopped;
    }
}

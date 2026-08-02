//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dma.h"

#include "clover/core/snes/Bus.h"

namespace
{
    constexpr std::array<uint8_t, 8> k_transfer_lengths{ 1, 2, 2, 4, 4, 4, 2, 4 };

    [[nodiscard]] clover::core::dma_channel_t& dma_channel_for_register(
        std::array<clover::core::dma_channel_t, 8>& channels,
        uint16_t address
    ) noexcept
    {
        const uint8_t channel_index{ static_cast<uint8_t>((address - 0x4300u) >> 4u) };
        return channels[channel_index & 7u];
    }

    [[nodiscard]] const clover::core::dma_channel_t& dma_channel_for_register(
        const std::array<clover::core::dma_channel_t, 8>& channels,
        uint16_t address
    ) noexcept
    {
        const uint8_t channel_index{ static_cast<uint8_t>((address - 0x4300u) >> 4u) };
        return channels[channel_index & 7u];
    }

} // anonymous namespace

namespace clover::core
{
    void dma_t::reset() noexcept
    {
        _channels = {};
        _pending_general_dma_mask = 0;
        _pending_hdma_setup_mask = 0;
        _pending_hdma_transfer_mask = 0;
        _activity = dma_activity_t::idle;
        _active_channel_index = 0;
        _substep = dma_substep_t::idle;
        _alignment_pending = false;
        _general_dma_batch_started = false;
        _cpu_bus_cycle_clocks = 6;
        _dma_counter = 0;
        _general_dma_units_remaining = 0;
        _general_dma_transfer_index = 0;
        _hdma_transfer_index = 0;
        _hdma_reload_pending = false;
        _general_dma_suspended = false;
        _suspended_general_dma_channel_index = 0;
        _suspended_general_dma_substep = dma_substep_t::idle;
        _suspended_general_dma_alignment_pending = false;
        _suspended_general_dma_batch_started = false;
        _suspended_general_dma_units_remaining = 0;
        _suspended_general_dma_transfer_index = 0;
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
        clear_provenance_records();
        _general_dma_initiator = {};
        _hdma_initiator = {};
#endif
    }

    void dma_t::request_general_dma() noexcept
    {
        for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
        {
            if (_channels[channel_index].dma_enabled)
                _pending_general_dma_mask |= static_cast<uint8_t>(1u << channel_index);
        }
    }

    void dma_t::request_hdma_setup() noexcept
    {
        for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
        {
            dma_channel_t& channel{ _channels[channel_index] };
            // The per-frame HDMA reset clears completion state even for a
            // channel that is disabled at the setup point. Software may
            // initialize the live HDMA registers and enable that channel
            // later in the same frame.
            channel.hdma_active = false;
            channel.hdma_completed = false;
            channel.hdma_do_transfer = false;
            if (!channel.hdma_enabled)
                continue;

            channel.hdma_active = true;
            channel.hdma_do_transfer = true;
            _pending_hdma_setup_mask |= static_cast<uint8_t>(1u << channel_index);
        }
    }

    void dma_t::request_hdma_transfer() noexcept
    {
        for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
        {
            if (hdma_channel_active(_channels[channel_index]))
                _pending_hdma_transfer_mask |= static_cast<uint8_t>(1u << channel_index);
        }
    }

    bool dma_t::has_pending_work() const noexcept
    {
        return _activity != dma_activity_t::idle
            || _pending_hdma_setup_mask != 0
            || _pending_hdma_transfer_mask != 0
            || _pending_general_dma_mask != 0;
    }

    bool dma_t::general_dma_pending() const noexcept
    {
        return _pending_general_dma_mask != 0 || _activity == dma_activity_t::general_dma;
    }

    bool dma_t::hdma_pending() const noexcept
    {
        return _pending_hdma_setup_mask != 0
            || _pending_hdma_transfer_mask != 0
            || _activity == dma_activity_t::hdma_setup
            || _activity == dma_activity_t::hdma_transfer;
    }

    dma_activity_t dma_t::activity() const noexcept
    {
        return _activity;
    }

    dma_causal_state_t dma_t::capture_causal_state() const noexcept
    {
        return {
            .channels = _channels,
            .pending_general_dma_mask = _pending_general_dma_mask,
            .pending_hdma_setup_mask = _pending_hdma_setup_mask,
            .pending_hdma_transfer_mask = _pending_hdma_transfer_mask,
            .activity = _activity,
            .active_channel_index = _active_channel_index,
            .substep = _substep,
            .alignment_pending = _alignment_pending,
            .general_dma_batch_started = _general_dma_batch_started,
            .cpu_bus_cycle_clocks = _cpu_bus_cycle_clocks,
            .dma_counter = _dma_counter,
            .general_dma_units_remaining = _general_dma_units_remaining,
            .general_dma_transfer_index = _general_dma_transfer_index,
            .hdma_transfer_index = _hdma_transfer_index,
            .hdma_reload_pending = _hdma_reload_pending,
            .general_dma_suspended = _general_dma_suspended,
            .suspended_general_dma_channel_index =
                _suspended_general_dma_channel_index,
            .suspended_general_dma_substep = _suspended_general_dma_substep,
            .suspended_general_dma_alignment_pending =
                _suspended_general_dma_alignment_pending,
            .suspended_general_dma_batch_started =
                _suspended_general_dma_batch_started,
            .suspended_general_dma_units_remaining =
                _suspended_general_dma_units_remaining,
            .suspended_general_dma_transfer_index =
                _suspended_general_dma_transfer_index,
        };
    }

    bool dma_t::restore_causal_state(const dma_causal_state_t& state) noexcept
    {
        const auto valid_activity = [](dma_activity_t activity) noexcept
        {
            switch (activity)
            {
            case dma_activity_t::idle:
            case dma_activity_t::general_dma:
            case dma_activity_t::hdma_setup:
            case dma_activity_t::hdma_transfer:
                return true;
            }
            return false;
        };
        const auto valid_substep = [](dma_substep_t substep) noexcept
        {
            switch (substep)
            {
            case dma_substep_t::idle:
            case dma_substep_t::alignment:
            case dma_substep_t::general_batch_setup:
            case dma_substep_t::hdma_batch_setup:
            case dma_substep_t::general_setup:
            case dma_substep_t::general_transfer:
            case dma_substep_t::finish_sync:
            case dma_substep_t::hdma_reload_line_counter:
            case dma_substep_t::hdma_reload_indirect_low:
            case dma_substep_t::hdma_reload_indirect_high:
            case dma_substep_t::hdma_transfer:
            case dma_substep_t::hdma_advance:
                return true;
            }
            return false;
        };

        if (!valid_activity(state.activity)
            || !valid_substep(state.substep)
            || !valid_substep(state.suspended_general_dma_substep)
            || state.active_channel_index >= state.channels.size()
            || state.suspended_general_dma_channel_index >= state.channels.size()
            || state.cpu_bus_cycle_clocks == 0)
        {
            return false;
        }

        _channels = state.channels;
        _pending_general_dma_mask = state.pending_general_dma_mask;
        _pending_hdma_setup_mask = state.pending_hdma_setup_mask;
        _pending_hdma_transfer_mask = state.pending_hdma_transfer_mask;
        _activity = state.activity;
        _active_channel_index = state.active_channel_index;
        _substep = state.substep;
        _alignment_pending = state.alignment_pending;
        _general_dma_batch_started = state.general_dma_batch_started;
        _cpu_bus_cycle_clocks = state.cpu_bus_cycle_clocks;
        _dma_counter = state.dma_counter;
        _general_dma_units_remaining = state.general_dma_units_remaining;
        _general_dma_transfer_index = state.general_dma_transfer_index;
        _hdma_transfer_index = state.hdma_transfer_index;
        _hdma_reload_pending = state.hdma_reload_pending;
        _general_dma_suspended = state.general_dma_suspended;
        _suspended_general_dma_channel_index =
            state.suspended_general_dma_channel_index;
        _suspended_general_dma_substep = state.suspended_general_dma_substep;
        _suspended_general_dma_alignment_pending =
            state.suspended_general_dma_alignment_pending;
        _suspended_general_dma_batch_started =
            state.suspended_general_dma_batch_started;
        _suspended_general_dma_units_remaining =
            state.suspended_general_dma_units_remaining;
        _suspended_general_dma_transfer_index =
            state.suspended_general_dma_transfer_index;
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
        // Diagnostic history is deliberately outside causal checkpoints. A
        // restore starts a new observation timeline instead of retaining
        // transfers or initiators from the abandoned future.
        clear_provenance_records();
        _general_dma_initiator = {};
        _hdma_initiator = {};
#endif
        return true;
    }

    bool dma_t::hdma_channel_active(const dma_channel_t& channel) noexcept
    {
        return channel.hdma_enabled && !channel.hdma_completed;
    }

    bool dma_t::hdma_later_channel_active() const noexcept
    {
        for (uint8_t channel_index{ static_cast<uint8_t>(_active_channel_index + 1u) };
             channel_index < _channels.size();
             ++channel_index)
        {
            if (hdma_channel_active(_channels[channel_index]))
                return true;
        }

        return false;
    }

    uint8_t dma_t::read_register(uint16_t address) const noexcept
    {
        if (address == 0x420bu)
        {
            uint8_t result{ 0 };
            for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
            {
                if (_channels[channel_index].dma_enabled)
                    result |= static_cast<uint8_t>(1u << channel_index);
            }
            return result;
        }

        if (address == 0x420cu)
        {
            uint8_t result{ 0 };
            for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
            {
                if (_channels[channel_index].hdma_enabled)
                    result |= static_cast<uint8_t>(1u << channel_index);
            }
            return result;
        }

        if (address < 0x4300u || address > 0x437fu)
            return 0;

        const dma_channel_t& channel{ dma_channel_for_register(_channels, address) };
        switch (address & 0x000fu)
        {
        case 0x0u:
            return channel.control;
        case 0x1u:
            return channel.target_address;
        case 0x2u:
            return static_cast<uint8_t>(channel.source_address & 0x00ffu);
        case 0x3u:
            return static_cast<uint8_t>(channel.source_address >> 8u);
        case 0x4u:
            return channel.source_bank;
        case 0x5u:
            return static_cast<uint8_t>(channel.transfer_size & 0x00ffu);
        case 0x6u:
            return static_cast<uint8_t>(channel.transfer_size >> 8u);
        case 0x7u:
            return channel.indirect_bank;
        case 0x8u:
            return static_cast<uint8_t>(channel.hdma_table_address & 0x00ffu);
        case 0x9u:
            return static_cast<uint8_t>(channel.hdma_table_address >> 8u);
        case 0xau:
            return channel.line_counter;
        case 0xbu:
        case 0xfu:
            return channel.unused;
        default:
            return 0;
        }
    }

    void dma_t::write_register(uint16_t address, uint8_t value) noexcept
    {
        if (address == 0x420bu)
        {
            for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
            {
                dma_channel_t& channel{ _channels[channel_index] };
                channel.dma_enabled = (value & static_cast<uint8_t>(1u << channel_index)) != 0;
            }

            request_general_dma();
            return;
        }

        if (address == 0x420cu)
        {
            for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
                _channels[channel_index].hdma_enabled = (value & static_cast<uint8_t>(1u << channel_index)) != 0;
            return;
        }

        if (address < 0x4300u || address > 0x437fu)
            return;

        dma_channel_t& channel{ dma_channel_for_register(_channels, address) };
        switch (address & 0x000fu)
        {
        case 0x0u:
            channel.control = value;
            return;
        case 0x1u:
            channel.target_address = value;
            return;
        case 0x2u:
            channel.source_address = static_cast<uint16_t>((channel.source_address & 0xff00u) | value);
            return;
        case 0x3u:
            channel.source_address = static_cast<uint16_t>((channel.source_address & 0x00ffu) | (value << 8u));
            return;
        case 0x4u:
            channel.source_bank = value;
            return;
        case 0x5u:
            channel.transfer_size = static_cast<uint16_t>((channel.transfer_size & 0xff00u) | value);
            return;
        case 0x6u:
            channel.transfer_size = static_cast<uint16_t>((channel.transfer_size & 0x00ffu) | (value << 8u));
            return;
        case 0x7u:
            channel.indirect_bank = value;
            return;
        case 0x8u:
            channel.hdma_table_address = static_cast<uint16_t>((channel.hdma_table_address & 0xff00u) | value);
            return;
        case 0x9u:
            channel.hdma_table_address = static_cast<uint16_t>(
                (channel.hdma_table_address & 0x00ffu) | (value << 8u)
            );
            return;
        case 0xau:
            channel.line_counter = value;
            return;
        case 0xbu:
        case 0xfu:
            channel.unused = value;
            return;
        default:
            return;
        }
    }

    dma_step_result_t dma_t::step(bus_t& bus,
                                  uint8_t cpu_dma_phase,
                                  master_clock_delta_t cpu_bus_cycle_clocks) noexcept
    {
        _cpu_bus_cycle_clocks = cpu_bus_cycle_clocks;

        if (_activity == dma_activity_t::general_dma
            && (_pending_hdma_setup_mask != 0 || _pending_hdma_transfer_mask != 0))
        {
            suspend_general_dma_for_hdma();
        }

        if (_activity == dma_activity_t::idle)
        {
            if (_pending_hdma_setup_mask != 0)
                begin_hdma_setup();
            else if (_pending_hdma_transfer_mask != 0)
                begin_hdma_transfer();
            else if (_pending_general_dma_mask != 0)
                begin_general_dma();
        }

        if (_activity == dma_activity_t::idle)
            return { .master_clocks = 0 };

        if (_substep == dma_substep_t::alignment)
        {
            _alignment_pending = false;
            _substep = dma_substep_t::idle;
            const master_clock_delta_t k_dma_alignment_clocks{
                cpu_dma_phase == 0 ? 8u : static_cast<master_clock_delta_t>(8u - cpu_dma_phase)
            };
            _dma_counter = k_dma_alignment_clocks;
            if (_activity == dma_activity_t::hdma_setup
                || _activity == dma_activity_t::hdma_transfer)
            {
                _substep = dma_substep_t::hdma_batch_setup;
            }
            else
            {
                prepare_current_channel();
            }
            return {
                .master_clocks = k_dma_alignment_clocks,
                .consumed_alignment = true
            };
        }

        while (true)
        {
            if (_activity == dma_activity_t::idle)
                return { .master_clocks = 0 };

            if (_substep == dma_substep_t::hdma_batch_setup)
            {
                prepare_current_channel();
                _dma_counter = static_cast<master_clock_delta_t>(_dma_counter + 8u);
                return { .master_clocks = 8 };
            }

            if (_substep == dma_substep_t::finish_sync)
            {
                const master_clock_delta_t remainder{
                    static_cast<master_clock_delta_t>(_dma_counter % _cpu_bus_cycle_clocks)
                };
                const master_clock_delta_t clocks{
                    static_cast<master_clock_delta_t>(_cpu_bus_cycle_clocks - remainder)
                };
                _dma_counter = static_cast<master_clock_delta_t>(_dma_counter + clocks);
                _substep = dma_substep_t::idle;
                _activity = dma_activity_t::idle;
                _active_channel_index = 0;
                _alignment_pending = false;
                _general_dma_batch_started = false;
                return { .master_clocks = clocks };
            }

            dma_channel_t& channel{ _channels[_active_channel_index] };
            master_clock_delta_t clocks{ 0 };

            switch (_activity)
            {
            case dma_activity_t::general_dma:
                clocks = run_general_dma(bus, channel);
                break;
            case dma_activity_t::hdma_setup:
                clocks = run_hdma_setup(bus, channel);
                break;
            case dma_activity_t::hdma_transfer:
                clocks = run_hdma_transfer(bus, channel);
                break;
            case dma_activity_t::idle:
            default:
                clocks = 0;
                break;
            }

            if (clocks != 0)
                _dma_counter = static_cast<master_clock_delta_t>(_dma_counter + clocks);

            if (clocks != 0 || _activity == dma_activity_t::idle)
                return { .master_clocks = clocks };
        }
    }

    uint8_t dma_t::first_channel_index(uint8_t channel_mask) noexcept
    {
        for (uint8_t channel_index{ 0 }; channel_index < 8; ++channel_index)
        {
            if ((channel_mask & static_cast<uint8_t>(1u << channel_index)) != 0)
                return channel_index;
        }

        return 0;
    }

    bool dma_t::direction_to_b_bus(const dma_channel_t& channel) noexcept
    {
        return (channel.control & 0x80u) == 0;
    }

    bool dma_t::fixed_transfer(const dma_channel_t& channel) noexcept
    {
        return (channel.control & 0x08u) != 0;
    }

    bool dma_t::reverse_transfer(const dma_channel_t& channel) noexcept
    {
        return (channel.control & 0x10u) != 0;
    }

    bool dma_t::indirect_hdma(const dma_channel_t& channel) noexcept
    {
        return (channel.control & 0x40u) != 0;
    }

    uint8_t dma_t::transfer_mode(const dma_channel_t& channel) noexcept
    {
        return static_cast<uint8_t>(channel.control & 0x07u);
    }

    uint8_t dma_t::transfer_length(const dma_channel_t& channel) noexcept
    {
        return k_transfer_lengths[transfer_mode(channel)];
    }

    uint8_t dma_t::target_address_offset(const dma_channel_t& channel, uint8_t transfer_index) noexcept
    {
        switch (transfer_mode(channel))
        {
        case 1u:
        case 5u:
            return static_cast<uint8_t>(transfer_index & 0x01u);
        case 3u:
        case 7u:
            return static_cast<uint8_t>((transfer_index >> 1u) & 0x01u);
        case 4u:
            return transfer_index;
        default:
            return 0;
        }
    }

    bool dma_t::valid_a_bus_address(uint32_t address) noexcept
    {
        const uint32_t banked_address{ address & 0x00ffffu };
        const uint32_t mirrored_bank{ address & 0x40ffffu };
        return !((mirrored_bank & 0x40ff00u) == 0x002100u
            || (mirrored_bank & 0x40fe00u) == 0x004000u
            || (mirrored_bank & 0x40ffe0u) == 0x004200u
            || (mirrored_bank & 0x40ff80u) == 0x004300u
            || banked_address > 0x00ffffu);
    }

    bool dma_t::valid_b_bus_write(uint32_t source_address, uint8_t target_address) noexcept
    {
        if (target_address != 0x80u)
            return true;

        const bool source_in_wram_bank{ (source_address & 0xfe0000u) == 0x7e0000u };
        const bool source_in_low_wram_mirror{ (source_address & 0x40e000u) == 0x000000u };
        return !source_in_wram_bank && !source_in_low_wram_mirror;
    }

    uint8_t dma_t::read_a_bus(bus_t& bus, uint32_t address) noexcept
    {
        if (!valid_a_bus_address(address))
            return 0;

        return bus.read_u8(address);
    }

    void dma_t::write_a_bus(bus_t& bus, uint32_t address, uint8_t value) noexcept
    {
        if (!valid_a_bus_address(address))
            return;

        bus.write_u8(address, value);
    }

    uint8_t dma_t::read_b_bus(bus_t& bus, uint8_t address, bool valid) noexcept
    {
        return valid ? bus.read_u8(0x00002100u | address) : 0;
    }

    void dma_t::write_b_bus(bus_t& bus, uint8_t address, uint8_t value, bool valid) noexcept
    {
        if (!valid)
            return;

        // A DMA transfer reads its source halfway through the eight-clock
        // unit and exposes the B-bus write at the end of that unit.
        bus.write_cpu_u8(0x00002100u | address, value, 8);
    }

#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
    void dma_t::transfer_byte(bus_t& bus,
#else
    void dma_t::transfer_byte(bus_t& bus,
#endif
                              dma_channel_t& channel,
                              uint32_t source_address,
                              uint8_t transfer_index) noexcept
    {
        const uint8_t target_address{
            static_cast<uint8_t>(channel.target_address + target_address_offset(channel, transfer_index))
        };
        const bool valid_b_write{ valid_b_bus_write(source_address, target_address) };

        if (direction_to_b_bus(channel))
        {
            const uint8_t value{ read_a_bus(bus, source_address) };
            write_b_bus(bus, target_address, value, valid_b_write);
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
            record_transfer_byte(
                bus, channel, source_address, target_address, value,
                valid_b_write
            );
#endif
        }
        else
        {
            const uint8_t value{ read_b_bus(bus, target_address, valid_b_write) };
            write_a_bus(bus, source_address, value);
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
            record_transfer_byte(
                bus, channel, source_address, target_address, value,
                valid_b_write
            );
#endif
        }
    }

#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
    void dma_t::record_control_write(uint16_t address,
                                     uint8_t value,
                                     uint32_t instruction_address,
                                     uint64_t frame_index,
                                     timing_snapshot_t timing) noexcept
    {
        dma_provenance_initiator_t& initiator{
            address == 0x420bu ? _general_dma_initiator : _hdma_initiator
        };
        initiator = {
            .instruction_address = instruction_address & 0x00ffffffu,
            .frame_index = frame_index,
            .timing = timing,
            .channel_mask = value
        };
    }

    void dma_t::record_transfer_byte(bus_t& bus,
                                     const dma_channel_t& channel,
                                     uint32_t source_address,
                                     uint8_t target_address,
                                     uint8_t value,
                                     bool valid_b_access) noexcept
    {
        const dma_provenance_initiator_t& initiator{
            _activity == dma_activity_t::general_dma
                ? _general_dma_initiator
                : _hdma_initiator
        };
        const master_clock_count_t master_clock{
            bus.workbench_dma_master_clock()
        };
        const uint64_t frame_index{ bus.workbench_dma_frame_index() };
        const timing_snapshot_t timing{ bus.workbench_dma_timing() };
        const uint8_t target_offset{
            static_cast<uint8_t>(target_address - channel.target_address)
        };

        const bool continue_record{
            _provenance_open
                && _provenance_records[_open_provenance_index].activity
                    == _activity
                && _provenance_records[_open_provenance_index].channel
                    == _active_channel_index
                && _provenance_records[_open_provenance_index].control
                    == channel.control
                && _provenance_records[_open_provenance_index].initiator_address
                    == initiator.instruction_address
                && (_activity == dma_activity_t::general_dma
                    || _provenance_records[_open_provenance_index]
                           .last_timing.raster.scanline
                        == timing.raster.scanline)
        };

        if (!continue_record)
        {
            size_t index{};
            if (_provenance_count < k_provenance_capacity)
            {
                index = (_provenance_start + _provenance_count)
                    % k_provenance_capacity;
                ++_provenance_count;
            }
            else
            {
                index = _provenance_start;
                _provenance_start = (_provenance_start + 1u)
                    % k_provenance_capacity;
                ++_provenance_dropped;
            }

            _open_provenance_index = index;
            _provenance_open = true;
            _provenance_records[index] = {
                .sequence = _next_provenance_sequence++,
                .first_master_clock = master_clock,
                .last_master_clock = master_clock,
                .frame_index = frame_index,
                .first_timing = timing,
                .last_timing = timing,
                .initiator_address = initiator.instruction_address,
                .first_a_bus_address = source_address & 0x00ffffffu,
                .last_a_bus_address = source_address & 0x00ffffffu,
                .byte_count = 1u,
                .channel = _active_channel_index,
                .channel_mask = initiator.channel_mask,
                .control = channel.control,
                .b_bus_base = channel.target_address,
                .b_bus_offset_mask = static_cast<uint8_t>(
                    target_offset < 8u ? 1u << target_offset : 0u
                ),
                .first_value = value,
                .last_value = value,
                .activity = _activity,
                .direction_to_b_bus = direction_to_b_bus(channel),
                .b_bus_access_valid = valid_b_access
            };
            return;
        }

        dma_provenance_record_t& record{
            _provenance_records[_open_provenance_index]
        };
        record.last_master_clock = master_clock;
        record.last_timing = timing;
        record.last_a_bus_address = source_address & 0x00ffffffu;
        ++record.byte_count;
        if (target_offset < 8u)
        {
            record.b_bus_offset_mask |= static_cast<uint8_t>(
                1u << target_offset
            );
        }
        record.last_value = value;
        record.b_bus_access_valid = record.b_bus_access_valid
            && valid_b_access;
    }

    dma_provenance_snapshot_t dma_t::copy_provenance_records(
        std::span<dma_provenance_record_t> destination
    ) const noexcept
    {
        const size_t count{ std::min(destination.size(), _provenance_count) };
        const size_t skip{ _provenance_count - count };
        for (size_t index{}; index < count; ++index)
        {
            destination[index] = _provenance_records[
                (_provenance_start + skip + index) % k_provenance_capacity
            ];
        }
        return {
            .record_count = count,
            .records_dropped = _provenance_dropped
        };
    }

    void dma_t::clear_provenance_records() noexcept
    {
        _provenance_records = {};
        _provenance_start = 0u;
        _provenance_count = 0u;
        _open_provenance_index = 0u;
        _next_provenance_sequence = 1u;
        _provenance_dropped = 0u;
        _provenance_open = false;
    }
#endif

    uint32_t dma_t::current_general_dma_transfer_count(const dma_channel_t& channel) const noexcept
    {
        return channel.transfer_size == 0 ? 65536u : channel.transfer_size;
    }

    master_clock_delta_t dma_t::run_general_dma(bus_t& bus, dma_channel_t& channel) noexcept
    {
        if (_substep == dma_substep_t::general_batch_setup)
        {
            _general_dma_batch_started = true;
            _substep = dma_substep_t::general_setup;
            return 8;
        }

        if (_substep == dma_substep_t::general_setup)
        {
            _substep = dma_substep_t::general_transfer;
            return 8;
        }

        if (!channel.dma_enabled)
        {
            finish_active_channel();
            return 0;
        }

        if (_general_dma_units_remaining == 0)
        {
            finish_active_channel();
            return 0;
        }

        const uint32_t source_address{
            (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.source_address
        };
        transfer_byte(bus, channel, source_address, _general_dma_transfer_index);

        if (!fixed_transfer(channel))
        {
            if (reverse_transfer(channel))
                --channel.source_address;
            else
                ++channel.source_address;
        }

        --channel.transfer_size;
        --_general_dma_units_remaining;
        _general_dma_transfer_index = static_cast<uint8_t>(_general_dma_transfer_index + 1u);

        if (_general_dma_units_remaining == 0)
            finish_active_channel();

        return 8;
    }

    master_clock_delta_t dma_t::run_hdma_setup(bus_t& bus, dma_channel_t& channel) noexcept
    {
        switch (_substep)
        {
        case dma_substep_t::hdma_reload_line_counter:
        {
            const uint8_t line_counter_data{ read_a_bus(
                bus,
                (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address
            ) };
            if (!_hdma_reload_pending)
            {
                finish_active_channel();
                return 8;
            }

            channel.line_counter = line_counter_data;
            ++channel.hdma_table_address;
            channel.hdma_completed = channel.line_counter == 0;
            channel.hdma_do_transfer = !channel.hdma_completed;
            channel.hdma_active = channel.hdma_enabled && !channel.hdma_completed;
            if (!indirect_hdma(channel))
                finish_active_channel();
            else
                _substep = dma_substep_t::hdma_reload_indirect_low;
            return 8;
        }
        case dma_substep_t::hdma_reload_indirect_low:
            channel.indirect_address = read_a_bus(
                bus,
                (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address
            );
            ++channel.hdma_table_address;
            if (channel.hdma_completed && !hdma_later_channel_active())
                finish_active_channel();
            else
                _substep = dma_substep_t::hdma_reload_indirect_high;
            return 8;
        case dma_substep_t::hdma_reload_indirect_high:
            channel.indirect_address |= static_cast<uint16_t>(
                read_a_bus(bus,
                           (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address) << 8u
            );
            ++channel.hdma_table_address;
            finish_active_channel();
            return 8;
        default:
            finish_active_channel();
            return 0;
        }
    }

    master_clock_delta_t dma_t::run_hdma_transfer(bus_t& bus, dma_channel_t& channel) noexcept
    {
        const bool completing_indirect_reload{
            _substep == dma_substep_t::hdma_reload_indirect_low
                || _substep == dma_substep_t::hdma_reload_indirect_high
        };
        if (!hdma_channel_active(channel) && !completing_indirect_reload)
        {
            channel.hdma_active = false;
            finish_active_channel();
            return 0;
        }

        channel.dma_enabled = false;
        switch (_substep)
        {
        case dma_substep_t::hdma_transfer:
        {
            const uint32_t source_address{
                !indirect_hdma(channel)
                    ? (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address++
                    : (static_cast<uint32_t>(channel.indirect_bank) << 16u) | channel.indirect_address++
            };
            transfer_byte(bus, channel, source_address, _hdma_transfer_index);
            _hdma_transfer_index = static_cast<uint8_t>(_hdma_transfer_index + 1u);
            if (_hdma_transfer_index >= transfer_length(channel))
                _substep = dma_substep_t::hdma_advance;
            return 8;
        }
        case dma_substep_t::hdma_advance:
            --channel.line_counter;
            channel.hdma_do_transfer = (channel.line_counter & 0x80u) != 0;
            schedule_hdma_reload(channel);
            return 0;
        case dma_substep_t::hdma_reload_line_counter:
        case dma_substep_t::hdma_reload_indirect_low:
        case dma_substep_t::hdma_reload_indirect_high:
            return run_hdma_setup(bus, channel);
        default:
            if (channel.hdma_do_transfer)
            {
                _hdma_transfer_index = 0;
                _substep = dma_substep_t::hdma_transfer;
            }
            else
            {
                _substep = dma_substep_t::hdma_advance;
            }
            return 0;
        }
    }

    void dma_t::schedule_hdma_reload(const dma_channel_t& channel) noexcept
    {
        _hdma_reload_pending = (channel.line_counter & 0x7fu) == 0;
        _substep = dma_substep_t::hdma_reload_line_counter;
    }

    void dma_t::advance_to_next_channel() noexcept
    {
        switch (_activity)
        {
        case dma_activity_t::general_dma:
            if (_pending_general_dma_mask != 0)
            {
                _active_channel_index = first_channel_index(_pending_general_dma_mask);
                _pending_general_dma_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
                prepare_current_channel();
                return;
            }
            break;
        case dma_activity_t::hdma_setup:
            if (_pending_hdma_setup_mask != 0)
            {
                _active_channel_index = first_channel_index(_pending_hdma_setup_mask);
                _pending_hdma_setup_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
                prepare_current_channel();
                return;
            }
            break;
        case dma_activity_t::hdma_transfer:
            if (_pending_hdma_transfer_mask != 0)
            {
                _active_channel_index = first_channel_index(_pending_hdma_transfer_mask);
                _pending_hdma_transfer_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
                prepare_current_channel();
                return;
            }
            break;
        case dma_activity_t::idle:
        default:
            break;
        }

        if (_general_dma_suspended
            && (_activity == dma_activity_t::hdma_setup
                || _activity == dma_activity_t::hdma_transfer))
        {
            resume_suspended_general_dma();
            return;
        }

        _substep = dma_substep_t::finish_sync;
    }

    void dma_t::prepare_current_channel() noexcept
    {
        dma_channel_t& channel{ _channels[_active_channel_index] };
        switch (_activity)
        {
        case dma_activity_t::general_dma:
            _general_dma_units_remaining = current_general_dma_transfer_count(channel);
            _general_dma_transfer_index = 0;
            _substep = _general_dma_batch_started
                ? dma_substep_t::general_setup
                : dma_substep_t::general_batch_setup;
            return;
        case dma_activity_t::hdma_setup:
            channel.dma_enabled = false;
            channel.hdma_table_address = channel.source_address;
            channel.line_counter = 0;
            channel.hdma_completed = false;
            channel.hdma_do_transfer = true;
            channel.hdma_active = channel.hdma_enabled;
            _hdma_reload_pending = true;
            _substep = dma_substep_t::hdma_reload_line_counter;
            return;
        case dma_activity_t::hdma_transfer:
            channel.dma_enabled = false;
            if (channel.hdma_do_transfer)
            {
                _hdma_transfer_index = 0;
                _substep = dma_substep_t::hdma_transfer;
            }
            else
            {
                _substep = dma_substep_t::hdma_advance;
            }
            return;
        case dma_activity_t::idle:
        default:
            _substep = dma_substep_t::idle;
            return;
        }
    }

    void dma_t::begin_general_dma() noexcept
    {
        _activity = dma_activity_t::general_dma;
        _general_dma_batch_started = false;
        _active_channel_index = first_channel_index(_pending_general_dma_mask);
        _pending_general_dma_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
        _substep = dma_substep_t::alignment;
        _alignment_pending = true;
    }

    void dma_t::begin_hdma_setup() noexcept
    {
        _activity = dma_activity_t::hdma_setup;
        _active_channel_index = first_channel_index(_pending_hdma_setup_mask);
        _pending_hdma_setup_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
        _substep = dma_substep_t::alignment;
        _alignment_pending = true;
    }

    void dma_t::begin_hdma_transfer() noexcept
    {
        _activity = dma_activity_t::hdma_transfer;
        _active_channel_index = first_channel_index(_pending_hdma_transfer_mask);
        _pending_hdma_transfer_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
        _substep = dma_substep_t::alignment;
        _alignment_pending = true;
    }

    void dma_t::suspend_general_dma_for_hdma() noexcept
    {
        _general_dma_suspended = true;
        _suspended_general_dma_channel_index = _active_channel_index;
        _suspended_general_dma_substep = _substep;
        _suspended_general_dma_alignment_pending = _alignment_pending;
        _suspended_general_dma_batch_started = _general_dma_batch_started;
        _suspended_general_dma_units_remaining = _general_dma_units_remaining;
        _suspended_general_dma_transfer_index = _general_dma_transfer_index;

        if (_pending_hdma_setup_mask != 0)
        {
            _activity = dma_activity_t::hdma_setup;
            _active_channel_index = first_channel_index(_pending_hdma_setup_mask);
            _pending_hdma_setup_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
        }
        else
        {
            _activity = dma_activity_t::hdma_transfer;
            _active_channel_index = first_channel_index(_pending_hdma_transfer_mask);
            _pending_hdma_transfer_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
        }

        // MDMA is already synchronized to the DMA clock. HDMA keeps that
        // phase, pays its ordinary eight-clock batch setup, then resumes MDMA
        // at the following byte boundary.
        _substep = dma_substep_t::hdma_batch_setup;
        _alignment_pending = false;
    }

    void dma_t::resume_suspended_general_dma() noexcept
    {
        _activity = dma_activity_t::general_dma;
        _active_channel_index = _suspended_general_dma_channel_index;
        _substep = _suspended_general_dma_substep;
        _alignment_pending = _suspended_general_dma_alignment_pending;
        _general_dma_batch_started = _suspended_general_dma_batch_started;
        _general_dma_units_remaining = _suspended_general_dma_units_remaining;
        _general_dma_transfer_index = _suspended_general_dma_transfer_index;
        _general_dma_suspended = false;
    }

    void dma_t::finish_active_channel() noexcept
    {
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
        _provenance_open = false;
#endif
        dma_channel_t& channel{ _channels[_active_channel_index] };
        if (_activity == dma_activity_t::general_dma)
        {
            channel.dma_enabled = false;
            if (_pending_general_dma_mask == 0)
            {
                _general_dma_units_remaining = 0;
                _general_dma_transfer_index = 0;
                _hdma_transfer_index = 0;
                _substep = dma_substep_t::finish_sync;
                return;
            }
        }

        if (_activity != dma_activity_t::general_dma && channel.hdma_completed)
            channel.hdma_active = false;

        _general_dma_units_remaining = 0;
        _general_dma_transfer_index = 0;
        _hdma_transfer_index = 0;
        advance_to_next_channel();
    }
}

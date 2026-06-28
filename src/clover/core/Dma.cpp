//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Dma.h"

#include "clover/core/Bus.h"

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
        _general_dma_units_remaining = 0;
        _general_dma_transfer_index = 0;
        _hdma_transfer_index = 0;
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
            if (!channel.hdma_enabled)
                continue;

            channel.hdma_active = true;
            channel.hdma_completed = false;
            channel.hdma_do_transfer = true;
            _pending_hdma_setup_mask |= static_cast<uint8_t>(1u << channel_index);
        }
    }

    void dma_t::request_hdma_transfer() noexcept
    {
        for (uint8_t channel_index{ 0 }; channel_index < _channels.size(); ++channel_index)
        {
            if (_channels[channel_index].hdma_active)
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

    dma_step_result_t dma_t::step(bus_t& bus, uint8_t cpu_dma_phase) noexcept
    {
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
            prepare_current_channel();
            const master_clock_delta_t k_dma_alignment_clocks{
                cpu_dma_phase == 0 ? 8u : static_cast<master_clock_delta_t>(8u - cpu_dma_phase)
            };
            return {
                .master_clocks = k_dma_alignment_clocks,
                .consumed_alignment = true
            };
        }

        while (true)
        {
            if (_activity == dma_activity_t::idle)
                return { .master_clocks = 0 };

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

        bus.write_u8(0x00002100u | address, value);
    }

    void dma_t::transfer_byte(bus_t& bus,
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
        }
        else
        {
            const uint8_t value{ read_b_bus(bus, target_address, valid_b_write) };
            write_a_bus(bus, source_address, value);
        }
    }

    uint32_t dma_t::current_general_dma_transfer_count(const dma_channel_t& channel) const noexcept
    {
        return channel.transfer_size == 0 ? 65536u : channel.transfer_size;
    }

    master_clock_delta_t dma_t::run_general_dma(bus_t& bus, dma_channel_t& channel) noexcept
    {
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
            channel.line_counter = read_a_bus(
                bus,
                (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address
            );
            ++channel.hdma_table_address;
            channel.hdma_completed = channel.line_counter == 0;
            channel.hdma_do_transfer = !channel.hdma_completed;
            channel.hdma_active = channel.hdma_enabled && !channel.hdma_completed;
            if (!indirect_hdma(channel) || channel.hdma_completed)
                finish_active_channel();
            else
                _substep = dma_substep_t::hdma_reload_indirect_low;
            return 8;
        case dma_substep_t::hdma_reload_indirect_low:
            channel.indirect_address = read_a_bus(
                bus,
                (static_cast<uint32_t>(channel.source_bank) << 16u) | channel.hdma_table_address
            );
            ++channel.hdma_table_address;
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
        if (!channel.hdma_active || !channel.hdma_enabled)
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
            if ((channel.line_counter & 0x7fu) == 0)
            {
                schedule_hdma_reload(channel);
            }
            else
            {
                finish_active_channel();
            }
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
        if ((channel.line_counter & 0x7fu) != 0)
        {
            _substep = dma_substep_t::idle;
            return;
        }

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
                _substep = dma_substep_t::alignment;
                _alignment_pending = true;
                return;
            }
            break;
        case dma_activity_t::hdma_setup:
            if (_pending_hdma_setup_mask != 0)
            {
                _active_channel_index = first_channel_index(_pending_hdma_setup_mask);
                _pending_hdma_setup_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
                _substep = dma_substep_t::alignment;
                _alignment_pending = true;
                return;
            }
            break;
        case dma_activity_t::hdma_transfer:
            if (_pending_hdma_transfer_mask != 0)
            {
                _active_channel_index = first_channel_index(_pending_hdma_transfer_mask);
                _pending_hdma_transfer_mask &= static_cast<uint8_t>(~(1u << _active_channel_index));
                _substep = dma_substep_t::alignment;
                _alignment_pending = true;
                return;
            }
            break;
        case dma_activity_t::idle:
        default:
            break;
        }

        _activity = dma_activity_t::idle;
        _active_channel_index = 0;
        _substep = dma_substep_t::idle;
        _alignment_pending = false;
    }

    void dma_t::prepare_current_channel() noexcept
    {
        dma_channel_t& channel{ _channels[_active_channel_index] };
        switch (_activity)
        {
        case dma_activity_t::general_dma:
            _general_dma_units_remaining = current_general_dma_transfer_count(channel);
            _general_dma_transfer_index = 0;
            _substep = dma_substep_t::general_transfer;
            return;
        case dma_activity_t::hdma_setup:
            channel.dma_enabled = false;
            channel.hdma_table_address = channel.source_address;
            channel.line_counter = 0;
            channel.hdma_completed = false;
            channel.hdma_do_transfer = true;
            channel.hdma_active = channel.hdma_enabled;
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

    void dma_t::finish_active_channel() noexcept
    {
        dma_channel_t& channel{ _channels[_active_channel_index] };
        if (_activity == dma_activity_t::general_dma)
        {
            channel.dma_enabled = false;
        }

        if (_activity != dma_activity_t::general_dma && channel.hdma_completed)
            channel.hdma_active = false;

        _general_dma_units_remaining = 0;
        _general_dma_transfer_index = 0;
        _hdma_transfer_index = 0;
        advance_to_next_channel();
    }
}

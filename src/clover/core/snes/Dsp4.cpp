//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dsp4.h"
#include "clover/core/snes/CausalStateArchive.h"

#include <cstdint>
#include <cstring>

namespace clover::core::dsp4_legacy
{
    using bool8 = uint8_t;
    using int8 = int8_t;
    using int16 = int16_t;
    using int32 = int32_t;
    using int64 = int64_t;
    using uint8 = uint8_t;
    using uint16 = uint16_t;
    using uint32 = uint32_t;
    using uint64 = uint64_t;

    [[nodiscard]] inline uint16 read_word(const uint8* address) noexcept
    {
        return static_cast<uint16>(
            address[0] | (static_cast<uint16>(address[1]) << 8u)
        );
    }

    [[nodiscard]] inline uint32 read_dword(const uint8* address) noexcept
    {
        return static_cast<uint32>(
            address[0]
            | (static_cast<uint32>(address[1]) << 8u)
            | (static_cast<uint32>(address[2]) << 16u)
            | (static_cast<uint32>(address[3]) << 24u)
        );
    }

    inline void write_word(uint8* address, uint16 value) noexcept
    {
        address[0] = static_cast<uint8>(value);
        address[1] = static_cast<uint8>(value >> 8u);
    }

#define READ_WORD(address) read_word(address)
#define READ_DWORD(address) read_dword(address)
#define WRITE_WORD(address, value) write_word(address, value)
#include "clover/core/snes/Dsp4Legacy.inc"
#undef WRITE_WORD
#undef READ_DWORD
#undef READ_WORD
}

namespace clover::core
{
    struct dsp4_t::state_t
    {
        dsp4_legacy::DSP4_t protocol{};
        dsp4_legacy::DSP4_vars_t variables{};
    };

    namespace
    {
        template <typename archive_t>
        void dsp4_protocol_fields(archive_t& archive, dsp4_legacy::DSP4_t& value)
        {
            archive.field(value.waiting4command);
            archive.field(value.half_command);
            archive.field(value.command);
            archive.field(value.in_count);
            archive.field(value.in_index);
            archive.field(value.out_count);
            archive.field(value.out_index);
            archive.field(value.parameters);
            archive.field(value.output);
        }

        template <typename archive_t>
        void dsp4_variable_fields(archive_t& archive, dsp4_legacy::DSP4_vars_t& value)
        {
            archive.field(value.DSP4_Logic);
            archive.field(value.lcv); archive.field(value.distance);
            archive.field(value.raster); archive.field(value.segments);
            archive.field(value.world_x); archive.field(value.world_y);
            archive.field(value.world_dx); archive.field(value.world_dy);
            archive.field(value.world_ddx); archive.field(value.world_ddy);
            archive.field(value.world_xenv); archive.field(value.world_yofs);
            archive.field(value.view_x1); archive.field(value.view_y1);
            archive.field(value.view_x2); archive.field(value.view_y2);
            archive.field(value.view_dx); archive.field(value.view_dy);
            archive.field(value.view_xofs1); archive.field(value.view_yofs1);
            archive.field(value.view_xofs2); archive.field(value.view_yofs2);
            archive.field(value.view_yofsenv);
            archive.field(value.view_turnoff_x); archive.field(value.view_turnoff_dx);
            archive.field(value.viewport_cx); archive.field(value.viewport_cy);
            archive.field(value.viewport_left); archive.field(value.viewport_right);
            archive.field(value.viewport_top); archive.field(value.viewport_bottom);
            archive.field(value.sprite_x); archive.field(value.sprite_y);
            archive.field(value.sprite_attr); archive.field(value.sprite_size);
            archive.field(value.sprite_clipy); archive.field(value.sprite_count);
            archive.field(value.poly_clipLf); archive.field(value.poly_clipRt);
            archive.field(value.poly_ptr); archive.field(value.poly_raster);
            archive.field(value.poly_top); archive.field(value.poly_bottom);
            archive.field(value.poly_cx); archive.field(value.poly_start);
            archive.field(value.poly_plane);
            archive.field(value.OAM_attr); archive.field(value.OAM_index);
            archive.field(value.OAM_bits); archive.field(value.OAM_RowMax);
            archive.field(value.OAM_Row);
        }
    }

    dsp4_t::dsp4_t()
        : _state{ std::make_unique<state_t>() }
    {
    }

    dsp4_t::~dsp4_t() = default;
    dsp4_t::dsp4_t(dsp4_t&&) noexcept = default;
    dsp4_t& dsp4_t::operator=(dsp4_t&&) noexcept = default;

    void dsp4_t::power_on() noexcept
    {
        _state->protocol = {};
        _state->variables = {};
        dsp4_legacy::BindDSP4State(&_state->protocol, &_state->variables);
        dsp4_legacy::InitDSP4();
    }

    uint8_t dsp4_t::read_data() noexcept
    {
        dsp4_legacy::BindDSP4State(&_state->protocol, &_state->variables);
        return dsp4_legacy::DSP4GetByte();
    }

    uint8_t dsp4_t::read_status() noexcept
    {
        return 0x80u;
    }

    void dsp4_t::write_data(uint8_t value) noexcept
    {
        dsp4_legacy::BindDSP4State(&_state->protocol, &_state->variables);
        dsp4_legacy::DSP4SetByte(value);
    }

    bool dsp4_t::capture_causal_state(std::vector<std::byte>& state) const noexcept
    {
        try
        {
            causal_state_writer_t archive{};
            archive.field(uint32_t{ 1 });
            dsp4_protocol_fields(archive, _state->protocol);
            dsp4_variable_fields(archive, _state->variables);
            state = std::move(archive).finish();
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool dsp4_t::restore_causal_state(std::span<const std::byte> state) noexcept
    {
        try
        {
            dsp4_t candidate{};
            causal_state_reader_t archive{ state };
            uint32_t version{};
            archive.field(version);
            dsp4_protocol_fields(archive, candidate._state->protocol);
            dsp4_variable_fields(archive, candidate._state->variables);
            const auto& protocol{ candidate._state->protocol };
            if (version != 1u
                || protocol.in_count > sizeof(protocol.parameters)
                || protocol.in_index > sizeof(protocol.parameters)
                || protocol.out_count > sizeof(protocol.output)
                || protocol.out_index > sizeof(protocol.output)
                || !archive.complete())
            {
                return false;
            }
            _state.swap(candidate._state);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}

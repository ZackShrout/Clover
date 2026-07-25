//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Cpu.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace clover::core
{
    using snes_observation_mask_t = uint64_t;

    inline constexpr snes_observation_mask_t k_snes_observe_cpu_boundary{ 1u << 0u };
    inline constexpr snes_observation_mask_t k_snes_observation_mask_all{
        k_snes_observe_cpu_boundary
    };

    enum class snes_observation_kind_t : uint8_t
    {
        cpu_boundary
    };

    struct snes_cpu_boundary_observation_t
    {
        cpu_step_boundary_t boundary{ cpu_step_boundary_t::none };
        cpu_state_t state_before{};
        cpu_state_t state_after{};
    };

    struct snes_observation_event_t
    {
        snes_observation_kind_t kind{ snes_observation_kind_t::cpu_boundary };
        master_clock_count_t master_clock{ 0 };
        uint64_t frame_index{ 0 };
        timing_snapshot_t timing{};
        snes_cpu_boundary_observation_t cpu_boundary{};
    };

    struct snes_observation_sink_t
    {
    public:
        void configure(std::span<snes_observation_event_t> storage,
                       snes_observation_mask_t mask) noexcept
        {
            _storage = storage;
            _mask = mask & k_snes_observation_mask_all;
            clear();
        }

        void disable() noexcept
        {
            _storage = {};
            _mask = 0;
            clear();
        }

        [[nodiscard]] bool enabled(snes_observation_mask_t event_mask) const noexcept
        {
            return (_mask & event_mask) != 0u;
        }

        void push(const snes_observation_event_t& event) noexcept
        {
            if (_count < _storage.size())
            {
                _storage[_count++] = event;
                return;
            }
            ++_dropped;
        }

        [[nodiscard]] std::span<const snes_observation_event_t> events() const noexcept
        {
            return _storage.first(_count);
        }

        void discard(size_t count) noexcept
        {
            if (count >= _count)
            {
                _count = 0;
                return;
            }

            const size_t remaining{ _count - count };
            for (size_t index{ 0 }; index < remaining; ++index)
                _storage[index] = _storage[count + index];
            _count = remaining;
        }

        [[nodiscard]] uint64_t take_dropped() noexcept
        {
            const uint64_t result{ _dropped };
            _dropped = 0;
            return result;
        }

        void clear() noexcept
        {
            _count = 0;
            _dropped = 0;
        }

    private:
        std::span<snes_observation_event_t> _storage{};
        snes_observation_mask_t _mask{ 0 };
        size_t _count{ 0 };
        uint64_t _dropped{ 0 };
    };
}

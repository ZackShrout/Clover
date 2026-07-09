//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"

#include <array>
#include <cstdint>

namespace clover::core
{
    struct bus_t;
    struct ppu_step_result_t;

    enum class dma_activity_t : uint8_t
    {
        idle,
        general_dma,
        hdma_setup,
        hdma_transfer
    };

    enum class dma_substep_t : uint8_t
    {
        idle,
        alignment,
        general_batch_setup,
        general_setup,
        general_transfer,
        general_finish_sync,
        hdma_reload_line_counter,
        hdma_reload_indirect_low,
        hdma_reload_indirect_high,
        hdma_transfer,
        hdma_advance
    };

    struct dma_channel_t
    {
        bool dma_enabled{ false };
        bool hdma_enabled{ false };
        bool hdma_active{ false };
        bool hdma_completed{ false };
        bool hdma_do_transfer{ false };
        uint8_t control{ 0xff };
        uint8_t target_address{ 0xff };
        uint16_t source_address{ 0xffffu };
        uint8_t source_bank{ 0xff };
        uint8_t indirect_bank{ 0xff };
        uint16_t indirect_address{ 0xffffu };
        uint16_t hdma_table_address{ 0xffffu };
        uint8_t line_counter{ 0xff };
        uint8_t unused{ 0xff };
        uint8_t transfer_units{ 0 };
        uint16_t transfer_size{ 0xffffu };
    };

    struct dma_t
    {
    public:
        void reset() noexcept;
        void request_general_dma() noexcept;
        void request_hdma_setup() noexcept;
        void request_hdma_transfer() noexcept;
        [[nodiscard]] bool has_pending_work() const noexcept;
        [[nodiscard]] dma_step_result_t step(bus_t& bus, uint8_t cpu_dma_phase) noexcept;
        [[nodiscard]] uint8_t read_register(uint16_t address) const noexcept;
        void write_register(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] bool general_dma_pending() const noexcept;
        [[nodiscard]] bool hdma_pending() const noexcept;
        [[nodiscard]] dma_activity_t activity() const noexcept;

    private:
        [[nodiscard]] static uint8_t first_channel_index(uint8_t channel_mask) noexcept;
        [[nodiscard]] static bool direction_to_b_bus(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static bool fixed_transfer(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static bool reverse_transfer(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static bool indirect_hdma(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static bool hdma_channel_active(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static uint8_t transfer_mode(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static uint8_t transfer_length(const dma_channel_t& channel) noexcept;
        [[nodiscard]] static uint8_t target_address_offset(const dma_channel_t& channel,
                                                           uint8_t transfer_index) noexcept;
        [[nodiscard]] static bool valid_a_bus_address(uint32_t address) noexcept;
        [[nodiscard]] static bool valid_b_bus_write(uint32_t source_address, uint8_t target_address) noexcept;
        [[nodiscard]] static uint8_t read_a_bus(bus_t& bus, uint32_t address) noexcept;
        static void write_a_bus(bus_t& bus, uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] static uint8_t read_b_bus(bus_t& bus, uint8_t address, bool valid) noexcept;
        static void write_b_bus(bus_t& bus, uint8_t address, uint8_t value, bool valid) noexcept;
        static void transfer_byte(bus_t& bus,
                                  dma_channel_t& channel,
                                  uint32_t source_address,
                                  uint8_t transfer_index) noexcept;
        [[nodiscard]] uint32_t current_general_dma_transfer_count(const dma_channel_t& channel) const noexcept;
        [[nodiscard]] master_clock_delta_t run_general_dma(bus_t& bus, dma_channel_t& channel) noexcept;
        [[nodiscard]] master_clock_delta_t run_hdma_setup(bus_t& bus, dma_channel_t& channel) noexcept;
        [[nodiscard]] master_clock_delta_t run_hdma_transfer(bus_t& bus, dma_channel_t& channel) noexcept;
        void schedule_hdma_reload(const dma_channel_t& channel) noexcept;
        void advance_to_next_channel() noexcept;
        void prepare_current_channel() noexcept;
        void begin_general_dma() noexcept;
        void begin_hdma_setup() noexcept;
        void begin_hdma_transfer() noexcept;
        void finish_active_channel() noexcept;

        std::array<dma_channel_t, 8> _channels{};
        uint8_t _pending_general_dma_mask{ 0 };
        uint8_t _pending_hdma_setup_mask{ 0 };
        uint8_t _pending_hdma_transfer_mask{ 0 };
        dma_activity_t _activity{ dma_activity_t::idle };
        uint8_t _active_channel_index{ 0 };
        dma_substep_t _substep{ dma_substep_t::idle };
        bool _alignment_pending{ false };
        bool _general_dma_batch_started{ false };
        uint32_t _general_dma_units_remaining{ 0 };
        uint8_t _general_dma_transfer_index{ 0 };
        uint8_t _hdma_transfer_index{ 0 };
    };
}

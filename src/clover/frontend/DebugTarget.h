//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace clover::frontend
{
    using execution_domain_id_t = uint32_t;
    using address_space_id_t = uint32_t;

    enum class processor_architecture_t : uint8_t
    {
        unknown,
        wdc_65c816,
        sony_spc700
    };

    enum class address_space_kind_t : uint8_t
    {
        bus,
        memory,
        canonical_media
    };

    struct execution_domain_descriptor_t
    {
        execution_domain_id_t id{ 0 };
        std::string_view stable_id{};
        std::string_view label{};
        processor_architecture_t architecture{ processor_architecture_t::unknown };
    };

    struct address_space_descriptor_t
    {
        address_space_id_t id{ 0 };
        std::string_view stable_id{};
        std::string_view label{};
        address_space_kind_t kind{ address_space_kind_t::memory };
        uint8_t address_width_bits{ 0 };
        uint64_t size_bytes{ 0 };
    };

    struct debug_address_t
    {
        address_space_id_t space{ 0 };
        uint64_t value{ 0 };
    };

    enum class memory_inspection_status_t : uint8_t
    {
        complete,
        invalid_address_space,
        out_of_range,
        unavailable
    };

    struct memory_inspection_result_t
    {
        memory_inspection_status_t status{ memory_inspection_status_t::unavailable };
        size_t bytes_read{ 0 };
    };

    enum class address_translation_status_t : uint8_t
    {
        complete,
        invalid_address_space,
        unmapped,
        unsupported
    };

    struct address_translation_result_t
    {
        address_translation_status_t status{ address_translation_status_t::unsupported };
        debug_address_t address{};
    };

    enum class execution_step_status_t : uint8_t
    {
        complete,
        invalid_domain,
        unsupported,
        not_running,
        not_paused
    };

    enum class execution_boundary_t : uint8_t
    {
        none,
        instruction,
        reset,
        interrupt,
        waiting,
        stopped
    };

    struct execution_step_result_t
    {
        execution_step_status_t status{ execution_step_status_t::unsupported };
        execution_domain_id_t domain{ 0 };
        execution_boundary_t boundary{ execution_boundary_t::none };
        uint64_t machine_clocks_elapsed{ 0 };
    };

    struct execution_control_t
    {
    public:
        virtual ~execution_control_t() = default;
        [[nodiscard]] virtual execution_step_result_t step_execution_domain(
            execution_domain_id_t domain
        ) noexcept = 0;
    };

    enum class debug_session_state_t : uint8_t
    {
        not_running,
        running,
        paused
    };

    enum class debug_session_transition_status_t : uint8_t
    {
        complete,
        not_running
    };

    struct debug_session_transition_result_t
    {
        debug_session_transition_status_t status{
            debug_session_transition_status_t::not_running
        };
        debug_session_state_t state{ debug_session_state_t::not_running };
    };

    struct debug_session_control_t
    {
    public:
        virtual ~debug_session_control_t() = default;
        [[nodiscard]] virtual debug_session_state_t debug_session_state() const noexcept = 0;
        [[nodiscard]] virtual debug_session_transition_result_t pause_debug_session() noexcept = 0;
        [[nodiscard]] virtual debug_session_transition_result_t resume_debug_session() noexcept = 0;
    };

    enum class checkpoint_operation_status_t : uint8_t
    {
        success,
        not_running,
        not_paused,
        capture_failed,
        allocation_failed,
        invalid_checkpoint,
        incompatible_checkpoint,
        restore_failed
    };

    struct checkpoint_operation_result_t
    {
        checkpoint_operation_status_t status{
            checkpoint_operation_status_t::capture_failed
        };
    };

    struct checkpoint_control_t
    {
    public:
        virtual ~checkpoint_control_t() = default;
        [[nodiscard]] virtual checkpoint_operation_result_t capture_checkpoint(
            std::vector<std::byte>& checkpoint
        ) noexcept = 0;
        [[nodiscard]] virtual checkpoint_operation_result_t restore_checkpoint(
            std::span<const std::byte> checkpoint
        ) noexcept = 0;
    };

    using observation_mask_t = uint64_t;

    inline constexpr observation_mask_t k_observe_execution_boundary{ 1u << 0u };

    enum class observation_kind_t : uint8_t
    {
        execution_boundary
    };

    struct execution_boundary_observation_t
    {
        execution_boundary_t boundary{ execution_boundary_t::none };
        debug_address_t address_before{};
        debug_address_t address_after{};
    };

    struct observation_event_t
    {
        observation_kind_t kind{ observation_kind_t::execution_boundary };
        execution_domain_id_t domain{ 0 };
        uint64_t machine_clock{ 0 };
        uint64_t frame_index{ 0 };
        execution_boundary_observation_t execution_boundary{};
    };

    struct observation_drain_result_t
    {
        size_t events_written{ 0 };
        uint64_t events_dropped{ 0 };
    };

    struct observation_control_t
    {
    public:
        virtual ~observation_control_t() = default;
        [[nodiscard]] virtual observation_mask_t available_observations() const noexcept = 0;
        [[nodiscard]] virtual observation_mask_t observation_mask() const noexcept = 0;
        [[nodiscard]] virtual bool set_observation_mask(observation_mask_t mask) noexcept = 0;
        [[nodiscard]] virtual observation_drain_result_t drain_observations(
            std::span<observation_event_t> destination
        ) noexcept = 0;
        virtual void clear_observations() noexcept = 0;
    };

    struct debug_target_t
    {
    public:
        virtual ~debug_target_t() = default;

        [[nodiscard]] virtual std::span<const execution_domain_descriptor_t>
            execution_domains() const noexcept = 0;
        [[nodiscard]] virtual std::span<const address_space_descriptor_t>
            address_spaces() const noexcept = 0;
        [[nodiscard]] virtual memory_inspection_result_t inspect_memory(
            debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept = 0;
        [[nodiscard]] virtual address_translation_result_t translate_address(
            debug_address_t source,
            address_space_id_t destination_space
        ) const noexcept = 0;
        [[nodiscard]] virtual execution_control_t* execution_control() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual observation_control_t* observation_control() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual debug_session_control_t* debug_session_control() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual checkpoint_control_t* checkpoint_control() noexcept
        {
            return nullptr;
        }
    };
}

//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/ProgramModel.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clover::analysis
{
    enum class data_type_kind_t : uint8_t
    {
        unsigned_integer,
        signed_integer,
        array,
        structure,
        pointer,
        enumeration,
        bitfield,
        string
    };

    enum class byte_order_t : uint8_t
    {
        little_endian,
        big_endian
    };

    struct data_type_member_t
    {
        std::string stable_id{};
        std::string name{};
        std::string type_id{};
        uint64_t byte_offset{ 0 };
        uint8_t bit_offset{ 0 };
        uint8_t bit_width{ 0 };

        [[nodiscard]] bool operator==(
            const data_type_member_t&
        ) const noexcept = default;
    };

    struct data_type_value_t
    {
        std::string name{};
        int64_t value{ 0 };

        [[nodiscard]] bool operator==(
            const data_type_value_t&
        ) const noexcept = default;
    };

    struct data_type_t
    {
        std::string stable_id{};
        std::string name{};
        data_type_kind_t kind{ data_type_kind_t::unsigned_integer };
        uint64_t byte_size{ 1 };
        byte_order_t byte_order{ byte_order_t::little_endian };
        std::optional<std::string> element_type_id{};
        uint64_t element_count{ 0 };
        std::string pointer_address_space{};
        std::string encoding{};
        std::vector<data_type_member_t> members{};
        std::vector<data_type_value_t> values{};

        [[nodiscard]] bool operator==(const data_type_t&) const noexcept = default;
    };

    struct typed_object_t
    {
        std::string stable_id{};
        address_t location{};
        std::string type_id{};
        std::string name{};

        [[nodiscard]] bool operator==(
            const typed_object_t&
        ) const noexcept = default;
    };

    enum class typed_data_conflict_kind_t : uint8_t
    {
        invalid_definition,
        duplicate_identity,
        missing_type,
        size_mismatch,
        member_out_of_bounds,
        overlapping_member,
        overlapping_object,
        address_overflow,
        unavailable_byte,
        unavailable_pointer_target,
        recursive_type,
        decode_limit
    };

    struct typed_data_conflict_t
    {
        typed_data_conflict_kind_t kind{
            typed_data_conflict_kind_t::invalid_definition
        };
        std::string subject_id{};
        std::optional<address_t> location{};
        std::string detail{};

        [[nodiscard]] bool operator==(
            const typed_data_conflict_t&
        ) const noexcept = default;
    };

    struct typed_data_validation_t
    {
        std::vector<typed_data_conflict_t> conflicts{};

        [[nodiscard]] bool valid() const noexcept
        {
            return conflicts.empty();
        }
    };

    using typed_data_byte_reader_t = std::function<
        std::optional<uint8_t>(const address_t&)
    >;

    struct decoded_typed_value_t
    {
        std::string display{};
        uint64_t bytes_read{ 0 };
        std::vector<address_t> pointer_targets{};
        std::vector<typed_data_conflict_t> conflicts{};

        [[nodiscard]] bool complete() const noexcept
        {
            return conflicts.empty();
        }
    };

    [[nodiscard]] typed_data_validation_t validate_typed_data(
        const std::vector<data_type_t>& types,
        const std::vector<typed_object_t>& objects
    );

    [[nodiscard]] decoded_typed_value_t decode_typed_object(
        const std::vector<data_type_t>& types,
        const typed_object_t& object,
        const typed_data_byte_reader_t& reader
    );
}

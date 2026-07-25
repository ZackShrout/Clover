//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace clover::core
{
    class causal_state_writer_t
    {
    public:
        template <typename value_t>
        void field(const value_t& value)
        {
            using type_t = std::remove_cv_t<value_t>;
            if constexpr (std::is_enum_v<type_t>)
            {
                field(static_cast<std::underlying_type_t<type_t>>(value));
            }
            else if constexpr (std::is_same_v<type_t, bool>)
            {
                _bytes.push_back(value ? std::byte{ 1 } : std::byte{ 0 });
            }
            else if constexpr (std::is_integral_v<type_t>)
            {
                using unsigned_t = std::make_unsigned_t<type_t>;
                const unsigned_t bits{ std::bit_cast<unsigned_t>(value) };
                for (size_t index{ 0 }; index < sizeof(type_t); ++index)
                {
                    _bytes.push_back(static_cast<std::byte>(
                        bits >> (index * 8u)
                    ));
                }
            }
            else
            {
                static_assert(std::is_integral_v<type_t>,
                              "causal state field requires an explicit codec");
            }
        }

        template <typename value_t, size_t size>
        void field(const std::array<value_t, size>& values)
        {
            for (const auto& value : values)
                field(value);
        }

        template <typename value_t, size_t size>
        void field(const value_t (&values)[size])
        {
            for (const auto& value : values)
                field(value);
        }

        template <typename value_t>
        void field(const std::vector<value_t>& values)
        {
            field(static_cast<uint64_t>(values.size()));
            for (const auto& value : values)
                field(value);
        }

        [[nodiscard]] std::vector<std::byte> finish() &&
        {
            return std::move(_bytes);
        }

    private:
        std::vector<std::byte> _bytes{};
    };

    class causal_state_reader_t
    {
    public:
        explicit causal_state_reader_t(std::span<const std::byte> bytes) noexcept
            : _bytes{ bytes }
        {
        }

        template <typename value_t>
        void field(value_t& value) noexcept
        {
            using type_t = std::remove_cv_t<value_t>;
            if (!_valid)
                return;
            if constexpr (std::is_enum_v<type_t>)
            {
                std::underlying_type_t<type_t> raw{};
                field(raw);
                value = static_cast<type_t>(raw);
            }
            else if constexpr (std::is_same_v<type_t, bool>)
            {
                uint8_t raw{};
                field(raw);
                if (raw > 1u)
                    _valid = false;
                else
                    value = raw != 0u;
            }
            else if constexpr (std::is_integral_v<type_t>)
            {
                if (_offset > _bytes.size()
                    || sizeof(type_t) > _bytes.size() - _offset)
                {
                    _valid = false;
                    return;
                }
                using unsigned_t = std::make_unsigned_t<type_t>;
                unsigned_t bits{};
                for (size_t index{ 0 }; index < sizeof(type_t); ++index)
                {
                    bits |= static_cast<unsigned_t>(
                        static_cast<uint8_t>(_bytes[_offset++])
                    ) << (index * 8u);
                }
                value = std::bit_cast<type_t>(bits);
            }
            else
            {
                static_assert(std::is_integral_v<type_t>,
                              "causal state field requires an explicit codec");
            }
        }

        template <typename value_t, size_t size>
        void field(std::array<value_t, size>& values) noexcept
        {
            for (auto& value : values)
                field(value);
        }

        template <typename value_t, size_t size>
        void field(value_t (&values)[size]) noexcept
        {
            for (auto& value : values)
                field(value);
        }

        template <typename value_t>
        void fixed_vector(std::vector<value_t>& values, size_t expected_size)
        {
            uint64_t size{};
            field(size);
            if (!_valid || size != expected_size)
            {
                _valid = false;
                return;
            }
            values.resize(expected_size);
            for (auto& value : values)
                field(value);
        }

        [[nodiscard]] bool complete() const noexcept
        {
            return _valid && _offset == _bytes.size();
        }

    private:
        std::span<const std::byte> _bytes{};
        size_t _offset{ 0 };
        bool _valid{ true };
    };
}

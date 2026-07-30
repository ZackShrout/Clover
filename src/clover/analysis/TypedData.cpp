//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TypedData.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace
{
    using namespace clover::analysis;

    constexpr uint64_t k_max_type_size{ 16u * 1024u * 1024u };
    constexpr size_t k_max_decode_depth{ 16u };
    constexpr size_t k_max_display_elements{ 16u };

    using type_map_t = std::unordered_map<std::string, const data_type_t*>;

    void add_conflict(typed_data_validation_t& result,
                      typed_data_conflict_kind_t kind,
                      std::string_view subject,
                      std::string detail)
    {
        result.conflicts.push_back({
            .kind = kind,
            .subject_id = std::string{ subject },
            .detail = std::move(detail)
        });
    }

    [[nodiscard]] bool valid_scalar_size(uint64_t size) noexcept
    {
        return size >= 1u && size <= 8u;
    }

    [[nodiscard]] uint64_t range_end(uint64_t start,
                                     uint64_t size,
                                     bool& overflow) noexcept
    {
        overflow = size > std::numeric_limits<uint64_t>::max() - start;
        return overflow ? std::numeric_limits<uint64_t>::max() : start + size;
    }

    struct decoder_t
    {
        const type_map_t& types;
        const typed_data_byte_reader_t& reader;
        decoded_typed_value_t result{};
        std::set<std::string> active_types{};

        void conflict(typed_data_conflict_kind_t kind,
                      std::string_view subject,
                      const address_t& location,
                      std::string detail)
        {
            result.conflicts.push_back({
                .kind = kind,
                .subject_id = std::string{ subject },
                .location = location,
                .detail = std::move(detail)
            });
        }

        [[nodiscard]] std::optional<uint64_t> read_integer(
            const data_type_t& type,
            const address_t& location
        )
        {
            uint64_t value{};
            for (uint64_t index{}; index < type.byte_size; ++index)
            {
                const address_t current{
                    location.address_space,
                    location.address + index
                };
                const std::optional<uint8_t> byte{ reader(current) };
                if (!byte.has_value())
                {
                    conflict(
                        typed_data_conflict_kind_t::unavailable_byte,
                        type.stable_id,
                        current,
                        "Typed value reaches an unavailable byte"
                    );
                    return std::nullopt;
                }
                const uint64_t shift{
                    type.byte_order == byte_order_t::little_endian
                        ? index * 8u
                        : (type.byte_size - index - 1u) * 8u
                };
                value |= static_cast<uint64_t>(*byte) << shift;
                ++result.bytes_read;
            }
            return value;
        }

        [[nodiscard]] std::string format_hex(uint64_t value,
                                             uint64_t byte_size) const
        {
            std::ostringstream output{};
            output << "$" << std::uppercase << std::hex << std::setfill('0')
                   << std::setw(static_cast<int>(byte_size * 2u)) << value;
            return output.str();
        }

        [[nodiscard]] std::string decode(const data_type_t& type,
                                         const address_t& location,
                                         size_t depth)
        {
            if (depth >= k_max_decode_depth)
            {
                conflict(
                    typed_data_conflict_kind_t::decode_limit,
                    type.stable_id,
                    location,
                    "Typed value exceeded the recursive decode limit"
                );
                return "<depth-limit>";
            }
            if (!active_types.insert(type.stable_id).second
                && type.kind != data_type_kind_t::pointer)
            {
                conflict(
                    typed_data_conflict_kind_t::recursive_type,
                    type.stable_id,
                    location,
                    "Type recursively contains itself"
                );
                return "<recursive>";
            }
            struct active_guard_t
            {
                std::set<std::string>& active;
                std::string id;
                ~active_guard_t()
                {
                    active.erase(id);
                }
            } guard{ active_types, type.stable_id };

            switch (type.kind)
            {
            case data_type_kind_t::unsigned_integer:
            {
                const auto value{ read_integer(type, location) };
                return value.has_value()
                    ? format_hex(*value, type.byte_size)
                    : "<unavailable>";
            }
            case data_type_kind_t::signed_integer:
            {
                const auto raw{ read_integer(type, location) };
                if (!raw.has_value())
                    return "<unavailable>";
                const uint64_t bits{ type.byte_size * 8u };
                const uint64_t sign{ uint64_t{ 1 } << (bits - 1u) };
                const int64_t value{
                    bits == 64u
                        ? std::bit_cast<int64_t>(*raw)
                        : static_cast<int64_t>((*raw ^ sign) - sign)
                };
                return std::to_string(value);
            }
            case data_type_kind_t::enumeration:
            {
                const auto raw{ read_integer(type, location) };
                if (!raw.has_value())
                    return "<unavailable>";
                const auto named{
                    std::find_if(
                        type.values.begin(),
                        type.values.end(),
                        [raw](const data_type_value_t& value)
                        {
                            return static_cast<uint64_t>(value.value) == *raw;
                        }
                    )
                };
                return named == type.values.end()
                    ? format_hex(*raw, type.byte_size)
                    : named->name + " (" + format_hex(*raw, type.byte_size) + ")";
            }
            case data_type_kind_t::bitfield:
            {
                const auto raw{ read_integer(type, location) };
                if (!raw.has_value())
                    return "<unavailable>";
                uint64_t remaining{ *raw };
                std::string display{};
                for (const data_type_value_t& value : type.values)
                {
                    const uint64_t mask{ static_cast<uint64_t>(value.value) };
                    if (mask == 0u || (remaining & mask) != mask)
                        continue;
                    if (!display.empty())
                        display += " | ";
                    display += value.name;
                    remaining &= ~mask;
                }
                if (remaining != 0u || display.empty())
                {
                    if (!display.empty())
                        display += " | ";
                    display += format_hex(remaining, type.byte_size);
                }
                return display;
            }
            case data_type_kind_t::pointer:
            {
                const auto raw{ read_integer(type, location) };
                if (!raw.has_value())
                    return "<unavailable>";
                uint64_t target_value{ *raw };
                if (type.byte_size == 2u
                    && type.pointer_address_space == location.address_space)
                {
                    target_value |= location.address & 0x00ff0000u;
                }
                const address_t target{
                    type.pointer_address_space.empty()
                        ? location.address_space
                        : type.pointer_address_space,
                    target_value
                };
                result.pointer_targets.push_back(target);
                if (!reader(target).has_value())
                {
                    conflict(
                        typed_data_conflict_kind_t::unavailable_pointer_target,
                        type.stable_id,
                        target,
                        "Pointer target is not inspectable"
                    );
                }
                return "&" + target.address_space + ":"
                    + format_hex(target.address, type.byte_size);
            }
            case data_type_kind_t::string:
            {
                std::string display{ "\"" };
                for (uint64_t index{}; index < type.byte_size; ++index)
                {
                    const address_t current{
                        location.address_space,
                        location.address + index
                    };
                    const std::optional<uint8_t> byte{ reader(current) };
                    if (!byte.has_value())
                    {
                        conflict(
                            typed_data_conflict_kind_t::unavailable_byte,
                            type.stable_id,
                            current,
                            "String reaches an unavailable byte"
                        );
                        display += "<unavailable>";
                        break;
                    }
                    ++result.bytes_read;
                    if (*byte == 0u)
                        break;
                    if (*byte == '\\' || *byte == '"')
                        display += '\\';
                    if (std::isprint(*byte) != 0
                        || (type.encoding == "utf-8" && *byte >= 0x80u))
                        display += static_cast<char>(*byte);
                    else
                    {
                        std::ostringstream escaped{};
                        escaped << "\\x" << std::uppercase << std::hex
                                << std::setfill('0') << std::setw(2)
                                << static_cast<unsigned>(*byte);
                        display += escaped.str();
                    }
                }
                display += '"';
                return display;
            }
            case data_type_kind_t::array:
            {
                const auto element{ types.find(type.element_type_id.value_or("")) };
                if (element == types.end())
                    return "<missing-element-type>";
                std::string display{ "[" };
                const uint64_t shown{
                    std::min<uint64_t>(type.element_count, k_max_display_elements)
                };
                for (uint64_t index{}; index < shown; ++index)
                {
                    if (index != 0u)
                        display += ", ";
                    display += decode(
                        *element->second,
                        {
                            location.address_space,
                            location.address + index * element->second->byte_size
                        },
                        depth + 1u
                    );
                }
                if (shown < type.element_count)
                    display += ", ...";
                display += "]";
                return display;
            }
            case data_type_kind_t::structure:
            {
                std::string display{ "{ " };
                for (size_t index{}; index < type.members.size(); ++index)
                {
                    const data_type_member_t& member{ type.members[index] };
                    const auto member_type{ types.find(member.type_id) };
                    if (index != 0u)
                        display += ", ";
                    display += member.name + "=";
                    if (member_type == types.end())
                    {
                        display += "<missing-type>";
                        continue;
                    }
                    display += decode(
                        *member_type->second,
                        {
                            location.address_space,
                            location.address + member.byte_offset
                        },
                        depth + 1u
                    );
                }
                display += " }";
                return display;
            }
            }
            return "<invalid-type>";
        }
    };
}

namespace clover::analysis
{
    typed_data_validation_t validate_typed_data(
        const std::vector<data_type_t>& types,
        const std::vector<typed_object_t>& objects
    )
    {
        typed_data_validation_t result{};
        type_map_t by_id{};
        for (const data_type_t& type : types)
        {
            if (type.stable_id.empty() || type.name.empty()
                || type.byte_size == 0u || type.byte_size > k_max_type_size)
            {
                add_conflict(
                    result,
                    typed_data_conflict_kind_t::invalid_definition,
                    type.stable_id,
                    "Type identity, name, or byte size is invalid"
                );
                continue;
            }
            if (!by_id.emplace(type.stable_id, &type).second)
            {
                add_conflict(
                    result,
                    typed_data_conflict_kind_t::duplicate_identity,
                    type.stable_id,
                    "Type stable identity is duplicated"
                );
            }
        }

        for (const data_type_t& type : types)
        {
            const auto referenced{
                type.element_type_id.has_value()
                    ? by_id.find(*type.element_type_id)
                    : by_id.end()
            };
            switch (type.kind)
            {
            case data_type_kind_t::unsigned_integer:
            case data_type_kind_t::signed_integer:
            case data_type_kind_t::enumeration:
            case data_type_kind_t::bitfield:
                if (!valid_scalar_size(type.byte_size))
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::invalid_definition,
                        type.stable_id,
                        "Scalar, enum, and bitfield types must be 1-8 bytes"
                    );
                }
                {
                    std::set<std::string> names{};
                    std::set<int64_t> values{};
                    for (const data_type_value_t& value : type.values)
                    {
                        if (value.name.empty()
                            || !names.insert(value.name).second
                            || !values.insert(value.value).second)
                        {
                            add_conflict(
                                result,
                                typed_data_conflict_kind_t::duplicate_identity,
                                type.stable_id,
                                "Named values must have unique names and values"
                            );
                        }
                    }
                }
                break;
            case data_type_kind_t::array:
                if (!type.element_type_id.has_value()
                    || referenced == by_id.end())
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::missing_type,
                        type.stable_id,
                        "Array element type is missing"
                    );
                }
                else if (type.element_count == 0u
                    || referenced->second->byte_size
                        > std::numeric_limits<uint64_t>::max()
                            / type.element_count
                    || referenced->second->byte_size * type.element_count
                        != type.byte_size)
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::size_mismatch,
                        type.stable_id,
                        "Array byte size does not match element size and count"
                    );
                }
                break;
            case data_type_kind_t::structure:
            {
                std::vector<std::pair<uint64_t, uint64_t>> occupied{};
                std::set<std::string> member_ids{};
                std::set<std::string> member_names{};
                for (const data_type_member_t& member : type.members)
                {
                    const auto member_type{ by_id.find(member.type_id) };
                    if (member.stable_id.empty() || member.name.empty()
                        || member_type == by_id.end())
                    {
                        add_conflict(
                            result,
                            typed_data_conflict_kind_t::missing_type,
                            type.stable_id,
                            "Structure member identity, name, or type is missing"
                        );
                        continue;
                    }
                    if (!member_ids.insert(member.stable_id).second
                        || !member_names.insert(member.name).second)
                    {
                        add_conflict(
                            result,
                            typed_data_conflict_kind_t::duplicate_identity,
                            member.stable_id,
                            "Structure member identities and names must be unique"
                        );
                    }
                    if ((member.bit_width == 0u && member.bit_offset != 0u)
                        || member.bit_width
                            > member_type->second->byte_size * 8u
                        || static_cast<uint64_t>(member.bit_offset)
                                + member.bit_width
                            > member_type->second->byte_size * 8u)
                    {
                        add_conflict(
                            result,
                            typed_data_conflict_kind_t::invalid_definition,
                            member.stable_id,
                            "Structure member bit range is invalid"
                        );
                    }
                    bool overflow{};
                    const uint64_t end{
                        range_end(
                            member.byte_offset,
                            member_type->second->byte_size,
                            overflow
                        )
                    };
                    if (overflow || end > type.byte_size)
                    {
                        add_conflict(
                            result,
                            typed_data_conflict_kind_t::member_out_of_bounds,
                            member.stable_id,
                            "Structure member extends beyond its owner"
                        );
                        continue;
                    }
                    for (const auto& range : occupied)
                    {
                        if (member.byte_offset < range.second
                            && end > range.first)
                        {
                            add_conflict(
                                result,
                                typed_data_conflict_kind_t::overlapping_member,
                                member.stable_id,
                                "Structure members overlap"
                            );
                        }
                    }
                    occupied.emplace_back(member.byte_offset, end);
                }
                break;
            }
            case data_type_kind_t::pointer:
                if (type.byte_size < 2u || type.byte_size > 4u
                    || type.pointer_address_space.empty())
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::invalid_definition,
                        type.stable_id,
                        "Pointer must be 2-4 bytes and name a target address space"
                    );
                }
                if (type.element_type_id.has_value()
                    && referenced == by_id.end())
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::missing_type,
                        type.stable_id,
                        "Pointer target type is missing"
                    );
                }
                break;
            case data_type_kind_t::string:
                if (type.encoding != "ascii" && type.encoding != "utf-8")
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::invalid_definition,
                        type.stable_id,
                        "First-slice strings support ascii or utf-8 encoding"
                    );
                }
                break;
            }
        }

        std::unordered_map<std::string, uint8_t> visit_state{};
        std::set<std::string> reported_cycles{};
        const auto visit = [&](const auto& self, const data_type_t& type) -> void
        {
            uint8_t& state{ visit_state[type.stable_id] };
            if (state == 2u)
                return;
            if (state == 1u)
            {
                if (reported_cycles.insert(type.stable_id).second)
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::recursive_type,
                        type.stable_id,
                        "Type contains a non-pointer recursive cycle"
                    );
                }
                return;
            }
            state = 1u;
            if (type.kind == data_type_kind_t::array
                && type.element_type_id.has_value())
            {
                const auto element{ by_id.find(*type.element_type_id) };
                if (element != by_id.end())
                    self(self, *element->second);
            }
            else if (type.kind == data_type_kind_t::structure)
            {
                for (const data_type_member_t& member : type.members)
                {
                    const auto member_type{ by_id.find(member.type_id) };
                    if (member_type != by_id.end()
                        && member_type->second->kind
                            != data_type_kind_t::pointer)
                    {
                        self(self, *member_type->second);
                    }
                }
            }
            state = 2u;
        };
        for (const data_type_t& type : types)
            visit(visit, type);

        std::set<std::string> object_ids{};
        for (const typed_object_t& object : objects)
        {
            const auto type{ by_id.find(object.type_id) };
            if (object.stable_id.empty() || object.location.address_space.empty()
                || type == by_id.end())
            {
                add_conflict(
                    result,
                    typed_data_conflict_kind_t::missing_type,
                    object.stable_id,
                    "Typed object identity, location, or type is missing"
                );
                continue;
            }
            if (!object_ids.insert(object.stable_id).second)
            {
                add_conflict(
                    result,
                    typed_data_conflict_kind_t::duplicate_identity,
                    object.stable_id,
                    "Typed object stable identity is duplicated"
                );
            }
            bool overflow{};
            static_cast<void>(
                range_end(object.location.address, type->second->byte_size, overflow)
            );
            if (overflow)
            {
                add_conflict(
                    result,
                    typed_data_conflict_kind_t::address_overflow,
                    object.stable_id,
                    "Typed object address range overflows"
                );
            }
        }
        for (size_t left{}; left < objects.size(); ++left)
        {
            const auto left_type{ by_id.find(objects[left].type_id) };
            if (left_type == by_id.end())
                continue;
            const uint64_t left_end{
                objects[left].location.address + left_type->second->byte_size
            };
            for (size_t right{ left + 1u }; right < objects.size(); ++right)
            {
                const auto right_type{ by_id.find(objects[right].type_id) };
                if (right_type == by_id.end()
                    || objects[left].location.address_space
                        != objects[right].location.address_space)
                {
                    continue;
                }
                const uint64_t right_end{
                    objects[right].location.address
                        + right_type->second->byte_size
                };
                if (objects[left].location.address < right_end
                    && objects[right].location.address < left_end)
                {
                    add_conflict(
                        result,
                        typed_data_conflict_kind_t::overlapping_object,
                        objects[right].stable_id,
                        "Typed object ranges overlap"
                    );
                }
            }
        }
        return result;
    }

    decoded_typed_value_t decode_typed_object(
        const std::vector<data_type_t>& types,
        const typed_object_t& object,
        const typed_data_byte_reader_t& reader
    )
    {
        type_map_t by_id{};
        for (const data_type_t& type : types)
            by_id.emplace(type.stable_id, &type);
        const auto type{ by_id.find(object.type_id) };
        if (type == by_id.end())
        {
            decoded_typed_value_t result{};
            result.display = "<missing-type>";
            result.conflicts.push_back({
                .kind = typed_data_conflict_kind_t::missing_type,
                .subject_id = object.stable_id,
                .location = object.location,
                .detail = "Typed object's definition is unavailable"
            });
            return result;
        }
        decoder_t decoder{ by_id, reader };
        decoder.result.display = decoder.decode(*type->second, object.location, 0u);
        return decoder.result;
    }
}

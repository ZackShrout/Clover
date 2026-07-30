//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TypedData.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "TypedDataTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::analysis;

    const data_type_t u8{
        .stable_id = "u8",
        .name = "Unsigned byte",
        .kind = data_type_kind_t::unsigned_integer,
        .byte_size = 1u
    };
    const data_type_t u16{
        .stable_id = "u16",
        .name = "Unsigned word",
        .kind = data_type_kind_t::unsigned_integer,
        .byte_size = 2u
    };
    const data_type_t state{
        .stable_id = "state",
        .name = "Actor state",
        .kind = data_type_kind_t::enumeration,
        .byte_size = 1u,
        .values = {
            { .name = "idle", .value = 1 },
            { .name = "walking", .value = 2 }
        }
    };
    const data_type_t flags{
        .stable_id = "flags",
        .name = "Actor flags",
        .kind = data_type_kind_t::bitfield,
        .byte_size = 1u,
        .values = {
            { .name = "visible", .value = 1 },
            { .name = "hostile", .value = 4 }
        }
    };
    const data_type_t pointer{
        .stable_id = "actor-pointer",
        .name = "Actor pointer",
        .kind = data_type_kind_t::pointer,
        .byte_size = 2u,
        .element_type_id = "actor",
        .pointer_address_space = "snes.cpu-bus"
    };
    const data_type_t actor{
        .stable_id = "actor",
        .name = "Actor",
        .kind = data_type_kind_t::structure,
        .byte_size = 6u,
        .members = {
            {
                .stable_id = "actor.hp",
                .name = "hp",
                .type_id = "u16",
                .byte_offset = 0u
            },
            {
                .stable_id = "actor.state",
                .name = "state",
                .type_id = "state",
                .byte_offset = 2u
            },
            {
                .stable_id = "actor.flags",
                .name = "flags",
                .type_id = "flags",
                .byte_offset = 3u
            },
            {
                .stable_id = "actor.next",
                .name = "next",
                .type_id = "actor-pointer",
                .byte_offset = 4u
            }
        }
    };
    const data_type_t actors{
        .stable_id = "actors",
        .name = "Actor array",
        .kind = data_type_kind_t::array,
        .byte_size = 12u,
        .element_type_id = "actor",
        .element_count = 2u
    };
    const data_type_t text{
        .stable_id = "name8",
        .name = "Name",
        .kind = data_type_kind_t::string,
        .byte_size = 8u,
        .encoding = "ascii"
    };
    const data_type_t bytes4{
        .stable_id = "bytes4",
        .name = "Four bytes",
        .kind = data_type_kind_t::array,
        .byte_size = 4u,
        .element_type_id = "u8",
        .element_count = 4u
    };
    const std::vector<data_type_t> types{
        u8, u16, state, flags, pointer, actor, actors, text, bytes4
    };
    const typed_object_t object{
        .stable_id = "actor@7e1000",
        .location = { "snes.cpu-bus", 0x7e1000u },
        .type_id = "actor",
        .name = "Terra"
    };
    if (!validate_typed_data(types, { object }).valid())
        return fail("valid_model");

    std::array<uint8_t, 0x30u> memory{};
    memory[0] = 0x34u;
    memory[1] = 0x12u;
    memory[2] = 0x02u;
    memory[3] = 0x05u;
    memory[4] = 0x20u;
    memory[5] = 0x10u;
    memory[0x10] = 'C';
    memory[0x11] = 'e';
    memory[0x12] = 'l';
    memory[0x13] = 'e';
    memory[0x14] = 's';
    memory[0x20] = 0xabu;
    memory[0x24] = 1u;
    memory[0x25] = 2u;
    memory[0x26] = 3u;
    memory[0x27] = 4u;
    const auto reader{
        [&memory](const address_t& address) -> std::optional<uint8_t>
        {
            if (address.address_space != "snes.cpu-bus"
                || address.address < 0x7e1000u
                || address.address >= 0x7e1000u + memory.size())
            {
                return std::nullopt;
            }
            return memory[static_cast<size_t>(address.address - 0x7e1000u)];
        }
    };
    const decoded_typed_value_t decoded{
        decode_typed_object(types, object, reader)
    };
    if (!decoded.complete()
        || decoded.display.find("hp=$1234") == std::string::npos
        || decoded.display.find("walking ($02)") == std::string::npos
        || decoded.display.find("visible | hostile") == std::string::npos
        || decoded.display.find("&snes.cpu-bus:$7E1020") == std::string::npos
        || decoded.pointer_targets.size() != 1u
        || decoded.pointer_targets.front().address != 0x7e1020u)
    {
        return fail("structured_decode");
    }
    const decoded_typed_value_t decoded_text{
        decode_typed_object(
            types,
            {
                .stable_id = "name@7e1010",
                .location = { "snes.cpu-bus", 0x7e1010u },
                .type_id = "name8"
            },
            reader
        )
    };
    if (!decoded_text.complete() || decoded_text.display != "\"Celes\"")
        return fail("string_decode");
    const decoded_typed_value_t decoded_array{
        decode_typed_object(
            types,
            {
                .stable_id = "bytes@7e1024",
                .location = { "snes.cpu-bus", 0x7e1024u },
                .type_id = "bytes4"
            },
            reader
        )
    };
    if (!decoded_array.complete()
        || decoded_array.display != "[$01, $02, $03, $04]")
    {
        return fail("array_decode");
    }

    const typed_object_t overlapping{
        .stable_id = "overlap",
        .location = { "snes.cpu-bus", 0x7e1001u },
        .type_id = "u16"
    };
    const typed_data_validation_t overlap_validation{
        validate_typed_data(types, { object, overlapping })
    };
    if (overlap_validation.valid()
        || overlap_validation.conflicts.front().kind
            != typed_data_conflict_kind_t::overlapping_object)
    {
        return fail("object_overlap");
    }

    data_type_t bad_array{ actors };
    bad_array.byte_size = 11u;
    if (validate_typed_data(
            { u8, u16, state, flags, pointer, actor, bad_array, text, bytes4 },
            {}
        ).valid())
    {
        return fail("array_size_validation");
    }

    data_type_t bad_struct{ actor };
    bad_struct.members.back().byte_offset = 3u;
    if (validate_typed_data(
            { u8, u16, state, flags, pointer, bad_struct, actors, text, bytes4 },
            {}
        ).valid())
    {
        return fail("member_overlap_validation");
    }
    const data_type_t recursive_a{
        .stable_id = "recursive-a",
        .name = "Recursive A",
        .kind = data_type_kind_t::array,
        .byte_size = 1u,
        .element_type_id = "recursive-b",
        .element_count = 1u
    };
    const data_type_t recursive_b{
        .stable_id = "recursive-b",
        .name = "Recursive B",
        .kind = data_type_kind_t::array,
        .byte_size = 1u,
        .element_type_id = "recursive-a",
        .element_count = 1u
    };
    if (validate_typed_data({ recursive_a, recursive_b }, {}).valid())
        return fail("recursive_type_validation");

    const typed_object_t unavailable{
        .stable_id = "unavailable",
        .location = { "snes.cpu-bus", 0x7f0000u },
        .type_id = "u16"
    };
    const decoded_typed_value_t missing{
        decode_typed_object(types, unavailable, reader)
    };
    if (missing.complete()
        || missing.conflicts.front().kind
            != typed_data_conflict_kind_t::unavailable_byte)
    {
        return fail("unavailable_byte");
    }

    std::printf(
        "Typed-data tests passed: definitions, arrays, structures, pointers, "
        "enums, bitfields, strings, overlaps, and unavailable bytes\n"
    );
    return 0;
}

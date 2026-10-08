#include "shared.hpp"

#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

#include "head.hpp"

namespace cbor
{

struct item;

struct record {
    std::uint64_t tag;
    std::span<std::string_view const> names;
    std::span<std::byte const> fields;
};

struct item {
    cbor::major_type major_type;
    std::uint8_t additional_information;
    std::uint64_t argument;
    std::variant<std::monostate, lazy, std::span<std::byte const>, std::string_view, item const *, record, simple_value,
#if defined(__STDCPP_FLOAT16_T__)
                 std::float16_t,
#endif
                 float, double>
        content;
};

}

#endif

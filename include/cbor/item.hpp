#include "shared.hpp"

#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <variant>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

#include "error.hpp"
#include "head.hpp"

namespace cbor
{

struct item;

struct record {
    std::uint64_t tag;
    std::span<std::string_view const> names;
    std::span<std::byte const> fields;
};

struct data_items {
    std::vector<item const *> const *decoded;
    std::size_t first;
    std::size_t count;

    std::span<item const *const> span() const
    {
        return std::span(*decoded).subspan(first, count);
    }
};

struct sharedref {
    item const *shareable;
    std::uint8_t additional_information;
    std::uint64_t argument;
};

struct item {
    cbor::major_type major_type;
    std::uint8_t additional_information;
    std::uint64_t argument;
    std::variant<std::monostate, std::span<std::byte const>, std::string_view, item const *, data_items, sharedref,
                 record, simple_value,
#if defined(__STDCPP_FLOAT16_T__)
                 std::float16_t,
#endif
                 float, double>
        content;

    std::expected<cbor::type, error> type() const
    {
        return heads::type_of(major_type, additional_information, argument);
    }
};

}

#endif

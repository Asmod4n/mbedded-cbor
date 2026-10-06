#include "shared.hpp"

#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

#include "head.hpp"

namespace cbor
{

struct item;

struct record {
    std::uint64_t tag;
    std::vector<std::pair<std::string_view, item const *>> fields;
};

struct item {
    cbor::major_type major_type;
    std::uint8_t additional_information;
    std::uint64_t argument;
    std::variant<std::monostate, lazy, std::span<std::byte const>, std::string_view, std::vector<item const *>,
                 std::vector<std::pair<item const *, item const *>>, item const *, record,
#if defined(__STDCPP_FLOAT16_T__)
                 std::float16_t,
#endif
                 float, double>
        content;
};

}

#endif

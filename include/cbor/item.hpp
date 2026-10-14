#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "rfc8949.hpp"

namespace cbor
{

struct item {
    cbor::type type;
    std::uint8_t additional_information;
    union {
        std::uint64_t u64;
        double f64;
        std::string_view str;
        std::span<std::byte const> bin;
        std::span<item const> array;
        std::span<item const> map;
        struct {
            std::uint64_t number;
            item const *content;
        } tag;
        cbor::simple_value simple_value;
    } via;
};

}

#endif

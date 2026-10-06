#pragma once

#include "common.hpp"

namespace fuzz
{

// item_end frames one item without reading what it means. Where decode reads an item, item_end finds the same
// end; where item_end finds an end, decode of those bytes can refuse only for what the item means (a tag, a
// simple value), never because the bytes are not well-formed. Every offset into the input is a start.
inline void item_end_target(std::string_view const input)
{
    test_binding binding;
    for (std::size_t at = 0; at < input.size() && at < 64; ++at) {
        std::string_view const rest = input.substr(at);
        auto const end = cbor::item_end<16>(rest);
        auto const value = cbor::lazy_decode<16>(binding, *cbor::decode<16>(rest));
        if (value)
            require(end.has_value());
        if (!end)
            continue;
        require(*end >= 1 && *end <= rest.size());
        auto const framed = cbor::lazy_decode<16>(binding, *cbor::decode<16>(rest.substr(0, *end)));
        if (value)
            require(framed.has_value() && same(*framed, *value));
        if (!framed)
            require(std::error_code(framed.error()) != cbor::condition::not_well_formed);
        if (value) {
            auto const cut = cbor::item_end<16>(rest.substr(0, *end - 1));
            require(!cut.has_value());
        }
    }
}

} // namespace fuzz

#pragma once

#include "common.hpp"

namespace fuzz
{

// item_size frames one item without reading what it means. Where decode reads an item, item_size finds the same
// end; where item_size finds an end, decode of those bytes can refuse only for what the item means (a tag, a
// simple value), never because the bytes are not well-formed. Every offset into the input is a start.
inline void item_size_target(std::string_view const input)
{
    test_binding binding;
    for (std::size_t at = 0; at < input.size() && at < 64; ++at) {
        std::string_view const rest = input.substr(at);
        auto const end = cbor::item_size(rest);
        auto const value = cbor::lazy_decode(binding, *cbor::lazy::from(rest));
        if (value)
            require(end.has_value());
        if (!end)
            continue;
        require(*end >= 1 && *end <= rest.size());
        auto const framed = cbor::lazy_decode(binding, *cbor::lazy::from(rest.substr(0, *end)));
        if (value)
            require(framed.has_value() && same(*framed, *value));
        if (!framed)
            require(framed.error() != cbor::error::too_little_data && framed.error() != cbor::error::syntax_error);
        if (value) {
            auto const cut = cbor::item_size(rest.substr(0, *end - 1));
            require(!cut.has_value());
        }
    }
}

} // namespace fuzz

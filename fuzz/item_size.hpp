#pragma once

#include "common.hpp"

namespace fuzz
{

// item_size frames one item without reading what it means. Where decode reads an item, item_size finds the same
// end; where item_size finds an end, decode of those bytes can refuse only for what the item means (a tag, a
// simple value), never because the bytes are not well-formed. Every offset into the input is a start.
inline void item_size_target(std::string_view const input)
{
    for (std::size_t at = 0; at < input.size() && at < 64; ++at) {
        std::string_view const rest = input.substr(at);
        auto const end = cbor::item_size(rest);
        auto const value = test::decoded(rest);
        if (value)
            require(end.has_value());
        if (!end)
            continue;
        require(*end >= 1 && *end <= rest.size());
        auto const framed = test::decoded(rest.substr(0, *end));
        if (value)
            require(framed.has_value() && same(*framed, *value));
        // RFC 8949 3.4.5.1 and 5.3.2: the byte string of tag 24 is well-formed when its head and bytes are, and the
        // data item in it is a top-level item of its own. decode reads that item, so a fault in it is too_little_data
        // or syntax_error of the embedded item, while item_size frames the outer item. Such an error is the same with
        // and without the bytes after the frame, and the frame holds a tag 24 head.
        if (!framed && (framed.error() == cbor::error::too_little_data || framed.error() == cbor::error::syntax_error))
            require(!value && value.error() == framed.error() &&
                    rest.substr(0, *end).find("\xd8\x18") != std::string_view::npos);
        if (value) {
            auto const cut = cbor::item_size(rest.substr(0, *end - 1));
            require(!cut.has_value());
        }
    }
}

} // namespace fuzz

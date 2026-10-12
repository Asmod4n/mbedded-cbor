#pragma once

#include "common.hpp"

namespace fuzz
{

// The reference model copies a shared value to each reference, so a value marked near the root and referred to
// deep inside grows deeper than the input was; the decode of the written bytes then refuses it by its depth limit.
inline void decode_encode_decode(std::string_view const input)
{
    auto const value = test::decoded(input);
    if (!value)
        return;
    string_writer w;
    require(test::encode(w, *value).has_value());
    auto const again = test::decoded(w.encoded);
    if (!again) {
        require(again.error() == cbor::error::nesting_depth_exceeded);
        return;
    }
    require(same_number(*value, *again));
    string_writer twice;
    require(test::encode(twice, *again).has_value() && twice.encoded == w.encoded);
    auto const end = cbor::item_size(input);
    require(end.has_value() && *end <= input.size());
}

// encode of the decoded tree gives the bytes of the input back, tag 28 and tag 29 included, and the iov target
// gives the same bytes.
inline void tree_encode(std::string_view const input)
{
    auto const top_level = cbor::lazy::copy(input);
    if (!top_level)
        return;
    auto const tree = top_level->decode();
    if (!tree)
        return;
    auto const end = cbor::item_size(input);
    require(end.has_value());
    std::string out;
    auto const written = cbor::encode(**tree, out);
    require(written.has_value() && *written == out.size() && out == input.substr(0, *end));
    auto const pieces = cbor::encode_iov(**tree);
    require(pieces.has_value());
    std::string joined;
    for (std::string_view const piece : **pieces)
        joined.append(piece);
    require(joined == out);
}

inline void decode_target(std::string_view const input)
{
    decode_encode_decode(input);
    tree_encode(input);
}

} // namespace fuzz

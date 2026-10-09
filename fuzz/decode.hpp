#pragma once

#include "common.hpp"

namespace fuzz
{

// A binding of values copies a shared value to each reference, so a value marked near the root and referred
// to deep inside grows deeper than the input was; the encoder then refuses it by its depth limit.
inline void decode_encode_decode(std::string_view const input)
{
    test_binding binding;
    auto const value = cbor::lazy_decode(binding, *cbor::decode(input));
    if (!value)
        return;
    string_writer w;
    auto const written = cbor::encode(binding, w, *value);
    if (!written) {
        require(written.error() == cbor::error{cbor::error::nesting_depth_exceeded});
        return;
    }
    auto const again = cbor::lazy_decode(binding, *cbor::decode(w.encoded));
    require(again.has_value() && same_number(*value, *again));
    string_writer twice;
    require(cbor::encode(binding, twice, *again).has_value() && twice.encoded == w.encoded);
    auto const end = cbor::item_end(input);
    require(end.has_value() && *end <= input.size());
}

inline void shared_references(std::string_view const input)
{
    shared_test::ref_binding binding;
    auto const value = cbor::lazy_decode(binding, *cbor::decode(input));
    if (!value)
        return;
    string_writer w;
    if (cbor::encode<cbor::sharedrefs::on>(binding, w, *value)) {
        shared_test::ref_binding back;
        auto const again = cbor::lazy_decode(back, *cbor::decode(w.encoded));
        require(again.has_value());
        std::map<shared_test::node const *, shared_test::node const *> pairs;
        require(same_graph(*value, *again, pairs));
    }
}

inline void decode_target(std::string_view const input)
{
    decode_encode_decode(input);
    shared_references(input);
}

} // namespace fuzz

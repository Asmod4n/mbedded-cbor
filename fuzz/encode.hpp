#pragma once

#include "common.hpp"

namespace fuzz
{

inline void encode_from_input(std::string_view const input)
{
    source in{input};
    refusal r;
    test::value const v = value_from(in, r, 0);
    test_binding binding;
    string_writer w;
    auto const written = cbor::encode<16>(binding, w, v);
    if (!written) {
        require(r.reserved_simple || r.zero_negative);
        return;
    }
    require(!r.reserved_simple && !r.zero_negative);
    auto const back = cbor::decode<16>(binding, w.bytes);
    require(back.has_value() && same_number(v, *back));
    auto const end = cbor::doc_end<16>(w.bytes);
    require(end.has_value() && *end == w.bytes.size());
}

inline void encode_target(std::string_view const input)
{
    encode_from_input(input);
}

} // namespace fuzz

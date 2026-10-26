#pragma once

#include "common.hpp"

namespace fuzz
{

// diagnostic_notation writes the text of RFC 8949 8 for one data item. It appends to its own string while it reads
// the input, so a read of the input after an append must not reach freed memory; the address sanitizer sees that.
// Where decode reads the input as one item, the diagnostic notation exists and is not empty.
inline void diagnostic_target(std::string_view const input)
{
    auto const text = cbor::diagnostic_notation(input);
    auto const value = cbor::decode(input);
    if (value)
        require(text.has_value() && !text->empty());
}

} // namespace fuzz

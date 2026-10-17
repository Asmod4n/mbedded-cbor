#pragma once

#include <cbor/cbor.hpp>

#include <string>
#include <string_view>

namespace test
{

// The library takes no input by copy. A test that holds its input in a literal or a local copies it here into a
// string that the lazy then owns, so the test needs no owner of its own.
inline std::expected<cbor::lazy, cbor::error> lazy_of_copy(std::string_view const encoded)
{
    return cbor::lazy::take(std::string(encoded));
}

}

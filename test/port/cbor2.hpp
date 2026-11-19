// Helpers of the tests that are ported from the Python library cbor2: hex to bytes and back, and the decoded
// tree of a payload as a value of the reference model of the tests.
#pragma once

#include "../hex.hpp"
#include "../reference.hpp"
#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <charconv>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <system_error>
#include <string_view>

namespace cbor2
{

// binascii.unhexlify and binascii.hexlify of Python.
using test::hexlify;
using test::unhexlify;

// cbor2.loads: the tree that cbor::decode_shared_refs gives, as a value of the reference model. cbor2.loads resolves
// tags 28 and 29, and so does cbor::decode_shared_refs.
inline std::expected<test::value, cbor::error> loads(std::string_view const payload)
{
    auto const tree = cbor::decode_shared_refs(payload);
    if (!tree)
        return std::unexpected(tree.error());
    return test::make_value_from_item(**tree);
}

// cbor2.dumps: the bytes that cbor::structs writes for a C++ value, as hex.
template <class T>
std::string dumps(T const &value)
{
    auto const encoded = cbor::encode<T>(value);
    REQUIRE(encoded.has_value());
    return hexlify(*encoded);
}

}

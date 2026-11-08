// Helpers of the tests that are ported from the Python library cbor2: hex to bytes and back, and the decoded
// tree of a payload as a value of the reference model of the tests.
#pragma once

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

// binascii.unhexlify of Python: two hex digits give one byte.
inline std::string unhexlify(std::string_view const hex)
{
    std::string bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        std::string_view const pair = hex.substr(i, 2);
        unsigned byte = 0;
        char const *const last = std::to_address(pair.end());
        auto const [end, ec] = std::from_chars(pair.data(), last, byte, 16);
        REQUIRE(ec == std::errc{});
        REQUIRE(end == last);
        bytes.push_back(static_cast<char>(byte));
    }
    return bytes;
}

// binascii.hexlify of Python: each byte gives two lowercase hex digits.
inline std::string hexlify(std::string_view const bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (char const c : bytes) {
        auto const byte = static_cast<unsigned char>(c);
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0xf]);
    }
    return hex;
}

// cbor2.loads: the tree that cbor::decode gives, as a value of the reference model.
inline std::expected<test::value, cbor::error> loads(std::string_view const payload)
{
    auto const tree = cbor::decode(payload);
    if (!tree)
        return std::unexpected(tree.error());
    return test::value_of(**tree);
}

// cbor2.dumps: the bytes that cbor::structs writes for a C++ value, as hex.
template <class T>
std::string dumps(T const &value)
{
    auto const encoded = cbor::structs<T>::encode(value);
    REQUIRE(encoded.has_value());
    return hexlify(*encoded);
}

}

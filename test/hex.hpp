// Gives the bytes of a text of hex digits and the hex digits of bytes, as binascii.unhexlify and binascii.hexlify
// of Python do (RFC 4648 8, base 16). A wrong text throws std::logic_error, so a test with a wrong literal fails.
#pragma once

#include <charconv>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace test
{

inline constexpr std::size_t hex_digits_per_byte = 2;

// binascii.unhexlify: each pair of hex digits, upper or lower case, gives one byte.
inline std::string unhexlify(std::string_view const hex)
{
    constexpr int base = 16;
    if (hex.size() % hex_digits_per_byte != 0)
        throw std::logic_error("test::unhexlify: the text holds an odd count of hex digits");
    std::string bytes;
    for (std::size_t i = 0; i < hex.size(); i += hex_digits_per_byte) {
        std::string_view const pair{std::span(hex).subspan(i, hex_digits_per_byte)};
        unsigned byte = 0;
        auto const [end, ec] = std::from_chars(pair.data(), std::to_address(pair.end()), byte, base);
        if (ec != std::errc{} || end != std::to_address(pair.end()))
            throw std::logic_error("test::unhexlify: the text holds a character that is not a hex digit");
        bytes.push_back(static_cast<char>(byte));
    }
    return bytes;
}

// binascii.hexlify: each byte gives two lower case hex digits.
inline std::string hexlify(std::string_view const bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    constexpr unsigned digit_bits = 4;
    constexpr unsigned digit_mask = 0xf;
    std::string hex;
    for (char const c : bytes) {
        auto const byte = static_cast<unsigned char>(c);
        hex.push_back(digits[byte >> digit_bits]);
        hex.push_back(digits[byte & digit_mask]);
    }
    return hex;
}

}

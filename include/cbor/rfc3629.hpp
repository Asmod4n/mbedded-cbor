// Gives the octet forms of UTF-8 to cbor::extended_diagnostic_notation and cbor::path.
// https://www.rfc-editor.org/rfc/rfc3629
#pragma once

#include <cstdint>

namespace cbor
{

class rfc3629
{
    static constexpr std::uint32_t one_octet_last = 0x7f;

    static constexpr std::uint32_t two_octets_last = 0x7ff;

    static constexpr std::uint32_t three_octets_last = 0xffff;

    static constexpr std::uint32_t char_number_last = 0x10ffff;

    static constexpr std::uint8_t two_octets_initial = 0xc0;

    static constexpr std::uint8_t three_octets_initial = 0xe0;

    static constexpr std::uint8_t four_octets_initial = 0xf0;

    static constexpr std::uint8_t following_octet_high_bits = 0xc0;

    static constexpr std::uint8_t following_octet = 0x80;

    static constexpr int following_octet_bits = 6;

    static constexpr std::uint32_t following_octet_mask = 0x3f;

    friend class extended_diagnostic_notation;
    friend class path;
};

}

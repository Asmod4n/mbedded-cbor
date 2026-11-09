// Gives the surrogate pairs of UTF-16 to cbor::extended_diagnostic_notation.
// https://www.rfc-editor.org/rfc/rfc2781
#pragma once

#include <cstdint>

namespace cbor
{

class rfc2781
{
    static constexpr std::uint32_t high_surrogate_first = 0xd800;

    static constexpr std::uint32_t low_surrogate_first = 0xdc00;

    static constexpr std::uint32_t low_surrogate_last = 0xdfff;

    static constexpr std::uint32_t surrogate_pair_first = 0x10000;

    static constexpr int surrogate_bits = 10;

    friend class extended_diagnostic_notation;
};

}

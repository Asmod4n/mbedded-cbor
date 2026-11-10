#pragma once

#include <cstdint>

// The parameters of the binary interchange formats of IEEE 754-2019 (table 3.5) that the reference encoder of the
// tests uses. The tests write the preferred float of RFC 8949 4.2.2 from these numbers, not from the library.
namespace test::ieee754
{

struct binary_format {
    int precision;
    int emin;
    int emax;
    int trailing_significand_bits;
    int exponent_bias;
};

inline constexpr binary_format binary16{11, -14, 15, 10, 15};
inline constexpr binary_format binary32{24, -126, 127, 23, 127};
inline constexpr binary_format binary64{53, -1022, 1023, 52, 1023};

// The exponent field of all ones (infinity and NaN) of binary16 and binary32, in place in the encoding.
inline constexpr std::uint64_t binary16_exponent_all_ones = 0x7c00;
inline constexpr std::uint64_t binary32_exponent_all_ones = 0x7f800000;

// The bit of the sign is the highest bit of the encoding: 15, 31 and 63.
inline constexpr int binary16_sign_bit = 15;
inline constexpr int binary32_sign_bit = 31;
inline constexpr int binary64_sign_bit = 63;

}

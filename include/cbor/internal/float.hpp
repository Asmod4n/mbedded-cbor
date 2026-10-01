#pragma once

#include <bit>
#include <cstdint>

namespace cbor::internal
{

inline float float_decode_binary16(std::uint16_t half)
{
    std::uint32_t const sign = half & 0x8000u;
    std::uint32_t const exp = half & 0x7c00u;
    std::uint32_t const frac = half & 0x03ffu;
    if (exp == 0x7c00u)
        return std::bit_cast<float>(sign << 16 | 0x7f800000u | frac << 13);
    if (exp != 0)
        return std::bit_cast<float>(
            sign << 16 | ((exp >> 10) + (127 - 15)) << 23 | frac << 13);
    if (frac == 0)
        return std::bit_cast<float>(sign << 16);
    int const shift = std::countl_zero(static_cast<std::uint16_t>(frac << 5));
    std::uint32_t const mant = (frac << shift) & 0x03ffu;
    return std::bit_cast<float>(
        sign << 16 | static_cast<std::uint32_t>(127 - 14 - shift) << 23 |
        mant << 13);
}

inline std::uint16_t float_encode_binary16(float value)
{
    std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t const sign = bits >> 31;
    std::uint32_t const exp32 = bits >> 23 & 0xffu;
    std::uint32_t const mant32 = bits & 0x7fffffu;
    std::uint32_t exp16;
    std::uint32_t mant16;
    if (exp32 == 0xff) {
        if (mant32 != 0)
            return 0x7e00u;
        exp16 = 0x1f;
        mant16 = 0;
    } else if (exp32 == 0) {
        exp16 = 0;
        mant16 = 0;
    } else if (exp32 >= 113) {
        exp16 = exp32 - 112;
        mant16 = mant32 >> 13;
    } else {
        exp16 = 0;
        mant16 = (0x800000u | mant32) >> (126 - exp32);
    }
    return static_cast<std::uint16_t>(sign << 15 | exp16 << 10 | mant16);
}

inline std::uint8_t preferred_float_info(double value)
{
    std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
    std::uint32_t const exp = bits >> 52 & 0x7ffu;
    std::uint64_t const mant = bits & 0xfffffffffffffu;
    if (exp == 0x7ff)
        return 25;
    if (exp == 0)
        return mant == 0 ? 25 : 27;
    if ((mant & 0x1fffffffu) != 0 || exp < 897 || exp > 1150)
        return 27;
    if (exp >= 1009 && exp <= 1038)
        return (mant >> 29 & 0x1fffu) == 0 ? 25 : 26;
    if (exp >= 999 && exp <= 1008)
        return (mant >> 29 & ((1u << (1022 - exp)) - 1u)) == 0 ? 25 : 26;
    return 26;
}

} // namespace cbor::internal

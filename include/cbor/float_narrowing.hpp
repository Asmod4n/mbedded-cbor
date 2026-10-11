#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cbor
{

struct float_narrowing {
    std::uint8_t single_precision_shift;
    std::uint8_t half_precision_shift;
    std::uint16_t half_precision_exponent;
    std::uint32_t single_precision_exponent;

    template <typename Precision>
    static consteval auto table_make(Precision const h, Precision const f, Precision const d)
    {
        constexpr std::uint8_t no_narrowing = std::numeric_limits<std::uint64_t>::digits - 1;
        int const f_normal_min = d.exponent_bias - f.exponent_bias + 1;
        int const h_normal_min = d.exponent_bias - h.exponent_bias + 1;
        std::array<float_narrowing, std::size_t{std::numeric_limits<double>::max_exponent} * 2> table{};
        for (std::size_t i = 0; i < table.size(); ++i) {
            int const e = static_cast<int>(i);
            float_narrowing x{no_narrowing, no_narrowing, 0, 0};
            if (e >= f_normal_min && e <= d.exponent_bias + f.exponent_bias) {
                x.single_precision_shift = static_cast<std::uint8_t>(d.significand_bits - f.significand_bits);
                x.single_precision_exponent = static_cast<std::uint32_t>(e - f_normal_min)
                                              << f.significand_bits;
            } else if (e >= f_normal_min - f.significand_bits && e < f_normal_min)
                x.single_precision_shift =
                    static_cast<std::uint8_t>(d.significand_bits - f.significand_bits + f_normal_min - e);
            if (e >= h_normal_min && e <= d.exponent_bias + h.exponent_bias) {
                x.half_precision_shift = static_cast<std::uint8_t>(d.significand_bits - h.significand_bits);
                x.half_precision_exponent =
                    static_cast<std::uint16_t>((e - h_normal_min) << h.significand_bits);
            } else if (e >= h_normal_min - h.significand_bits && e < h_normal_min)
                x.half_precision_shift =
                    static_cast<std::uint8_t>(d.significand_bits - h.significand_bits + h_normal_min - e);
            table[i] = x;
        }
        return table;
    }
};

} // namespace cbor

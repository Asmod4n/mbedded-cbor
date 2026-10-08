#pragma once

#include <cstddef>
#include <cstdint>

namespace cbor
{

enum class pass;

enum class major_type : std::uint8_t {
    unsigned_integer,
    negative_integer,
    byte_string,
    text_string,
    array,
    map,
    tag,
    simple_float
};

enum class simple_value : std::uint8_t { false_value = 20, true_value, null, undefined };

class rfc8949
{
    enum class additional_information : std::uint8_t {
        one_byte_argument = 24,
        two_byte_argument,
        four_byte_argument,
        eight_byte_argument,
        indefinite_length = 31
    };

    enum class simple_float_information : std::uint8_t {
        simple_value_follows = 24,
        half_precision_float,
        single_precision_float,
        double_precision_float,
        break_stop_code = 31
    };

    static constexpr std::uint8_t simple_value_one_byte_min = 32;

    enum class tag_number : std::uint64_t {
        standard_date_time_string = 0,
        epoch_based_date_time = 1,
        unsigned_bignum = 2,
        negative_bignum = 3,
        encoded_cbor_data_item = 24,
        shareable = 28,
        sharedref = 29,
        self_described_cbor = 55799
    };

    friend class validity;

    friend class heads;

    friend class decoding;

    friend class well_formedness;

    friend class value_sharing;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <class Writer>
    friend struct encoder;

    template <std::size_t, class, class, pass>
    friend class walker;

    friend struct lazy;

#ifdef __cpp_impl_reflection
    friend class packed;

    friend class generic;
#endif
};

}

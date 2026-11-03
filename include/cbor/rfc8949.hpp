// Gives the major types, the simple values, cbor::tag and cbor::type, the
// types of a cbor::item. Gives the values of the additional information and the
// tag numbers that the library reads and writes.
// https://www.rfc-editor.org/rfc/rfc8949#section-3
#pragma once

#include <cstddef>
#include <cstdint>

namespace cbor
{

namespace detail
{

template <bool>
struct tree_decoder;

}


template <std::size_t N>
struct fixed_string;

template <fixed_string Path>
class is_singular_query;

/// The eight CBOR major types defined by RFC 8949.
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

/// The named CBOR simple values false, true, null and undefined.
enum class simple_value : std::uint8_t { false_value = 20, true_value, null, undefined };

/// A CBOR tag number used to describe a type.
/// schema requires each described struct to have a distinct tag. Tags of
/// standard types and numbers reserved as invalid are rejected at compile time.
/// These include 0, 1, 2, 3, 24, 64-87, 100, 258, 1004, 21334, 32873,
/// 65535, 4294967295 and 18446744073709551615.
struct tag {
    /// The tag number.
    std::uint64_t number;
};

/// The kind of a decoded item, including recognized simple values and tags.
enum class type : std::uint8_t {
    /// An unsigned integer, including a bignum that fits in 64 bits.
    unsigned_integer,
    /// A negative integer, including a bignum that fits in 64 bits.
    negative_integer,
    /// A byte string.
    byte_string,
    /// A CBOR text string. The library does not validate UTF-8.
    text_string,
    /// An array of items.
    array,
    /// A map of key and value pairs.
    map,
    /// A tagged item.
    tag,
    /// The false simple value.
    false_value,
    /// The true simple value.
    true_value,
    /// The null simple value.
    null,
    /// The undefined simple value.
    undefined,
    /// A simple value other than false, true, null or undefined.
    simple_value,
    /// A binary16 floating-point value.
    half_float,
    /// A binary32 floating-point value.
    single_float,
    /// A binary64 floating-point value.
    double_float,
    /// A positive bignum represented by tag 2.
    unsigned_bignum,
    /// A negative bignum represented by tag 3.
    negative_bignum,
    /// A typed array represented by an RFC 8746 tag.
    typed_array
};

class rfc8949
{
    static constexpr int additional_information_bits = 5;

    static constexpr std::size_t data_items_per_pair = 2;

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

    friend class sequence_reader;

    friend class well_formedness;

    friend class extended_diagnostic_notation;

    friend class jsonpath;

    friend class encoding;

    friend struct lazy;

    template <bool>
    friend struct detail::tree_decoder;

    template <class Target>
    friend class encoder;

    template <fixed_string Path>
    friend class is_singular_query;

    friend class reflection;

    friend class generic;
};

}

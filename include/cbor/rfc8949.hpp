/**
 * @file
 * Gives the major types, the simple values, cbor::tag and cbor::type, the
 * types of a cbor::item. Gives the values of the additional information and the
 * tag numbers that the library reads and writes.
 * https://www.rfc-editor.org/rfc/rfc8949#section-3
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace cbor
{

namespace detail
{

class validity;

template <bool, bool>
struct tree_decoder;

}


template <std::size_t N>
struct fixed_string;

template <fixed_string Path>
class is_singular_query;

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

/**
 * @brief The tag number of a struct, in the member `tag` of its cbor::describe_members specialization.
 *
 * `schema` writes the number as the first item of each record of the struct.
 * A read compares it, so a message of another struct gives `incorrect_type`.
 * The tag of a record of a `std::variant` member chooses a class alternative; a head of no alternative is
 * `incorrect_type`.
 * Each alternative has its own number, also when it derives from the class of another number.
 * `structs` takes the same description.
 * These do not compile in `schema<T>`: a struct in `T` with no description or with `cbor::untagged`; two structs in
 * `T` with one number, the alternatives of a variant included; a number of a standard type (0, 1, 2, 3, 24, 64 to 87,
 * 100, 258, 1004 and 32873); a number that the IANA registry calls always invalid (21334, 65535, 4294967295 and
 * 18446744073709551615).
 *
 * @code{.callseq}
 * cbor::tag{number} → cbor::tag
 * @endcode
 * @code{.example}
 * bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
 * call: cbor::tag{1802}
 * result: cbor::tag{1802}
 * call: cbor::schema<point>::encode(point{1, 2})
 * result: 54 bytes: d8 71 82 9a 00 00 00 10 d8 72 82 61 78 61 79 5a 00 00 00 00 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 d9 07 0a d8 80 9a 00 00 00 02 fa 3f 80 00 00 fa 40 00 00 00
 * call: cbor::schema<car>::path(std::move(bytes)).has_value()
 * result: true
 * @endcode
 *
 * @snippet doc_reference_schema.cpp tag
 * @see cbor::describe_members
 * @see cbor::schema::encode()
 *
 * @ingroup schema
 */
struct tag {
    std::uint64_t number;
};

enum class type : std::uint8_t {
    unsigned_integer,
    negative_integer,
    byte_string,
    text_string,
    array,
    map,
    tag,
    false_value,
    true_value,
    null,
    undefined,
    simple_value,
    half_float,
    single_float,
    double_float,
    unsigned_bignum,
    negative_bignum,
    typed_array,
    shared_reference
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

    friend class detail::validity;

    friend class heads;

    friend class sequence_reader;

    friend class well_formedness;

    friend class extended_diagnostic_notation;

    friend class path;

    friend class encoding;

    friend struct lazy;

    template <bool, bool>
    friend struct detail::tree_decoder;

    template <class Target>
    friend class encoder;

    template <fixed_string Path>
    friend class is_singular_query;

    friend class reflection;

    friend class generic;
};

}

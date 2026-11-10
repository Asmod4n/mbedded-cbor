#pragma once

// The numbers of RFC 8949 that the tests count over. A test writes its expectation from the specification, so these
// names stand here and not in the library.
namespace test::rfc8949
{

// 3: the initial byte holds the major type in its high 3 bits and the additional information in its low 5 bits.
inline constexpr unsigned major_types = 8;
inline constexpr unsigned additional_information_bits = 5;
inline constexpr unsigned additional_information_values = 32;
inline constexpr unsigned initial_byte_values = 256;

// 3: the additional information 24 to 27 gives the width of the argument, 28 to 30 are reserved, 31 is the
// indefinite length.
inline constexpr unsigned one_byte_argument = 24;
inline constexpr unsigned two_byte_argument = 25;
inline constexpr unsigned four_byte_argument = 26;
inline constexpr unsigned eight_byte_argument = 27;

// 3.1: major type 7 holds the floats and the simple values.
inline constexpr unsigned float_and_simple_major_type = 7;
inline constexpr unsigned first_reserved_information = 28;
inline constexpr unsigned last_reserved_information = 30;
inline constexpr unsigned indefinite_length = 31;

// 3.3: a one-byte argument holds 256 values; the simple values 24 to 31 are reserved.
inline constexpr unsigned one_byte_values = 256;
inline constexpr unsigned first_reserved_simple_value = 24;
inline constexpr unsigned last_reserved_simple_value = 31;

}

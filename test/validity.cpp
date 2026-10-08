#include "binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using cbor::error;
using cbor::major_type;
using cbor::validity;

// The owner set the bound of DepthMax to 1024 and its default to 128. Every form that takes DepthMax reads
// both from these two constants, so this test is the one place that holds the two numbers.
TEST_CASE("validity: the limit and the default of the nesting depth")
{
    CHECK_EQ(validity::nesting_depth_limit, 1024u);
    CHECK_EQ(validity::nesting_depth_default, 128u);
}

// One function decides whether a depth is allowed, at compile time for DepthMax and at run time for the depth
// of an item. A depth up to the maximum is allowed and every depth above it is refused, for every depth up to
// one past the limit.
TEST_CASE("validity: check_nesting_depth for every depth up to one past the limit")
{
    for (std::size_t depth = 0; depth <= 1025; ++depth) {
        auto const r = validity::check_nesting_depth(depth, 1024);
        if (depth <= 1024)
            CHECK(r.has_value());
        else
            CHECK_EQ(r.error(), error::nesting_depth_exceeded);
    }
    constexpr bool limit_allowed =
        validity::check_nesting_depth(1024, validity::nesting_depth_limit).has_value();
    constexpr bool past_limit_allowed =
        validity::check_nesting_depth(1025, validity::nesting_depth_limit).has_value();
    CHECK(limit_allowed);
    CHECK_FALSE(past_limit_allowed);
}

namespace
{

struct shared_ptr_case {
    std::shared_ptr<int const> pointer;
    bool empty;
    bool null;
};

std::shared_ptr<int const> const holder = std::make_shared<int const>(7);
int const elsewhere = 8;

// The four states of a std::shared_ptr after [util.smartptr.shared]: it owns an object or not (empty means it
// owns none), and it stores a pointer or not (null). Each state is built with a constructor of the standard.
shared_ptr_case const shared_ptr_cases[] = {
    {std::shared_ptr<int const>{}, true, true},
    {std::shared_ptr<int const>(std::shared_ptr<int const>{}, &elsewhere), true, false},
    {std::shared_ptr<int const>(holder, nullptr), false, true},
    {holder, false, false},
};

} // namespace

// An owner that owns no object keeps nothing alive, whatever pointer it stores. The check must throw for
// exactly the two empty states, so that a view beside such an owner is never handed out.
TEST_CASE("validity: throw_logic_error_if_empty for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.empty)
            CHECK_THROWS_AS(validity::throw_logic_error_if_empty(c.pointer, "empty"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_empty(c.pointer, "empty"));
    }
}

// A pointer that is read through must not be null, whether it owns an object or not. The check must throw for
// exactly the two null states.
TEST_CASE("validity: throw_logic_error_if_null for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.null)
            CHECK_THROWS_AS(validity::throw_logic_error_if_null(c.pointer, "null"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_null(c.pointer, "null"));
    }
}

namespace
{

constexpr std::size_t size_max = std::numeric_limits<std::size_t>::max();

// Factors around each width a product can have: zero, one, small numbers, the borders of 32 bits, the halves
// and the top of std::size_t.
constexpr std::array<std::size_t, 13> factors{0,
                                              1,
                                              2,
                                              3,
                                              std::size_t{0xffffffff},
                                              std::size_t{0xffffffff} + 1,
                                              std::size_t{0xffffffff} + 2,
                                              size_max / 3,
                                              size_max / 2,
                                              size_max / 2 + 1,
                                              size_max - 1,
                                              size_max,
                                              std::size_t{1}
                                                  << (std::numeric_limits<std::size_t>::digits / 2)};

constexpr auto sums_at_compile_time = [] {
    std::array<std::array<bool, factors.size()>, factors.size()> fits{};
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j)
            fits[i][j] = cbor::validity::checked_add(factors[i], factors[j]).has_value();
    return fits;
}();

constexpr auto products_at_compile_time = [] {
    std::array<std::array<bool, factors.size()>, factors.size()> fits{};
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j)
            fits[i][j] = cbor::validity::checked_mul(factors[i], factors[j]).has_value();
    return fits;
}();

} // namespace

// A map or a set of members is valid only when no two of its keys are equal. The check reads a sorted range,
// so it must agree with a comparison of every pair, for every sorted sequence of up to four keys from three
// values, and through a projection to the key of a pair.
TEST_CASE("validity: keys_unique for every sorted sequence of up to four keys")
{
    std::vector<std::vector<int>> sequences{{}};
    for (std::size_t length = 1; length <= 4; ++length) {
        std::vector<std::vector<int>> longer;
        for (std::vector<int> const &s : sequences)
            if (s.size() == length - 1)
                for (int key = s.empty() ? 0 : s.back(); key <= 2; ++key) {
                    std::vector<int> next = s;
                    next.push_back(key);
                    longer.push_back(next);
                }
        sequences.insert(sequences.end(), longer.begin(), longer.end());
    }
    CHECK_EQ(sequences.size(), 35u);
    for (std::vector<int> const &s : sequences) {
        bool distinct = true;
        for (std::size_t i = 0; i < s.size(); ++i)
            for (std::size_t j = i + 1; j < s.size(); ++j)
                distinct = distinct && s[i] != s[j];
        CHECK_EQ(validity::keys_unique(s), distinct);
        std::vector<std::pair<int, std::string>> pairs;
        for (int const key : s)
            pairs.emplace_back(key, std::to_string(pairs.size()));
        CHECK_EQ(validity::keys_unique(pairs, &std::pair<int, std::string>::first), distinct);
    }
}

// RFC 8949 5.6: a second key equal to a key already met makes the map not valid. The input set is {false,
// true}.
TEST_CASE("validity: check_key_unique for both inputs")
{
    CHECK(validity::check_key_unique(false).has_value());
    CHECK_EQ(validity::check_key_unique(true).error(), error::duplicate_key);
}

#ifdef __SIZEOF_INT128__
// C23 ckd_mul gives the product when it is representable and reports an overflow otherwise. The compiler and
// the run time use the same function, so both must give the answer of the exact product, which a wider type
// computes here.
TEST_CASE("validity: checked_mul for every pair of factors, at compile time and at run time")
{
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j) {
            std::size_t const a = factors[i];
            std::size_t const b = factors[j];
            cbor::uint128 const exact = static_cast<cbor::uint128>(a) * b;
            bool const fits = exact <= size_max;
            auto const product = validity::checked_mul(a, b);
            CHECK_EQ(product.has_value(), fits);
            CHECK_EQ(products_at_compile_time[i][j], fits);
            if (fits)
                CHECK_EQ(*product, a * b);
            else
                CHECK_EQ(product.error(), std::errc::value_too_large);
        }
}

// C23 ckd_add gives the sum when it is representable and reports an overflow otherwise. The compiler and the
// run time use the same function, so both must give the answer of the exact sum, which a wider type computes
// here.
TEST_CASE("validity: checked_add for every pair of summands, at compile time and at run time")
{
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j) {
            std::size_t const a = factors[i];
            std::size_t const b = factors[j];
            cbor::uint128 const exact = static_cast<cbor::uint128>(a) + b;
            bool const fits = exact <= size_max;
            auto const sum = validity::checked_add(a, b);
            CHECK_EQ(sum.has_value(), fits);
            CHECK_EQ(sums_at_compile_time[i][j], fits);
            if (fits)
                CHECK_EQ(*sum, a + b);
            else
                CHECK_EQ(sum.error(), std::errc::value_too_large);
        }
}
#endif

namespace
{

constexpr std::array<major_type, 8> major_types{major_type::unsigned_integer,
                                                major_type::negative_integer,
                                                major_type::byte_string,
                                                major_type::text_string,
                                                major_type::array,
                                                major_type::map,
                                                major_type::tag,
                                                major_type::simple_float};

} // namespace

// RFC 8949 Appendix C refuses the additional information 28, 29 and 30 on every major type, and 31 on major
// types 0, 1 and 6. Every other head is well-formed. The decoders of the library and the diagnostic notation
// read every head through this one check, so it must agree with the appendix for all 8 x 32 pairs.
TEST_CASE("validity: check_additional_information for every major type and every additional information")
{
    for (major_type const major : major_types)
        for (std::uint8_t info = 0; info < 32; ++info) {
            bool const reserved = info >= 28 && info <= 30;
            bool const no_indefinite_form =
                info == 31 && (major == major_type::unsigned_integer ||
                               major == major_type::negative_integer || major == major_type::tag);
            auto const r = validity::check_additional_information(major, info);
            if (reserved || no_indefinite_form)
                CHECK_EQ(r.error(), error::syntax_error);
            else
                CHECK(r.has_value());
        }
}

// The owner decided that the decoder never reads an indefinite length. A string, an array or a map with the
// additional information 31 is refused as indefinite_length, and a break code outside of an indefinite-length
// item is not well-formed (RFC 8949 3.2.1). Only the additional information 0 to 27 passes, on every major
// type.
TEST_CASE("validity: check_definite_length for every major type and every additional information")
{
    for (major_type const major : major_types)
        for (std::uint8_t info = 0; info < 32; ++info) {
            auto const r = validity::check_definite_length(major, info);
            bool const container = major == major_type::byte_string || major == major_type::text_string ||
                                   major == major_type::array || major == major_type::map;
            if (info <= 27)
                CHECK(r.has_value());
            else if (info == 31 && container)
                CHECK_EQ(r.error(), error::indefinite_length);
            else
                CHECK_EQ(r.error(), error::syntax_error);
        }
}

// RFC 8949 3.2.3: each chunk of an indefinite-length string is a definite-length string of the same major
// type. The check must refuse every other major type and the indefinite form, for both string types.
TEST_CASE("validity: check_chunk for both string types, every major type and every additional information")
{
    for (major_type const string : {major_type::byte_string, major_type::text_string})
        for (major_type const major : major_types)
            for (std::uint8_t info = 0; info < 32; ++info) {
                auto const r = validity::check_chunk(string, major, info);
                if (major == string && info != 31)
                    CHECK(r.has_value());
                else
                    CHECK_EQ(r.error(), error::syntax_error);
            }
}

// RFC 8949 Appendix C: a simple value in the two-byte form (additional information 24) below 32 is not
// well-formed. The decoders and the encoder use this one check, so it must agree for every additional
// information and every one-byte argument.
TEST_CASE("validity: check_simple_value for every additional information and every one-byte argument")
{
    for (std::uint8_t info = 0; info < 32; ++info)
        for (std::uint64_t argument = 0; argument < 256; ++argument) {
            auto const r = validity::check_simple_value(info, argument);
            if (info == 24 && argument < 32)
                CHECK_EQ(r.error(), error::syntax_error);
            else
                CHECK(r.has_value());
        }
}

namespace
{

// RFC 8746 2.1, Tables 1 and 2: the number of bytes of one element of each typed array tag from 64 to 87. Tag
// 76 is reserved and has no element size; 0 stands for it.
constexpr std::array<std::size_t, 24> element_bytes{1, 2, 4, 8, 1, 2, 4, 8,  1, 2, 4, 8,
                                                    0, 2, 4, 8, 2, 4, 8, 16, 2, 4, 8, 16};

} // namespace

// The element size of a typed array decides how its length is checked and how its elements are read. The
// formula of the library must give the size of RFC 8746 for every tag that has one.
TEST_CASE("validity: typed_array_element_size for every typed array tag")
{
    for (std::uint64_t tag = 64; tag <= 87; ++tag)
        if (tag != 76)
            CHECK_EQ(validity::typed_array_element_size(tag), element_bytes[tag - 64]);
}

// A typed array tag must lie in 64 to 87 and must not be the reserved tag 76, and its byte string must hold a
// whole number of elements (RFC 8746 2). The tags around the range and every length up to two elements of the
// widest type are checked, so that every remainder of every element size is met.
TEST_CASE("validity: typed_array_check for the tags around 64 to 87 and the lengths 0 to 32")
{
    for (std::uint64_t tag = 60; tag <= 90; ++tag)
        for (std::size_t size = 0; size <= 32; ++size) {
            auto const r = validity::typed_array_check(tag, size);
            if (tag < 64 || tag > 87 || tag == 76)
                CHECK_EQ(r.error(), error::incorrect_type);
            else if (size % element_bytes[tag - 64] != 0)
                CHECK_EQ(r.error(), error::inadmissible_type_for_tag_content);
            else
                CHECK(r.has_value());
        }
}

// RFC 8949 5.3.2 calls a tag content of the wrong type "inadmissible type for tag content". The content of
// tags 2 and 3 (RFC 8949 3.4.3), of tag 24 (RFC 8949 3.4.5.1) and of tags 64 to 87 (RFC 8746 2) is a byte
// string; the content of tag 29 is an unsigned integer (value-sharing). The content of tag 0 is a text string
// (RFC 8949 3.4.1). The content of tag 1 is an unsigned or negative integer, or a float with additional
// information 25, 26 or 27 (RFC 8949 3.4.2). Every other tag takes every major type. Each additional
// information 0 to 31 is checked, because tag 1 admits major type 7 only with three of them.
TEST_CASE("validity: check_tag_content for the tags 0 to 100 and 1113, every major type and additional information")
{
    std::vector<std::uint64_t> tags;
    for (std::uint64_t tag = 0; tag <= 100; ++tag)
        tags.push_back(tag);
    tags.push_back(1113);
    tags.push_back(std::numeric_limits<std::uint64_t>::max());
    for (std::uint64_t const tag : tags)
        for (major_type const major : major_types)
            for (std::uint8_t info = 0; info < 32; ++info) {
                auto const r = validity::check_tag_content(tag, major, info);
                bool const byte_string = tag == 2 || tag == 3 || tag == 24 || (tag >= 64 && tag <= 87);
                bool admitted = true;
                if (byte_string)
                    admitted = major == major_type::byte_string;
                else if (tag == 29)
                    admitted = major == major_type::unsigned_integer;
                else if (tag == 0)
                    admitted = major == major_type::text_string;
                else if (tag == 1)
                    admitted = major == major_type::unsigned_integer || major == major_type::negative_integer ||
                               (major == major_type::simple_float && info >= 25 && info <= 27);
                if (admitted)
                    CHECK(r.has_value());
                else
                    CHECK_EQ(r.error(), error::inadmissible_type_for_tag_content);
            }
}

// A shared reference names one of the values that were marked before it. The decoder and the lazy reader read
// the index through this one check, so an index that no mark has reached gives the same error on both paths,
// and the index is converted to std::size_t only after it is known to fit.
TEST_CASE("validity: check_sharedref_index for up to three marks")
{
    std::array<std::uint64_t, 6> const arguments{0, 1, 2, 3, 4, std::numeric_limits<std::uint64_t>::max()};
    for (std::size_t marked = 0; marked <= 3; ++marked)
        for (std::uint64_t const argument : arguments) {
            auto const r = validity::check_sharedref_index(argument, marked);
            if (argument > std::numeric_limits<std::size_t>::max())
                CHECK_EQ(r.error(), error::sharedref_index_out_of_range);
            else if (argument >= marked)
                CHECK_EQ(r.error(), error::sharedref_index_not_marked);
            else
                CHECK_EQ(*r, argument);
        }
}

#ifdef __SIZEOF_INT128__
// RFC 9535 2.3.3.2: an index selects the element Normalize(i, len), and only an index in 0 to len - 1 after
// the normalization selects an element. The path, lazy::at and the schema use this one check, so it must
// agree with the exact arithmetic for small arrays and at the ends of std::int64_t and std::uint64_t.
TEST_CASE("validity: check_index against Normalize of RFC 9535")
{
    constexpr std::int64_t min = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t max = std::numeric_limits<std::int64_t>::max();
    std::vector<std::int64_t> indexes{min, min + 1, max - 1, max};
    for (std::int64_t i = -6; i <= 6; ++i)
        indexes.push_back(i);
    std::vector<std::uint64_t> lengths{0,
                                       1,
                                       2,
                                       3,
                                       4,
                                       static_cast<std::uint64_t>(max),
                                       static_cast<std::uint64_t>(max) + 1,
                                       std::numeric_limits<std::uint64_t>::max()};
    for (std::uint64_t const length : lengths)
        for (std::int64_t const index : indexes) {
            cbor::int128 const normalized =
                index >= 0 ? cbor::int128{index} : static_cast<cbor::int128>(length) + index;
            bool const selects = normalized >= 0 && normalized < static_cast<cbor::int128>(length);
            auto const r = validity::check_index(index, length);
            if (selects)
                CHECK_EQ(static_cast<cbor::int128>(*r), normalized);
            else
                CHECK_EQ(r.error(), error::index_out_of_bounds);
            if (index >= 0) {
                auto const u = validity::check_index(static_cast<std::size_t>(index), length);
                CHECK_EQ(u.has_value(), selects);
            }
        }
}

// RFC 8949 3.1: a negative integer has the value -1 minus its argument. A magnitude fits in a type exactly
// when this value lies in the range of the type, for every width and signedness that the library reads into.
template <class T>
void number_range_check()
{
    constexpr std::uint64_t top = std::numeric_limits<std::uint64_t>::max();
    std::vector<std::uint64_t> magnitudes{0, 1, 2, top - 1, top};
    for (std::uint64_t const edge : {static_cast<std::uint64_t>(std::numeric_limits<T>::max())})
        for (std::uint64_t const m : {edge - 1, edge, edge + 1})
            magnitudes.push_back(m);
    for (bool const negative : {false, true})
        for (std::uint64_t const magnitude : magnitudes) {
            cbor::int128 const value = negative ? -1 - cbor::int128{magnitude} : cbor::int128{magnitude};
            bool const fits = value >= cbor::int128{std::numeric_limits<T>::min()} &&
                              value <= cbor::int128{std::numeric_limits<T>::max()};
            auto const r = validity::check_number_range<T>(negative, magnitude);
            if (fits)
                CHECK(r.has_value());
            else
                CHECK_EQ(r.error(), error::number_out_of_range);
        }
}

TEST_CASE("validity: check_number_range for every standard integer width and signedness")
{
    number_range_check<std::int8_t>();
    number_range_check<std::uint8_t>();
    number_range_check<std::int16_t>();
    number_range_check<std::uint16_t>();
    number_range_check<std::int32_t>();
    number_range_check<std::uint32_t>();
    number_range_check<std::int64_t>();
    number_range_check<std::uint64_t>();
}
#endif

// [span.sub]: first(count) and last(count) need count <= size(). A sub-view of a typed array is made only
// after this check, for every count up to one past the size.
TEST_CASE("validity: check_count for every count up to one past the size")
{
    for (std::size_t size = 0; size <= 4; ++size)
        for (std::size_t count = 0; count <= size + 1; ++count) {
            auto const r = validity::check_count(count, size);
            if (count <= size)
                CHECK(r.has_value());
            else
                CHECK_EQ(r.error(), error::index_out_of_bounds);
        }
}

// A bignum magnitude is read into an integer of a fixed width. A magnitude with more bytes than the width,
// after the leading zero bytes are removed, does not fit and must be refused before it is read.
TEST_CASE("validity: check_magnitude_size for every size up to one past the width")
{
    for (std::size_t width : {std::size_t{8}, std::size_t{16}})
        for (std::size_t size = 0; size <= width + 1; ++size) {
            auto const r = validity::check_magnitude_size(size, width);
            if (size <= width)
                CHECK(r.has_value());
            else
                CHECK_EQ(r.error(), error::number_out_of_range);
        }
}

#include "small_stack.hpp"
#include "reference.hpp"
#include "owned_copy.hpp"
#include <cbor/shared.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace std::literals;
using cbor::error;
using cbor::major_type;
using cbor::value_sharing;
using cbor::detail::validity;

// The end of the stack of the thread bounds every reader with the one check that every recursive reader calls: an
// item that every stack holds passes, and an input deeper than a small stack holds is refused on a thread with that
// stack.
TEST_CASE("validity: the stack of the thread bounds the decode and the diagnostic notation")
{
    std::string const held = std::string(test::levels_every_stack_holds, '\x81') + '\x00';
    std::string const deep = std::string(test::levels_beyond_small_stack, '\x81') + '\x00';
    CHECK(test::value_of(*test::lazy_of_copy(held)).has_value());
    CHECK(cbor::diagnostic_notation(held).has_value());
    CHECK_EQ(test::error_on_small_stack([&deep] { return test::value_of(*test::lazy_of_copy(deep)); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&deep] { return cbor::diagnostic_notation(deep); }),
             error::nesting_depth_exceeded);
}

namespace
{

// A recursion that calls the one check at each level, as each recursive reader does, and counts its levels.
std::expected<std::size_t, error> levels_until_refused(std::size_t const level)
{
    if (auto const r = validity::check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    return levels_until_refused(level + 1).transform([](std::size_t const n) { return n + 1; });
}

} // namespace

// check_recursion_limit passes on the main thread, and is a constant expression at compile time, where the compiler
// bounds the recursion. A recursion that calls it at each level stops with nesting_depth_exceeded on a small stack,
// and does not run past the stack.
TEST_CASE("validity: check_recursion_limit stops a recursion before the stack ends")
{
    CHECK(validity::check_recursion_limit().has_value());
    constexpr bool allowed_at_compile_time = validity::check_recursion_limit().has_value();
    CHECK(allowed_at_compile_time);
    CHECK_EQ(test::error_on_small_stack([] { return levels_until_refused(0); }), error::nesting_depth_exceeded);
}

namespace
{

// The factors and their results serve the checks against an exact int128 product below.
#ifdef CBOR_HAS_INT128
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
            fits[i][j] = cbor::detail::validity::checked_add(factors[i], factors[j]).has_value();
    return fits;
}();

constexpr auto products_at_compile_time = [] {
    std::array<std::array<bool, factors.size()>, factors.size()> fits{};
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j)
            fits[i][j] = cbor::detail::validity::checked_mul(factors[i], factors[j]).has_value();
    return fits;
}();
#endif

} // namespace

// RFC 8949 5.6: a second key equal to a key already met makes the map not valid. The input set is {false,
// true}.
TEST_CASE("validity: check_key_unique for both inputs")
{
    CHECK(validity::check_key_unique(false).has_value());
    CHECK_EQ(validity::check_key_unique(true).error(), error::duplicate_key);
}

#ifdef CBOR_HAS_INT128
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

// A shared reference names one of the values that were marked before it. The decoder and the lazy reader read
// the index through this one check, so an index that no mark has reached gives the same error on both paths,
// and the index is converted to std::size_t only after it is known to fit.
TEST_CASE("value_sharing: check_sharedref_index for up to three marks")
{
    std::array<std::uint64_t, 6> const arguments{0, 1, 2, 3, 4, std::numeric_limits<std::uint64_t>::max()};
    for (std::size_t marked = 0; marked <= 3; ++marked)
        for (std::uint64_t const argument : arguments) {
            auto const r = value_sharing::check_sharedref_index(argument, marked);
            if (argument >= marked)
                CHECK_EQ(r.error(), error::sharedref_index_not_marked);
            else
                CHECK_EQ(std::to_underlying(*r), argument);
        }
}

template <class Argument>
constexpr bool sharedref_index_accepts =
    requires(Argument const argument, void (*const take)(value_sharing::index)) { take(argument); };

// An index into the marks of tags 28 and 29 is a type of its own. A byte offset or an index of an array that
// stands where a shared reference index is expected does not compile, so the two cannot be mixed up silently.
TEST_CASE("value_sharing: index takes no byte offset and no other index")
{
    CHECK(sharedref_index_accepts<value_sharing::index>);
    CHECK_FALSE(sharedref_index_accepts<validity::index>);
    CHECK_FALSE(sharedref_index_accepts<validity::offset>);
    CHECK_FALSE(sharedref_index_accepts<std::size_t>);
}

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

// RFC 8949 5.6 lets a map repeat a key outside a deterministic profile. Two maps are equal only when each pair
// occurs the same number of times in both. A check that each pair has a partner in the other map finds the
// first two maps equal, because every pair of each occurs in the other.
TEST_CASE("validity: keys_equivalent compares the pairs of two maps as multisets")
{
    std::string_view const kkj = "\xa3\x61k\x01\x61k\x01\x61j\x01"sv;
    std::string_view const kjj = "\xa3\x61k\x01\x61j\x01\x61j\x01"sv;
    std::string_view const jkk = "\xa3\x61j\x01\x61k\x01\x61k\x01"sv;
    std::string_view const kk = "\xa2\x61k\x01\x61k\x01"sv;
    std::string_view const kj = "\xa2\x61k\x01\x61j\x01"sv;
    CHECK_FALSE(*validity::keys_equivalent<false>(kkj, validity::offset{}, kjj, validity::offset{}));
    CHECK_FALSE(*validity::keys_equivalent<false>(kjj, validity::offset{}, kkj, validity::offset{}));
    CHECK(*validity::keys_equivalent<false>(kkj, validity::offset{}, jkk, validity::offset{}));
    CHECK(*validity::keys_equivalent<false>(jkk, validity::offset{}, kkj, validity::offset{}));
    CHECK_FALSE(*validity::keys_equivalent<false>(kk, validity::offset{}, kj, validity::offset{}));
    CHECK(*validity::keys_equivalent<false>(kk, validity::offset{}, kk, validity::offset{}));
}

// An offset, an element size and an element index are three kinds of number. Each has its own type, so a call that
// swaps two of them does not compile. The checks ask the compiler whether each swapped call is well-formed.
TEST_CASE("validity: element_offset refuses a swapped offset, size or index")
{
    using offset = validity::offset;
    using index = validity::index;
    auto const callable = [](auto const a, auto const b, auto const c) {
        return requires { validity::element_offset(a, b, c); };
    };
    CHECK(callable(offset{}, std::size_t{}, index{}));
    CHECK_FALSE(callable(std::size_t{}, offset{}, index{}));
    CHECK_FALSE(callable(index{}, std::size_t{}, offset{}));
    CHECK_FALSE(callable(offset{}, index{}, std::size_t{}));
    CHECK_FALSE(callable(index{}, std::size_t{}, index{}));
    CHECK_FALSE(callable(offset{}, std::size_t{}, offset{}));
    CHECK_FALSE(callable(std::size_t{}, std::size_t{}, std::size_t{}));
    CHECK_FALSE(callable(offset{}, std::size_t{}, std::uint64_t{}));
    // Element 3 of 8 bytes each starts 24 bytes after the first element.
    CHECK_EQ(validity::element_offset(offset{5}, 8, index{3}), offset{29});
}

// next, distance, subspan and substr take an offset where an offset is meant and a size where a size is meant. A
// size, an index or a plain integer in the place of an offset does not compile, and an offset does not convert back
// to a size. The result of check_index is an index and not an offset.
TEST_CASE("validity: an offset, a size and an index do not take the place of each other")
{
    using offset = validity::offset;
    using index = validity::index;
    auto const next_callable = [](auto const a, auto const b) { return requires { validity::next(a, b); }; };
    CHECK(next_callable(offset{}, std::size_t{}));
    CHECK_FALSE(next_callable(std::size_t{}, std::size_t{}));
    CHECK_FALSE(next_callable(index{}, std::size_t{}));
    CHECK_FALSE(next_callable(std::size_t{}, offset{}));
    CHECK_FALSE(next_callable(offset{}, offset{}));
    auto const subspan_callable = [](auto const a) {
        return requires { validity::subspan(std::string_view{}, a); };
    };
    CHECK(subspan_callable(offset{}));
    CHECK_FALSE(subspan_callable(std::size_t{}));
    CHECK_FALSE(subspan_callable(index{}));
    CHECK_FALSE(std::is_convertible_v<offset, std::size_t>);
    CHECK_FALSE(std::is_convertible_v<std::size_t, offset>);
    CHECK_FALSE(std::is_convertible_v<index, offset>);
    CHECK_FALSE(std::is_convertible_v<offset, index>);
    CHECK(std::same_as<decltype(validity::check_index(0, 1))::value_type, index>);
}

// subspan accepts every offset from the start to the end of its input. An offset at the end gives an empty view, and
// an offset after the end is a wrong use of the library, so it fails loudly and does not read past the input.
TEST_CASE("validity: subspan accepts an offset at the end and refuses an offset after it")
{
    std::string_view const encoded = "abc";
    CHECK_EQ(validity::subspan(encoded, validity::offset{}), "abc");
    CHECK_EQ(validity::subspan(encoded, validity::offset{1}), "bc");
    CHECK(validity::subspan(encoded, validity::offset{3}).empty());
    std::array<char const, 3> const bytes{'a', 'b', 'c'};
    CHECK_EQ(validity::subspan(std::span<char const>(bytes), validity::offset{2}).size(), 1u);
    CHECK(std::same_as<decltype(validity::subspan(std::span<char const>(bytes), validity::offset{})),
                       std::span<char const>>);
#if defined(__cpp_exceptions)
    CHECK_THROWS_AS(static_cast<void>(validity::subspan(encoded, validity::offset{4})), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(validity::subspan(std::span<char const>(bytes), validity::offset{4})),
                    std::logic_error);
#endif
    // substr refuses a piece that ends after the input with an error value, because the size comes from the input.
    CHECK_EQ(*validity::substr(encoded, validity::offset{1}, 2), "bc");
    CHECK_EQ(validity::substr(encoded, validity::offset{1}, 3).error(), error::too_little_data);
    CHECK_EQ(validity::distance(encoded, encoded.substr(2)), validity::offset{2});
    CHECK_EQ(validity::next(validity::offset{2}, 3), validity::offset{5});
}

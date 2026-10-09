#include "binding.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each limit is read at the bound, one below and one above it. The expectation is the decision of the owner of
// 2026-10-09: a value up to the limit passes, a value above it fails with the error of that limit.

namespace
{

std::string text_of_length(std::size_t const n)
{
    if (n < 24)
        return std::string(1, static_cast<char>(0x60 + n)) + std::string(n, 'a');
    return "\x78"s + static_cast<char>(n) + std::string(n, 'a');
}

std::string bytes_of_length(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x40 + n)) + std::string(n, 'b');
}

std::string array_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x80 + n)) + std::string(n, '\x00');
}

std::string map_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0xa0 + n)) + std::string(2 * n, '\x00');
}

} // namespace

// The defaults hold where no macro overrides them: the nesting depth 128, every other limit none.
TEST_CASE("limits: the defaults")
{
    CHECK_EQ(std::size_t{cbor::limits.nesting_depth}, 128u);
    CHECK_EQ(std::size_t{cbor::limits.decoded_bytes}, std::numeric_limits<std::size_t>::max());
    CHECK_EQ(std::size_t{cbor::limits.string_length}, std::numeric_limits<std::size_t>::max());
    CHECK_EQ(std::size_t{cbor::limits.container_elements}, std::numeric_limits<std::size_t>::max());
    CHECK_EQ(std::size_t{cbor::limits.input_bytes}, std::numeric_limits<std::size_t>::max());
}

// A value above the hard bound is a fault of the caller, so operator= throws std::logic_error and the value before
// stays. The aggregate form checks the same bound.
TEST_CASE("limits: operator= above the bound throws std::logic_error")
{
    CHECK_THROWS_AS(cbor::limits.nesting_depth = cbor::validity::nesting_depth_limit + 1, std::logic_error);
    CHECK_EQ(std::size_t{cbor::limits.nesting_depth}, cbor::validity::nesting_depth_default);
    CHECK_THROWS_AS((cbor::limits = {.nesting_depth = cbor::validity::nesting_depth_limit + 1}), std::logic_error);
    CHECK_EQ(std::size_t{cbor::limits.nesting_depth}, cbor::validity::nesting_depth_default);
    CHECK_EQ(std::size_t{cbor::limits.string_length}, std::numeric_limits<std::size_t>::max());
    {
        test::limits_guard const at{{.nesting_depth = cbor::validity::nesting_depth_limit}};
        CHECK_EQ(std::size_t{cbor::limits.nesting_depth}, cbor::validity::nesting_depth_limit);
    }
    {
        test::limits_guard const all{{.nesting_depth = 1, .decoded_bytes = 2, .string_length = 3,
                                      .container_elements = 4, .input_bytes = 5}};
        CHECK_EQ(std::size_t{cbor::limits.nesting_depth}, 1u);
        CHECK_EQ(std::size_t{cbor::limits.decoded_bytes}, 2u);
        CHECK_EQ(std::size_t{cbor::limits.string_length}, 3u);
        CHECK_EQ(std::size_t{cbor::limits.container_elements}, 4u);
        CHECK_EQ(std::size_t{cbor::limits.input_bytes}, 5u);
    }
    CHECK_EQ(std::size_t{cbor::limits.input_bytes}, std::numeric_limits<std::size_t>::max());
}

// The length of each string head is checked, in the immediate form, in the form with a one-byte argument, and in
// every reader: the well-formedness walk, the lazy item, the decoder of a binding and the diagnostic notation.
TEST_CASE("limits: string_length on each string head")
{
    test_binding binding;
    for (std::size_t const limit : {3uz, 30uz}) {
        test::limits_guard const guard{{.string_length = limit}};
        for (std::size_t const n : {limit - 1, limit, limit + 1}) {
            CAPTURE(n);
            for (std::string const &encoded : {text_of_length(n), "\x82"s + text_of_length(n) + "\x00"s}) {
                bool const passes = n <= limit;
                auto const size = cbor::item_size(encoded);
                auto const decoded = cbor::lazy_decode(binding, *cbor::lazy::from(encoded));
                auto const notation = cbor::diagnostic_notation(encoded);
                CHECK_EQ(size.has_value(), passes);
                CHECK_EQ(decoded.has_value(), passes);
                CHECK_EQ(notation.has_value(), passes);
                if (!passes) {
                    CHECK_EQ(size.error(), error::string_length_exceeded);
                    CHECK_EQ(decoded.error(), error::string_length_exceeded);
                    CHECK_EQ(notation.error(), error::string_length_exceeded);
                }
            }
            auto const text = cbor::lazy::from(text_of_length(n))->get<std::string_view>();
            CHECK_EQ(text.has_value(), n <= limit);
        }
    }
    test::limits_guard const guard{{.string_length = 3}};
    CHECK(cbor::item_size(bytes_of_length(3)).has_value());
    CHECK_EQ(cbor::item_size(bytes_of_length(4)).error(), error::string_length_exceeded);
}

// The count of each array and map head is checked; a map counts its pairs.
TEST_CASE("limits: container_elements on each array and map head")
{
    test_binding binding;
    test::limits_guard const guard{{.container_elements = 2}};
    for (std::size_t const n : {1uz, 2uz, 3uz}) {
        CAPTURE(n);
        bool const passes = n <= 2;
        for (std::string const &encoded : {array_of(n), map_of(n), "\x81"s + array_of(n)}) {
            auto const size = cbor::item_size(encoded);
            auto const decoded = cbor::lazy_decode(binding, *cbor::lazy::from(encoded));
            auto const notation = cbor::diagnostic_notation(encoded);
            CHECK_EQ(size.has_value(), passes);
            CHECK_EQ(decoded.has_value(), passes);
            CHECK_EQ(notation.has_value(), passes);
            if (!passes) {
                CHECK_EQ(size.error(), error::container_elements_exceeded);
                CHECK_EQ(decoded.error(), error::container_elements_exceeded);
                CHECK_EQ(notation.error(), error::container_elements_exceeded);
            }
        }
        CHECK_EQ(cbor::lazy::from(array_of(n))->elements().has_value(), passes);
        CHECK_EQ(cbor::lazy::from(map_of(n))->entries().has_value(), passes);
    }
}

// The size of the input is checked once, where the input enters the library.
TEST_CASE("limits: input_bytes on entry")
{
    test::limits_guard const guard{{.input_bytes = 3}};
    for (std::string const &encoded : {"\x81\x01"s, "\x82\x01\x02"s, "\x83\x01\x02\x03"s}) {
        CAPTURE(encoded.size());
        bool const passes = encoded.size() <= 3;
        auto const size = cbor::item_size(encoded);
        auto const lazy = cbor::lazy::from(encoded);
        auto const notation = cbor::diagnostic_notation(encoded);
        auto const first = cbor::at_path<"$[0]", std::int64_t>(encoded);
        CHECK_EQ(size.has_value(), passes);
        CHECK_EQ(lazy.has_value(), passes);
        CHECK_EQ(notation.has_value(), passes);
        CHECK_EQ(first.has_value(), passes);
        if (!passes) {
            CHECK_EQ(size.error(), error::input_bytes_exceeded);
            CHECK_EQ(lazy.error(), error::input_bytes_exceeded);
            CHECK_EQ(notation.error(), error::input_bytes_exceeded);
            CHECK_EQ(first.error(), error::input_bytes_exceeded);
        }
    }
}

// The decoder of a binding counts the bytes of each string and one value for each element as it builds them.
TEST_CASE("limits: decoded_bytes counted by lazy_decode")
{
    test_binding binding;
    for (std::size_t const limit : {2uz, 3uz, 4uz}) {
        test::limits_guard const guard{{.decoded_bytes = limit}};
        auto const decoded = cbor::lazy_decode(binding, *cbor::lazy::from("\x63\x61\x62\x63"s));
        CHECK_EQ(decoded.has_value(), limit >= 3);
        if (limit < 3)
            CHECK_EQ(decoded.error(), error::decoded_bytes_exceeded);
    }
    constexpr std::size_t element = sizeof(test_binding::value);
    for (std::size_t const limit : {2 * element - 1, 2 * element, 2 * element + 1}) {
        test::limits_guard const guard{{.decoded_bytes = limit}};
        auto const decoded = cbor::lazy_decode(binding, *cbor::lazy::from("\x82\x00\x00"s));
        CHECK_EQ(decoded.has_value(), limit >= 2 * element);
        if (limit < 2 * element)
            CHECK_EQ(decoded.error(), error::decoded_bytes_exceeded);
    }
}

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1590)]] note {
    std::string text;
    std::vector<std::int32_t> numbers;
};

struct [[=cbor::tag(1591)]] names {
    std::vector<std::string> list;
};

} // namespace

// databind counts the characters of a std::string and one element for each element of a container.
TEST_CASE("limits: decoded_bytes counted by databind")
{
    constexpr std::size_t numbers = 2 * sizeof(int);
    for (std::size_t const limit : {numbers - 1, numbers, numbers + 1}) {
        test::limits_guard const guard{{.decoded_bytes = limit}};
        auto const decoded = cbor::databind<std::vector<int>>::decode("\x82\x01\x02"s);
        CHECK_EQ(decoded.has_value(), limit >= numbers);
        if (limit < numbers)
            CHECK_EQ(decoded.error(), error::decoded_bytes_exceeded);
    }
    for (std::size_t const limit : {2uz, 3uz, 4uz}) {
        test::limits_guard const guard{{.decoded_bytes = limit}};
        auto const decoded = cbor::databind<std::string>::decode("\x63\x61\x62\x63"s);
        CHECK_EQ(decoded.has_value(), limit >= 3);
        if (limit < 3)
            CHECK_EQ(decoded.error(), error::decoded_bytes_exceeded);
    }
}

// schema counts the characters of the text and the elements of the list as it allocates them.
TEST_CASE("limits: decoded_bytes counted by schema decode, and the string and container limits of schema")
{
    auto const encoded = cbor::schema<note>::encode(note{"abc", {1, 2}});
    REQUIRE(encoded.has_value());
    constexpr std::size_t total = 3 + 2 * sizeof(std::int32_t);
    for (std::size_t const limit : {total - 1, total, total + 1}) {
        test::limits_guard const guard{{.decoded_bytes = limit}};
        auto const decoded = cbor::schema<note>::decode(*encoded);
        CHECK_EQ(decoded.has_value(), limit >= total);
        if (limit < total)
            CHECK_EQ(decoded.error(), error::decoded_bytes_exceeded);
    }
    // The list of std::int32_t is a typed array (RFC 8746), a tag over a byte string of 8 bytes.
    constexpr std::size_t typed_array_bytes = 2 * sizeof(std::int32_t);
    for (std::size_t const limit : {typed_array_bytes - 1, typed_array_bytes, typed_array_bytes + 1}) {
        test::limits_guard const guard{{.string_length = limit}};
        auto const decoded = cbor::schema<note>::decode(*encoded);
        CHECK_EQ(decoded.has_value(), limit >= typed_array_bytes);
        if (limit < typed_array_bytes)
            CHECK_EQ(decoded.error(), error::string_length_exceeded);
    }
    auto const list = cbor::schema<names>::encode(names{{"a", "b"}});
    REQUIRE(list.has_value());
    for (std::size_t const limit : {1uz, 2uz, 3uz}) {
        test::limits_guard const guard{{.container_elements = limit}};
        auto const decoded = cbor::schema<names>::decode(*list);
        CHECK_EQ(decoded.has_value(), limit >= 2);
        if (limit < 2)
            CHECK_EQ(decoded.error(), error::container_elements_exceeded);
    }
    {
        test::limits_guard const guard{{.input_bytes = encoded->size() - 1}};
        CHECK_EQ(cbor::schema<note>::decode(*encoded).error(), error::input_bytes_exceeded);
    }
}

#endif

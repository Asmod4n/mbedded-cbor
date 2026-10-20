#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <concepts>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "owned_copy.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

struct numbers {
    std::shared_ptr<std::vector<int> const> owner;
    auto begin() const
    {
        return owner->begin();
    }
    auto end() const
    {
        return owner->end();
    }
};

cbor::result<numbers> numbers_of()
{
    return numbers{std::make_shared<std::vector<int> const>(std::vector{1, 2, 3})};
}

std::expected<numbers, error> plain_numbers_of()
{
    return numbers_of();
}

cbor::result<numbers> no_numbers()
{
    return std::unexpected(error::incorrect_type);
}

int sum_of(numbers const &n)
{
    int s = 0;
    for (int const x : n)
        s += x;
    return s;
}

} // namespace

// Before C++23 (P2718) a range-for destroys the temporary result before it calls begin(). The rvalue forms of
// operator* and value give the value itself, so the loop keeps it alive on GCC 14 and clang 18 too. The test runs
// under AddressSanitizer, which reports a read of the destroyed temporary.
TEST_CASE("result: a range-for over the value of a temporary result reads a living value")
{
    int sum = 0;
    for (int const x : *numbers_of())
        sum += x;
    for (int const x : numbers_of().value())
        sum += x;
    for (int const x : *numbers_of().and_then([](numbers const &) { return numbers_of(); }))
        sum += x;
    for (int const x : *numbers_of().and_then([](numbers const &) { return plain_numbers_of(); }))
        sum += x;
    CHECK_EQ(sum, 24);
}

// The library loop forms of the documentation, on a lazy from a message.
TEST_CASE("result: a range-for over elements and entries of a temporary result")
{
    auto const doc = *test::lazy_of_copy("\xa1\x61\x6b\x83\x01\x02\x03"s);
    std::int64_t sum = 0;
    for (auto const e : *doc.at("k").and_then([](cbor::lazy const &l) { return l.elements(); }))
        sum += e->get<std::int64_t>().value_or(0);
    std::size_t pairs = 0;
    for (auto const p : *doc.entries()) {
        CHECK(p.has_value());
        ++pairs;
    }
    CHECK_EQ(sum, 6);
    CHECK_EQ(pairs, 1u);
}

// A named result gives references, as std::expected does; only a temporary gives the value.
TEST_CASE("result: the lvalue forms give references and the rvalue forms give values")
{
    cbor::result<std::string> named = "text"s;
    CHECK(std::same_as<decltype(*named), std::string &>);
    CHECK(std::same_as<decltype(*std::as_const(named)), std::string const &>);
    CHECK(std::same_as<decltype(named.value()), std::string &>);
    CHECK(std::same_as<decltype(*std::move(named)), std::string>);
    CHECK(std::same_as<decltype(std::move(named).value()), std::string>);
    CHECK(noexcept(*std::move(named)));
    CHECK_EQ(*std::move(named), "text");
    CHECK_THROWS_AS((void)cbor::result<int>(std::unexpected(error::syntax_error)).value(), std::bad_expected_access<error>);
}

// Each monadic call returns a result, whether the callable returns a result or a std::expected, so a chain keeps the
// rvalue forms at each step.
TEST_CASE("result: and_then, or_else, transform and transform_error return a result")
{
    auto const a = numbers_of().and_then([](numbers const &) { return numbers_of(); });
    auto const b = numbers_of().and_then([](numbers const &) { return plain_numbers_of(); });
    auto const c = no_numbers().or_else([](error) { return numbers_of(); });
    auto const d = numbers_of().transform([](numbers const &n) { return sum_of(n); });
    auto const e = no_numbers().transform_error([](error) { return error::syntax_error; });
    auto const f = no_numbers().transform_error([](error) { return 7; });
    CHECK(std::same_as<decltype(a), cbor::result<numbers> const>);
    CHECK(std::same_as<decltype(b), cbor::result<numbers> const>);
    CHECK(std::same_as<decltype(c), cbor::result<numbers> const>);
    CHECK(std::same_as<decltype(d), cbor::result<int> const>);
    CHECK(std::same_as<decltype(e), cbor::result<numbers> const>);
    CHECK(std::same_as<decltype(f), std::expected<numbers, int> const>);
    CHECK_EQ(sum_of(*c), 6);
    CHECK_EQ(d, 6);
    CHECK_EQ(e.error(), error::syntax_error);
    CHECK_EQ(f.error(), 7);
    CHECK_EQ(no_numbers().and_then([](numbers const &) { return numbers_of(); }).error(), error::incorrect_type);
}

// result<void> has no value to give, and its monadic calls take no argument.
TEST_CASE("result: result<void>")
{
    cbor::result<void> const done;
    cbor::result<void> const failed = std::unexpected(error::too_little_data);
    CHECK(done.has_value());
    CHECK_EQ(failed.error(), error::too_little_data);
    auto const next = done.and_then([] { return cbor::result<int>(5); });
    auto const plain = done.and_then([] { return std::expected<void, error>(); });
    auto const counted = done.transform([] { return 1; });
    auto const repaired = failed.or_else([](error) { return cbor::result<void>(); });
    CHECK(std::same_as<decltype(next), cbor::result<int> const>);
    CHECK(std::same_as<decltype(plain), cbor::result<void> const>);
    CHECK_EQ(next, 5);
    CHECK_EQ(counted, 1);
    CHECK(repaired.has_value());
    CHECK_EQ(done, cbor::result<void>());
    CHECK_EQ(failed, std::unexpected(error::too_little_data));
}

// Every comparison that std::expected has works on result with no ambiguity, a value of another type included.
TEST_CASE("result: comparisons")
{
    cbor::result<std::int64_t> const forty_two = 42;
    cbor::result<std::int64_t> const bad = std::unexpected(error::key_not_found);
    std::expected<std::int64_t, error> const plain = 42;
    CHECK_EQ(forty_two, 42);
    CHECK_EQ(42, forty_two);
    CHECK_EQ(forty_two, cbor::result<std::int64_t>(42));
    CHECK_NE(forty_two, bad);
    CHECK_EQ(forty_two, plain);
    CHECK_EQ(plain, forty_two);
    CHECK_EQ(bad, std::unexpected(error::key_not_found));
    CHECK_EQ(cbor::result<std::string_view>("ab"sv), "ab");
    CHECK_EQ(std::optional<cbor::result<int>>(cbor::result<int>(1)), cbor::result<int>(1));
}

// A result converts to the std::expected that it derives from, and back, so code written for std::expected keeps
// working.
TEST_CASE("result: conversion to and from std::expected")
{
    auto const to_plain = [](std::expected<numbers, error> const &x) { return x.has_value(); };
    CHECK(to_plain(numbers_of()));
    std::expected<numbers, error> const kept = numbers_of();
    cbor::result<numbers> const back = kept;
    CHECK_EQ(sum_of(*back), 6);
    constexpr cbor::result<int> compile_time = cbor::result<int>(2).transform([](int const x) { return x * 3; });
    CHECK_EQ(compile_time, 6);
}

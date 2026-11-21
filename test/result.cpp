#include <cbor/lazy.hpp>
#include <cbor/result.hpp>
#include <cbor/structs.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

using cbor::error;

namespace
{

// The document {"a": 7}, written by the encoder of the library.
cbor::lazy make_document()
{
    auto encoded = cbor::encode<std::map<std::string, std::int64_t>>({{"a", 7}});
    REQUIRE(encoded.has_value());
    auto const doc = cbor::lazy::adopt(std::move(*encoded));
    REQUIRE(doc.has_value());
    return *doc;
}

constexpr auto read_number = [](cbor::lazy const &l) { return l.get<std::int64_t>(); };

constexpr auto size_of_encoded = [](cbor::lazy const &l) { return l.encoded().size(); };

constexpr auto code_of = [](error const e) { return static_cast<int>(e); };

struct read_a {
    cbor::lazy doc;

    cbor::result<cbor::lazy> operator()(error const &) const
    {
        return doc.at("a");
    }
};

using lazy_result = cbor::result<cbor::lazy>;

using number_result = cbor::result<std::int64_t>;

template <class R>
concept deref_of_call = requires(R (*f)()) { *f(); };

template <class R>
concept arrow_of_call = requires(R (*f)()) { f()->size(); };

template <class R>
concept value_of_call = requires(R (*f)()) { f().value(); };

template <class R>
concept error_of_call = requires(R (*f)()) { f().error(); };

template <class R>
concept deref_of_moved = requires(R r) { *std::move(r); };

template <class R>
concept loop_over_call = requires(R (*f)()) {
    { *f() } -> std::same_as<typename R::value_type const &>;
};

template <class R, class F>
concept and_then_takes = requires(R r, F f) { std::forward<R>(r).and_then(f); };

template <class R, class F>
concept transform_takes = requires(R r, F f) { std::forward<R>(r).transform(f); };

template <class R, class F>
concept or_else_takes = requires(R r, F f) { std::forward<R>(r).or_else(f); };

template <class R, class F>
concept transform_error_takes = requires(R r, F f) { std::forward<R>(r).transform_error(f); };

} // namespace

// A read that finds its value gives a result that holds it. Each reader of a named result gives that value, on a
// const and on a non-const result.
TEST_CASE("result: the readers of a named result give the value")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    REQUIRE(a.has_value());
    CHECK(static_cast<bool>(a));
    auto const number = a->get<std::int64_t>();
    CHECK_EQ(*number, 7);
    CHECK_EQ(number.value(), 7);
    CHECK_EQ(number.value_or(0), 7);
    CHECK_EQ((*a).encoded(), a.value().encoded());
    auto changed = a->get<std::int64_t>();
    *changed = 8;
    CHECK_EQ(changed.value(), 8);
    changed.value() = 9;
    CHECK_EQ(*changed, 9);
    auto other = doc.at("a");
    CHECK_EQ(other->encoded(), a->encoded());
    CHECK_EQ(std::move(changed).value_or(0), 9);
}

// A read that fails gives a result that holds its error. The readers give that error, value() throws, and value_or
// gives the fallback.
TEST_CASE("result: the readers of a named result give the error")
{
    cbor::lazy const doc = make_document();
    auto const b = doc.at("b");
    CHECK_FALSE(b.has_value());
    CHECK_FALSE(static_cast<bool>(b));
    CHECK_EQ(b.error(), error::key_not_found);
    CHECK_THROWS_AS(static_cast<void>(b.value()), std::bad_expected_access<error>);
    auto const a = doc.at("a");
    auto const wrong = a->get<bool>();
    CHECK_EQ(wrong.error(), error::incorrect_type);
    CHECK_EQ(wrong.value_or(true), true);
    auto moved = a->get<bool>();
    CHECK_EQ(std::move(moved).value_or(true), true);
}

// has_value, the conversion to bool and value_or give no reference into the result, so they also work on the result
// of a call.
TEST_CASE("result: has_value, bool and value_or work on a temporary")
{
    cbor::lazy const doc = make_document();
    CHECK(doc.at("a").has_value());
    CHECK_FALSE(doc.at("b").has_value());
    CHECK(static_cast<bool>(doc.size()));
    CHECK_EQ(doc.size().value_or(0), 1u);
    CHECK_EQ(doc.type().value_or(cbor::type::array), cbor::type::map);
    CHECK_EQ(doc.empty().value_or(true), false);
}

// and_then gives the value to the next read, and gives the error of the first read without a call.
TEST_CASE("result: and_then on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a.and_then(read_number);
    CHECK_EQ(number, 7);
    auto const b = doc.at("b");
    bool called = false;
    auto const missing = b.and_then([&called](cbor::lazy const &l) {
        called = true;
        return l.get<std::int64_t>();
    });
    CHECK_FALSE(called);
    CHECK_EQ(missing.error(), error::key_not_found);
}

// transform gives a result that holds the value of the function, or the error of the read.
TEST_CASE("result: transform on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const size = a.transform(size_of_encoded);
    CHECK_EQ(size, std::size_t{1});
    auto const b = doc.at("b");
    auto const missing = b.transform(size_of_encoded);
    CHECK_EQ(missing.error(), error::key_not_found);
}

// or_else gives a copy of the value without a call, and gives the error to the function.
TEST_CASE("result: or_else on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    bool called = false;
    auto const kept = a.or_else([&called, &doc](error const &) {
        called = true;
        return doc.at("a");
    });
    CHECK_FALSE(called);
    CHECK_EQ(kept->encoded(), a->encoded());
    auto const b = doc.at("b");
    auto const fallback = b.or_else(read_a{doc});
    REQUIRE(fallback.has_value());
    CHECK_EQ(fallback->encoded(), a->encoded());
}

// transform_error gives a copy of the value, or a result that holds the value of the function for the error.
TEST_CASE("result: transform_error on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a->get<std::int64_t>();
    auto const kept = number.transform_error(code_of);
    CHECK_EQ(*kept, 7);
    auto const b = doc.at("b");
    auto const code = b.transform_error(code_of);
    CHECK_EQ(code.error(), static_cast<int>(error::key_not_found));
}

// == compares as two std::expected compare: two results, a result with a value, and a result with an error.
TEST_CASE("result: == compares the value or the error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a->get<std::int64_t>();
    auto const same = a->get<std::int64_t>();
    auto const wrong = same.transform([](std::int64_t const n) { return n + 1; });
    CHECK(number == same);
    CHECK_FALSE(number == wrong);
    CHECK(number == 7);
    CHECK_FALSE(number == 8);
    CHECK_FALSE(number == std::unexpected(error::key_not_found));
    auto const b = doc.at("b");
    auto const missing = b.and_then(read_number);
    auto const also_missing = b.and_then(read_number);
    CHECK(missing == also_missing);
    CHECK_FALSE(missing == number);
    CHECK(missing == std::unexpected(error::key_not_found));
    CHECK_FALSE(missing == std::unexpected(error::incorrect_type));
    CHECK_FALSE(missing == 7);
}

// A reference or a pointer into the result of a call ends with the full expression. So each reader that gives one,
// and each monadic form, does not compile on a temporary or a moved result: the program names the result first.
TEST_CASE("result: a reader of a temporary result does not compile")
{
    CHECK_FALSE(deref_of_call<lazy_result>);
    CHECK_FALSE(deref_of_call<number_result>);
    CHECK_FALSE(arrow_of_call<lazy_result>);
    CHECK_FALSE(value_of_call<lazy_result>);
    CHECK_FALSE(value_of_call<number_result>);
    CHECK_FALSE(error_of_call<lazy_result>);
    CHECK_FALSE(error_of_call<number_result>);
    CHECK_FALSE(deref_of_moved<lazy_result>);
    CHECK_FALSE(deref_of_moved<number_result>);
    CHECK_FALSE(loop_over_call<lazy_result>);
    CHECK_FALSE((std::is_constructible_v<std::expected<std::int64_t, error>, number_result>));
    CHECK_FALSE((std::is_convertible_v<number_result, std::expected<std::int64_t, error>>));
}

// The monadic forms read the value or the error in place, so they take a named result only. The same call on a named
// const result compiles.
TEST_CASE("result: a monadic form of a temporary result does not compile")
{
    using after = decltype(read_number);
    using measure = decltype(size_of_encoded);
    using code = decltype(code_of);
    CHECK((and_then_takes<lazy_result const &, after>));
    CHECK_FALSE((and_then_takes<lazy_result &&, after>));
    CHECK_FALSE((and_then_takes<lazy_result const &&, after>));
    CHECK((transform_takes<lazy_result const &, measure>));
    CHECK_FALSE((transform_takes<lazy_result &&, measure>));
    CHECK_FALSE((transform_takes<lazy_result const &&, measure>));
    CHECK((or_else_takes<lazy_result const &, read_a>));
    CHECK_FALSE((or_else_takes<lazy_result &&, read_a>));
    CHECK_FALSE((or_else_takes<lazy_result const &&, read_a>));
    CHECK((transform_error_takes<lazy_result const &, code>));
    CHECK_FALSE((transform_error_takes<lazy_result &&, code>));
    CHECK_FALSE((transform_error_takes<lazy_result const &&, code>));
}

// Only the library makes a result. A program copies and moves one that a read gave, and makes none of its own.
TEST_CASE("result: a program cannot make a result")
{
    CHECK_FALSE((std::is_constructible_v<number_result, std::int64_t>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::unexpected<error>>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::unexpect_t, error>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::in_place_t, std::int64_t>));
    CHECK_FALSE(std::is_default_constructible_v<number_result>);
    CHECK_FALSE((std::is_constructible_v<lazy_result, cbor::lazy>));
    CHECK(std::is_copy_constructible_v<number_result>);
    CHECK(std::is_move_constructible_v<number_result>);
    CHECK(std::is_copy_assignable_v<number_result>);
    CHECK(std::is_move_assignable_v<number_result>);
}

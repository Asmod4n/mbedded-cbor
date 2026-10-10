#include "binding.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/basics.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

// The section "Errors" states that a cbor::error is a std::error_code with its own category, and that four errors
// compare equal to a std::errc.
TEST_CASE("doc basics: an error is a std::error_code")
{
    std::error_code const e = error::key_not_found;
    CHECK_EQ(e.message(), "key not found");
    CHECK_EQ(std::string_view(e.category().name()), "cbor"sv);
    CHECK(std::error_code{error::no_buffer_space} == std::errc::no_buffer_space);
    CHECK(std::error_code{error::value_too_large} == std::errc::value_too_large);
    CHECK(std::error_code{error::not_enough_memory} == std::errc::not_enough_memory);
    CHECK(std::error_code{error::io_error} == std::errc::io_error);
    CHECK_FALSE(std::error_code{error::key_not_found} == std::errc::no_buffer_space);
}

// The section "Wrong use" lists the wrong uses that throw std::logic_error, because the compiler cannot see them.
TEST_CASE("doc basics: a wrong use throws std::logic_error")
{
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<std::string const>{}), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<void const>{}, "\x01"sv), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
    CHECK_THROWS_AS(cbor::limits.nesting_depth = 513, std::logic_error);
    CHECK_EQ(cbor::limits.nesting_depth, std::size_t{CBOR_NESTING_DEPTH_DEFAULT});
}

// The section "The error values" gives one example for the errors that a lazy gives most often.
TEST_CASE("doc basics: the error values")
{
    CHECK_EQ(cbor::lazy::from("\x82\x01"s)->at(1).error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::from("\x9f\x01\xff"s)->at(0).error(), error::indefinite_length);
    CHECK_EQ(cbor::lazy::from("\xa1\x61" "a\x01"s)->at("b").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy::from("\x01"s)->at(0).error(), error::not_indexable);
    CHECK_EQ(cbor::lazy::from("\x61" "a"s)->get<std::int64_t>().error(), error::incorrect_type);
    CHECK_EQ(cbor::lazy::from("\x19\x01\x00"s)->get<std::uint8_t>().error(), error::number_out_of_range);
}

// The section "Limits" states how a field reads, how an assignment of limit_values sets every field, and that a
// value above the bound throws and leaves the field as it was.
TEST_CASE("doc basics: limits")
{
    test::limits_guard const g{{}};
    std::size_t const depth = cbor::limits.nesting_depth;
    CHECK_EQ(depth, 128u);
    cbor::limits.string_length = 64;
    CHECK_EQ(cbor::limits.string_length, 64u);
    cbor::limits.nesting_depth = 16;
    cbor::limits = {.string_length = 2, .container_elements = 8};
    CHECK_EQ(cbor::limits.nesting_depth, 128u);
    CHECK_EQ(cbor::limits.string_length, 2u);
    CHECK_EQ(cbor::limits.container_elements, 8u);
    CHECK_EQ(cbor::limits.input_bytes, SIZE_MAX);
    CHECK_THROWS_AS(cbor::limits.nesting_depth = 513, std::logic_error);
    CHECK_EQ(cbor::limits.nesting_depth, 128u);
    CHECK_EQ(cbor::lazy::from("\x63xyz"s)->get<std::string_view>().error(), error::string_length_exceeded);
    CHECK_EQ(cbor::lazy::from("\x89\x01\x01\x01\x01\x01\x01\x01\x01\x01"s)->size().error(),
             error::container_elements_exceeded);
}

// The section "Limits" states that the limits count what a call reads, and that a skip does not count.
TEST_CASE("doc basics: a skip does not count")
{
    test::limits_guard const g{{.string_length = 2}};
    std::string const bytes = encoded(M("a"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::lazy::from(bytes)->at("n")->get<std::int64_t>(), 1);
    CHECK_EQ(cbor::lazy::from(bytes)->at("a")->get<std::string_view>().error(), error::string_length_exceeded);
    CHECK_EQ(cbor::item_size(bytes).error(), error::string_length_exceeded);
    test_binding b;
    CHECK_EQ(cbor::lazy_decode(b, *cbor::lazy::from(bytes)).error(), error::string_length_exceeded);
}

// The section "Nesting depth" states that the top-level item has the depth 0, and that item_size keeps no stack.
TEST_CASE("doc basics: nesting depth")
{
    test::limits_guard const g{{.nesting_depth = 2}};
    test_binding binding;
    CHECK(*cbor::lazy_decode(binding, *cbor::lazy::from("\x81\x81\x01"s)) == A(A(1)));
    CHECK_EQ(cbor::lazy_decode(binding, *cbor::lazy::from("\x81\x81\x81\x01"s)).error(),
             error::nesting_depth_exceeded);
    CHECK_EQ(cbor::item_size("\x81\x81\x81\x01"sv), 4u);
}

// The section "Threads" states the two traits and that cbor::transfer refuses a lazy that something else shares.
TEST_CASE("doc basics: threads")
{
    static_assert(cbor::is_thread_bound_v<cbor::lazy>);
    static_assert(cbor::is_thread_bound_v<cbor::lazy_elements>);
    static_assert(cbor::is_thread_bound_v<cbor::lazy_entries::iterator>);
    static_assert(cbor::is_thread_bound_v<cbor::owning_ref<std::string_view>>);
    static_assert(cbor::is_sendable_v<std::string>);
    static_assert(!cbor::is_sendable_v<cbor::lazy>);
    static_assert(cbor::is_sendable_v<cbor::sendable<cbor::lazy>>);
    cbor::lazy l = *cbor::lazy::from("\x82\x01\x02"s);
    {
        cbor::lazy const copy = l;
        CHECK_THROWS_AS(std::ignore = cbor::transfer(std::move(l)), std::logic_error);
    }
    std::int64_t read = 0;
    std::jthread([&read](cbor::sendable<cbor::lazy> const &v) { read = *v.value.at(1)->get<std::int64_t>(); },
                 cbor::transfer(std::move(l)))
        .join();
    CHECK_EQ(read, 2);
}

// The section "Ownership" states what * , -> and == of an owning_ref give, and that a view keeps its bytes alive.
TEST_CASE("doc basics: ownership")
{
    auto const name = cbor::lazy::from("\xa1\x61" "a\x63xyz"s)->at("a")->get<std::string_view>();
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
    CHECK_EQ((*name)->size(), 3u);
    CHECK(*name == "xyz"sv);
    auto const copy = *name;
    CHECK(copy == *name);
    CHECK_EQ((*copy).data(), (**name).data());
    auto const held = std::make_shared<std::string const>("\x63xyz"s);
    std::shared_ptr<void const> const alias(held, nullptr);
    auto const view = cbor::lazy::from(alias, *held)->get<std::string_view>();
    REQUIRE(view.has_value());
    CHECK_EQ((**view).data(), held->data() + 1);
}

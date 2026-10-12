#include "reference.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/advanced/basics.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

// The section "Error codes" states that a cbor::error is a std::error_code with its own category, and that four
// errors compare equal to a std::errc.
TEST_CASE("doc advanced basics: an error is a std::error_code")
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

// A user compared a cbor::error with a std::errc directly. The section "Error codes" states that this does not
// compile, and that a limit error is equal to no std::errc.
TEST_CASE("doc advanced basics: a cbor::error and a std::errc do not compare directly")
{
    static_assert(!std::equality_comparable_with<cbor::error, std::errc>);
    CHECK(std::error_code{error::string_length_exceeded} != std::errc::value_too_large);
    CHECK(std::error_code{error::string_length_exceeded} != std::errc::no_buffer_space);
}

// The section "Wrong use" lists the wrong uses that throw std::logic_error, because the compiler cannot see them.
TEST_CASE("doc advanced basics: a wrong use throws std::logic_error")
{
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<std::string const>{}), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy::from(std::shared_ptr<void const>{}, "\x01"sv), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
    CHECK_THROWS_AS(cbor::limits.nesting_depth = 513, std::logic_error);
    CHECK_EQ(cbor::limits.nesting_depth, std::size_t{CBOR_NESTING_DEPTH_DEFAULT});
}

// The section "Every error value" gives one example for the errors that a lazy gives most often.
TEST_CASE("doc advanced basics: the error values")
{
    std::string const pair = preferred(A(1, 2));
    CHECK_EQ(cbor::lazy::from(pair.substr(0, pair.size() - 1))->at(1).error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::from("\x9f\x01\xff"s)->at(0).error(), error::indefinite_length);
    CHECK_EQ(cbor::lazy::from(preferred(M("a"s, 1)))->at("b").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy::from(preferred(V(1)))->at(0).error(), error::not_indexable);
    CHECK_EQ(cbor::lazy::from(preferred(V("a"s)))->get<std::int64_t>().error(), error::incorrect_type);
    CHECK_EQ(cbor::lazy::from(preferred(V(256)))->get<std::uint8_t>().error(), error::number_out_of_range);
}

// The table of the section "Every error value" says which errors structs gives besides the errors of a lazy.
TEST_CASE("doc advanced basics: the errors of structs")
{
#ifdef __cpp_impl_reflection
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x01\x02"s).error(), error::syntax_error);
    CHECK_EQ(cbor::structs<std::map<std::string, int>>::decode(preferred(M("a"s, 1)) + "\x00"s).error(),
             error::syntax_error);
#endif
    CHECK_EQ(cbor::diagnostic_notation("\x9f\x01\xff"sv), "[_ 1]");
}

// The section "Limits" states how a field reads, how an assignment of limit_values sets every field, and that a
// value above the bound throws and leaves the field as it was.
TEST_CASE("doc advanced basics: limits")
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

// A user expected a string limit at lazy::from. The section "Limits" states that the step that reads the string
// checks it.
TEST_CASE("doc advanced basics: a string limit is checked at get")
{
    test::limits_guard const g{{.string_length = 2}};
    auto const l = cbor::lazy::from(preferred(V("xyz"s)));
    REQUIRE(l.has_value());
    CHECK_EQ(l->get<std::string_view>().error(), error::string_length_exceeded);
}

// The section "Limits" states that the limits count what a call reads, and that a skip does not count.
TEST_CASE("doc advanced basics: a skip does not count")
{
    test::limits_guard const g{{.string_length = 2}};
    std::string const bytes = preferred(M("a"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::lazy::from(bytes)->at("n")->get<std::int64_t>(), 1);
    CHECK_EQ(cbor::lazy::from(bytes)->at("a")->get<std::string_view>().error(), error::string_length_exceeded);
    CHECK_EQ(cbor::item_size(bytes).error(), error::string_length_exceeded);
    CHECK_EQ(test::value_of(*cbor::lazy::from(bytes)).error(), error::string_length_exceeded);
}

// The section "Nesting depth" states that the top-level item has the depth 0, and that item_size keeps no stack.
TEST_CASE("doc advanced basics: nesting depth")
{
    std::string const two = preferred(A(A(1)));
    std::string const three = preferred(A(A(A(1))));
    test::limits_guard const g{{.nesting_depth = 2}};
    CHECK_EQ(cbor::diagnostic_notation(two), "[[1]]");
    CHECK_EQ(cbor::diagnostic_notation(three).error(), error::nesting_depth_exceeded);
    CHECK_EQ(cbor::item_size(three), 4u);
    CHECK(*test::value_of(*cbor::lazy::from(two)) == A(A(1)));
    CHECK_EQ(test::value_of(*cbor::lazy::from(three)).error(), error::nesting_depth_exceeded);
}

#ifdef __cpp_impl_reflection
// The section "Memory" shows that the caller bounds the memory of a decode with a std::pmr resource, and that
// a resource that refuses after 64 bytes gives not_enough_memory.
TEST_CASE("doc advanced basics: a std::pmr target")
{
    using texts = std::pmr::vector<std::pmr::string>;
    auto const owner = std::make_shared<std::string const>(
        *cbor::structs<std::vector<std::string>>::encode({std::string(40, 'a'), std::string(40, 'b')}));
    std::array<std::byte, 64> buffer{};
    std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
    CHECK_EQ(cbor::structs<texts>::decode(owner, *owner, texts(&budget)).error(), error::not_enough_memory);
    std::array<std::byte, 4096> large{};
    std::pmr::monotonic_buffer_resource enough(large.data(), large.size(), std::pmr::null_memory_resource());
    auto const fits = cbor::structs<texts>::decode(owner, *owner, texts(&enough));
    REQUIRE(fits.has_value());
    CHECK_EQ((**fits).size(), 2u);
}
#endif

// The section "Threads" states the two traits and that cbor::transfer refuses a lazy that something else shares.
TEST_CASE("doc advanced basics: threads")
{
    static_assert(cbor::is_thread_bound_v<cbor::lazy>);
    static_assert(cbor::is_thread_bound_v<cbor::lazy_elements>);
    static_assert(cbor::is_thread_bound_v<cbor::lazy_entries::iterator>);
    static_assert(cbor::is_thread_bound_v<cbor::owning_ref<std::string_view>>);
    static_assert(cbor::is_sendable_v<std::string>);
    static_assert(!cbor::is_sendable_v<cbor::lazy>);
    static_assert(cbor::is_sendable_v<cbor::sendable<cbor::lazy>>);
    cbor::lazy l = *cbor::lazy::from(preferred(A(1, 2)));
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

// A user moved a lazy out of the std::expected of lazy::from, and saw no throw over plain data in another thread.
// The section "Threads" shows the move and states that only marks are checked.
TEST_CASE("doc advanced basics: a lazy moved out of the expected to another thread")
{
    std::string const bytes = preferred(A(1, 2));
    auto l = cbor::lazy::from(bytes);
    std::int64_t read = 0;
    std::jthread([&read](cbor::sendable<cbor::lazy> const &v) { read = *v.value.at(1)->get<std::int64_t>(); },
                 cbor::transfer(std::move(*l)))
        .join();
    CHECK_EQ(read, 2);
    auto const plain = *cbor::lazy::from(preferred(M("a"s, 1)));
    std::int64_t other = 0;
    std::jthread([&] { other = *plain.at("a")->get<std::int64_t>(); }).join();
    CHECK_EQ(other, 1);
#ifndef NDEBUG
    auto const marked = *cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    CHECK_EQ(marked.at(1)->get<std::int64_t>(), 1);
    bool threw = false;
    std::jthread([&] {
        try {
            std::ignore = marked.at(1)->get<std::int64_t>();
        } catch (std::logic_error const &) {
            threw = true;
        }
    }).join();
    CHECK(threw);
#endif
}

// A user did not know that a lazy from a step holds the bytes after the string and the first lazy end.
TEST_CASE("doc advanced basics: a lazy holds the owner")
{
    auto const message = [] { return preferred(M("tags"s, A("a"s, "b"s))); };
    std::optional<cbor::lazy> keep;
    {
        std::string bytes = message();
        keep = *cbor::lazy::from(std::move(bytes))->at("tags");
    }
    CHECK(*keep->at(1)->get<std::string_view>() == "b"sv);
}

// The section "Owners" states what *, -> and == of an owning_ref give, that a copy shares the owner, and that the
// aliasing constructor of std::shared_ptr makes an owner from an object that holds the bytes.
TEST_CASE("doc advanced basics: owners")
{
    auto const name = cbor::lazy::from(preferred(M("a"s, "xyz"s)))->at("a")->get<std::string_view>();
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
    CHECK_EQ((*name)->size(), 3u);
    CHECK(*name == "xyz"sv);
    auto const copy = *name;
    CHECK(copy == *name);
    CHECK_EQ((*copy).data(), (**name).data());

    struct page {
        std::string bytes;
    };
    auto const message = [] { return preferred(V("xyz"s)); };
    auto const p = std::make_shared<page const>(message());
    std::shared_ptr<void const> const owner(p, p->bytes.data());
    auto const view = cbor::lazy::from(owner, p->bytes)->get<std::string_view>();
    REQUIRE(view.has_value());
    CHECK_EQ(**view, "xyz"sv);
    CHECK_EQ((**view).data(), p->bytes.data() + 1);
}

// The section "Owners" states that an lvalue string is copied, and that a shared_ptr or an rvalue avoids the copy.
TEST_CASE("doc advanced basics: an owner avoids the copy")
{
    auto const message = [] { return preferred(V("xyz"s)); };
    std::string const lvalue = message();
    auto const copied = cbor::lazy::from(lvalue)->get<std::string_view>();
    CHECK_NE((**copied).data(), lvalue.data() + 1);

    auto const shared = std::make_shared<std::string const>(message());
    auto const a = cbor::lazy::from(shared).value();
    auto const va = a.get<std::string_view>().value();
    CHECK_EQ((*va).data(), shared->data() + 1);

    std::string bytes = preferred(V(std::string(40, 'x')));
    char const *const data = bytes.data();
    auto const b = cbor::lazy::from(std::move(bytes)).value();
    auto const vb = b.get<std::string_view>().value();
    CHECK_EQ((*vb).data(), data + 2);
}

#include "reference.hpp"
#include "owned_copy.hpp"

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
#include <type_traits>
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
    CHECK((!std::equality_comparable_with<cbor::error, std::errc>));
    CHECK(std::error_code{error::string_length_exceeded} != std::errc::value_too_large);
    CHECK(std::error_code{error::string_length_exceeded} != std::errc::no_buffer_space);
}

// The section "Wrong use" lists the wrong uses that throw std::logic_error, because the compiler cannot see them.
TEST_CASE("doc advanced basics: a wrong use throws std::logic_error")
{
    CHECK_THROWS_AS((void)cbor::lazy::view(std::shared_ptr<void const>{}, ""sv), std::logic_error);
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
}

// The section "Every error value" gives one example for the errors that a lazy gives most often.
TEST_CASE("doc advanced basics: the error values")
{
    auto const owner_of = [](std::string bytes) { return std::make_shared<std::string const>(std::move(bytes)); };
    std::string const pair = preferred(A(1, 2));
    auto owner = owner_of(pair.substr(0, pair.size() - 1));
    CHECK_EQ(cbor::lazy::view(owner)->at(1).error(), error::too_little_data);
    owner = owner_of("\x9f\x01\xff"s);
    CHECK_EQ(cbor::lazy::view(owner)->at(0).error(), error::indefinite_length);
    owner = owner_of(preferred(M("a"s, 1)));
    CHECK_EQ(cbor::lazy::view(owner)->at("b").error(), error::key_not_found);
    owner = owner_of(preferred(V(1)));
    CHECK_EQ(cbor::lazy::view(owner)->at(0).error(), error::not_indexable);
    owner = owner_of(preferred(V("a"s)));
    CHECK_EQ(cbor::lazy::view(owner)->get<std::int64_t>().error(), error::incorrect_type);
    owner = owner_of(preferred(V(256)));
    CHECK_EQ(cbor::lazy::view(owner)->get<std::uint8_t>().error(), error::number_out_of_range);
}

// The row not_singular_query of the section "Every error value" names the call that gives it.
TEST_CASE("doc advanced basics: not_singular_query")
{
    CHECK_EQ(cbor::jsonpath::compile<cbor::lazy>("$.a[*]").error(), error::not_singular_query);
    CHECK_EQ(cbor::jsonpath::compile<std::int64_t>("$..a").error(), error::not_singular_query);
    CHECK(cbor::jsonpath::compile<cbor::nodelist>("$.a[*]").has_value());
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

// The section "Limits" states that each limit is a constant of the build, and the value of each in a build
// that sets none. The examples with a limit set are in test/limits/, built with the flags that the examples
// name.
TEST_CASE("doc advanced basics: limits")
{
    std::size_t const depth = cbor::limits::nesting_depth;
    CHECK_EQ(depth, std::size_t{CBOR_NESTING_DEPTH_DEFAULT});
    CHECK_EQ(depth, 128u);
    CHECK_EQ(cbor::limits::string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits::container_elements, SIZE_MAX);
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
    CHECK_EQ(cbor::structs<texts>::decode(owner, texts(&budget)).error(), error::not_enough_memory);
    std::array<std::byte, 4096> large{};
    std::pmr::monotonic_buffer_resource enough(large.data(), large.size(), std::pmr::null_memory_resource());
    auto const fits = cbor::structs<texts>::decode(owner, texts(&enough));
    REQUIRE(fits.has_value());
    CHECK_EQ((**fits).size(), 2u);
}
#endif

// The section "Threads" states that a lazy holds only its bytes and its offset, so several threads read one lazy.
TEST_CASE("doc advanced basics: threads")
{
    cbor::lazy const l = *test::lazy_of_copy(preferred(A(1, 2)));
    std::int64_t first = 0;
    std::int64_t second = 0;
    {
        std::jthread const a([&] { first = *l.at(1)->get<std::int64_t>(); });
        std::jthread const b([&] { second = *l.at(1)->get<std::int64_t>(); });
    }
    CHECK_EQ(first, 2);
    CHECK_EQ(second, 2);
}

// A user moved a lazy out of the std::expected of lazy::take_ownership. The section "Threads" shows the move.
TEST_CASE("doc advanced basics: a lazy moved out of the expected to another thread")
{
    std::string bytes = preferred(A(1, 2));
    auto l = cbor::lazy::take_ownership(std::move(bytes));
    std::int64_t read = 0;
    std::jthread([&read](cbor::lazy const &v) { read = *v.at(1)->get<std::int64_t>(); }, std::move(*l)).join();
    CHECK_EQ(read, 2);
    auto const plain = *test::lazy_of_copy(preferred(M("a"s, 1)));
    std::int64_t other = 0;
    std::jthread([&] { other = *plain.at("a")->get<std::int64_t>(); }).join();
    CHECK_EQ(other, 1);
    auto const tagged = *test::lazy_of_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    std::int64_t content = -1;
    std::jthread([&] { content = *tagged.at(1)->get<std::int64_t>(); }).join();
    CHECK_EQ(content, 0);
}

// A user did not know that a lazy from a step holds the bytes after the string and the first lazy end.
TEST_CASE("doc advanced basics: a lazy holds the owner")
{
    auto const message = [] { return preferred(M("tags"s, A("a"s, "b"s))); };
    std::optional<cbor::lazy> keep;
    {
        std::string bytes = message();
        keep = *cbor::lazy::take_ownership(std::move(bytes))->at("tags");
    }
    CHECK(*keep->at(1)->get<std::string_view>() == "b"sv);
}

// The section "Owners" states what *, -> and == of a ref give, that a copy shares the owner, and that the
// aliasing constructor of std::shared_ptr makes an owner from an object that holds the bytes.
TEST_CASE("doc advanced basics: owners")
{
    auto const name = test::lazy_of_copy(preferred(M("a"s, "xyz"s)))->at("a")->get<std::string_view>();
    REQUIRE(name.has_value());
    CHECK_EQ(**name, "xyz"sv);
    CHECK_EQ((*name)->size(), 3u);
    CHECK(*name == "xyz"sv);
    auto const copy = *name;
    CHECK(copy == *name);
    CHECK_EQ((*copy).data(), (**name).data());
    CHECK(std::is_same_v<decltype(**name), std::string_view>);
    std::string const own = name->clone();
    CHECK_EQ(own, "xyz");
    auto const array = test::lazy_of_copy("\xd8\x45\x44\x07\x00\x08\x00"s)->get<cbor::typed_array>();
    REQUIRE(array.has_value());
    cbor::owning<cbor::typed_array> const held = array->clone();
    CHECK_EQ(held->bytes.size(), 4u);
    CHECK_NE(held->bytes.data(), (*array)->bytes.data());

    struct page {
        std::string bytes;
    };
    auto const message = [] { return preferred(V("xyz"s)); };
    auto const p = std::make_shared<page const>(message());
    std::shared_ptr<std::string const> const owner(p, &p->bytes);
    auto const aliased = cbor::lazy::view(owner)->get<std::string_view>();
    REQUIRE(aliased.has_value());
    CHECK_EQ(**aliased, "xyz"sv);
    CHECK_EQ((**aliased).data(), p->bytes.data() + 1);
    auto const view = cbor::lazy::view(p, std::string_view(p->bytes))->get<std::string_view>();
    REQUIRE(view.has_value());
    CHECK_EQ(**view, "xyz"sv);
    CHECK_EQ((**view).data(), p->bytes.data() + 1);
    CHECK_FALSE(([]<class S>(S const &s) {
        return requires(std::shared_ptr<void const> o) { cbor::lazy::view(o, s); };
    }(p->bytes)));
}

// The section "Owners" states that cbor::sequence takes the two forms with a std::shared_ptr and no rvalue
// std::string.
TEST_CASE("doc advanced basics: the forms of cbor::sequence")
{
    CHECK(std::is_constructible_v<cbor::sequence, std::shared_ptr<std::string const>>);
    CHECK(std::is_constructible_v<cbor::sequence, std::shared_ptr<void const>, std::string_view>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string &&>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::shared_ptr<void const>, std::string const &>);
}

// The section "Owners" states that a std::string lvalue and a std::string_view alone do not compile, and that a
// shared_ptr or an rvalue reads the bytes with no copy.
TEST_CASE("doc advanced basics: no form copies the bytes")
{
    auto const message = [] { return preferred(V("xyz"s)); };
    CHECK_FALSE(([]<class S>(S &s) { return requires { cbor::lazy::take_ownership(s); }; }(*std::make_unique<std::string>())));
    CHECK_FALSE(([]<class S>(S const &s) { return requires { cbor::lazy::view(s); }; }(std::string_view())));

    auto const shared = std::make_shared<std::string const>(message());
    auto const a = cbor::lazy::view(shared).value();
    auto const va = a.get<std::string_view>().value();
    CHECK_EQ((*va).data(), shared->data() + 1);

    std::string bytes = preferred(V(std::string(40, 'x')));
    char const *const data = bytes.data();
    auto const b = cbor::lazy::take_ownership(std::move(bytes)).value();
    auto const vb = b.get<std::string_view>().value();
    CHECK_EQ((*vb).data(), data + 2);
}

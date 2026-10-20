#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/basics.md. The tests exist so that every example in the document
// compiles and gives the result that the document states. Where the document writes a comment with the input as
// text, the test builds the same input with preferred().

// The section "Errors" shows how a program reads a std::expected and the text of its error, because a new user
// reaches for an exception first.
TEST_CASE("doc basics: errors")
{
    std::string const bytes = preferred(M("user"s, M("id"s, 42)));
    std::int64_t used = 0;
    std::string reported;
    auto const use = [&used](std::int64_t const v) { used = v; };
    auto const report = [&reported](std::string const &text) { reported = text; };

    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    if (id)
        use(*id);
    else
        report(std::error_code{id.error()}.message());
    CHECK_EQ(used, 42);
    CHECK(reported.empty());

    auto const age = cbor::at_path<"$.user.age", std::int64_t>(bytes);
    CHECK(age.error() == cbor::error::key_not_found);
    CHECK_EQ(std::error_code{age.error()}.message(), "key not found");
}

// The table of errors in the section "Errors" gives one example for each row. The test runs each example, because a
// user who meets an error looks up its cause there.
TEST_CASE("doc basics: the errors that a program meets most often")
{
    CHECK_EQ(cbor::at_path<"$.b", int>(preferred(M("a"s, 1))).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path<"$[2]", int>(preferred(A(1, 2))).error(), error::index_out_of_bounds);
    CHECK_EQ(cbor::at_path<"$.a.b", int>(preferred(M("a"s, 1))).error(), error::not_indexable);
    CHECK_EQ(cbor::at_path<"$", int>(preferred(V("a"s))).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(preferred(V(256))).error(), error::number_out_of_range);
    std::string const pair = preferred(A(1, 2));
    CHECK_EQ(cbor::at_path<"$[1]", int>(pair.substr(0, pair.size() - 1)).error(), error::too_little_data);
    CHECK_EQ(cbor::at_path<"$", int>("\xff"s).error(), error::syntax_error);
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x9f\x01\xff"s).error(), error::indefinite_length);
    CHECK_EQ(test::at_path("$.a[", *test::lazy_of_copy(preferred(M("a"s, 1)))).error(), error::invalid_path);
}

// The section "Errors" says that a wrong use that the compiler cannot see throws std::logic_error.
TEST_CASE("doc basics: a wrong use throws std::logic_error")
{
    CHECK_THROWS_AS((void)cbor::lazy{}.at(0), std::logic_error);
}

// The section "Results that point into the bytes" states the two forms in which bytes go into the library: a copy
// form, where an rvalue std::string is moved, and a view form over a std::shared_ptr<std::string const>.
TEST_CASE("doc basics: bytes go in as a copy or as a view")
{
    std::string const kept = preferred(M("name"s, std::string(40, 'a')));
    auto const copied = cbor::at_path<"$.name", std::string_view>(std::string(kept));
    REQUIRE(copied.has_value());
    CHECK_NE((**copied).data(), kept.data() + 8);
    std::string moved = kept;
    char const *const data = moved.data();
    auto const taken = cbor::at_path<"$.name", std::string_view>(std::move(moved));
    REQUIRE(taken.has_value());
    CHECK_EQ((**taken).data(), data + 8);
    auto const owner = std::make_shared<std::string const>(kept);
    auto const viewed = cbor::at_path<"$.name", std::string_view>(owner);
    REQUIRE(viewed.has_value());
    CHECK_EQ((**viewed).data(), owner->data() + 8);
    std::shared_ptr<void const> const any_owner = owner;
    auto const beside = cbor::at_path<"$.name", std::string_view>(any_owner, std::string_view(*owner));
    REQUIRE(beside.has_value());
    CHECK_EQ((**beside).data(), owner->data() + 8);
    CHECK_FALSE(([]<class S>(S const &s) {
        return requires(std::shared_ptr<void const> o) { cbor::at_path<"$.name", std::string_view>(o, s); };
    }(kept)));
}

// The section "Results that point into the bytes" says that std::string as the type copies the text of the value
// into the result, so that a user who wants a copy of their own finds the form.
TEST_CASE("doc basics: std::string as the type copies the text")
{
    std::string const bytes = preferred(M("name"s, std::string(40, 'a')));
    auto const name = cbor::at_path<"$.name", std::string>(bytes);
    REQUIRE(name.has_value());
    CHECK_EQ(*name, std::string(40, 'a'));
    CHECK_NE(name->data(), bytes.data() + 8);
    auto const raw = cbor::at_path<"$.key", std::vector<std::byte>>(preferred(M("key"s, test::bytes{"\x01\x02"})));
    REQUIRE(raw.has_value());
    CHECK_EQ(*raw, std::vector{std::byte{1}, std::byte{2}});
}

// The section "Results that point into the bytes" shows the owner rule: the view stays valid after the owner of the
// caller ends, because the ref holds the bytes.
TEST_CASE("doc basics: results that point into the bytes")
{
    std::string bytes = preferred(M("name"s, "ann"s));
    std::string_view text;
    std::string copy;
    std::optional<std::expected<cbor::ref<std::string_view>, cbor::error>> kept;
    {
        auto const owner = std::make_shared<std::string const>(std::move(bytes));
        auto const name = cbor::at_path<"$.name", std::string_view>(owner);
        REQUIRE(name.has_value());
        if (name) {
            text = **name;
            copy = std::string{text};
            CHECK(*name == "ann");
        }
        kept = name;
    }
    CHECK_EQ(text, "ann"sv);
    CHECK_EQ(copy, "ann");
    CHECK_EQ(***kept, "ann"sv);
    CHECK_EQ((***kept).data(), text.data());
    CHECK_FALSE(([]<class R>(R &&) { return requires(R &&r) { *std::forward<R>(r); }; }(std::move(**kept))));
}

// The section "Threads" shows two threads that read the same bytes, one with a path over the bytes and one with a
// lazy of its own.
TEST_CASE("doc basics: threads")
{
    auto const owner = std::make_shared<std::string const>(preferred(M("id"s, 42)));
    std::int64_t first_read = 0;
    std::int64_t second_read = 0;
    {
        auto const use_first = [&first_read](std::expected<std::int64_t, error> const &v) { first_read = *v; };
        auto const use_second = [&second_read](std::expected<std::int64_t, error> const &v) { second_read = *v; };
        std::thread first([&owner, &use_first] { use_first(cbor::at_path<"$.id", std::int64_t>(*owner)); });
        std::thread second([&owner, &use_second] {
            if (auto const doc = cbor::lazy::view(owner))
                use_second(doc->at_path<"$.id", std::int64_t>());
        });
        first.join();
        second.join();
    }
    CHECK_EQ(first_read, 42);
    CHECK_EQ(second_read, 42);
}

// The section "Limits" states the default of each limit for a build that sets none.
TEST_CASE("doc basics: limits")
{
    CHECK_EQ(cbor::limits::nesting_depth, 128u);
    CHECK_EQ(cbor::limits::string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits::container_elements, SIZE_MAX);
}

// The table of the section "Limits" names the error of the nesting depth. The examples of the size limits are
// in test/limits/sizes_2.cpp, which is built with them.
TEST_CASE("doc basics: the error of the nesting depth")
{
    std::string const deep = test::repeat("\x81"sv, cbor::limits::nesting_depth + 1) + '\x01';
    CHECK_EQ(test::lazy_of_copy(deep)->clone().error(), error::nesting_depth_exceeded);
}

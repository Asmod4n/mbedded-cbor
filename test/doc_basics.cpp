#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

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
    CHECK_EQ(test::at_path("$.a[", *cbor::lazy::copy(preferred(M("a"s, 1)))).error(), error::invalid_path);
    std::string const three = preferred(A(1, 2, 3));
    test::limits_guard const g{{.container_elements = 2}};
    CHECK_EQ(cbor::at_path<"$[0]", int>(three).error(), error::container_elements_exceeded);
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
    auto const copied = cbor::at_path<"$.name", std::string_view>(std::string_view(kept));
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
}

// The section "Results that point into the bytes" shows the owner rule: the view stays valid after the owner of the
// caller ends, because the owning_ref holds the bytes.
TEST_CASE("doc basics: results that point into the bytes")
{
    std::string bytes = preferred(M("name"s, "ann"s));
    std::string_view text;
    std::string copy;
    std::optional<std::expected<cbor::owning_ref<std::string_view>, cbor::error>> kept;
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
    std::string const bytes = preferred(M("id"s, 42));
    std::int64_t first_read = 0;
    std::int64_t second_read = 0;
    {
        auto const use_first = [&first_read](std::expected<std::int64_t, error> const &v) { first_read = *v; };
        auto const use_second = [&second_read](std::expected<std::int64_t, error> const &v) { second_read = *v; };
        std::jthread first([&bytes, &use_first] { use_first(cbor::at_path<"$.id", std::int64_t>(bytes)); });
        std::jthread second([&bytes, &use_second] {
            if (auto const doc = cbor::lazy::copy(bytes))
                use_second(cbor::at_path<"$.id", std::int64_t>(*doc));
        });
    }
    CHECK_EQ(first_read, 42);
    CHECK_EQ(second_read, 42);
}

// The section "Limits" sets one field and then all four, and states that a field that the braces do not name gets
// its default, because a user expects the old value there.
TEST_CASE("doc basics: limits")
{
    test::limits_guard const g{{}};
    CHECK_EQ(cbor::limits.nesting_depth, 128u);
    CHECK_EQ(cbor::limits.string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits.container_elements, SIZE_MAX);
    CHECK_EQ(cbor::limits.input_bytes, SIZE_MAX);
    CHECK_THROWS_AS(cbor::limits.nesting_depth = 513, std::logic_error);
    cbor::limits.container_elements = 8;
    cbor::limits.string_length = 1024;
    cbor::limits = {.nesting_depth = 16, .input_bytes = 65536};
    CHECK_EQ(cbor::limits.nesting_depth, 16u);
    CHECK_EQ(cbor::limits.input_bytes, 65536u);
    CHECK_EQ(cbor::limits.string_length, SIZE_MAX);
    CHECK_EQ(cbor::limits.container_elements, SIZE_MAX);
}

// The table of the section "Limits" names the error of each field. The test exceeds each field once.
TEST_CASE("doc basics: the error of each limit")
{
    {
        std::string const deep = preferred(A(A(A(1))));
        test::limits_guard const g{{.nesting_depth = 2}};
        CHECK_EQ(cbor::at_path<"$[0][0][0]", int>(deep).error(), error::nesting_depth_exceeded);
    }
    {
        std::string const text = preferred(V("xyz"s));
        test::limits_guard const g{{.string_length = 2}};
        CHECK_EQ(cbor::lazy::copy(text)->get<std::string_view>().error(), error::string_length_exceeded);
    }
    {
        std::string const three = preferred(A(1, 2, 3));
        test::limits_guard const g{{.container_elements = 2}};
        CHECK_EQ(cbor::lazy::copy(three)->size().error(), error::container_elements_exceeded);
    }
    {
        std::string const three = preferred(A(1, 2, 3));
        test::limits_guard const g{{.input_bytes = 2}};
        CHECK_EQ(cbor::at_path<"$[0]", int>(three).error(), error::input_bytes_exceeded);
    }
}

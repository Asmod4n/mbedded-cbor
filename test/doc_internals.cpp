#include "reference.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one claim of doc/advanced/internals.md that a caller can check. The tests exist so that a
// change of the mechanism that breaks a claim fails here.

// The section "How the documents are tested" states that preferred() builds the input that a comment shows as text.
// The test exists because every document test depends on that.
TEST_CASE("doc internals: encoded builds the input that a comment shows")
{
    CHECK_EQ(cbor::diagnostic_notation(preferred(M("user"s, M("id"s, 42, "name"s, "ann"s)))),
             R"({"user": {"id": 42, "name": "ann"}})");
    CHECK_EQ(cbor::diagnostic_notation(preferred(A(1, 2.5, "a"s, simple{21}))), R"([1, 2.5, "a", true])");
}

// The section "A lazy reads heads" states that lazy::copy and lazy::view read no head and that a step reads the head of its
// target.
TEST_CASE("doc internals: a lazy reads heads when a step needs them")
{
    auto const broken = cbor::lazy::copy("\xff"s);
    REQUIRE(broken.has_value());
    CHECK_EQ(broken->get<std::int64_t>().error(), error::syntax_error);
    auto const cut = cbor::lazy::copy("\x82\x01"s);
    REQUIRE(cut.has_value());
    CHECK_EQ(cut->at(0).error(), error::too_little_data);
    CHECK_EQ(cut->at(1).error(), error::too_little_data);
}

// The section "A skip keeps a count" states that the nesting depth does not bound a skip, and that a skip stops
// where the bytes end.
TEST_CASE("doc internals: a skip keeps a count")
{
    std::string const deep = preferred(M("a"s, A(A(A(A(1)))), "b"s, 2));
    test::limits_guard const g{{.nesting_depth = 2}};
    CHECK_EQ(cbor::at_path<"$.b", int>(deep), 2);
    CHECK_EQ(cbor::item_size(deep), deep.size());
    CHECK_EQ(cbor::at_path<"$[1]", int>("\x82\x82\x01"s).error(), error::too_little_data);
}

// The section "The limits are read once" states that a field is atomic, so a thread reads the assignment of another.
TEST_CASE("doc internals: a limit is one atomic field")
{
    test::limits_guard const g{{.string_length = 2}};
    std::size_t seen = 0;
    std::jthread([&seen] { seen = cbor::limits.string_length; }).join();
    CHECK_EQ(seen, 2u);
}

// The section "Tags 28 and 29" states that a lazy keeps no marks and reads a tag 29 as a tag over its index.
TEST_CASE("doc internals: two references to one mark")
{
    auto const l = *cbor::lazy::copy("\x83\xd8\x1c\x01\xd8\x1d\x00\xd8\x1d\x00"s);
    CHECK_EQ(l.at(1)->get<std::int64_t>(), 0);
    CHECK_EQ(l.at(2)->get<std::int64_t>(), 0);
    CHECK_EQ(cbor::at_path<"$[2]", int>("\x83\xd8\x1c\x01\xd8\x1d\x00\xd8\x1d\x00"s), 0);
}

// The section "A compile-time path" states that the compiler parses the path.
TEST_CASE("doc internals: the compiler parses a compile-time path")
{
    static_assert(cbor::is_valid_path_v<"$.a[0]">);
    static_assert(!cbor::is_valid_path_v<"$.a[">);
}

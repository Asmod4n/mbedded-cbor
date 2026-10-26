#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
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

// The section "A lazy reads heads" states that lazy::take_ownership and lazy::view read no head but a leading tag
// 55799, and that a step reads the head of its target.
TEST_CASE("doc internals: a lazy reads heads when a step needs them")
{
    auto const described = test::lazy_of_copy("\xd9\xd9\xf7\x01"s);
    REQUIRE(described.has_value());
    CHECK_EQ(described->type(), cbor::type::unsigned_integer);
    auto const broken = test::lazy_of_copy("\xff"s);
    REQUIRE(broken.has_value());
    CHECK_EQ(broken->get<std::int64_t>().error(), error::syntax_error);
    auto const cut = test::lazy_of_copy("\x82\x01"s);
    REQUIRE(cut.has_value());
    CHECK_EQ(cut->at(0).error(), error::too_little_data);
    CHECK_EQ(cut->at(1).error(), error::too_little_data);
}

// The section "A skip keeps a count" states that the nesting depth does not bound a skip, and that a skip stops
// where the bytes end.
TEST_CASE("doc internals: a skip keeps a count")
{
    value deeper = V(1);
    for (std::size_t i = 0; i <= cbor::limits::nesting_depth; ++i)
        deeper = A(deeper);
    std::string const deep = preferred(M("a"s, deeper, "b"s, 2));
    CHECK_EQ(cbor::at_path<"$.b", int>(deep), 2);
    CHECK_EQ(cbor::item_size(deep), deep.size());
    CHECK_EQ(cbor::at_path<"$[1]", int>("\x82\x82\x01"s).error(), error::too_little_data);
}

// The section "Tags 28 and 29" states that a lazy keeps no marks and reads a tag 29 as a tag over its index.
TEST_CASE("doc internals: two references to one mark")
{
    auto const l = *test::lazy_of_copy("\x83\xd8\x1c\x01\xd8\x1d\x00\xd8\x1d\x00"s);
    CHECK_EQ(l.at(1)->get<std::int64_t>(), 0);
    CHECK_EQ(l.at(2)->get<std::int64_t>(), 0);
    CHECK_EQ(cbor::at_path<"$[2]", int>("\x83\xd8\x1c\x01\xd8\x1d\x00\xd8\x1d\x00"s), 0);
}

// The section "The item tree" states that a text of the tree is copied into the resource of the tree.
TEST_CASE("doc internals: a text of the item tree is a copy")
{
    std::string const bytes = preferred(V(std::string(40, 'a')));
    auto const owner = std::make_shared<std::string const>(bytes);
    auto const tree = cbor::lazy::view(owner)->clone();
    REQUIRE(tree.has_value());
    auto const text = (*tree)->get<cbor::type::text_string>();
    REQUIRE(text.has_value());
    CHECK_EQ(*text, std::string(40, 'a'));
    CHECK((text->data() < owner->data() || text->data() >= owner->data() + owner->size()));
}

// The section "A compile-time path" states that the compiler parses the path.
TEST_CASE("doc internals: the compiler parses a compile-time path")
{
    CHECK((cbor::is_valid_path_v<"$.a[0]">));
    CHECK((!cbor::is_valid_path_v<"$.a[">));
}

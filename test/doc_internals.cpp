#include "reference.hpp"
#include "owned_copy.hpp"

#include <bit>
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

// The section "The item tree" states that encode of an item keeps the sign and the payload of a NaN. The test exists
// because a conversion of the processor can set the quiet bit or drop the payload.
TEST_CASE("doc internals: an item keeps the bits of a NaN")
{
    std::string const single = "\xfa\xff\x80\x01\x23"s;
    auto const tree = test::lazy_of_copy(single)->clone();
    REQUIRE(tree.has_value());
    std::string again;
    REQUIRE(cbor::encode(**tree, again).has_value());
    CHECK_EQ(again, single);
}

// The section "The width of a float" gives the width of a NaN from the zero bits at the right of its significand. The
// test exists because a NaN that lost bits in a shorter width would read back as another NaN.
TEST_CASE("doc internals: a NaN takes the shortest width that keeps its bits")
{
    auto const of_double = [](std::uint64_t const bits) { return cbor::structs<double>::encode(std::bit_cast<double>(bits))->size(); };
    auto const of_float = [](std::uint32_t const bits) { return cbor::structs<float>::encode(std::bit_cast<float>(bits))->size(); };
    CHECK_EQ(of_double(0x7ff8'0400'0000'0000), 3u);
    CHECK_EQ(of_double(0x7ff8'0000'2000'0000), 5u);
    CHECK_EQ(of_double(0x7ff8'0000'0000'0001), 9u);
    CHECK_EQ(of_float(0x7fc0'2000), 3u);
    CHECK_EQ(of_float(0x7fc0'0001), 5u);
    auto const back = cbor::structs<float>::decode(*cbor::structs<float>::encode(std::bit_cast<float>(std::uint32_t{0x7fc0'0001})));
    REQUIRE(back.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>(**back), 0x7fc0'0001u);
}

namespace doc_internals
{

struct reading {
    std::uint16_t id;
    std::string name;
};

} // namespace doc_internals

CBOR_DEFINE_TYPE(doc_internals::reading, cbor::tag{1720}, CBOR_MEMBER(doc_internals::reading, id),
                 CBOR_MEMBER(doc_internals::reading, name));

// The section "The schema reads the descriptions" states that a message that ends too early gives too_little_data in
// path, at_path and decode. The test exists because the root is read from the end, so a cut gives a wrong tag first.
TEST_CASE("doc internals: a cut message gives too_little_data in a schema read")
{
    std::string const bytes = *cbor::schema<doc_internals::reading>::encode(doc_internals::reading{7, "t1"});
    for (std::size_t n = 1; n < bytes.size(); ++n) {
        std::string const cut = bytes.substr(0, n);
        CAPTURE(n);
        CHECK_EQ(cbor::schema<doc_internals::reading>::at_path<"$.id">(cut).error(), error::too_little_data);
        CHECK_EQ(cbor::schema<doc_internals::reading>::path(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(cbor::schema<doc_internals::reading>::decode(std::string(cut)).error(), error::too_little_data);
    }
}

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace
{

// The vectors of RFC 8949 Appendix A, read with the function under test: JSON text is EDN text.
cbor::owning<cbor::item> read_vectors()
{
    std::ifstream file(std::string(TEST_VECTORS) + "/appendix_a.json");
    std::string const text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto tree = cbor::parse_edn(text);
    REQUIRE(tree.has_value());
    return std::move(*tree);
}

cbor::item const *member_of(cbor::item const &object, std::string_view const key)
{
    auto const pairs = object.get<cbor::map_range>();
    REQUIRE(pairs.has_value());
    for (auto const pair : *pairs)
        if (std::get<0>(pair).get<std::string_view>() == key)
            return &std::get<1>(pair);
    return nullptr;
}

std::string encoded(cbor::item const &node)
{
    std::string out;
    REQUIRE(cbor::encode(out, node).has_value());
    return out;
}

// The bytes of a hex text, made by the parser from the EDN literal h'...'.
std::string bytes_of_hex(std::string_view const hex)
{
    auto const literal = cbor::parse_edn("h'" + std::string(hex) + "'");
    REQUIRE(literal.has_value());
    auto const bytes = (*literal)->get<std::span<std::byte const>>();
    REQUIRE(bytes.has_value());
    return std::string(reinterpret_cast<char const *>(bytes->data()), bytes->size());
}

// The bytes of a vector, made from its hex text.
std::string bytes_of(cbor::item const &vector)
{
    auto const hex = member_of(vector, "hex");
    REQUIRE(hex != nullptr);
    return bytes_of_hex(*hex->get<std::string_view>());
}

bool round_trips(cbor::item const &vector)
{
    auto const flag = member_of(vector, "roundtrip");
    return flag && flag->get<bool>().value_or(false);
}

} // namespace

// RFC 8949 Appendix A gives the diagnostic notation of 23 items. The parser must turn each of those that round-trip
// into the exact bytes of the vector, or the text and the CBOR of the library do not agree.
TEST_CASE("parse_edn: the diagnostic notation of RFC 8949 Appendix A gives the bytes of the vector")
{
    auto const vectors = read_vectors();
    auto const all = vectors->get<cbor::array_range>();
    REQUIRE(all.has_value());
    std::size_t checked = 0;
    for (cbor::item const &vector : *all) {
        auto const diagnostic = member_of(vector, "diagnostic");
        if (!diagnostic || !round_trips(vector))
            continue;
        std::string const text(*diagnostic->get<std::string_view>());
        CAPTURE(text);
        auto const tree = cbor::parse_edn(text);
        REQUIRE(tree.has_value());
        CHECK_EQ(encoded(**tree), bytes_of(vector));
        ++checked;
    }
    CHECK_GT(checked, 0u);
}

// The JSON value of each other vector was read by parse_edn with the file. Its CBOR must be the bytes of the vector,
// so that JSON text read as EDN gives the CBOR that RFC 8949 names for it.
TEST_CASE("parse_edn: the JSON values of RFC 8949 Appendix A give the bytes of the vector")
{
    auto const vectors = read_vectors();
    auto const all = vectors->get<cbor::array_range>();
    REQUIRE(all.has_value());
    std::size_t checked = 0;
    for (cbor::item const &vector : *all) {
        auto const decoded = member_of(vector, "decoded");
        if (!decoded || !round_trips(vector))
            continue;
        CAPTURE(*member_of(vector, "hex")->get<std::string_view>());
        CHECK_EQ(encoded(*decoded), bytes_of(vector));
        ++checked;
    }
    CHECK_GT(checked, 0u);
}

// item::to_edn gives text that parse_edn reads back to the same bytes, for every vector. A text that loses a head
// width, a tag or a float form would give other bytes.
TEST_CASE("item::to_edn: the text of each vector reads back to its bytes")
{
    auto const vectors = read_vectors();
    auto const all = vectors->get<cbor::array_range>();
    REQUIRE(all.has_value());
    for (cbor::item const &vector : *all) {
        std::string const bytes = bytes_of(vector);
        auto const tree = cbor::decode(bytes);
        if (!tree)
            continue;
        auto const text = (*tree)->to_edn();
        REQUIRE(text.has_value());
        CAPTURE(*text);
        auto const back = cbor::parse_edn(*text);
        REQUIRE(back.has_value());
        CHECK_EQ(encoded(**back), encoded(**tree));
    }
}

// A text that is not exactly one item must be refused with syntax_error, also where the path parser says
// invalid_path for the same text.
TEST_CASE("parse_edn: a text that is not one item gives syntax_error")
{
    for (std::string_view const text : {"", "[1,", "1 2", "{1:}", "h'0'"}) {
        CAPTURE(text);
        auto const tree = cbor::parse_edn(text);
        REQUIRE_FALSE(tree.has_value());
        CHECK_EQ(tree.error(), cbor::error::syntax_error);
    }
}

// RFC 8949 section 3: the encoding indicators _0 to _3 put the argument in 1, 2, 4 and 8 bytes after the initial
// byte. diag2cbor must write the width that the text gives, and diagnostic_notation must give the text back, or the
// two functions do not agree on the head.
// The literal "..."_edn reads its text at compile time, so diag2cbor must run in a constant expression. The constexpr
// variable makes the compiler run the reader there; it holds the size, because a constant cannot keep the memory of
// the string. h'01' is a byte string head and one byte (RFC 8949 3.1).
TEST_CASE("diag2cbor: the reader runs in a constant expression")
{
    constexpr std::size_t size = cbor::diag2cbor("h'01'").value().size();
    CHECK_EQ(size, 2u);
}

TEST_CASE("diag2cbor: an encoding indicator gives the width of the head")
{
    for (auto const &[text, size] : {std::pair<std::string_view, std::size_t>{"1_0", 2}, {"1_1", 3}, {"1_2", 5},
                                     {"1_3", 9}}) {
        CAPTURE(text);
        auto const bytes = cbor::diag2cbor(text);
        REQUIRE(bytes.has_value());
        CHECK_EQ(bytes->size(), size);
        CHECK_EQ(cbor::item_size(*bytes), bytes->size());
        CHECK_EQ(cbor::diagnostic_notation(*bytes), text);
    }
}

// RFC 8949 Appendix A gives the diagnostic notation of the vectors. diag2cbor must give the exact bytes of each
// vector that round-trips, and the bytes must be one item.
TEST_CASE("diag2cbor: the diagnostic notation of RFC 8949 Appendix A gives the bytes of the vector")
{
    auto const vectors = read_vectors();
    auto const all = vectors->get<cbor::array_range>();
    REQUIRE(all.has_value());
    std::size_t checked = 0;
    for (cbor::item const &vector : *all) {
        auto const diagnostic = member_of(vector, "diagnostic");
        if (!diagnostic || !round_trips(vector))
            continue;
        std::string_view const text = *diagnostic->get<std::string_view>();
        CAPTURE(text);
        auto const bytes = cbor::diag2cbor(text);
        REQUIRE(bytes.has_value());
        CHECK_EQ(*bytes, bytes_of(vector));
        CHECK_EQ(cbor::item_size(*bytes), bytes->size());
        ++checked;
    }
    CHECK_GT(checked, 0u);
}

// diag2cbor gives one item. A text that is not one item, and a text with an indefinite length, which the reader
// does not write, must be refused with syntax_error.
TEST_CASE("diag2cbor: a text that is not one item gives syntax_error")
{
    for (std::string_view const text : {"", "[1,", "1 2", "{1:}", "h'0'", "[_ 1]"}) {
        CAPTURE(text);
        auto const bytes = cbor::diag2cbor(text);
        REQUIRE_FALSE(bytes.has_value());
        CHECK_EQ(bytes.error(), cbor::error::syntax_error);
    }
}

// diag2cbor does not check validity: the declaration says that a tag content and a text string stay as written.
// parse_edn decodes the bytes, so it refuses the tags that diag2cbor gives.
TEST_CASE("diag2cbor: bytes that are not valid stay as written")
{
    for (auto const &[text, hex] : {std::pair<std::string_view, std::string_view>{"64(1)", "d84001"},
                                    {"24(<<1, 2>>)", "d818420102"},
                                    {"\"\xff\"", "61ff"}}) {
        CAPTURE(hex);
        auto const bytes = cbor::diag2cbor(text);
        REQUIRE(bytes.has_value());
        CHECK_EQ(*bytes, bytes_of_hex(hex));
        CHECK_EQ(cbor::item_size(*bytes), bytes->size());
    }
    CHECK_EQ(cbor::parse_edn("64(1)").error(), cbor::error::inadmissible_type_for_tag_content);
    CHECK_EQ(cbor::parse_edn("24(<<1, 2>>)").error(), cbor::error::syntax_error);
}

// RFC 8746 reserves tag 76 (the little-endian sint8 form "MUST NOT be used"), so it is no typed array: its content
// has no rule, and the notation prints it as a plain tag over its content, as it prints any tag it does not know.
TEST_CASE("diagnostic_notation: tag 76 is a plain tag")
{
    for (std::string_view const text : {"76(h'01')", "76(1)"}) {
        CAPTURE(text);
        auto const bytes = cbor::diag2cbor(text);
        REQUIRE(bytes.has_value());
        CHECK_EQ(cbor::diagnostic_notation(*bytes), text);
        CHECK(cbor::parse_edn(text).has_value());
    }
}

namespace
{

cbor::owning<cbor::item> decoded_from_edn(std::string_view const text)
{
    auto const bytes = cbor::diag2cbor(text);
    REQUIRE(bytes.has_value());
    auto tree = cbor::decode(*bytes);
    REQUIRE(tree.has_value());
    return std::move(*tree);
}

cbor::owning<cbor::item> shared_from_edn(std::string_view const text)
{
    auto const bytes = cbor::diag2cbor(text);
    REQUIRE(bytes.has_value());
    auto tree = cbor::decode_cycles(*bytes);
    REQUIRE(tree.has_value());
    return std::move(*tree);
}

cbor::item const &element_of(cbor::item const &array, std::size_t const index)
{
    auto const elements = array.get<cbor::array_range>();
    REQUIRE(elements.has_value());
    REQUIRE_LT(index, elements->size());
    return elements->next(static_cast<std::ptrdiff_t>(index)).front();
}

cbor::item const *target_of_reference(cbor::item const &reference)
{
    REQUIRE_EQ(reference.type(), cbor::type::shared_reference);
    return reference.get<std::pair<std::uint64_t, cbor::item const *>>().value().second;
}

} // namespace

// cbor::to_edn walks the tree, so each kind of item and each width that an item keeps must give the text that
// cbor::diagnostic_notation gives for the bytes. Each text below is written in that form: an encoding indicator only
// where a head is not in preferred form (RFC 8949 4.2.1, draft-ietf-cbor-edn-literals).
TEST_CASE("cbor::to_edn: each kind of item gives its text with its encoding indicator")
{
    for (std::string_view const text :
         {"0", "1_0", "23_0", "-1", "-25_1", "18446744073709551615", "-18446744073709551616", "h'0102'", "h''_0",
          "\"a\"", "\"\\n\\\"\"", "\"a\"_1", "[]", "[_0 1]", "[1, [2, 3]]", "{\"a\": 1}", "{_1 1: 2, 3: [4]}", "false",
          "true", "null", "undefined", "simple(99)", "1.5", "1.5_2", "100000.0", "NaN", "Infinity", "-Infinity_3",
          "1(0)", "1_1(0)", "64(h'0102')", "2(h'010000000000000000')", "3(h'010000000000000000')", "24(h'01')",
          "28(1)", "29(0)"}) {
        CAPTURE(text);
        auto const tree = decoded_from_edn(text);
        CHECK_EQ(cbor::to_edn(*tree), text);
        CHECK_EQ(cbor::to_edn(tree), text);
    }
}

// The item form takes any node of a tree, not only the root.
TEST_CASE("cbor::to_edn: an item inside a tree gives the text of that item")
{
    auto const tree = decoded_from_edn("[1, {\"a\": [2_0]}]");
    CHECK_EQ(cbor::to_edn(element_of(*tree, 1)), "{\"a\": [2_0]}");
}

// A tree of cbor::decode_cycles with a shared_reference gives the text of the bytes of cbor::encode. So a tag 28
// stands only on a value that a tag 29 names, each tag 29 has the index of its mark in the text and its shortest head,
// and a tag 28 over a tag 29 marks no value of its own. A tag 28 that no tag 29 names makes no shared_reference, so
// its tree is walked, and the walk writes no tag 28.
TEST_CASE("cbor::to_edn: a tree of decode_cycles gives tags 28 and 29 only for its cycles")
{
    for (auto const &[text, expected] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"28([1, 29(0)])", "28([1, 29(0)])"},
             {"28([29(0)])", "28([29(0)])"},
             {"[28(1)]", "[1]"},
             {"28(28([29(0), 29(1)]))", "28([29(0), 29(0)])"},
             {"28([28(29(0)), 29(1)])", "28([29(0), 29(0)])"},
             {"28(999(29(0)))", "28(999(29(0)))"},
             {"28({\"k\": 29(0)})", "28({\"k\": 29(0)})"},
             {"28([1, 29(0_1)])", "28([1, 29(0)])"},
             {"28([1, 28([2, 29(1)]), 29(0)])", "28([1, 28([2, 29(1)]), 29(0)])"},
             {"2(28(h'05'))", "2(h'05')"}}) {
        CAPTURE(text);
        CHECK_EQ(cbor::to_edn(shared_from_edn(text)), expected);
    }
}

// The text of a shared tree, read back, must name the same targets: cbor::parse_edn keeps tags 28 and 29 as tags and
// gives the text back, and cbor::decode_cycles of its bytes resolves each tag 29 to the node it named before.
TEST_CASE("cbor::to_edn: the text of a shared tree reads back to the same tree")
{
    auto const shared = shared_from_edn("28([1, 28([2, 29(1), 29(0)])])");
    auto const text = cbor::to_edn(shared);
    REQUIRE(text.has_value());
    auto const parsed = cbor::parse_edn(*text);
    REQUIRE(parsed.has_value());
    CHECK_EQ(cbor::to_edn(*parsed), text);
    auto const back = shared_from_edn(*text);
    cbor::item const &inner = element_of(*back, 1);
    CHECK_EQ(target_of_reference(element_of(inner, 1)), &inner);
    CHECK_EQ(target_of_reference(element_of(inner, 2)), &*back);
    auto const cycle = shared_from_edn(*cbor::to_edn(shared_from_edn("28([29(0)])")));
    CHECK_EQ(target_of_reference(element_of(*cycle, 0)), &*cycle);
}

// An item of a tree with a shared_reference needs no owner: a shared_reference stands for its target, and an inner
// item also writes the values above it that its references name. An owning item that holds no tree is a wrong use.
TEST_CASE("cbor::to_edn: an item of a tree with a cycle gives its text without the owner")
{
    auto const shared = shared_from_edn("28([1, 29(0)])");
    CHECK_EQ(cbor::to_edn(*shared), "28([1, 29(0)])");
    CHECK_EQ(cbor::to_edn(element_of(*shared, 1)), "28([1, 29(0)])");
    CHECK_EQ(cbor::to_edn(element_of(*shared, 0)), "1");
    auto const nested = shared_from_edn("28([1, 28([2, 29(1), 29(0)])])");
    CHECK_EQ(cbor::to_edn(element_of(*nested, 1)), "28([2, 29(0), [1, 29(0)]])");
    cbor::owning<cbor::item> moved = shared_from_edn("1");
    cbor::owning<cbor::item> const kept = std::move(moved);
    CHECK_THROWS_AS(std::ignore = cbor::to_edn(moved), std::logic_error);
    CHECK_EQ(cbor::to_edn(kept), "1");
}

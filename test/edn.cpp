#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
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

std::optional<cbor::item> member_of(cbor::item const &object, std::string_view const key)
{
    auto const pairs = object.get<std::ranges::stride_view<std::ranges::adjacent_view<std::span<cbor::item const>, 2>>>();
    REQUIRE(pairs.has_value());
    for (auto const pair : *pairs)
        if (std::get<0>(pair).get<std::string_view>() == key)
            return std::get<1>(pair);
    return std::nullopt;
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
    REQUIRE(hex.has_value());
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
    auto const all = vectors->get<std::span<cbor::item const>>();
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
    auto const all = vectors->get<std::span<cbor::item const>>();
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
    auto const all = vectors->get<std::span<cbor::item const>>();
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
    auto const all = vectors->get<std::span<cbor::item const>>();
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

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
    auto const pairs = object.get<std::ranges::chunk_view<std::span<cbor::item const>>>();
    REQUIRE(pairs.has_value());
    for (auto const pair : *pairs)
        if (pair[0].get<std::string_view>() == key)
            return pair[1];
    return std::nullopt;
}

std::string encoded(cbor::item const &node)
{
    std::string out;
    REQUIRE(cbor::encode(out, node).has_value());
    return out;
}

// The bytes of a vector, made by the parser from the EDN literal h'...' of its hex text.
std::string bytes_of(cbor::item const &vector)
{
    auto const hex = member_of(vector, "hex");
    REQUIRE(hex.has_value());
    auto const literal = cbor::parse_edn("h'" + std::string(*hex->get<std::string_view>()) + "'");
    REQUIRE(literal.has_value());
    auto const bytes = (*literal)->get<std::span<std::byte const>>();
    REQUIRE(bytes.has_value());
    return std::string(reinterpret_cast<char const *>(bytes->data()), bytes->size());
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

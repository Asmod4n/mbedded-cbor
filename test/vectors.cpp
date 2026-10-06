#include "binding.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using cbor::error;

namespace
{

struct vector_case {
    std::string hex;
    bool roundtrip;
};

// Reads the "hex" and "roundtrip" members of each object of a test-vectors file. The files are
// plain arrays of flat objects, so a search for the two keys is enough.
std::vector<vector_case> cases_of(std::string const &file)
{
    std::ifstream in(file);
    REQUIRE(in.good());
    std::string const text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<vector_case> cases;
    std::size_t at = 0;
    while ((at = text.find("\"hex\"", at)) != std::string::npos) {
        std::size_t const open = text.find('"', text.find(':', at)) + 1;
        std::size_t const close = text.find('"', open);
        std::size_t const object_end = text.find('}', close);
        std::size_t const roundtrip = text.find("\"roundtrip\": true", at);
        cases.push_back({text.substr(open, close - open), roundtrip < object_end});
        at = close;
    }
    return cases;
}

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.substr(i, 2).data(), std::to_address(hex.substr(i, 2).end()), value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

} // namespace

// Ported from test-vectors.rb: 'passes the official test vectors', the examples of RFC 8949
// Appendix A. Indefinite lengths are refused, as in mruby-cbor. Every other example decodes, and
// one that a generic encoder writes back identically comes back byte for byte. The skip list of
// mruby-cbor is not needed: the test binding keeps bytes, text, simple values and tags apart.
TEST_CASE("test vectors: RFC 8949 Appendix A")
{
    auto const cases = cases_of(TEST_VECTORS "/appendix_a.json");
    REQUIRE_EQ(cases.size(), 82);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const v = decoded(wire);
        if (!v) {
            CHECK_EQ(v.error(), error::indefinite_length);
            continue;
        }
        if (c.roundtrip)
            CHECK_EQ(encoded(*v), wire);
    }
}

// fail.json holds data items that are not well-formed. Read as exactly one data item, none may
// succeed: decode fails, or bytes remain after the item, which RFC 8949 Appendix F calls too much
// data. cbor::decode itself leaves trailing bytes alone, as CBOR.decode of mruby-cbor does.
TEST_CASE("test vectors: not well-formed items fail")
{
    auto const cases = cases_of(TEST_VECTORS "/fail.json");
    REQUIRE_GT(cases.size(), 600);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const end = cbor::item_end<16>(wire);
        bool const one_item = end.has_value() && *end == wire.size();
        CHECK_FALSE((one_item && decoded(wire).has_value()));
    }
}

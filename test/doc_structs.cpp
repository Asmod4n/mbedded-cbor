#include "reference.hpp"

#ifdef __cpp_impl_reflection

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/structs.md. The tests exist so that every example in the document
// compiles and gives the result that the document states. Where the document writes a comment with the input as
// text, the test builds the same input with preferred().

namespace doc_structs
{

struct address {
    std::string city;
    std::string zip;
};

struct person {
    std::string name;
    int age;
    std::vector<std::string> tags;
    std::optional<address> home;
};

// The section "Write a struct and read it back" shows this round trip whole, with the check of each result, because
// a new user meets two std::expected values and an owning_ref in it.
std::expected<std::string, cbor::error> round_trip()
{
    auto const bytes = cbor::structs<person>::encode(person{"ann", 30, {"admin"}, address{"Graz", "8010"}});
    if (!bytes)
        return std::unexpected(bytes.error());
    auto const text = cbor::diagnostic_notation(*bytes);

    auto const back = cbor::structs<person>::decode(*bytes);
    if (!back)
        return std::unexpected(back.error());
    person const &p = **back;
    return *text + " " + p.home->city;
}

enum class color : std::uint8_t { red, green };

struct header {
    std::string content_type;
};

struct kinds {
    bool b;
    std::uint64_t u;
    float f;
    double d;
    std::vector<std::byte> raw;
    std::list<int> l;
    std::set<int> s;
    std::unordered_map<std::string, int> m;
    color c;
};

} // namespace doc_structs

template <>
struct cbor::describe_members<doc_structs::header> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160060, 0};
    static constexpr std::tuple members{
        cbor::member{"content-type", &doc_structs::header::content_type},
    };
};

namespace doc_structs
{

// The section "Write a struct and read it back" gives the text of the bytes and a member of the struct that decode
// gives back.
TEST_CASE("doc structs: write a struct and read it back")
{
    CHECK_EQ(round_trip(),
             R"({"age": 30, "home": {"zip": "8010", "city": "Graz"}, "name": "ann", "tags": ["admin"]} Graz)");
}

// The table of the section "Types" names the CBOR of each common type. The test writes each one and reads it back.
TEST_CASE("doc structs: types")
{
    kinds const k{true, 7, 1.5f, 2.5, {std::byte{1}}, {1, 2}, {3}, {{"a", 1}}, color::green};
    auto const bytes = cbor::structs<kinds>::encode(k);
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*bytes),
             R"({"b": true, "c": 1, "d": 2.5, "f": 1.5, "l": [1, 2], "m": {"a": 1}, "s": 258([3]), "u": 7, "raw": 64(h'01')})");
    auto const back = cbor::structs<kinds>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->c, color::green);
    CHECK_EQ((*back)->m.at("a"), 1);
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<std::optional<int>>::encode(std::nullopt)), "null");
}

// The section "Keys" shows a key that a description gives, the tag that the description writes, and that an
// empty optional member has no key.
TEST_CASE("doc structs: keys")
{
    auto const bytes = cbor::structs<header>::encode(header{"text/plain"});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*bytes), R"(160060({"content-type": "text/plain"}))");
    CHECK_EQ(cbor::structs<header>::decode(preferred(M("content-type"s, "text/plain"s))).error(),
             error::incorrect_type);
    auto const none = cbor::structs<person>::encode(person{"bob", 40, {}, std::nullopt});
    REQUIRE(none.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*none), R"({"age": 40, "name": "bob", "tags": []})");
    auto const back = cbor::structs<person>::decode(*none);
    REQUIRE(back.has_value());
    CHECK_FALSE((*back)->home.has_value());
}

// The section "Input from other programs" reads input with keys in another order, an unknown key and a missing key,
// and gives one input for each row of its table.
TEST_CASE("doc structs: input from other programs")
{
    auto const other = cbor::structs<person>::decode(preferred(M("age"s, 30, "name"s, "ann"s, "tags"s, A(), "id"s, 7)));
    REQUIRE(other.has_value());
    CHECK_EQ((*other)->name, "ann");
    CHECK_EQ((*other)->age, 30);
    CHECK_FALSE((*other)->home.has_value());
    CHECK_EQ(cbor::structs<person>::decode(preferred(M("name"s, "ann"s))).error(), error::key_not_found);
    CHECK_EQ(cbor::structs<int>::decode(preferred(V("a"s))).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::uint8_t>::decode(preferred(V(256))).error(), error::number_out_of_range);
    CHECK_EQ((cbor::structs<std::array<int, 2>>::decode(preferred(A(1))).error()), error::incorrect_type);
    CHECK_EQ(cbor::structs<int>::decode(preferred(V(1)) + preferred(V(2))).error(), error::syntax_error);
    std::string const cut = preferred(V("abc"s));
    CHECK_EQ(cbor::structs<std::string>::decode(cut.substr(0, cut.size() - 1)).error(), error::too_little_data);
}

} // namespace doc_structs

#endif

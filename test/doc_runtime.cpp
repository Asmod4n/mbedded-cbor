#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/runtime.md. The tests exist so that every example in the document
// compiles and gives the result that the document states. Where the document writes a comment with the input as
// text, the test builds the same input with preferred().

namespace doc_runtime
{

template <class R>
concept dereferences = requires(R &&r) { *std::forward<R>(r); };

template <class R>
concept arrows = requires(R &&r) { std::forward<R>(r).operator->(); };

// The section "Loop over an array or a map" shows this function whole, because a loop over std::expected values is
// the place where a new user leaves out a check.
std::expected<std::int64_t, cbor::error> sum(std::string_view bytes)
{
    auto const array = test::lazy_of_copy(bytes);
    if (!array)
        return std::unexpected(array.error());
    auto const elements = array->elements();
    if (!elements)
        return std::unexpected(elements.error());
    std::int64_t total = 0;
    for (auto const element : *elements) {
        if (!element)
            return std::unexpected(element.error());
        auto const n = element->get<std::int64_t>();
        if (!n)
            return std::unexpected(n.error());
        total += *n;
    }
    return total;
}

} // namespace doc_runtime

// The section "Read step by step" shows the chain of steps with and_then, and the error that each step can give.
TEST_CASE("doc runtime: read step by step")
{
    std::string bytes = preferred(M("cars"s, A(M("hp"s, 5), M("hp"s, 7))));
    std::int64_t used = 0;
    auto const use = [&used](std::int64_t const v) { used = v; };
    auto const hp = test::lazy_of_copy(bytes)
                        .and_then([](cbor::lazy const &doc) { return doc.at("cars"); })
                        .and_then([](cbor::lazy const &cars) { return cars.at(1); })
                        .and_then([](cbor::lazy const &car) { return car.at("hp"); })
                        .and_then([](cbor::lazy const &h) { return h.get<std::int64_t>(); });
    if (hp)
        use(*hp);
    CHECK_EQ(used, 7);
    CHECK_EQ(cbor::at_path<"$.cars[1].hp", std::int64_t>(bytes), 7);
    auto const doc = cbor::lazy::take_ownership(std::move(bytes));
    REQUIRE(doc.has_value());
    CHECK_EQ(doc->at("bus").error(), error::key_not_found);
    CHECK_EQ(doc->at("cars")->at(2).error(), error::index_out_of_bounds);
    CHECK_EQ(doc->at("cars")->at(0)->at("hp")->at(0).error(), error::not_indexable);
    auto const owner = std::make_shared<std::string const>(preferred(M("cars"s, A(M("hp"s, 5)))));
    auto const viewed = cbor::lazy::view(owner);
    REQUIRE(viewed.has_value());
    CHECK_EQ(viewed->top_level.encoded.data(), owner->data());
    auto const part = cbor::lazy::view(owner, std::string_view(*owner).substr(1));
    REQUIRE(part.has_value());
    CHECK_EQ(part->top_level.encoded.data(), owner->data() + 1);
    auto const key = part->get<std::string_view>();
    REQUIRE(key.has_value());
    CHECK_EQ(**key, "cars"sv);
    auto const keyed = test::lazy_of_copy(preferred(M(7, "seven"s)));
    auto const seven = keyed->at(cbor::key{7})->get<std::string_view>();
    REQUIRE(seven.has_value());
    CHECK_EQ(**seven, "seven"sv);
}

// The section "Read a value as a type" shows a text, a number and the two errors of get.
TEST_CASE("doc runtime: read a value as a type")
{
    std::string const bytes = preferred(M("name"s, "ann"s, "age"s, 300));
    auto const doc = test::lazy_of_copy(bytes);
    auto const name = doc->at("name")->get<std::string_view>();
    CHECK_EQ(**name, "ann"sv);
    CHECK_EQ(doc->at("age")->get<std::int64_t>(), 300);
    CHECK_EQ(doc->at("age")->get<std::uint8_t>().error(), error::number_out_of_range);
    CHECK_EQ(doc->at("name")->get<std::int64_t>().error(), error::incorrect_type);
    CHECK(doc_runtime::dereferences<cbor::ref<std::string_view> const &>);
    CHECK_FALSE(doc_runtime::dereferences<cbor::ref<std::string_view>>);
    CHECK(doc_runtime::arrows<cbor::ref<std::string_view> const &>);
    CHECK_FALSE(doc_runtime::arrows<cbor::ref<std::string_view>>);
    CHECK_EQ(test::lazy_of_copy(preferred(V(1.5)))->get<double>(), 1.5);
    CHECK_EQ(test::lazy_of_copy(preferred(V(42)))->get<double>(), 42.0);
    CHECK_EQ(test::lazy_of_copy(preferred(V(simple{21})))->get<bool>(), true);
    auto const two = test::lazy_of_copy(preferred(V(test::bytes{"\x01\x02"})))->get<std::span<std::byte const>>();
    CHECK_EQ((*two)->size(), 2u);
}

// The section "Loop over an array or a map" sums an array and lists the pairs of a map.
TEST_CASE("doc runtime: loop over an array or a map")
{
    CHECK_EQ(doc_runtime::sum(preferred(A(1, 2, 3))), 6);
    CHECK_EQ(doc_runtime::sum(preferred(A(1, "a"s))).error(), error::incorrect_type);
    CHECK_EQ(doc_runtime::sum(preferred(M("a"s, 1))).error(), error::not_indexable);

    std::string const bytes = preferred(M("a"s, 1, "b"s, 2));
    std::vector<std::pair<std::string, std::int64_t>> seen;
    auto const use = [&seen](std::expected<cbor::ref<std::string_view>, error> const &k,
                             std::expected<std::int64_t, error> const &v) { seen.emplace_back(std::string{**k}, *v); };
    auto const map = test::lazy_of_copy(bytes);
    for (auto const pair : *map->entries())
        if (pair)
            use(pair->first.get<std::string_view>(), pair->second.get<std::int64_t>());
    CHECK(seen == std::vector<std::pair<std::string, std::int64_t>>{{"a", 1}, {"b", 2}});
}

// The section "Size and keys" shows size and a missing key.
TEST_CASE("doc runtime: size and keys")
{
    std::string const bytes = preferred(M("a"s, 1, "b"s, 2));
    auto const map = test::lazy_of_copy(bytes);
    CHECK_EQ(map->size(), 2u);
    CHECK_EQ(map->at("z").error(), cbor::error::key_not_found);
    CHECK_EQ(test::lazy_of_copy(preferred(A(1, 2, 3)))->size(), 3u);
}

// The section "Print CBOR as text" prints the input of the first section, because a user needs to look at bytes.
TEST_CASE("doc runtime: print CBOR as text")
{
    std::string const bytes = preferred(M("cars"s, A(M("hp"s, 5), M("hp"s, 7))));
    std::expected<std::string, error> const text = cbor::diagnostic_notation(bytes);
    CHECK_EQ(text, R"({"cars": [{"hp": 5}, {"hp": 7}]})");
    CHECK_EQ(cbor::diagnostic_notation("\xff"sv).error(), error::syntax_error);
}

// The section "Write CBOR" writes {"a": 5} with the encoder, one item per call.
TEST_CASE("doc runtime: write CBOR")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.map(1).has_value());
    REQUIRE(e.text_string("a").has_value());
    REQUIRE(e.unsigned_integer(std::uint8_t{5}).has_value());
    REQUIRE(e.finish().has_value());
    CHECK_EQ(cbor::diagnostic_notation(out), "{\"a\": 5_0}");
}

// The section "Read items from a socket": the bytes of [1, 2] and of "abc" arrive in three pieces, and each piece
// ends in the middle of an item.
TEST_CASE("doc runtime: read items from a socket")
{
    std::vector<std::string> handled;
    auto const handle = [&handled](cbor::lazy const &item) {
        handled.push_back(cbor::diagnostic_notation(item.top_level->encoded).value());
    };
    cbor::sequence_reader r;
    for (std::string_view const chunk : {"\x82\x01"sv, "\x02\x63" "a"sv, "bc"sv}) {
        r.feed(chunk);
        while (auto item = r.next())
            handle(*item);
        CHECK_EQ(r.next().error(), cbor::error::too_little_data);
    }
    CHECK_EQ(handled, std::vector<std::string>{"[1, 2]", "\"abc\""});
    CHECK(r.empty());
}

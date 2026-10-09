#include "binding.hpp"

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::vector<std::expected<std::string_view, error>> elements_of(std::string_view const encoded)
{
    std::vector<std::expected<std::string_view, error>> out;
    for (auto const e : cbor::sequence{encoded})
        out.push_back(e);
    return out;
}

bool inside(std::string const &bytes, char const *const p)
{
    return !std::less<char const *>{}(p, bytes.data()) &&
           std::less<char const *>{}(p, bytes.data() + bytes.size());
}

} // namespace

// RFC 8742 2: an empty sequence of bytes is a CBOR Sequence of no data items.
TEST_CASE("sequence: an empty CBOR Sequence has no element")
{
    CHECK(elements_of(""sv).empty());
    CHECK(std::ranges::input_range<cbor::sequence>);
}

// RFC 8742 2: the elements are the encoded data items, one after the other, with no marker between them. Each
// element is a view into the bytes of the caller.
TEST_CASE("sequence: each element is the view of one encoded data item")
{
    std::string const bytes = "\x01\x61\x61\xa1\x61\x61\x82\x01\x02\xf6"s;
    auto const e = elements_of(bytes);
    REQUIRE_EQ(e.size(), 4u);
    CHECK_EQ(e[0].value(), "\x01"sv);
    CHECK_EQ(e[1].value(), "\x61\x61"sv);
    CHECK_EQ(e[2].value(), "\xa1\x61\x61\x82\x01\x02"sv);
    CHECK_EQ(e[3].value(), "\xf6"sv);
    CHECK(inside(bytes, e[2]->data()));
}

// RFC 8742 2: a truncated last item is detected. The bytes that follow the last complete item stay in the
// iterator, so that a reader of a stream can wait for more bytes and read them again.
TEST_CASE("sequence: a partial last item gives too_little_data and keeps its bytes")
{
    std::string const bytes = "\x01\x19\x01"s;
    cbor::sequence const s{bytes};
    auto it = s.begin();
    REQUIRE(it != s.end());
    CHECK_EQ((*it).value(), "\x01"sv);
    ++it;
    REQUIRE(it != s.end());
    CHECK_EQ((*it).error(), error::too_little_data);
    CHECK_EQ(it.encoded, "\x19\x01"sv);
    ++it;
    CHECK(it == s.end());
}

// RFC 8742 2: after an item that is not well formed the rest cannot be read reliably, so the sequence ends
// there. Indefinite length is never read (decision of the owner). The end of an item is found with no stack,
// so a deep item is no error there.
TEST_CASE("sequence: an error ends the sequence")
{
    std::vector<std::expected<std::string_view, error>> const reserved = elements_of("\x01\x1c\x01"sv);
    REQUIRE_EQ(reserved.size(), 2u);
    CHECK_EQ(reserved[1].error(), error::syntax_error);
    std::vector<std::expected<std::string_view, error>> const indefinite = elements_of("\x9f\x01\xff\x01"sv);
    REQUIRE_EQ(indefinite.size(), 1u);
    CHECK_EQ(indefinite[0].error(), error::indefinite_length);
    std::string const deep = "\x01"s + std::string(20, '\x81') + "\x01"s;
    std::vector<std::expected<std::string_view, error>> const nested = elements_of(deep);
    REQUIRE_EQ(nested.size(), 2u);
    CHECK_EQ(nested[1].value().size(), 21u);
}

// Each element is a top-level item of its own (RFC 8742 2). lazy, path and decode read it with the one owner
// of the whole sequence: every result holds that owner and no copy of the bytes.
TEST_CASE("sequence: lazy, path and decode read each element with the owner of the sequence")
{
    auto const owner = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz\x82\x01\x02"s);
    std::vector<cbor::lazy> items;
    for (auto const e : cbor::sequence{*owner}) {
        REQUIRE(e.has_value());
        auto const l = cbor::lazy::from(owner, *e);
        REQUIRE(l.has_value());
        items.push_back(*l);
    }
    REQUIRE_EQ(items.size(), 2u);
    auto const text = items[0].at("a");
    REQUIRE(text.has_value());
    auto const view = text->get<std::string_view>();
    REQUIRE(view.has_value());
    CHECK_EQ(**view, "xyz"sv);
    CHECK(inside(*owner, (*view)->data()));
    auto const second = items[1].at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(second->get<std::uint64_t>().value(), 2u);

    std::vector<std::string_view> views;
    for (auto const e : cbor::sequence{*owner})
        views.push_back(e.value());
    auto const path = cbor::at_path<"$.a", std::string_view>(owner, views[0]);
    REQUIRE(path.has_value());
    CHECK_EQ(**path, "xyz"sv);
    CHECK(inside(*owner, (*path)->data()));
    CHECK_EQ(cbor::at_path<"$[0]", std::uint64_t>(views[1]).value(), 1u);

    test_binding binding;
    auto const decoded = cbor::lazy_decode(binding, items[1]);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == A(1, 2));
}

// Value sharing (tags 28 and 29) counts within one top-level item. A reference in the second element cannot
// reach a value that the first element marks.
TEST_CASE("sequence: shared references do not cross elements")
{
    auto const owner = std::make_shared<std::string const>("\xd8\x1c\x01\x81\xd8\x1d\x00"s);
    std::vector<std::string_view> views;
    for (auto const e : cbor::sequence{*owner})
        views.push_back(e.value());
    REQUIRE_EQ(views.size(), 2u);
    auto const l = cbor::lazy::from(owner, views[1]);
    REQUIRE(l.has_value());
    auto const element = l->at(0).and_then([](cbor::lazy const &e) { return e.get<std::uint64_t>(); });
    CHECK_EQ(element.error(), error::sharedref_index_not_marked);
    CHECK_EQ(cbor::at_path<"$[0]", std::uint64_t>(views[1]).error(), error::sharedref_index_not_marked);
}

// A sequence of views has no owner: a temporary std::string would free the bytes under the views.
TEST_CASE("sequence: a temporary std::string does not compile")
{
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string &&>);
    CHECK(std::is_constructible_v<cbor::sequence, std::string_view>);
}

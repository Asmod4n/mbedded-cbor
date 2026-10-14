#include "reference.hpp"

#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <memory_resource>
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

std::shared_ptr<std::string const> held(std::string_view const encoded)
{
    return std::make_shared<std::string const>(encoded);
}

std::vector<std::expected<std::string_view, error>> elements_of(std::shared_ptr<std::string const> const &encoded)
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

// RFC 8742 2: an empty sequence of bytes is a CBOR Sequence of no data items. empty() answers that without a
// read, as std::ranges::empty does for any range; a sequence of bytes that are not well-formed is not empty.
// The iterator is an input iterator: the size of each item is read once, on the step to it.
TEST_CASE("sequence: an empty CBOR Sequence has no element")
{
    CHECK(elements_of(held(""sv)).empty());
    CHECK((std::ranges::input_range<cbor::sequence>));
    CHECK((!std::ranges::forward_range<cbor::sequence>));
    CHECK((!std::ranges::sized_range<cbor::sequence>));
    CHECK(cbor::sequence{held(""sv)}.empty());
    CHECK(std::ranges::empty(cbor::sequence{held(""sv)}));
    CHECK_FALSE(cbor::sequence{held("\x01"sv)}.empty());
    CHECK_FALSE(cbor::sequence{held("\x18"sv)}.empty());
}

// RFC 8742 2: the elements are the encoded data items, one after the other, with no marker between them. Each
// element is a view into the bytes of the owner that the caller gave.
TEST_CASE("sequence: each element is the view of one encoded data item")
{
    auto const bytes = held("\x01\x61\x61\xa1\x61\x61\x82\x01\x02\xf6"sv);
    auto const e = elements_of(bytes);
    REQUIRE_EQ(e.size(), 4u);
    CHECK_EQ(e[0].value(), "\x01"sv);
    CHECK_EQ(e[1].value(), "\x61\x61"sv);
    CHECK_EQ(e[2].value(), "\xa1\x61\x61\x82\x01\x02"sv);
    CHECK_EQ(e[3].value(), "\xf6"sv);
    CHECK(inside(*bytes, e[2]->data()));
}

// RFC 8742 2: a truncated last item is detected. The bytes that follow the last complete item stay in the
// iterator, so that a reader of a stream can wait for more bytes and read them again.
TEST_CASE("sequence: a partial last item gives too_little_data and keeps its bytes")
{
    cbor::sequence const s{held("\x01\x19\x01"sv)};
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
    std::vector<std::expected<std::string_view, error>> const reserved = elements_of(held("\x01\x1c\x01"sv));
    REQUIRE_EQ(reserved.size(), 2u);
    CHECK_EQ(reserved[1].error(), error::syntax_error);
    std::vector<std::expected<std::string_view, error>> const indefinite = elements_of(held("\x9f\x01\xff\x01"sv));
    REQUIRE_EQ(indefinite.size(), 1u);
    CHECK_EQ(indefinite[0].error(), error::indefinite_length);
    auto const deep = held("\x01"s + std::string(20, '\x81') + "\x01"s);
    std::vector<std::expected<std::string_view, error>> const nested = elements_of(deep);
    REQUIRE_EQ(nested.size(), 2u);
    CHECK_EQ(nested[1].value().size(), 21u);
}

// Each element is a top-level item of its own (RFC 8742 2). lazy and path read an element through the copy form,
// because an element is a part of the bytes of the owner and a view takes a whole std::string.
TEST_CASE("sequence: lazy, path and decode read each element through a copy")
{
    auto const owner = std::make_shared<std::string const>("\xa1\x61\x61\x63xyz\x82\x01\x02"s);
    std::vector<cbor::lazy> items;
    for (auto const e : cbor::sequence{owner}) {
        REQUIRE(e.has_value());
        auto const l = cbor::lazy::copy(*e);
        REQUIRE(l.has_value());
        items.push_back(*l);
    }
    REQUIRE_EQ(items.size(), 2u);
    auto const text = items[0].at("a");
    REQUIRE(text.has_value());
    auto const view = text->get<std::string_view>();
    REQUIRE(view.has_value());
    CHECK_EQ(**view, "xyz"sv);
    CHECK_FALSE(inside(*owner, (*view)->data()));
    auto const second = items[1].at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(second->get<std::uint64_t>().value(), 2u);

    std::vector<std::string_view> views;
    for (auto const e : cbor::sequence{owner})
        views.push_back(e.value());
    auto const path = cbor::at_path<"$.a", std::string_view>(views[0]);
    REQUIRE(path.has_value());
    CHECK_EQ(**path, "xyz"sv);
    CHECK_FALSE(inside(*owner, (*path)->data()));
    CHECK_EQ(cbor::at_path<"$[0]", std::uint64_t>(views[1]).value(), 1u);

    auto const decoded = test::value_of(items[1]);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == A(1, 2));
}

// A lazy reads tags 28 and 29 as plain tags, so a reference in the second element is a tag over its index and
// reaches nothing in the first element.
TEST_CASE("sequence: shared references do not cross elements")
{
    auto const owner = std::make_shared<std::string const>("\xd8\x1c\x01\x81\xd8\x1d\x00"s);
    std::vector<std::string_view> views;
    for (auto const e : cbor::sequence{owner})
        views.push_back(e.value());
    REQUIRE_EQ(views.size(), 2u);
    auto const l = cbor::lazy::copy(views[1]);
    REQUIRE(l.has_value());
    auto const element = l->at(0).and_then([](cbor::lazy const &e) { return e.get<std::uint64_t>(); });
    CHECK_EQ(element, 0u);
    CHECK_EQ(l->at(0)->type(), cbor::type::tag);
    CHECK_EQ(cbor::at_path<"$[0]", std::uint64_t>(views[1]), 0u);
}

// Owner decision: a sequence takes its bytes only in the view form, so each element is a view that the sequence
// keeps alive. A string or a view of any kind would let an element outlive its bytes; a const std::string prvalue and
// a std::pmr::string prvalue did so once through the old constructor from std::string_view.
TEST_CASE("sequence: only a std::shared_ptr<std::string const> makes a sequence")
{
    CHECK(std::is_constructible_v<cbor::sequence, std::shared_ptr<std::string const>>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string &&>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string const &>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string const &&>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::string_view>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::pmr::string &>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, std::pmr::string &&>);
    CHECK_FALSE(std::is_constructible_v<cbor::sequence, char const (&)[3]>);
    CHECK_THROWS_AS(cbor::sequence{std::shared_ptr<std::string const>{}}, std::logic_error);
}

// RFC 8742 2 and RFC 8949 3: each data item of a CBOR Sequence has at least its initial byte. An element whose count is
// larger than the bytes left is too_little_data before its elements are read, by owner decision.
TEST_CASE("sequence: a count larger than the bytes left is too_little_data before the elements")
{
    for (std::string_view const wire : {"\x8c\xff"sv, "\x95\xf8\x04"sv, "\x95\xbf"sv, "\x88\x00\x7f"sv,
                                        "\x87\xd8\x1c\x1f"sv, "\x85\xd8\x1c\xd8\x1c"sv, "\xa2\xff\x00\x00"sv}) {
        CAPTURE(wire);
        auto const elements = elements_of(held(wire));
        REQUIRE_EQ(elements.size(), 1);
        CHECK_EQ(elements.front().error(), error::too_little_data);
    }
}

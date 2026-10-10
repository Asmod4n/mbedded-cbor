#include "binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <algorithm>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/runtime.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

// The section "lazy::from" lists four forms, and states that a form reads no head and does not check that the
// bytes are one complete item.
TEST_CASE("doc runtime: lazy::from in every form")
{
    std::string moved = "\x78\x28"s + std::string(40, 'x');
    char const *const data = moved.data();
    auto const a = cbor::lazy::from(std::move(moved));
    REQUIRE(a.has_value());
    CHECK_EQ(a->top_level->encoded.data(), data);
    auto const b = cbor::lazy::from("\x82\x01\x02"sv);
    auto const held = std::make_shared<std::string const>("\x82\x01\x02"s);
    auto const c = cbor::lazy::from(held);
    auto const d = cbor::lazy::from(held, *held);
    for (auto const *l : {&b, &c, &d}) {
        REQUIRE(l->has_value());
        CHECK_EQ((*l)->at(1)->get<std::int64_t>(), 2);
    }
    CHECK_EQ(c->top_level->encoded.data(), held->data());
    CHECK_EQ(d->top_level->encoded.data(), held->data());
    auto const cut = cbor::lazy::from("\x82\x01"s);
    REQUIRE(cut.has_value());
    REQUIRE(cut->at(1).has_value());
    CHECK_EQ(cut->at(1)->get<std::int64_t>().error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::from("\x01\x02"s)->get<std::int64_t>(), 1);
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::lazy::from(o, std::string(*o)); }; }(held)));
}

// The section "A step into a map or an array" shows each step and the chain with and_then.
TEST_CASE("doc runtime: a step into a map or an array")
{
    auto const doc = *cbor::lazy::from(encoded(M("cars"s, A(M("hp"s, 5), M("hp"s, 7)), 7, "seven"s)));
    auto const hp = doc.at("cars")
                        .and_then([](cbor::lazy const &c) { return c.at(1); })
                        .and_then([](cbor::lazy const &c) { return c.at("hp"); })
                        .and_then([](cbor::lazy const &h) { return h.get<std::int64_t>(); });
    CHECK_EQ(hp, 7);
    auto const seven = doc.at(cbor::key{7})->get<std::string_view>();
    CHECK_EQ(**seven, "seven"sv);
    CHECK(*doc.contains("cars"));
    CHECK_EQ(*doc.count(std::int64_t{7}), 1u);
    CHECK_EQ(*doc.size(), 2u);
    CHECK_FALSE(*doc.empty());
    auto const found = doc.find("cars");
    REQUIRE(found.has_value());
    REQUIRE(*found != std::default_sentinel);
    CHECK_EQ(*(**found)->second.size(), 2u);
    CHECK(*doc.find("bus") == std::default_sentinel);
    CHECK_EQ(doc.at("cars")->at(5).error(), error::index_out_of_bounds);
    CHECK_EQ(doc.at("bus").error(), error::key_not_found);
    CHECK_EQ(doc.at(0).error(), error::not_indexable);
}

// The section "A step into a map or an array" states that a byte string is not a text key, and that a repeated key
// gives the first pair and counts every pair.
TEST_CASE("doc runtime: keys")
{
    CHECK_EQ(cbor::lazy::from("\xa1\x41" "a\x01"s)->at("a").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy::from("\xa1\xd8\x1c\x61" "a\x01"s)->at("a")->get<std::int64_t>(), 1);
    auto const twice = *cbor::lazy::from("\xa2\x61" "a\x01\x61" "a\x02"s);
    CHECK_EQ(twice.at("a")->get<std::int64_t>(), 1);
    CHECK_EQ(*twice.count("a"), 2u);
}

// The section "elements and entries" shows both views with a loop and with a standard view.
TEST_CASE("doc runtime: elements and entries")
{
    auto const l = *cbor::lazy::from(encoded(A(1, "a"s, A(2))));
    std::vector<std::expected<std::int64_t, error>> read;
    for (auto const e : *l.elements())
        read.push_back(e->get<std::int64_t>());
    REQUIRE_EQ(read.size(), 3u);
    CHECK_EQ(read[0], 1);
    CHECK_EQ(read[1].error(), error::incorrect_type);
    CHECK_EQ(read[2].error(), error::incorrect_type);
    CHECK_EQ(l.elements()->size(), 3u);
    auto const m = *cbor::lazy::from(encoded(M("a"s, 1, 2, "b"s)));
    std::vector<value> keys;
    for (auto const p : *m.entries())
        keys.push_back(*cbor::lazy_decode(*std::make_unique<test_binding>(), p->first));
    CHECK(keys == std::vector<value>{V("a"s), V(2)});
    CHECK_EQ(std::ranges::distance(*m.entries() | std::views::drop(1)), 1);
    CHECK_EQ(m.elements().error(), error::not_indexable);
    CHECK_EQ(l.entries().error(), error::not_indexable);
    CHECK_EQ(cbor::lazy::from("\x9f\x01\xff"s)->elements().error(), error::indefinite_length);
    auto const it = l.elements()->begin();
    CHECK_EQ((*it)->get<std::int64_t>(), 1);
}

// A user did not know that an algorithm takes the view directly and that -> works on each element.
TEST_CASE("doc runtime: an algorithm over elements")
{
    auto const people = *cbor::lazy::from(encoded(A(M("age"s, 31), M("age"s, 20), M("age"s, 40))));
    CHECK_EQ(std::ranges::count_if(*people.elements(), [](std::expected<cbor::lazy, cbor::error> const &e) {
                 auto const age = e->at("age");
                 return age && age->get<std::int64_t>().value_or(0) > 30;
             }),
             2);
}

// The section "A search between two iterators" shows find and equal_range over a part of a sorted map.
TEST_CASE("doc runtime: a search between two iterators")
{
    auto const m = *cbor::lazy::from(encoded(M("b"s, 2, "d"s, 4, "f"s, 6, "h"s, 8)));
    auto const e = *m.entries();
    auto const h = *m.find("h");
    auto const d = cbor::lazy::find(e.begin(), h, "d");
    REQUIRE(d.has_value());
    CHECK_EQ((**d)->second.get<std::int64_t>(), 4);
    CHECK(*cbor::lazy::find(h, std::default_sentinel, "d") == std::default_sentinel);
    auto const f = cbor::lazy::equal_range(e.begin(), h, "f");
    REQUIRE(f.has_value());
    REQUIRE_EQ(std::ranges::distance(*f), 1);
    CHECK_EQ((*f->begin())->second.get<std::int64_t>(), 6);
    auto const miss = cbor::lazy::equal_range(e.begin(), h, "e");
    REQUIRE(miss.has_value());
    CHECK(miss->empty());
    auto const at = (*miss->begin())->first.get<std::string_view>();
    CHECK_EQ(**at, "f"sv);
    auto const other = *cbor::lazy::from(encoded(M("x"s, 1)));
    CHECK_THROWS_AS(std::ignore = cbor::lazy::equal_range(e.begin(), *other.find("x"), "x"), std::logic_error);
}

// The section "get" shows a read of each kind and the two errors.
TEST_CASE("doc runtime: get")
{
    CHECK_EQ(cbor::lazy::from("\x18\x2a"s)->get<std::int64_t>(), 42);
    CHECK_EQ(cbor::lazy::from("\x19\x01\x00"s)->get<std::uint8_t>().error(), error::number_out_of_range);
    CHECK_EQ(cbor::lazy::from("\xf9\x3e\x00"s)->get<double>(), 1.5);
    CHECK_EQ(cbor::lazy::from("\x18\x2a"s)->get<double>(), 42.0);
    CHECK_EQ(cbor::lazy::from("\xf5"s)->get<bool>(), true);
    CHECK_EQ(cbor::lazy::from("\xf6"s)->get<std::nullptr_t>(), nullptr);
    CHECK_EQ(cbor::lazy::from("\xf7"s)->get<cbor::simple_value>(), cbor::simple_value::undefined);
    auto const text = cbor::lazy::from("\x63" "abc"s)->get<std::string_view>();
    CHECK_EQ(**text, "abc"sv);
    auto const two = cbor::lazy::from("\x42\x01\x02"s)->get<std::span<std::byte const>>();
    CHECK_EQ((*two)->size(), 2u);
    auto const typed = cbor::lazy::from("\xd8\x45\x44\x07\x00\x08\x00"s)->get<cbor::typed_array>();
    REQUIRE(typed.has_value());
    CHECK_EQ((*typed)->tag, 69u);
    CHECK_EQ(cbor::lazy::from("\x63" "abc"s)->get<bool>().error(), error::incorrect_type);
    CHECK_EQ(cbor::lazy::from("\xc2\x41\x05"s)->get<std::int64_t>(), 5);
    CHECK_EQ(cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(1)->get<std::int64_t>(), 1);
    CHECK_EQ(cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(0)->get<std::int64_t>(), 1);
}

// The section "decode and lazy_decode" shows the item of an array and the decode into a binding.
TEST_CASE("doc runtime: decode and lazy_decode")
{
    auto const i = cbor::lazy::from("\x82\x01\x02"s)->decode();
    REQUIRE(i.has_value());
    CHECK_EQ((*i)->major_type, cbor::major_type::array);
    CHECK_EQ((*i)->argument, 2u);
    test_binding binding;
    CHECK(*cbor::lazy_decode(binding, *cbor::lazy::from("\xa1\x61" "a\x82\x01\x02"s)) == M("a"s, A(1, 2)));
    CHECK_EQ(cbor::lazy_decode(binding, *cbor::lazy::from("\x82\x01"s)).error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::from("\x82\x01"s)->decode().error(), error::too_little_data);
}

// The section "encode" shows each kind of target and the shared form.
TEST_CASE("doc runtime: encode")
{
    test_binding binding;
    std::string out = "\x00"s;
    REQUIRE(cbor::encode(binding, out, A(1, 2)).has_value());
    CHECK_EQ(out, "\x00\x82\x01\x02"s);
    std::vector<std::byte> bytes;
    REQUIRE(cbor::encode(binding, bytes, A(1, 2)).has_value());
    CHECK_EQ(bytes.size(), 3u);
    std::array<char, 3> fixed{};
    REQUIRE(cbor::encode(binding, std::span<char>(fixed), A(1, 2)).has_value());
    CHECK_EQ(std::string_view(fixed.data(), fixed.size()), "\x82\x01\x02"sv);
    std::array<char, 2> small{};
    CHECK_EQ(cbor::encode(binding, std::span<char>(small), A(1, 2)).error(), error::no_buffer_space);
    string_writer w;
    REQUIRE(cbor::encode(binding, w, A(1, 2)).has_value());
    CHECK_EQ(w.encoded, "\x82\x01\x02"s);
}

// The section "cbor::sequence" shows the items of a sequence, the error at a cut item, and the owner of the
// sequence for each element.
TEST_CASE("doc runtime: cbor::sequence")
{
    std::string const bytes = "\x01\x61" "a\x82\x01\x02"s;
    std::vector<std::string_view> items;
    for (auto const e : cbor::sequence{bytes})
        items.push_back(*e);
    CHECK(items == std::vector{"\x01"sv, "\x61" "a"sv, "\x82\x01\x02"sv});
    std::string const cut = "\x01\x19\x01"s;
    cbor::sequence const s{cut};
    auto it = s.begin();
    ++it;
    CHECK_EQ((*it).error(), error::too_little_data);
    CHECK_EQ(it.encoded, "\x19\x01"sv);
    auto const owner = std::make_shared<std::string const>(bytes);
    std::vector<std::int64_t> last;
    for (auto const e : cbor::sequence{*owner})
        if (auto const l = cbor::lazy::from(owner, *e); l->size().has_value())
            last.push_back(*l->at(1)->get<std::int64_t>());
    CHECK(last == std::vector<std::int64_t>{2});
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&t) { cbor::sequence{std::forward<S>(t)}; }; }(std::string())));
}

// The section "diagnostic_notation" shows an array, an encoding indicator and an error.
TEST_CASE("doc runtime: diagnostic_notation")
{
    CHECK_EQ(cbor::diagnostic_notation("\x82\x01\x02"sv), "[1, 2]");
    CHECK_EQ(cbor::diagnostic_notation("\x19\x00\x01"sv), "1_1");
    CHECK_EQ(cbor::diagnostic_notation("\x82\x01"sv).error(), error::too_little_data);
}

// The section "item_size" shows the size of the first item and the error of a cut item.
TEST_CASE("doc runtime: item_size")
{
    CHECK_EQ(cbor::item_size("\x01\x02"sv), 1u);
    CHECK_EQ(cbor::item_size("\x82\x01"sv).error(), error::too_little_data);
}

// The section "Tags 28 and 29" shows a reference that is read, written and refused.
TEST_CASE("doc runtime: tags 28 and 29")
{
    auto const l = *cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    CHECK_EQ(l.at(1)->get<std::int64_t>(), 1);
    CHECK_EQ(cbor::diagnostic_notation("\x82\xd8\x1c\x01\xd8\x1d\x00"sv), "[28(1), 29(0)]");
    test_binding binding;
    CHECK_EQ(cbor::lazy_decode(binding, *cbor::lazy::from("\x82\xd8\x1d\x00\xd8\x1c\x05"s)).error(),
             error::sharedref_index_not_marked);
    CHECK_EQ(cbor::lazy_decode(binding, *cbor::lazy::from("\xd8\x1c\x81\xd8\x1d\x00"s)).error(),
             error::sharedref_not_complete);
}

// The section "Tag 55799" shows that only a leading tag 55799 is read as its content.
TEST_CASE("doc runtime: tag 55799")
{
    CHECK_EQ(cbor::lazy::from("\xd9\xd9\xf7\x82\x01\x02"s)->at(1)->get<std::int64_t>(), 2);
    test_binding binding;
    CHECK(*cbor::lazy_decode(binding, *cbor::lazy::from("\x81\xd9\xd9\xf7\x01"s)) ==
          A(value{test::tagged{55799, V(1)}}));
    CHECK_EQ(encoded(value{test::tagged{55799, A(1, 2)}}), "\xd9\xd9\xf7\x82\x01\x02"s);
}

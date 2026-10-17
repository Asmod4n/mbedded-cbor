#include "reference.hpp"
#include "item_head.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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

// Each test in this file is one example of doc/advanced/runtime.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

// The section "lazy::copy and lazy::view" lists the three forms, and states that a form does not check that the bytes
// are one complete item.
TEST_CASE("doc advanced runtime: lazy::copy and lazy::view in every form")
{
    std::string bytes = "\x78\x28"s + std::string(40, 'x');
    char const *const data = bytes.data();
    std::string const kept = bytes;
    auto const a = cbor::lazy::copy(std::move(bytes));
    REQUIRE(a.has_value());
    CHECK_EQ(a->top_level->encoded.data(), data);
    auto const b = cbor::lazy::copy(std::string_view{kept});
    auto const held = std::make_shared<std::string const>(preferred(A(1, 2)));
    auto const c = cbor::lazy::view(held);
    REQUIRE(b.has_value());
    CHECK_NE(b->top_level->encoded.data(), kept.data());
    REQUIRE(c.has_value());
    CHECK_EQ(c->at(1)->get<std::int64_t>(), 2);
    CHECK_EQ(c->top_level->encoded.data(), held->data());
    auto const cut = cbor::lazy::copy("\x82\x01"s);
    REQUIRE(cut.has_value());
    CHECK_EQ(cut->at(1).error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::copy("\x01\x02"s)->get<std::int64_t>(), 1);
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&t) { cbor::lazy::view(std::forward<S>(t)); }; }(*held)));
}

// The section "Steps and searches in a map or an array" shows each step and each search.
TEST_CASE("doc advanced runtime: steps and searches")
{
    auto const doc = *cbor::lazy::copy(preferred(M("cars"s, A(M("hp"s, 5), M("hp"s, 7)), 7, "seven"s)));
    auto const seven = doc.at(cbor::key{7})->get<std::string_view>();
    CHECK_EQ(**seven, "seven"sv);
    CHECK_EQ(*doc.size(), 2u);
    CHECK_FALSE(*doc.empty());
    CHECK_EQ(*doc["cars"]->size(), 2u);
    CHECK_EQ(doc["cars"]->at(1)->at("hp")->get<std::int64_t>(), 7);
    CHECK_EQ(doc.at("cars")->at(5).error(), error::index_out_of_bounds);
    CHECK_EQ(doc.at("bus").error(), error::key_not_found);
    CHECK_EQ(doc.at(0).error(), error::not_indexable);
}

// The section "Steps and searches in a map or an array" states that a byte string is not a text key, and that a
// repeated key gives the first pair.
TEST_CASE("doc advanced runtime: keys")
{
    CHECK_EQ(cbor::lazy::copy("\xa1\x41" "a\x01"s)->at("a").error(), error::key_not_found);
    CHECK_EQ(cbor::lazy::copy("\xa1\xd8\x1c\x61" "a\x01"s)->at("a").error(), error::key_not_found);
    auto const twice = *cbor::lazy::copy("\xa2\x61" "a\x01\x61" "a\x02"s);
    CHECK_EQ(twice.at("a")->get<std::int64_t>(), 1);
    CHECK_EQ(twice["a"]->get<std::int64_t>(), 1);
    CHECK_EQ(twice.at_path<"$.a", std::int64_t>(), 1);
}

// The section "elements and entries" shows both views with a loop and with a standard view.
TEST_CASE("doc advanced runtime: elements and entries")
{
    auto const l = *cbor::lazy::copy(preferred(A(1, "a"s, A(2))));
    std::vector<std::expected<std::int64_t, error>> read;
    for (auto const e : *l.elements())
        read.push_back(e->get<std::int64_t>());
    REQUIRE_EQ(read.size(), 3u);
    CHECK_EQ(read[0], 1);
    CHECK_EQ(read[1].error(), error::incorrect_type);
    CHECK_EQ(read[2].error(), error::incorrect_type);
    CHECK_EQ(l.elements()->size(), 3u);
    auto const m = *cbor::lazy::copy(preferred(M("a"s, 1, 2, "b"s)));
    std::vector<value> keys;
    for (auto const p : *m.entries())
        keys.push_back(*test::value_of(p->first));
    CHECK(keys == std::vector<value>{V("a"s), V(2)});
    CHECK_EQ(std::ranges::distance(*m.entries() | std::views::drop(1)), 1);
    CHECK_EQ(m.elements().error(), error::not_indexable);
    CHECK_EQ(l.entries().error(), error::not_indexable);
    CHECK_EQ(cbor::lazy::copy("\x9f\x01\xff"s)->elements().error(), error::indefinite_length);
    auto const it = l.elements()->begin();
    CHECK_EQ((*it)->get<std::int64_t>(), 1);
}

// A user did not know that an algorithm takes the view directly and that -> works on each element.
TEST_CASE("doc advanced runtime: an algorithm over elements")
{
    auto const people = *cbor::lazy::copy(preferred(A(M("age"s, 31), M("age"s, 20), M("age"s, 40))));
    CHECK_EQ(std::ranges::count_if(*people.elements(), [](std::expected<cbor::lazy, cbor::error> const &e) {
                 auto const age = e->at("age");
                 return age && age->get<std::int64_t>().value_or(0) > 30;
             }),
             2);
}

// The section "get" shows a read of each kind.
TEST_CASE("doc advanced runtime: get")
{
    CHECK_EQ(cbor::lazy::copy("\x18\x2a"s)->get<std::int64_t>(), 42);
    CHECK_EQ(cbor::lazy::copy("\x19\x01\x00"s)->get<std::uint8_t>().error(), error::number_out_of_range);
    CHECK_EQ(cbor::lazy::copy("\xf9\x3e\x00"s)->get<double>(), 1.5);
    CHECK_EQ(cbor::lazy::copy("\x18\x2a"s)->get<double>(), 42.0);
    CHECK_EQ(cbor::lazy::copy("\xf5"s)->get<bool>(), true);
    CHECK_EQ(cbor::lazy::copy("\xf6"s)->get<std::nullptr_t>(), nullptr);
    CHECK_EQ(cbor::lazy::copy("\xf7"s)->get<cbor::simple_value>(), cbor::simple_value::undefined);
    auto const text = cbor::lazy::copy("\x63" "abc"s)->get<std::string_view>();
    CHECK_EQ(**text, "abc"sv);
    auto const two = cbor::lazy::copy("\x42\x01\x02"s)->get<std::span<std::byte const>>();
    CHECK_EQ((*two)->size(), 2u);
    auto const typed = cbor::lazy::copy("\xd8\x45\x44\x07\x00\x08\x00"s)->get<cbor::typed_array>();
    REQUIRE(typed.has_value());
    CHECK_EQ((*typed)->tag, 69u);
    CHECK_EQ((*typed)->bytes.size(), 4u);
    CHECK_EQ(cbor::lazy::copy("\x63" "abc"s)->get<bool>().error(), error::incorrect_type);
    CHECK_EQ(cbor::lazy::copy("\xc2\x41\x05"s)->get<std::int64_t>(), 5);
    CHECK_EQ(cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(1)->get<std::int64_t>(), 0);
    CHECK_EQ(cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(0)->get<std::int64_t>(), 1);
}

// The section "decode" shows the item of an array, its children, an error, and type().
TEST_CASE("doc advanced runtime: decode")
{
    auto const i = cbor::lazy::copy(preferred(A(1, M("a"s, 2))))->decode();
    REQUIRE(i.has_value());
    CHECK_EQ((*i)->type, cbor::type::array);
    CHECK_EQ(children_of(*(*i)).size(), 2u);
    CHECK_EQ(cbor::lazy::copy("\x82\x01"s)->decode().error(), error::too_little_data);
    CHECK_EQ(cbor::lazy::copy("\x9b\x80\x00\x00\x00\x00\x00\x00\x00\x01"s)->decode().error(), error::too_little_data);
    CHECK_EQ(*cbor::lazy::copy("\xc2\x41\x05"s)->type(), cbor::type::unsigned_integer);
    CHECK_EQ(*cbor::lazy::copy("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s)->type(), cbor::type::unsigned_bignum);
    CHECK_EQ(*cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(1)->type(), cbor::type::tag);
    auto const tree = cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->decode();
    REQUIRE(tree.has_value());
    CHECK_EQ(children_of(*(*tree))[1]->type, cbor::type::tag);
}

// The section "encode" shows the calls of the encoder, each kind of target, and encode of an item.
TEST_CASE("doc advanced runtime: encode")
{
    std::string out;
    {
        cbor::encoder e(out);
        REQUIRE(e.array(2).has_value());
        REQUIRE(e.unsigned_integer(std::uint8_t{1}).has_value());
        REQUIRE(e.text_string("a").has_value());
        CHECK_EQ(e.finish(), 5u);
    }
    CHECK_EQ(out, "\x82\x18\x01\x61" "a"s);
    value const v = A(1, 2);
    std::string appended = "\x00"s;
    REQUIRE(test::encode(appended, v).has_value());
    CHECK_EQ(appended, "\x00"s + "\x82"s + native_unsigned(1) + native_unsigned(2));
    std::vector<std::byte> bytes;
    REQUIRE(test::encode(bytes, v).has_value());
    CHECK_EQ(bytes.size(), 19u);
    std::array<char, 19> fixed{};
    REQUIRE(test::encode(std::span<char>(fixed), v).has_value());
    CHECK_EQ(std::string(fixed.data(), fixed.size()), "\x82"s + native_unsigned(1) + native_unsigned(2));
    std::array<char, 18> small{};
    CHECK_EQ(test::encode(std::span<char>(small), v).error(), error::no_buffer_space);
    string_writer w;
    REQUIRE(test::encode(w, v).has_value());
    CHECK_EQ(w.encoded, "\x82"s + native_unsigned(1) + native_unsigned(2));
    std::string const cycle = "\xd8\x1c\xc6\xd8\x1d\x00"s;
    auto const tagged = *cbor::lazy::copy(cycle)->decode();
    cbor::item const *const six = tagged->via.tag.content;
    CHECK_EQ(argument_of(*six), 6u);
    cbor::item const *const reference = six->via.tag.content;
    CHECK_EQ((*reference).via.tag.number, 29u);
    std::string const wire = "\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x00"s;
    std::string const own = wire;
    std::string again;
    CHECK_EQ(cbor::encode(**cbor::lazy::copy(wire)->decode(), again), own.size());
    CHECK_EQ(again, own);
    CHECK(same_value(again, wire));
}

// The section "cbor::sequence" shows the items of a sequence, the error at a cut item, and the owner of the
// sequence for each element.
TEST_CASE("doc advanced runtime: cbor::sequence")
{
    std::string const bytes = preferred(V(1)) + preferred(V("a"s)) + preferred(A(1, 2));
    auto const owner = std::make_shared<std::string const>(bytes);
    std::vector<std::string_view> items;
    for (auto const e : cbor::sequence{owner})
        items.push_back(*e);
    CHECK(items == std::vector{"\x01"sv, "\x61" "a"sv, "\x82\x01\x02"sv});
    cbor::sequence const s{std::make_shared<std::string const>("\x01\x19\x01"s)};
    auto it = s.begin();
    ++it;
    CHECK_EQ((*it).error(), error::too_little_data);
    CHECK_EQ(it.encoded, "\x19\x01"sv);
    std::vector<std::int64_t> last;
    for (auto const e : cbor::sequence{owner})
        if (auto const l = cbor::lazy::copy(*e); l->size().has_value())
            last.push_back(*l->at(1)->get<std::int64_t>());
    CHECK(last == std::vector<std::int64_t>{2});
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&t) { cbor::sequence{std::forward<S>(t)}; }; }(std::string())));
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&t) { cbor::sequence{std::forward<S>(t)}; }; }(bytes)));
}

// The section "diagnostic_notation" shows an encoding indicator and an error.
TEST_CASE("doc advanced runtime: diagnostic_notation")
{
    CHECK_EQ(cbor::diagnostic_notation("\x82\x01\x02"sv), "[1, 2]");
    CHECK_EQ(cbor::diagnostic_notation("\x19\x00\x01"sv), "1_1");
    CHECK_EQ(cbor::diagnostic_notation("\x82\x01"sv).error(), error::too_little_data);
}

// The section "item_size" shows the size of the first item and the error of a cut item.
TEST_CASE("doc advanced runtime: item_size")
{
    CHECK_EQ(cbor::item_size("\x01\x02"sv), 1u);
    CHECK_EQ(cbor::item_size("\x82\x01"sv).error(), error::too_little_data);
}

// The section "Tags 28 and 29" shows that a lazy, a tree decode and diagnostic notation read both as plain tags.
TEST_CASE("doc advanced runtime: tags 28 and 29")
{
    auto const l = *cbor::lazy::copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    CHECK_EQ(l.at(1)->type(), cbor::type::tag);
    CHECK_EQ(l.at(1)->get<std::int64_t>(), 0);
    CHECK_EQ(cbor::diagnostic_notation("\x82\xd8\x1c\x01\xd8\x1d\x00"sv), "[28(1), 29(0)]");
    auto const forward = cbor::lazy::copy("\x82\xd8\x1d\x00\xd8\x1c\x05"s)->decode();
    REQUIRE(forward.has_value());
    CHECK_EQ((*forward)->via.array[0].via.tag.number, 29u);
    CHECK(cbor::lazy::copy("\xd8\x1c\x81\xd8\x1d\x00"s)->decode().has_value());
    CHECK_EQ(cbor::lazy::copy("\xd8\x1c\xd8\x1d\x00"s)->at(0).error(), error::not_indexable);
}

// The section "Tag 55799" shows that only a leading tag 55799 is read as its content.
TEST_CASE("doc advanced runtime: tag 55799")
{
    CHECK_EQ(cbor::lazy::copy("\xd9\xd9\xf7\x82\x01\x02"s)->at(1)->get<std::int64_t>(), 2);
    auto const tree = cbor::lazy::copy("\x81\xd9\xd9\xf7\x01"s)->decode();
    REQUIRE(tree.has_value());
    cbor::item const *const inner = children_of(*(*tree))[0];
    CHECK_EQ(inner->type, cbor::type::tag);
    CHECK_EQ(argument_of(*inner), 55799u);
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.tag(55799).has_value());
    REQUIRE(e.finish().has_value());
    CHECK_EQ(out, "\xd9\xd9\xf7"s);
}

// The section "encode_iov" writes an item into pieces and joins them; the joined bytes are those of encode. The
// encoder over an iov_writer keeps a string that the caller moved in as a piece of its own.
TEST_CASE("doc advanced runtime: encode_iov")
{
    std::string const wire = "\x82\x01\x63" "abc"s;
    auto const tree = cbor::lazy::copy(wire)->decode();
    REQUIRE(tree.has_value());
    auto const pieces = cbor::encode_iov(**tree);
    REQUIRE(pieces.has_value());
    std::string joined;
    for (std::string_view const piece : **pieces)
        joined.append(piece);
    CHECK_EQ(joined, wire);

    std::string text(40, 'x');
    cbor::iov_writer w;
    cbor::encoder e(w);
    REQUIRE(e.text_string(std::move(text)).has_value());
    auto const kept = e.finish();
    REQUIRE(kept.has_value());
    CHECK_EQ((**kept).size(), 2u);
}

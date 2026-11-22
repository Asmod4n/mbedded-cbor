#include "pieces.hpp"
#include "reference.hpp"
#include "owned_copy.hpp"
#include "item_head.hpp"
#include <cbor/sequence.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
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

// Each test in this file is one example of the former advanced runtime guide. The tests exist so that every example
// compiles and gives the result that the text states.

// The section "lazy::adopt and lazy::view" lists the three forms, and states that a form does not check that
// the bytes are one complete item.
TEST_CASE("doc advanced runtime: lazy::adopt and lazy::view in every form")
{
    std::string bytes = "\x78\x28"s + std::string(40, 'x');
    char const *const data = bytes.data();
    std::string const kept = bytes;
    auto const a = cbor::lazy::adopt(std::move(bytes));
    REQUIRE(a.has_value());
    CHECK_EQ(a->encoded().data(), data);
    auto const b = test::make_lazy_from_copy(std::string_view{kept});
    auto const held = std::make_shared<std::string const>(encode_in_preferred_form(A(1, 2)));
    auto const c = cbor::lazy::view(held);
    REQUIRE(b.has_value());
    CHECK_NE(b->encoded().data(), kept.data());
    REQUIRE(c.has_value());
    auto const at_1 = c->at(1);
    CHECK_EQ(at_1->get<std::int64_t>(), 2);
    CHECK_EQ(c->encoded().data(), held->data());
    auto const cut = test::make_lazy_from_copy("\x82\x01"s);
    REQUIRE(cut.has_value());
    auto const at_1_2 = cut->at(1);
    CHECK_EQ(at_1_2.error(), error::too_little_data);
    CHECK_EQ(test::make_lazy_from_copy("\x01\x02"s)->get<std::int64_t>(), 1);
    CHECK_FALSE(([]<class S>(S &&) { return requires(S &&t) { cbor::lazy::view(std::forward<S>(t)); }; }(*held)));
}

// The section "Steps and searches in a map or an array" shows each step and each search.
TEST_CASE("doc advanced runtime: steps and searches")
{
    auto const doc = *test::make_lazy_from_copy(encode_in_preferred_form(M("cars"s, A(M("hp"s, 5), M("hp"s, 7)), 7, "seven"s)));
    auto const at_7 = doc.at(7);
    auto const seven = at_7->get<std::string_view>();
    CHECK_EQ(**seven, "seven"sv);
    auto const count = doc.size();
    CHECK_EQ(*count, 2u);
    auto const is_empty = doc.empty();
    CHECK_FALSE(*is_empty);
    auto const cars = doc.at("cars");
    auto const count_2 = cars->size();
    CHECK_EQ(*count_2, 2u);
    auto const cars_2 = doc.at("cars");
    auto const at_1 = cars_2->at(1);
    auto const hp = at_1->at("hp");
    CHECK_EQ(hp->get<std::int64_t>(), 7);
    auto const cars_3 = doc.at("cars");
    auto const at_5 = cars_3->at(5);
    CHECK_EQ(at_5.error(), error::index_out_of_bounds);
    auto const bus = doc.at("bus");
    CHECK_EQ(bus.error(), error::key_not_found);
    auto const at_0 = doc.at(0);
    CHECK_EQ(at_0.error(), error::key_not_found);
}

// The section "Steps and searches in a map or an array" states that a byte string is not a text key. That a repeated key
// gives the first pair.
TEST_CASE("doc advanced runtime: keys")
{
    auto const a = test::make_lazy_from_copy("\xa1\x41" "a\x01"s)->at("a");
    CHECK_EQ(a.error(), error::key_not_found);
    auto const a_2 = test::make_lazy_from_copy("\xa1\xd8\x1c\x61" "a\x01"s)->at("a");
    CHECK_EQ(a_2.error(), error::key_not_found);
    auto const twice = *test::make_lazy_from_copy("\xa2\x61" "a\x01\x61" "a\x02"s);
    auto const a_3 = twice.at("a");
    CHECK_EQ(a_3->get<std::int64_t>(), 1);
    CHECK_EQ(twice.at_path<"$.a", std::int64_t>(), 1);
}

// The section "elements and entries" shows both views with a loop and with a standard view.
TEST_CASE("doc advanced runtime: elements and entries")
{
    auto const l = *test::make_lazy_from_copy(encode_in_preferred_form(A(1, "a"s, A(2))));
    std::vector<cbor::result<std::int64_t>> read;
    auto const elements = *l.elements();
    for (auto const e : elements)
        read.push_back(e->get<std::int64_t>());
    REQUIRE_EQ(read.size(), 3u);
    CHECK_EQ(read[0], 1);
    CHECK_EQ(read[1].error(), error::incorrect_type);
    CHECK_EQ(read[2].error(), error::incorrect_type);
    CHECK_EQ(l.elements()->size(), 3u);
    auto const m = *test::make_lazy_from_copy(encode_in_preferred_form(M("a"s, 1, 2, "b"s)));
    std::vector<value> keys;
    auto const entries = *m.entries();
    for (auto const p : entries)
        keys.push_back(*test::make_value_from_item(p->first));
    CHECK(keys == std::vector<value>{V("a"s), V(2)});
    CHECK_EQ(std::ranges::distance(*m.entries() | std::views::drop(1)), 1);
    CHECK_EQ(m.elements().error(), error::not_indexable);
    CHECK_EQ(l.entries().error(), error::not_indexable);
    CHECK_EQ(test::make_lazy_from_copy("\x9f\x01\xff"s)->elements().error(), error::indefinite_length);
    auto const it = l.elements()->begin();
    CHECK_EQ((*it)->get<std::int64_t>(), 1);
}

// A user read that the iterator stands at the end after an error. It gives the error at each later position instead,
// and the view gives exactly size() values.
TEST_CASE("doc advanced runtime: an error repeats at each later position")
{
    auto const owner = std::make_shared<std::string const>("\x83\x1c\x00\x00"s);
    auto const bad = *cbor::lazy::view(owner);
    std::vector<bool> read;
    auto const elements = *bad.elements();
    for (auto const e : elements)
        read.push_back(e.has_value());
    CHECK(read == std::vector<bool>{true, false, false});
    auto const first = *bad.elements()->begin();
    REQUIRE(first.has_value());
    CHECK_FALSE(first->get<std::int64_t>().has_value());
    auto it = bad.elements()->begin();
    ++it;
    CHECK_EQ((*it).error(), error::syntax_error);
}

// A user did not know that an algorithm takes the view directly and that -> works on each element.
TEST_CASE("doc advanced runtime: an algorithm over elements")
{
    auto const people = *test::make_lazy_from_copy(encode_in_preferred_form(A(M("age"s, 31), M("age"s, 20), M("age"s, 40))));
    CHECK_EQ(std::ranges::count_if(*people.elements(), [](std::expected<cbor::lazy, cbor::error> const &e) {
                 auto const age = e->at("age");
                 return age && age->get<std::int64_t>().value_or(0) > 30;
             }),
             2);
}

// The section "get" shows a read of each kind.
TEST_CASE("doc advanced runtime: get")
{
    CHECK_EQ(test::make_lazy_from_copy("\x18\x2a"s)->get<std::int64_t>(), 42);
    auto const read = test::make_lazy_from_copy("\x19\x01\x00"s)->get<std::uint8_t>();
    CHECK_EQ(read.error(), error::number_out_of_range);
    CHECK_EQ(test::make_lazy_from_copy("\xf9\x3e\x00"s)->get<double>(), 1.5);
    CHECK_EQ(test::make_lazy_from_copy("\x18\x2a"s)->get<double>(), 42.0);
    CHECK_EQ(test::make_lazy_from_copy("\xf5"s)->get<bool>(), true);
    CHECK_EQ(test::make_lazy_from_copy("\xf6"s)->get<std::nullptr_t>(), nullptr);
    CHECK_EQ(test::make_lazy_from_copy("\xf7"s)->get<cbor::simple_value>(), cbor::simple_value::undefined);
    auto const text = test::make_lazy_from_copy("\x63" "abc"s)->get<std::string_view>();
    CHECK_EQ(**text, "abc"sv);
    auto const two = test::make_lazy_from_copy("\x42\x01\x02"s)->get<std::span<std::byte const>>();
    CHECK_EQ((*two)->size(), 2u);
    auto const typed = test::make_lazy_from_copy("\xd8\x45\x44\x07\x00\x08\x00"s)->get<cbor::typed_array>();
    REQUIRE(typed.has_value());
    CHECK_EQ((*typed)->tag, 69u);
    CHECK_EQ((*typed)->bytes.size(), 4u);
    auto const read_2 = test::make_lazy_from_copy("\x63" "abc"s)->get<bool>();
    CHECK_EQ(read_2.error(), error::incorrect_type);
    CHECK_EQ(test::make_lazy_from_copy("\xc2\x41\x05"s)->get<std::int64_t>(), 5);
    auto const at_1 = test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(1);
    CHECK_EQ(at_1->get<std::int64_t>(), 0);
    auto const at_0 = test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(0);
    CHECK_EQ(at_0->get<std::int64_t>(), 1);
}

// The section "clone and decode" shows the item of an array, its children, an error, and type().
TEST_CASE("doc advanced runtime: clone and decode")
{
    auto const owner = std::make_shared<std::string const>(encode_in_preferred_form(A(1, M("a"s, 2))));
    auto const tree = cbor::lazy::view(owner)->to_item();
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->type(), cbor::type::array);
    auto const elements = (*tree)->elements();
    CHECK_EQ(elements->size(), 2u);
    auto const tree_pairs = (*tree)->pairs();
    CHECK_EQ(tree_pairs.error(), error::incorrect_type);
    auto const second = elements->next();
    cbor::item const pairs = second.front();
    auto const entries = pairs.pairs();
    CHECK_EQ(entries->size(), 1u);
    CHECK_EQ(std::get<0>(entries->front()).get<std::string_view>(), "a"sv);
    CHECK_EQ(std::get<1>(entries->front()).get<std::uint64_t>(), 2u);
    auto const cut = std::make_shared<std::string const>("\x82\x01"s);
    CHECK_EQ(cbor::lazy::view(cut)->to_item().error(), error::too_little_data);
    CHECK_EQ(test::make_lazy_from_copy("\x9b\x80\x00\x00\x00\x00\x00\x00\x00\x01"s)->to_item().error(), error::too_little_data);
    auto const kind = test::make_lazy_from_copy("\xc2\x41\x05"s)->type();
    CHECK_EQ(*kind, cbor::type::unsigned_integer);
    auto const kind_2 = test::make_lazy_from_copy("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s)->type();
    CHECK_EQ(*kind_2, cbor::type::unsigned_bignum);
    auto const at_1 = test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->at(1);
    auto const kind_3 = at_1->type();
    CHECK_EQ(*kind_3, cbor::type::tag);
    auto const tags = test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s)->to_item();
    REQUIRE(tags.has_value());
    CHECK_EQ(list_children_of_item(*(*tags))[1].type(), cbor::type::tag);
}

// The section "clone and decode" says that the tree holds a copy of each string. So that it stays valid after the input
// ends, that a larger bignum holds a copy of its magnitude. That only cbor::decode refuses a byte after the item.
TEST_CASE("doc advanced runtime: the tree copies its strings")
{
    std::string input = "\x82\x63" "abc\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s;
    auto const tree = cbor::decode(input);
    REQUIRE(tree.has_value());
    auto const elements = (*tree)->elements();
    auto const text = elements->front().get<std::string_view>();
    REQUIRE(text.has_value());
    CHECK_NE(static_cast<void const *>(text->data()), static_cast<void const *>(std::span(input).subspan(2).data()));
    auto const big = elements->next().front().get<std::pair<std::uint64_t, cbor::item>>();
    REQUIRE(big.has_value());
    auto const magnitude = big->second.get<std::span<std::byte const>>();
    REQUIRE(magnitude.has_value());
    CHECK_EQ(magnitude->size(), 9u);
    std::less<void const *> const before;
    void const *const held = magnitude->data();
    CHECK((before(held, input.data()) || !before(held, std::to_address(input.end()))));
    input.assign(input.size(), '\0');
    CHECK_EQ(*text, "abc"sv);
    CHECK(cbor::decode("\x01\x02"sv).has_value());
    CHECK(test::make_lazy_from_copy("\x01\x02"s)->to_item().has_value());
}

// The section "encode" shows each kind of target and encode of an item.
TEST_CASE("doc advanced runtime: encode")
{
    auto const decoded = cbor::decode("\x82\x01\x02"sv);
    REQUIRE(decoded.has_value());
    std::string appended = "\x00"s;
    REQUIRE(cbor::encode(appended, **decoded).has_value());
    CHECK_EQ(appended, "\x00\x82\x01\x02"s);
    std::vector<std::byte> bytes;
    REQUIRE(cbor::encode(bytes, **decoded).has_value());
    CHECK_EQ(bytes.size(), 3u);
    std::array<char, 3> fixed{};
    REQUIRE(cbor::encode(std::span<char>(fixed), **decoded).has_value());
    CHECK_EQ(std::string(fixed.data(), fixed.size()), "\x82\x01\x02"s);
    std::array<char, 2> small{};
    CHECK_EQ(cbor::encode(std::span<char>(small), **decoded), std::unexpected(error::no_buffer_space));
    string_writer w;
    REQUIRE(cbor::encode(w, **decoded).has_value());
    CHECK_EQ(w.encoded, "\x82\x01\x02"s);
    std::string const cycle = "\xd8\x1c\xc6\xd8\x1d\x00"s;
    auto const tagged = *test::make_lazy_from_copy(cycle)->to_item();
    auto const tagged_tagged = tagged->get<std::pair<std::uint64_t, cbor::item>>();
    cbor::item const six = tagged_tagged.value().second;
    CHECK_EQ(read_argument_of_item(six), 6u);
    auto const six_tagged = six.get<std::pair<std::uint64_t, cbor::item>>();
    cbor::item const reference = six_tagged.value().second;
    auto const reference_tagged = reference.get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(reference_tagged.value().first, 29u);
    std::string const wire = "\x82\xd8\x1c\x19\x00\x01\xd8\x1d\x00"s;
    std::string const own = wire;
    std::string again;
    auto const tree = test::make_lazy_from_copy(wire)->to_item();
    CHECK_EQ(cbor::encode(again, **tree), own.size());
    CHECK_EQ(again, own);
    CHECK(same_value(again, wire));
}

// The section "encode" states that a NaN keeps its bits in encode of an item. A NaN with a payload shows it, because a
// writer that makes every NaN the quiet NaN loses the payload.
TEST_CASE("doc advanced runtime: a NaN keeps its bits")
{
    for (std::string_view const wire :
         {"\xf9\x7e\x01"sv, "\xfa\x7f\xc0\x00\x01"sv, "\xfb\x7f\xf8\x00\x00\x00\x00\x00\x01"sv}) {
        auto const tree = cbor::decode(wire);
        REQUIRE(tree.has_value());
        std::string again;
        REQUIRE(cbor::encode(again, **tree).has_value());
        CHECK_EQ(again, wire);
    }
}

// The section "cbor::sequence" shows the items of a sequence, the error at a cut item, and the owner of the
// sequence for each element.
TEST_CASE("doc advanced runtime: cbor::sequence")
{
    std::string const bytes = encode_in_preferred_form(V(1)) + encode_in_preferred_form(V("a"s)) + encode_in_preferred_form(A(1, 2));
    auto const owner = std::make_shared<std::string const>(bytes);
    std::vector<std::string_view> items;
    for (auto const e : cbor::sequence{owner})
        items.push_back(*e);
    CHECK(items == std::vector{"\x01"sv, "\x61" "a"sv, "\x82\x01\x02"sv});
    std::vector<std::string_view> beside;
    for (auto const e : cbor::sequence{owner, std::string_view(*owner).substr(1)})
        beside.push_back(*e);
    CHECK(beside == std::vector{"\x61" "a"sv, "\x82\x01\x02"sv});
    cbor::sequence const s{std::make_shared<std::string const>("\x01\x19\x01"s)};
    auto it = s.begin();
    ++it;
    CHECK_EQ((*it).error(), error::too_little_data);
    CHECK_EQ(it.encoded(), "\x19\x01"sv);
    std::vector<std::int64_t> last;
    for (auto const e : cbor::sequence{owner})
        if (auto const l = test::make_lazy_from_copy(*e); l->size().has_value()) {
            auto const at_1 = l->at(1);
            auto const read = at_1->get<std::int64_t>();
            last.push_back(*read);
        }
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
    auto const size = cbor::item_size("\x82\x01"sv);
    CHECK_EQ(size.error(), error::too_little_data);
}

// The section "Tags 28 and 29" shows that a lazy, clone and diagnostic notation read both as plain tags. That
// cbor::decode_cycles resolves them. The second element is a shared_reference to the array that holds it. A
// reference to a value whose decode has ended, an index with no mark and 28(29(0)) are errors. encode writes the cycle.
TEST_CASE("doc advanced runtime: tags 28 and 29")
{
    auto const tree = cbor::decode_cycles("\xd8\x1c\x82\x01\xd8\x1d\x00"sv);
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const elements = tree_elements.value();
    CHECK_EQ(elements.next().front().type(), cbor::type::shared_reference);
    auto const elements_tagged = elements.next().front().get<std::pair<std::uint64_t, cbor::item>>();
    CHECK(elements_tagged.value().second == **tree);
    CHECK_EQ(cbor::decode_cycles("\x82\xd8\x1c\x01\xd8\x1d\x00"sv), std::unexpected(error::sharedref_not_cyclic));
    CHECK_EQ(cbor::decode_cycles("\x81\xd8\x1d\x00"sv), std::unexpected(error::sharedref_index_not_marked));
    auto const decoded = cbor::decode_cycles("\xd8\x1c\xd8\x1d\x00"sv);
    CHECK_EQ(decoded.error(), error::sharedref_not_defined);
    std::string out;
    CHECK_EQ(cbor::encode(out, **tree), 7u);
    CHECK_EQ(out, cbor::diag2cbor("28([1, 29(0)])").value());
    std::string const cycle_written = cbor::diag2cbor("28([29(0)])").value();
    auto const cyclic = cbor::decode_cycles(cycle_written);
    REQUIRE(cyclic.has_value());
    std::string cycle;
    CHECK_EQ(cbor::encode(cycle, **cyclic), cycle_written.size());
    CHECK_EQ(cycle, cycle_written);
    auto const l = *test::make_lazy_from_copy("\x82\xd8\x1c\x01\xd8\x1d\x00"s);
    auto const at_1 = l.at(1);
    CHECK_EQ(at_1->type(), cbor::type::tag);
    auto const at_1_2 = l.at(1);
    CHECK_EQ(at_1_2->get<std::int64_t>(), 0);
    CHECK_EQ(cbor::diagnostic_notation("\x82\xd8\x1c\x01\xd8\x1d\x00"sv), "[28(1), 29(0)]");
    auto const forward = test::make_lazy_from_copy("\x82\xd8\x1d\x00\xd8\x1c\x05"s)->to_item();
    REQUIRE(forward.has_value());
    auto const forward_elements = (*forward)->elements();
    auto const tagged = forward_elements.value().front().get<std::pair<std::uint64_t, cbor::item>>();
    CHECK_EQ(tagged.value().first, 29u);
    CHECK(test::make_lazy_from_copy("\xd8\x1c\x81\xd8\x1d\x00"s)->to_item().has_value());
    auto const at_0 = test::make_lazy_from_copy("\xd8\x1c\xd8\x1d\x00"s)->at(0);
    CHECK_EQ(at_0.error(), error::not_indexable);
}

// The section "Tag 55799" shows that one leading tag 55799 is read as its content, by a lazy and by decode.
TEST_CASE("doc advanced runtime: tag 55799")
{
    auto const owner = std::make_shared<std::string const>("\xd9\xd9\xf7\x82\x01\x02"s);
    auto const at_1 = cbor::lazy::view(owner)->at(1);
    CHECK_EQ(at_1->get<std::int64_t>(), 2);
    auto const content = cbor::lazy::view(owner)->to_item();
    REQUIRE(content.has_value());
    CHECK_EQ((*content)->type(), cbor::type::array);
    auto const decoded = cbor::decode(*owner);
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->type(), cbor::type::array);
    auto const tree = test::make_lazy_from_copy("\x81\xd9\xd9\xf7\x01"s)->to_item();
    REQUIRE(tree.has_value());
    cbor::item const inner = list_children_of_item(*(*tree))[0];
    CHECK_EQ(inner.type(), cbor::type::tag);
    CHECK_EQ(read_argument_of_item(inner), 55799u);
}

// The iov forms exist where the platform has <sys/uio.h>, as in the library (encode.hpp); Windows has none.
#if __has_include(<sys/uio.h>)
// The section "encode_iov" writes an item into pieces and joins them; the joined bytes are those of encode. The
// overload for a decoded tree gives a text longer than the copy limit as a piece of its own, after the piece of its
// head.
TEST_CASE("doc advanced runtime: encode_iov")
{
    std::string const bytes = "\x82\x01\x63" "abc"s;
    std::string const wire = bytes;
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree.has_value());
    auto const pieces = cbor::encode_iov(*tree);
    REQUIRE(pieces.has_value());
    auto const read = test::join_pieces_into_bytes(**pieces);
    REQUIRE(read.has_value());
    std::string const joined = *read;
    CHECK_EQ(joined, wire);

    std::string const long_text =
        test::write_integer_at_native_width(cbor::major_type::text_string, CBOR_ENCODER_COPYBREAK + 1) +
        std::string(CBOR_ENCODER_COPYBREAK + 1, 'x');
    auto const long_tree = cbor::decode(long_text);
    REQUIRE(long_tree.has_value());
    auto const kept = cbor::encode_iov(*long_tree);
    REQUIRE(kept.has_value());
    CHECK_EQ((**kept).size(), 2u);
}
#endif

#include "reference.hpp"
#include "owned_copy.hpp"
#include <cbor/diagnostic_notation.hpp>
#include <cbor/sequence.hpp>
#include <cbor/structs.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <version>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

value tag(std::uint64_t const number, value content)
{
    return {tagged{number, std::move(content)}};
}

struct reading {
    std::uint64_t a;
};

struct tagged_reading {
    std::uint64_t a;
};

class built_reading
{
public:
    explicit built_reading(std::uint64_t const value) : a(value)
    {
    }

    std::uint64_t a;
};

std::string put_under_self_described_cbor_tag(std::string const &message)
{
    auto const edn = cbor::diagnostic_notation(message);
    REQUIRE(edn.has_value());
    auto tagged = cbor::diag2cbor("55799(" + *edn + ")");
    REQUIRE(tagged.has_value());
    return std::move(*tagged);
}

template <class T>
std::expected<cbor::ref<T>, error> decode_edn_with_structs(std::string_view const edn)
{
    auto bytes = cbor::diag2cbor(edn);
    REQUIRE(bytes.has_value());
    return cbor::structs<T>::decode(std::move(*bytes));
}

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(tagged_reading, cbor::tag{1900}, a);
CBOR_DEFINE_TYPE_MEMBERS(built_reading, cbor::tag{1901}, a);

// RFC 8949 3.4.6: the semantics of the tag content enclosed in tag number 55799 is exactly identical to the
// semantics of the tag content itself. So decode gives the tag content and no tag.
TEST_CASE("self-described CBOR: decode gives the tag content of a leading tag 55799")
{
    auto const v = decode_wire_into_value("\xd9\xd9\xf7\x83\x01\x02\x03"sv);
    REQUIRE(v.has_value());
    CHECK(*v == A(1, 2, 3));
}

// RFC 8949 3.4.6 gives the head 0xd9d9f7. The tag number is 55799 in every width of the argument, and this
// library reads every width of an argument.
TEST_CASE("self-described CBOR: a tag 55799 with a four-byte argument is also read as its tag content")
{
    auto const v = decode_wire_into_value("\xda\x00\x00\xd9\xf7\x01"sv);
    REQUIRE(v.has_value());
    CHECK(*v == V(1));
}

// RFC 8949 3.4.6 puts the tag at the start of a stored encoded data item. A second tag 55799 is the tag
// content of the first one, and it stays a tag.
TEST_CASE("self-described CBOR: only the leading tag 55799 is read as its tag content")
{
    auto const twice = decode_wire_into_value("\xd9\xd9\xf7\xd9\xd9\xf7\x01"sv);
    REQUIRE(twice.has_value());
    CHECK(*twice == tag(55799, V(1)));
    auto const inside = decode_wire_into_value("\x81\xd9\xd9\xf7\x01"sv);
    REQUIRE(inside.has_value());
    CHECK(*inside == A(tag(55799, V(1))));
}

// A tag head with no tag content is a truncated item.
TEST_CASE("self-described CBOR: a tag 55799 with no tag content gives too_little_data")
{
    CHECK_EQ(read_decode_error_of_wire("\xd9\xd9\xf7"sv), error::too_little_data);
    CHECK_EQ(read_decode_error_of_wire("\xd9\xd9"sv), error::too_little_data);
}

// lazy reads into the tag content of a leading tag 55799, so a key and an index reach the item behind the
// tag.
TEST_CASE("self-described CBOR: lazy reads through a leading tag 55799")
{
    std::string const document = "\xd9\xd9\xf7\xa1\x61\x61\x82\x05\x06"s;
    auto const l = test::make_lazy_from_copy(document);
    REQUIRE(l.has_value());
    auto const a = l->at("a"sv);
    REQUIRE(a.has_value());
    auto const second = a->at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(second->get<std::uint64_t>().value(), 6u);
    auto const owned = cbor::lazy::adopt(std::string(document));
    REQUIRE(owned.has_value());
    CHECK_EQ(owned->at("a"sv)->at(0)->get<std::uint64_t>().value(), 5u);
}

// From the former runtime guide: lazy::view and lazy::adopt read one leading tag 55799 as its content. A second tag
// 55799 or a tag 55799 inside an item stays a tag. type() gives the kind of the item there. So it gives tag, as decode
// and clone do, in each width of the argument. Then get, a key and a path pass the tag. So they still give the content.
// A tag 55799 inside the item of a tag 24 is passed by a step and a path as well. The content of a tag 2, tag 3 or
// typed array is the exception; the next test covers it.
TEST_CASE("self-described CBOR: type() of a second tag 55799 and of a tag 55799 inside an item is tag")
{
    for (std::string const &head : {"\xd9\xd9\xf7"s, "\xda\x00\x00\xd9\xf7"s, "\xdb\x00\x00\x00\x00\x00\x00\xd9\xf7"s}) {
        CAPTURE(head.size());
        auto const one = cbor::lazy::adopt(head + "\x01");
        REQUIRE(one.has_value());
        CHECK_EQ(one->type().value(), cbor::type::unsigned_integer);
        auto const two = cbor::lazy::adopt(head + head + "\x01");
        REQUIRE(two.has_value());
        CHECK_EQ(two->type().value(), cbor::type::tag);
        CHECK_EQ(two->get<std::int64_t>().value(), 1);
        auto const cloned = two->to_item();
        REQUIRE(cloned.has_value());
        CHECK_EQ((*cloned)->type(), cbor::type::tag);
        auto const tree = cbor::decode(head + head + "\x01");
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type(), cbor::type::tag);
        auto const array = cbor::lazy::adopt("\x81" + head + "\x01");
        REQUIRE(array.has_value());
        auto const element = array->at(0);
        REQUIRE(element.has_value());
        CHECK_EQ(element->type().value(), cbor::type::tag);
        CHECK_EQ(element->get<std::int64_t>().value(), 1);
        CHECK_EQ((cbor::at_path<"$[0]", std::int64_t>("\x81" + head + "\x01")).value(), 1);
        std::string const map = "\xa1" + head + "\x61k\x01";
        CHECK_EQ(cbor::lazy::adopt(std::string(map))->at("k"sv)->get<std::int64_t>().value(), 1);
        CHECK_EQ((cbor::at_path<"$.k", std::int64_t>(map)).value(), 1);
        std::string const embedded = "\xa1\x61\x61\xd8\x18" + std::string(1, static_cast<char>(0x40 + head.size() + 3)) +
                                     head + "\x82\x01\x02";
        CHECK_EQ(cbor::lazy::adopt(std::string(embedded))->at("a"sv)->at(1)->get<std::int64_t>().value(), 2);
        CHECK_EQ((cbor::at_path<"$.a[1]", std::int64_t>(embedded)).value(), 2);
    }
}

// The owner decided that a tag 55799 inside an item stays a tag. The content of a tag 2, tag 3 or typed array is then a
// tag and not a byte string. So every reader gives for 2(55799(h'01')) what it gives for 2("a"), and for
// 64(55799(h'0102')) what it gives for 64("a"): the same type, value or error. Each width of the 55799 head is read.
TEST_CASE("self-described CBOR: a tag 55799 in the content of a bignum or a typed array stays a tag")
{
    auto const check_both_give_the_same = [](auto const &a, auto const &b) {
        REQUIRE_EQ(a.has_value(), b.has_value());
        if (a.has_value())
            CHECK_EQ(*a, *b);
        else
            CHECK_EQ(a.error(), b.error());
    };
    for (std::string const &head : {"\xd9\xd9\xf7"s, "\xda\x00\x00\xd9\xf7"s, "\xdb\x00\x00\x00\x00\x00\x00\xd9\xf7"s}) {
        CAPTURE(head.size());
        for (char const bignum : {'\xc2', '\xc3'}) {
            std::string const tagged = std::string(1, bignum) + head + "\x41\x01";
            std::string const text = std::string(1, bignum) + "\x61\x61";
            auto const l = cbor::lazy::adopt(std::string(tagged));
            auto const t = cbor::lazy::adopt(std::string(text));
            REQUIRE(l.has_value());
            REQUIRE(t.has_value());
            check_both_give_the_same(l->type(), t->type());
            check_both_give_the_same(l->get<std::int64_t>(), t->get<std::int64_t>());
            check_both_give_the_same(l->get<double>(), t->get<double>());
            check_both_give_the_same((cbor::at_path<"$", std::int64_t>(tagged)), (cbor::at_path<"$", std::int64_t>(text)));
            check_both_give_the_same((cbor::at_path<"$", double>(tagged)), (cbor::at_path<"$", double>(text)));
            auto const tree = cbor::decode(tagged);
            REQUIRE(tree.has_value());
            CHECK_EQ((*tree)->type(), l->type().value());
            auto const cloned = l->to_item();
            REQUIRE(cloned.has_value());
            CHECK_EQ((*cloned)->type(), (*tree)->type());
            std::string const map_tagged = "\xa1" + tagged + "\x05";
            std::string const map_text = "\xa1" + text + "\x05";
            if (bignum == '\xc2')
                check_both_give_the_same((cbor::at_path<"$[1]", std::int64_t>(map_tagged)), (cbor::at_path<"$[1]", std::int64_t>(map_text)));
            else
                check_both_give_the_same((cbor::at_path<"$[-2]", std::int64_t>(map_tagged)), (cbor::at_path<"$[-2]", std::int64_t>(map_text)));
            std::string const array_tagged = "\x81" + tagged;
            std::string const array_text = "\x81" + text;
            auto const fa = cbor::lazy::adopt(std::string(array_tagged))->at_path<"$[?@ == 1]", cbor::nodelist>();
            auto const fb = cbor::lazy::adopt(std::string(array_text))->at_path<"$[?@ == 1]", cbor::nodelist>();
            REQUIRE_EQ(fa.has_value(), fb.has_value());
            if (fa.has_value())
                CHECK_EQ(fa->size(), fb->size());
            else
                CHECK_EQ(fa.error(), fb.error());
        }
        std::string const typed = "\xd8\x40" + head + "\x42\x01\x02";
        std::string const typed_text = "\xd8\x40\x61\x61"s;
        auto const l = cbor::lazy::adopt(std::string(typed));
        auto const t = cbor::lazy::adopt(std::string(typed_text));
        REQUIRE(l.has_value());
        REQUIRE(t.has_value());
        CHECK_EQ(l->get<cbor::typed_array>().error(), t->get<cbor::typed_array>().error());
        CHECK_EQ(l->at(0).error(), t->at(0).error());
        CHECK_EQ((cbor::at_path<"$[0]", std::int64_t>(typed)).error(), (cbor::at_path<"$[0]", std::int64_t>(typed_text)).error());
        CHECK_EQ(cbor::decode(typed).error(), cbor::decode(typed_text).error());
        CHECK_EQ(l->to_item().error(), t->to_item().error());
    }
}

// Both forms of at_path read through a leading tag 55799: the typed read on the bytes and the form that gives a
// lazy node.
TEST_CASE("self-described CBOR: at_path reads through a leading tag 55799")
{
    std::string const document = "\xd9\xd9\xf7\xa1\x61\x61\x82\x05\x06"s;
    CHECK_EQ(cbor::at_path<"$.a[1]", std::uint64_t>(document).value(), 6u);
    auto const owner = std::make_shared<std::string const>("\xd9\xd9\xf7\xa1\x61\x61\x62\x68\x69"s);
    auto const text = cbor::at_path<"$.a", std::string_view>(owner);
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "hi"sv);
    auto const l = test::make_lazy_from_copy(document);
    REQUIRE(l.has_value());
    auto const v = test::read_value_at_path("$.a"sv, *l);
    REQUIRE(v.has_value());
    CHECK(*v == A(5, 6));
}

// RFC 9277 2.2.1: the CBOR Tag Wrapped method. The outer tag 55799 is read as its tag content, and the inner
// protocol tag 1668546929 stays.
TEST_CASE("RFC 9277: a CBOR Tag Wrapped item gives the protocol tag")
{
    auto const v = decode_wire_into_value("\xd9\xd9\xf7\xda\x63\x74\x01\x71\x81\xa1\x00\x67\x63\x75\x72\x72\x65\x6e\x74"sv);
    REQUIRE(v.has_value());
    CHECK(*v == tag(1668546929, A(M(0, "current"s))));
}

// RFC 9277 2.3: the label of a Labeled CBOR Sequence is a data item of its own, with tag 55800 outside. The
// tag 55800 has semantics, so decode keeps it, and the label is the first element of the sequence.
TEST_CASE("RFC 9277: the label of a Labeled CBOR Sequence is the first element and keeps tag 55800")
{
    std::string const label = "\xd9\xd9\xf8\xda\x63\x74\x02\x12\x43\x42\x4f\x52"s;
    value const expected = tag(55800, tag(1668547090, {bytes{"BOR"}}));
    auto const v = decode_wire_into_value(label);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    auto const tree = cbor::decode(label);
    REQUIRE(tree.has_value());
    std::string again;
    REQUIRE(cbor::encode(again, **tree).has_value());
    CHECK_EQ(again, label);
    auto const file = std::make_shared<std::string const>(label + "\x00\x08\x0f"s);
    std::vector<std::string_view> elements;
    for (auto const e : cbor::sequence{file})
        elements.push_back(e.value());
    REQUIRE_EQ(elements.size(), 4u);
    CHECK_EQ(elements[0], std::string_view(label));
    CHECK_EQ(elements[3], "\x0f"sv);
}

// RFC 9277 Appendix D: the header of CBOR-Labeled Non-CBOR Data is 12 bytes. The Non-CBOR data follows it directly.
// item_size gives the end of the header, and decode keeps tag 55801.
TEST_CASE("RFC 9277: the header of CBOR-Labeled Non-CBOR Data ends after 12 bytes")
{
    std::string const header = "\xd9\xd9\xf9\xda\x63\x74\x01\x01\x43\x42\x4f\x52"s;
    std::string const file = header + "text/plain"s;
    CHECK_EQ(cbor::item_size(file).value(), 12u);
    auto const v = decode_wire_into_value(file);
    REQUIRE(v.has_value());
    CHECK(*v == tag(55801, tag(1668546817, {bytes{"BOR"}})));
}

// RFC 8949 3.4.6: tag 55799 "does not impart any special semantics on the data item that it encloses". get reads
// through any number of them (lazy.dox), so structs reads through them as well, in every width of the argument.
TEST_CASE("self-described CBOR: structs reads a number through tags 55799")
{
    for (std::string_view const edn : {"55799(1)"sv, "55799_2(1)"sv, "55799_3(1)"sv, "55799(55799(1))"sv}) {
        CAPTURE(edn);
        auto const r = decode_edn_with_structs<std::uint64_t>(edn);
        REQUIRE(r.has_value());
        CHECK_EQ(**r, 1u);
    }
}

// A key under tag 55799 matches as the key without it (lazy.dox), and a value under it is the value. So both maps
// decode as {"a": 1}.
TEST_CASE("self-described CBOR: structs reads a key and a value of a struct through tag 55799")
{
    for (std::string_view const edn : {R"({"a": 55799(1)})"sv, R"({55799("a"): 1})"sv}) {
        CAPTURE(edn);
        auto const r = decode_edn_with_structs<reading>(edn);
        REQUIRE(r.has_value());
        CHECK_EQ((*r)->a, 1u);
    }
}

// Each place in structs that decodes a head by itself reads through tag 55799: the content of a standard tag, the
// array of a set, a key of a map, the values array of a multimap and the map of a tagged struct.
TEST_CASE("self-described CBOR: structs reads through tag 55799 in every place that takes an item")
{
    SUBCASE("the content of tag 1")
    {
        auto const time = decode_edn_with_structs<std::chrono::sys_seconds>("1(55799(0))");
        REQUIRE(time.has_value());
        CHECK_EQ(**time, std::chrono::sys_seconds{});
    }
    SUBCASE("the content of tag 100")
    {
        auto const days = decode_edn_with_structs<std::chrono::year_month_day>("100(55799(0))");
        REQUIRE(days.has_value());
        CHECK_EQ(**days, std::chrono::year_month_day{std::chrono::sys_days{}});
    }
    SUBCASE("the array of a set")
    {
        auto const set = decode_edn_with_structs<std::set<int>>("258(55799([1]))");
        REQUIRE(set.has_value());
        CHECK_EQ(**set, std::set<int>{1});
    }
    SUBCASE("a key of a map")
    {
        auto const map = decode_edn_with_structs<std::map<std::string, int>>(R"({55799("a"): 1})");
        REQUIRE(map.has_value());
        CHECK_EQ(**map, std::map<std::string, int>{{"a", 1}});
    }
    SUBCASE("the values array of a multimap")
    {
        auto const multimap = decode_edn_with_structs<std::multimap<std::string, int>>(R"({"a": 55799([1, 2])})");
        REQUIRE(multimap.has_value());
        CHECK_EQ(**multimap, std::multimap<std::string, int>{{"a", 1}, {"a", 2}});
    }
    SUBCASE("the map of a tagged struct")
    {
        auto const tagged = decode_edn_with_structs<tagged_reading>(R"(1900(55799({"a": 1})))");
        REQUIRE(tagged.has_value());
        CHECK_EQ((*tagged)->a, 1u);
    }
}

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
// The text of tag 0 and of tag 1004 is an item as well, so structs reads it through tag 55799.
TEST_CASE("self-described CBOR: structs reads the text of a date through tag 55799")
{
    SUBCASE("tag 0")
    {
        auto const time = decode_edn_with_structs<std::chrono::sys_seconds>(R"(0(55799("1970-01-01T00:00:00Z")))");
        REQUIRE(time.has_value());
        CHECK_EQ(**time, std::chrono::sys_seconds{});
    }
    SUBCASE("tag 1004")
    {
        auto const date = decode_edn_with_structs<std::chrono::year_month_day>(R"(1004(55799("1970-01-01")))");
        REQUIRE(date.has_value());
        CHECK_EQ(**date, std::chrono::year_month_day{std::chrono::sys_days{}});
    }
}
#endif

// The content of a tag 2, a tag 3 or a typed array is the exception of lazy.dox: a tag 55799 there is not a byte
// string. So structs gives for 2(55799(h'01')) what it gives for 2("a"), and for 64(55799(h'01')) what it gives for
// 64("a").
TEST_CASE("self-described CBOR: structs does not read through tag 55799 in a bignum or a typed array")
{
#ifdef CBOR_HAS_INT128
    for (std::string_view const tag : {"2"sv, "3"sv}) {
        CAPTURE(tag);
        auto const through = decode_edn_with_structs<cbor::int128>(std::string(tag) + "(55799(h'01'))");
        auto const text = decode_edn_with_structs<cbor::int128>(std::string(tag) + R"(("a"))");
        REQUIRE_FALSE(through.has_value());
        REQUIRE_FALSE(text.has_value());
        CHECK_EQ(through.error(), text.error());
    }
#endif
    auto const through = decode_edn_with_structs<std::vector<std::uint8_t>>("64(55799(h'01'))");
    auto const text = decode_edn_with_structs<std::vector<std::uint8_t>>(R"(64("a"))");
    REQUIRE_FALSE(through.has_value());
    REQUIRE_FALSE(text.has_value());
    CHECK_EQ(through.error(), text.error());
}

// A schema message is a stored item, and RFC 8949 3.4.6 puts tag 55799 at the start of a stored item. So every read
// form of schema skips one leading tag 55799, as cbor::decode does, and gives what it gives for the message alone.
TEST_CASE("self-described CBOR: schema reads a message under a leading tag 55799 as the message")
{
    std::string const message = cbor::schema<tagged_reading>::encode(tagged_reading{7}).value();
    std::string const tagged = put_under_self_described_cbor_tag(message);
    REQUIRE_NE(tagged, message);
    SUBCASE("decode")
    {
        auto const plain = cbor::schema<tagged_reading>::decode(std::string(message));
        auto const through = cbor::schema<tagged_reading>::decode(std::string(tagged));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ((*through)->a, (*plain)->a);
    }
    SUBCASE("decode of a class with a constructor")
    {
        std::string const built = cbor::schema<built_reading>::encode(built_reading{7}).value();
        auto const plain = cbor::schema<built_reading>::decode(std::string(built));
        auto const through =
            cbor::schema<built_reading>::decode(std::string(put_under_self_described_cbor_tag(built)));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ((*through)->a, (*plain)->a);
    }
    SUBCASE("unverified_decode")
    {
        auto const plain = cbor::schema<tagged_reading>::unverified_decode(std::string(message));
        auto const through = cbor::schema<tagged_reading>::unverified_decode(std::string(tagged));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ((*through)->a, (*plain)->a);
    }
    SUBCASE("adopt")
    {
        auto const plain = cbor::schema<tagged_reading>::adopt(std::string(message));
        auto const through = cbor::schema<tagged_reading>::adopt(std::string(tagged));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ(through->at_path<"$.a">().value(), plain->at_path<"$.a">().value());
    }
    SUBCASE("view")
    {
        auto const plain = cbor::schema<tagged_reading>::view(std::make_shared<std::string const>(message));
        auto const through = cbor::schema<tagged_reading>::view(std::make_shared<std::string const>(tagged));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ(through->at_path<"$.a">().value(), plain->at_path<"$.a">().value());
    }
    SUBCASE("at_path")
    {
        auto const plain = cbor::schema<tagged_reading>::at_path<"$.a">(message);
        auto const through = cbor::schema<tagged_reading>::at_path<"$.a">(tagged);
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ(*through, *plain);
    }
    SUBCASE("static_dictionary::decode")
    {
        std::string const data = cbor::static_dictionary<tagged_reading>::encode(tagged_reading{7}).value();
        auto const plain = cbor::static_dictionary<tagged_reading>::decode(std::string(data));
        auto const through =
            cbor::static_dictionary<tagged_reading>::decode(std::string(put_under_self_described_cbor_tag(data)));
        REQUIRE(plain.has_value());
        REQUIRE(through.has_value());
        CHECK_EQ((*through)->a, (*plain)->a);
    }
}

#include "reference.hpp"
#include "owned_copy.hpp"
#include <cbor/sequence.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

value tag(std::uint64_t const number, value content)
{
    return {tagged{number, std::move(content)}};
}

} // namespace

// RFC 8949 3.4.6: the semantics of the tag content enclosed in tag number 55799 is exactly identical to the
// semantics of the tag content itself. So decode gives the tag content and no tag.
TEST_CASE("self-described CBOR: decode gives the tag content of a leading tag 55799")
{
    auto const v = decoded("\xd9\xd9\xf7\x83\x01\x02\x03"sv);
    REQUIRE(v.has_value());
    CHECK(*v == A(1, 2, 3));
}

// RFC 8949 3.4.6 gives the head 0xd9d9f7. The tag number is 55799 in every width of the argument, and this
// library reads every width of an argument.
TEST_CASE("self-described CBOR: a tag 55799 with a four-byte argument is also read as its tag content")
{
    auto const v = decoded("\xda\x00\x00\xd9\xf7\x01"sv);
    REQUIRE(v.has_value());
    CHECK(*v == V(1));
}

// RFC 8949 3.4.6 puts the tag at the start of a stored encoded data item. A second tag 55799 is the tag
// content of the first one, and it stays a tag.
TEST_CASE("self-described CBOR: only the leading tag 55799 is read as its tag content")
{
    auto const twice = decoded("\xd9\xd9\xf7\xd9\xd9\xf7\x01"sv);
    REQUIRE(twice.has_value());
    CHECK(*twice == tag(55799, V(1)));
    auto const inside = decoded("\x81\xd9\xd9\xf7\x01"sv);
    REQUIRE(inside.has_value());
    CHECK(*inside == A(tag(55799, V(1))));
}

// A tag head with no tag content is a truncated item.
TEST_CASE("self-described CBOR: a tag 55799 with no tag content gives too_little_data")
{
    CHECK_EQ(decode_error("\xd9\xd9\xf7"sv), error::too_little_data);
    CHECK_EQ(decode_error("\xd9\xd9"sv), error::too_little_data);
}

// lazy reads into the tag content of a leading tag 55799, so a key and an index reach the item behind the
// tag.
TEST_CASE("self-described CBOR: lazy reads through a leading tag 55799")
{
    std::string const document = "\xd9\xd9\xf7\xa1\x61\x61\x82\x05\x06"s;
    auto const l = test::lazy_of_copy(document);
    REQUIRE(l.has_value());
    auto const a = l->at("a"sv);
    REQUIRE(a.has_value());
    auto const second = a->at(1);
    REQUIRE(second.has_value());
    CHECK_EQ(second->get<std::uint64_t>().value(), 6u);
    auto const owned = cbor::lazy::take_ownership(std::string(document));
    REQUIRE(owned.has_value());
    CHECK_EQ(owned->at("a"sv)->at(0)->get<std::uint64_t>().value(), 5u);
}

// runtime.md section 11: lazy::view and lazy::take_ownership read one leading tag 55799 as its content, and a
// second tag 55799 or a tag 55799 inside an item stays a tag. type() gives the kind of the item there, so it gives tag,
// as decode and clone do, in each width of the argument. get, a key and a path pass the tag (runtime.md, path.md), so
// they still give the content. A tag 55799 inside the item of a tag 24 is passed by a step and a path as well. The
// content of a tag 2, tag 3 or typed array is the exception; the next test covers it.
TEST_CASE("self-described CBOR: type() of a second tag 55799 and of a tag 55799 inside an item is tag")
{
    for (std::string const &head : {"\xd9\xd9\xf7"s, "\xda\x00\x00\xd9\xf7"s, "\xdb\x00\x00\x00\x00\x00\x00\xd9\xf7"s}) {
        CAPTURE(head.size());
        auto const one = cbor::lazy::take_ownership(head + "\x01");
        REQUIRE(one.has_value());
        CHECK_EQ(one->type().value(), cbor::type::unsigned_integer);
        auto const two = cbor::lazy::take_ownership(head + head + "\x01");
        REQUIRE(two.has_value());
        CHECK_EQ(two->type().value(), cbor::type::tag);
        CHECK_EQ(two->get<std::int64_t>().value(), 1);
        auto const cloned = two->clone();
        REQUIRE(cloned.has_value());
        CHECK_EQ((*cloned)->type, cbor::type::tag);
        auto const tree = cbor::decode(head + head + "\x01");
        REQUIRE(tree.has_value());
        CHECK_EQ((*tree)->type, cbor::type::tag);
        auto const array = cbor::lazy::take_ownership("\x81" + head + "\x01");
        REQUIRE(array.has_value());
        auto const element = array->at(0);
        REQUIRE(element.has_value());
        CHECK_EQ(element->type().value(), cbor::type::tag);
        CHECK_EQ(element->get<std::int64_t>().value(), 1);
        CHECK_EQ((cbor::at_path<"$[0]", std::int64_t>("\x81" + head + "\x01")).value(), 1);
        std::string const map = "\xa1" + head + "\x61k\x01";
        CHECK_EQ(cbor::lazy::take_ownership(std::string(map))->at("k"sv)->get<std::int64_t>().value(), 1);
        CHECK_EQ((cbor::at_path<"$.k", std::int64_t>(map)).value(), 1);
        std::string const embedded = "\xa1\x61\x61\xd8\x18" + std::string(1, static_cast<char>(0x40 + head.size() + 3)) +
                                     head + "\x82\x01\x02";
        CHECK_EQ(cbor::lazy::take_ownership(std::string(embedded))->at("a"sv)->at(1)->get<std::int64_t>().value(), 2);
        CHECK_EQ((cbor::at_path<"$.a[1]", std::int64_t>(embedded)).value(), 2);
    }
}

// The owner decided that a tag 55799 inside an item stays a tag. The content of a tag 2, tag 3 or typed array is then
// a tag and not a byte string, so every reader gives for 2(55799(h'01')) what it gives for 2("a"), and for
// 64(55799(h'0102')) what it gives for 64("a"): the same type, value or error. Each width of the 55799 head is read.
TEST_CASE("self-described CBOR: a tag 55799 in the content of a bignum or a typed array stays a tag")
{
    auto const same = [](auto const &a, auto const &b) {
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
            auto const l = cbor::lazy::take_ownership(std::string(tagged));
            auto const t = cbor::lazy::take_ownership(std::string(text));
            REQUIRE(l.has_value());
            REQUIRE(t.has_value());
            same(l->type(), t->type());
            same(l->get<std::int64_t>(), t->get<std::int64_t>());
            same(l->get<double>(), t->get<double>());
            same((cbor::at_path<"$", std::int64_t>(tagged)), (cbor::at_path<"$", std::int64_t>(text)));
            same((cbor::at_path<"$", double>(tagged)), (cbor::at_path<"$", double>(text)));
            auto const tree = cbor::decode(tagged);
            REQUIRE(tree.has_value());
            CHECK_EQ((*tree)->type, l->type().value());
            auto const cloned = l->clone();
            REQUIRE(cloned.has_value());
            CHECK_EQ((*cloned)->type, (*tree)->type);
            std::string const map_tagged = "\xa1" + tagged + "\x05";
            std::string const map_text = "\xa1" + text + "\x05";
            if (bignum == '\xc2')
                same((cbor::at_path<"$[1]", std::int64_t>(map_tagged)), (cbor::at_path<"$[1]", std::int64_t>(map_text)));
            else
                same((cbor::at_path<"$[-2]", std::int64_t>(map_tagged)), (cbor::at_path<"$[-2]", std::int64_t>(map_text)));
            std::string const array_tagged = "\x81" + tagged;
            std::string const array_text = "\x81" + text;
            auto const fa = cbor::lazy::take_ownership(std::string(array_tagged))->at_path<"$[?@ == 1]", cbor::nodelist>();
            auto const fb = cbor::lazy::take_ownership(std::string(array_text))->at_path<"$[?@ == 1]", cbor::nodelist>();
            REQUIRE_EQ(fa.has_value(), fb.has_value());
            if (fa.has_value())
                CHECK_EQ(fa->size(), fb->size());
            else
                CHECK_EQ(fa.error(), fb.error());
        }
        std::string const typed = "\xd8\x40" + head + "\x42\x01\x02";
        std::string const typed_text = "\xd8\x40\x61\x61"s;
        auto const l = cbor::lazy::take_ownership(std::string(typed));
        auto const t = cbor::lazy::take_ownership(std::string(typed_text));
        REQUIRE(l.has_value());
        REQUIRE(t.has_value());
        CHECK_EQ(l->get<cbor::typed_array>().error(), t->get<cbor::typed_array>().error());
        CHECK_EQ(l->at(0).error(), t->at(0).error());
        CHECK_EQ((cbor::at_path<"$[0]", std::int64_t>(typed)).error(), (cbor::at_path<"$[0]", std::int64_t>(typed_text)).error());
        CHECK_EQ(cbor::decode(typed).error(), cbor::decode(typed_text).error());
        CHECK_EQ(l->clone().error(), t->clone().error());
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
    auto const l = test::lazy_of_copy(document);
    REQUIRE(l.has_value());
    auto const v = test::at_path("$.a"sv, *l);
    REQUIRE(v.has_value());
    CHECK(*v == A(5, 6));
}

// RFC 8949 3.4.6: an encoder helps a decoder when it tags the entire item with tag number 55799. The encoder
// writes the tag in its preferred head 0xd9d9f7.
TEST_CASE("self-described CBOR: the encoder writes tag 55799 as 0xd9d9f7")
{
    CHECK_EQ(encoded(tag(55799, A(1, 2, 3))), "\xd9\xd9\xf7\x83"s + native_unsigned(1) + native_unsigned(2) + native_unsigned(3));
}

// RFC 9277 2.2.1: the CBOR Tag Wrapped method. The outer tag 55799 is read as its tag content, and the inner
// protocol tag 1668546929 stays.
TEST_CASE("RFC 9277: a CBOR Tag Wrapped item gives the protocol tag")
{
    auto const v = decoded("\xd9\xd9\xf7\xda\x63\x74\x01\x71\x81\xa1\x00\x67\x63\x75\x72\x72\x65\x6e\x74"sv);
    REQUIRE(v.has_value());
    CHECK(*v == tag(1668546929, A(M(0, "current"s))));
}

// RFC 9277 2.3: the label of a Labeled CBOR Sequence is a data item of its own, with tag 55800 outside. The
// tag 55800 has semantics, so decode keeps it, and the label is the first element of the sequence.
TEST_CASE("RFC 9277: the label of a Labeled CBOR Sequence is the first element and keeps tag 55800")
{
    std::string const label = "\xd9\xd9\xf8\xda\x63\x74\x02\x12\x43\x42\x4f\x52"s;
    value const expected = tag(55800, tag(1668547090, {bytes{"BOR"}}));
    auto const v = decoded(label);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    CHECK_EQ(encoded(expected), label);
    auto const file = std::make_shared<std::string const>(label + "\x00\x08\x0f"s);
    std::vector<std::string_view> elements;
    for (auto const e : cbor::sequence{file})
        elements.push_back(e.value());
    REQUIRE_EQ(elements.size(), 4u);
    CHECK_EQ(elements[0], std::string_view(label));
    CHECK_EQ(elements[3], "\x0f"sv);
}

// RFC 9277 Appendix D: the header of CBOR-Labeled Non-CBOR Data is 12 bytes, and the Non-CBOR data follows it
// directly. item_size gives the end of the header, and decode keeps tag 55801.
TEST_CASE("RFC 9277: the header of CBOR-Labeled Non-CBOR Data ends after 12 bytes")
{
    std::string const header = "\xd9\xd9\xf9\xda\x63\x74\x01\x01\x43\x42\x4f\x52"s;
    std::string const file = header + "text/plain"s;
    CHECK_EQ(cbor::item_size(file).value(), 12u);
    auto const v = decoded(file);
    REQUIRE(v.has_value());
    CHECK(*v == tag(55801, tag(1668546817, {bytes{"BOR"}})));
}

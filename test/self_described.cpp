#include "binding.hpp"

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
    auto const l = cbor::lazy::from(document);
    REQUIRE(l.has_value());
    auto const a = l->at("a"sv);
    REQUIRE(a.has_value());
    auto const second = a->at(std::int64_t{1});
    REQUIRE(second.has_value());
    CHECK_EQ(second->get<std::uint64_t>().value(), 6u);
    auto const owned = cbor::decode(std::make_shared<std::string const>(document));
    REQUIRE(owned.has_value());
    CHECK_EQ(owned->at("a"sv)->at(std::int64_t{0})->get<std::uint64_t>().value(), 5u);
}

// Both forms of at_path read through a leading tag 55799: the typed read on the bytes and the read into a
// binding.
TEST_CASE("self-described CBOR: at_path reads through a leading tag 55799")
{
    std::string const document = "\xd9\xd9\xf7\xa1\x61\x61\x82\x05\x06"s;
    CHECK_EQ(cbor::at_path<"$.a[1]", std::uint64_t>(document).value(), 6u);
    auto const owner = std::make_shared<std::string const>("\xd9\xd9\xf7\xa1\x61\x61\x62\x68\x69"s);
    auto const text = cbor::at_path<"$.a", std::string_view>(owner, *owner);
    REQUIRE(text.has_value());
    CHECK_EQ(**text, "hi"sv);
    test_binding binding;
    auto const l = cbor::lazy::from(document);
    REQUIRE(l.has_value());
    auto const v = cbor::at_path(binding, "$.a"sv, *l);
    REQUIRE(v.has_value());
    CHECK(*v == A(5, 6));
}

// RFC 8949 3.4.6: an encoder helps a decoder when it tags the entire item with tag number 55799. The encoder
// writes the tag in its preferred head 0xd9d9f7.
TEST_CASE("self-described CBOR: the encoder writes tag 55799 as 0xd9d9f7")
{
    CHECK_EQ(encoded(tag(55799, A(1, 2, 3))), "\xd9\xd9\xf7\x83\x01\x02\x03"sv);
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
    std::string const file = label + "\x00\x08\x0f"s;
    std::vector<std::string_view> elements;
    for (auto const e : cbor::sequence{file})
        elements.push_back(e.value());
    REQUIRE_EQ(elements.size(), 4u);
    CHECK_EQ(elements[0], std::string_view(label));
    CHECK_EQ(elements[3], "\x0f"sv);
}

// RFC 9277 Appendix D: the header of CBOR-Labeled Non-CBOR Data is 12 bytes, and the Non-CBOR data follows it
// directly. item_end gives the end of the header, and decode keeps tag 55801.
TEST_CASE("RFC 9277: the header of CBOR-Labeled Non-CBOR Data ends after 12 bytes")
{
    std::string const header = "\xd9\xd9\xf9\xda\x63\x74\x01\x01\x43\x42\x4f\x52"s;
    std::string const file = header + "text/plain"s;
    CHECK_EQ(cbor::item_end(file).value(), 12u);
    auto const v = decoded(file);
    REQUIRE(v.has_value());
    CHECK(*v == tag(55801, tag(1668546817, {bytes{"BOR"}})));
}

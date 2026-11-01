#include "types.hpp"

namespace schema_types
{



template <class C>
C chain_of(std::uint8_t const v)
{
    if constexpr (requires { C::next; })
        return C{v, chain_of<decltype(C::next)>(static_cast<std::uint8_t>(v + 1))};
    else
        return C{v, "ab", {"x", "yz"}};
}

} // namespace schema_types

// schema::at reads one number from the message with no accessor and no owner, because the number is returned by
// value. It makes every check that path and accessor::at make: the table of keys, the undefined values after the
// directory, the tag of the root, the tag and the length of the typed array, and the index. The expected bits of 1.0
// and 2.0 are 3ff0000000000000 and 4000000000000000 in binary64 (IEEE 754).
TEST_CASE("schema::at: one number of a typed array, with every check of path")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    using S = form<doubles>;
    CHECK_EQ(std::bit_cast<std::uint64_t>(*S::at_path<"$.v[0]">(bytes)), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*S::at_path<"$.v[]">(bytes, 1uz)), 0x4000000000000000u);
    CHECK_EQ(S::at_path<"$.v[2]">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(S::at_path<"$.v[]">(bytes, std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
    CHECK_EQ(S::at_path<"$.v[0]">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    std::string wrong = bytes;
    wrong[at + 1] = '\x52';
    CHECK_EQ(S::at_path<"$.v[0]">(wrong).error(), error::incorrect_type);
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(S::at_path<"$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(S::at_path<"$.v[0]">(longer).error(), error::too_little_data);
    std::size_t const filler = bytes.find('\xf7');
    REQUIRE_NE(filler, std::string::npos);
    for (std::size_t j = filler; j < bytes.size() && bytes[j] == '\xf7'; ++j) {
        std::string defined = bytes;
        defined[j] = '\xf6';
        CHECK_EQ(S::at_path<"$.v[0]">(defined).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(defined).error(), error::incorrect_type);
    }
    std::string root = bytes;
    root[root.size() - S::fixed_size() + 2] = '\xfc';
    CHECK_EQ(S::at_path<"$.v[0]">(root).error(), error::incorrect_type);
    CHECK_EQ(at_path<doubles, "$.v[0]">(root).error(), error::incorrect_type);
}

// schema::at gives only a value that holds no reference into the message, because no owner keeps the message alive.
// A path to a text, a list or a struct has no form of schema::at, and a wrong use does not compile.
TEST_CASE("schema::at: a path to a text or a list does not compile")
{
    auto const at_compiles = []<class T, cbor::fixed_string Path>() { return requires { form<T>::template at_path<Path>(std::string_view{}); }; };
    CHECK(at_compiles.template operator()<doubles, "$.v[0]">());
    CHECK_FALSE(at_compiles.template operator()<doubles, "$.v">());
    CHECK_FALSE(at_compiles.template operator()<login, "$.name">());
    CHECK(at_compiles.template operator()<login, "$.id">());
    CHECK_FALSE(at_compiles.template operator()<people, "$.people[0]">());
    CHECK_FALSE(at_compiles.template operator()<people, "$.people[0].n">());
}

// RFC 8949 5.6 lets a decoder that is not in a deterministic profile keep one entry of a repeated key. The key
// 9 of the sample is written over with 7, the other key, and decode keeps the first entry with no error.
TEST_CASE("schema: decode keeps the first entry of a repeated key")
{
    std::string bytes = schema_bytes(sample_garage);
    std::string const nine = "\x19\x00\x09"s;
    auto const at = bytes.find(nine);
    REQUIRE(at != std::string::npos);
    REQUIRE(bytes.find(nine, at + 1) == std::string::npos);
    bytes.replace(at, nine.size(), "\x19\x00\x07"s);
    auto const read = form<garage>::decode(std::string(bytes));
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->owners, (std::map<std::uint16_t, std::string>{{7, "seven"}}));
    CHECK(form<garage>::decode(schema_bytes(sample_garage)).has_value());
}

// The key that the description gives takes the place of the member name in the table. The expectation is
// counted from the draft and RFC 8949 3 by hand: the keys with the text heads 69 and 63, an empty directory,
// f7 up to index 15, then the class tag 65536, which needs the 4-byte argument da 00 01 00 00 (RFC 8949 3 and
// 4.1), the record head of 7 bytes, the integer with its fixed width of 18 and 1 byte, and the simple value.
// The test exists because a tag number above 65535 makes the head of the class tag 5 bytes long, and every
// offset behind it moves by that.
TEST_CASE("schema: the description gives the key of a member")
{
    CHECK_EQ(form<keyed>::fixed_size(), 15u);
    CHECK_EQ(form<keyed>::member_offset<&keyed::m1>(), 12u);
    CHECK_EQ(form<keyed>::member_offset<&keyed::m0>(), 13u);
    std::string const bytes = *form<keyed>::encode(keyed{7, true});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x10\xd8\x72\x82\x63"
                    "EOF\x69x-user-id\x5a\x00\x00\x00\x00"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xda\x00\x01\x00\x00\xd8\x80\x9a\x00\x00\x00\x02\xf5\x18\x07"s);
    auto const decoded_back = *form<keyed>::decode(std::string(bytes));
    keyed const &back = *decoded_back;
    CHECK_EQ(back.m0, 7u);
    CHECK(back.m1);
    CHECK_EQ(at_path<keyed, "$.EOF">(bytes), true);
}

// CTAP 2.1 6.2.2 lets the user of a getAssertion response be absent. An optional struct lies in the
// fixed item as an array of a presence flag and its fields. decode reads it; a path does not step into it. Both
// forms stay two well-formed CBOR items.
TEST_CASE("schema: an optional struct and an optional string, present and absent")
{
    passkey_login const full{{std::byte{1}, std::byte{2}}, passkey_user{{std::byte{9}}, "alice"}, "hello"};
    std::string const bytes = *form<passkey_login>::encode(full);
    CHECK_EQ(at_path<passkey_login, "$.note">(bytes).value(), std::optional<std::string>{"hello"});
    auto const decoded_back = *form<passkey_login>::decode(std::string(bytes));
    passkey_login const &back = *decoded_back;
    REQUIRE(back.user.has_value());
    CHECK_EQ(back.user->name, "alice");
    CHECK_EQ(back.note, std::optional<std::string>{"hello"});

    passkey_login const empty{{std::byte{1}}, std::nullopt, std::nullopt};
    std::string const none = *form<passkey_login>::encode(empty);
    auto const absent = at_path<passkey_login, "$.note">(none);
    REQUIRE(absent.has_value());
    CHECK_FALSE(absent->has_value());
    for (std::string_view message : {std::string_view(bytes), std::string_view(none)}) {
        auto const end = cbor::item_size(message);
        REQUIRE(end.has_value());
        CHECK_EQ(*end, message.size());
    }
    CHECK_EQ(form<wheel>::fixed_size<std::optional<passkey_user>>(), 2 + form<passkey_user>::fixed_size());
    auto const decoded_back2 = *form<passkey_login>::decode(std::string(none));
    passkey_login const &back2 = *decoded_back2;
    CHECK_FALSE(back2.user.has_value());
    CHECK_FALSE(back2.note.has_value());
}

// draft-ietf-cbor-packed-19 2.3 Table 2 gives tags 128..135 to the first 8 table indexes only. The test exists
// because a root with 20 struct types needs record functions at indexes 8 to 19, which a record reaches with the
// straight reference 6([N, rump]) at index 8 + N. chain19 is index 19, so N = 11 (0b): c6 82 0b, then the rump
// 9a 00 00 00 03. The directory takes index 20, so the shared items start at 21 and no f7 is left. Table 1 maps
// index 21 to 6(-3) = c6 3a 00 00 00 02 and index 22 to 6(3) = c6 1a 00 00 00 03. The table head counts 21 + 4
// entries. A record of index 8 or more is one byte longer, so its size depends on the root. The class tag of chainK
// is 1600 + K, 3 bytes in front of each record, and stands outside the reference: 1619 = d9 06 53 before c6 82 0b.
TEST_CASE("schema: a root with 20 struct types reaches indexes 8 and more with tag 6")
{
    CHECK_EQ(form<chain0>::fixed_size<chain19>(), 3u + 3u + 5u + 2u + 6u + 6u);
    CHECK_EQ(form<chain19>::fixed_size(), 3u + 2u + 5u + 2u + 6u + 6u);
    CHECK_EQ(form<chain0>::fixed_size(), 8u * 12u + 11u * 13u + 25u);
    CHECK_EQ((form<chain0>::member_offset<&chain8::next, chain8>()), 13u);
    CHECK_EQ(form<chain8>::member_offset<&chain8::next>(), 12u);

    chain0 const value = chain_of<chain0>(0);
    std::string const bytes = *form<chain0>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x19"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x0b\x9a\x00\x00\x00\x03\x18\x13\xc6\x3a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x03"s);
    CHECK_EQ(bytes.substr(bytes.size() - form<chain0>::fixed_size() + 8u * 12u, 6), "\xd9\x06\x48\xc6\x82\x00"s);

    auto const decoded_back = *form<chain0>::decode(std::string(bytes));
    chain0 const &back = *decoded_back;
    CHECK_EQ(*form<chain0>::encode(back), bytes);
    CHECK_EQ(back.next.next.next.next.next.next.next.next.next.v, 9u);

    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.v">(bytes), 9u);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.v">(bytes),
             19u);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(
                 bytes),
             "ab"s);
    CHECK_EQ(at_path<chain0, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.words[1]">(
                 bytes),
             "yz"s);
}

// draft-ietf-cbor-packed-19 2.2 Table 1: with 17 struct types the directory is index 17 and the first shared item
// is index 18, which is 6(1) = c6 1a 00 00 00 01. The test exists because a reference below index 18 then names
// no shared item: 6(0) is index 16, a record function, and the reader refuses it.
TEST_CASE("schema: a root with 17 struct types starts its shared items at index 18")
{
    chain3 const value = chain_of<chain3>(3);
    std::string const bytes = *form<chain3>::encode(value);
    CHECK_EQ(cbor::item_size(bytes), bytes.size());
    CHECK_EQ(bytes.substr(0, 8), "\xd8\x71\x82\x9a\x00\x00\x00\x16"s);
    CHECK_EQ(bytes.substr(bytes.size() - 25),
             "\xd9\x06\x53\xc6\x82\x08\x9a\x00\x00\x00\x03\x18\x13\xc6\x1a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x01"s);

    auto const decoded_back = *form<chain3>::decode(std::string(bytes));
    chain3 const &back = *decoded_back;
    CHECK_EQ(*form<chain3>::encode(back), bytes);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(bytes), "ab"s);

    std::string broken = bytes;
    broken.at(broken.size() - 7) = '\x00';
    CHECK_EQ(form<chain3>::decode(std::string(broken)).error(), error::unpopulated_table_index);
    CHECK_EQ(at_path<chain3, "$.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.next.text">(broken).error(),
             error::unpopulated_table_index);
}

// The tag of the class names the class on the wire. point_a and point_b have the same keys and the same layout,
// so only the class tag tells them apart: 1520 = d9 05 f0 against 1521 = d9 05 f1 (RFC 8949 3.4). The test exists
// because a reader that ignored the class tag would take a message of one class for the other.
TEST_CASE("schema: a root of another class tag is incorrect_type")
{
    std::string const bytes = *form<point_a>::encode(point_a{7});
    CHECK_EQ(bytes.substr(bytes.size() - form<point_a>::fixed_size(), 3), "\xd9\x05\xf0"s);
    auto const point = *form<point_a>::decode(std::string(bytes));
    CHECK_EQ(point->x, 7u);
    CHECK_EQ(form<point_b>::decode(std::string(bytes)).error(), error::incorrect_type);
    CHECK_EQ(at_path<point_b, "$.x">(bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<point_a, "$.x">(bytes), 7u);
}

// The class tag of a nested record is a fixed head like any other. car holds the engine record at offset 12, and
// the low byte of its tag 1501 = d9 05 dd is changed to de. The test exists because a forged nested tag must stop
// decode, and a path through that record must stop too, while the root and the other fields still read.
TEST_CASE("schema: a forged class tag of a nested record is incorrect_type")
{
    std::string const bytes = *form<car>::encode(car{4, {300, 1800}, true});
    std::size_t const motor = bytes.size() - form<car>::fixed_size() + form<car>::member_offset<&car::motor>();
    CHECK_EQ(bytes.substr(motor, 3), "\xd9\x05\xdd"s);
    std::string forged = bytes;
    forged.at(motor + 2) = '\xde';
    CHECK_EQ(form<car>::decode(std::string(forged)).error(), error::incorrect_type);
    CHECK_EQ(at_path<car, "$.seats">(forged), 4u);
    CHECK_EQ(at_path<car, "$.motor.cc">(forged).error(), error::incorrect_type);
    CHECK_EQ(at_path<car, "$.motor.cc">(bytes), 1800u);
}

// An accessor inside an optional shares the owner of the message like every other accessor, so a read through it
// does not see a moved-from accessor.
TEST_CASE("schema: an accessor inside an optional reads its fields")
{
    auto const root = form<everything>::path(schema_bytes(sample_everything));
    REQUIRE(root.has_value());
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.x">(), 1);
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
}

// The check for a moved-from accessor compares the control block of the owner, not its count. An owner that shares a
// control block and holds a null pointer is still an owner; an owner with no control block is empty.
TEST_CASE("schema: an owner is empty only when it has no control block")
{
    std::string const bytes = schema_bytes(sample_everything);
    auto const held = std::make_shared<std::string const>(bytes);
    auto const alias = std::shared_ptr<void const>(held, nullptr);
    auto root = form<everything>::path(alias, std::string_view(*held));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);

    auto const moved = std::move(*root);
    CHECK_EQ(moved.at_path<"$.text">(), "text"sv);
    CHECK_THROWS_AS((void)root->at_path<"$.text">(), std::logic_error);
    CHECK_THROWS_AS(std::ignore = form<everything>::path(std::shared_ptr<void const>(), std::string_view(bytes)), std::logic_error);
    auto unverified = form<everything>::unverified_path(held);
    REQUIRE(unverified.has_value());
    auto const taken = std::move(*unverified);
    CHECK_THROWS_AS((void)unverified->at_path<"$.text">(), std::logic_error);
}

namespace
{

struct nan_fields {
    float single;
    double twice;
};

} // namespace

CBOR_DEFINE_TYPE(nan_fields, cbor::tag{1736}, CBOR_MEMBER(nan_fields, single), CBOR_MEMBER(nan_fields, twice));

// Owner decision 2026-10-12: a NaN keeps its bits. A signaling NaN of a float and a double, with its payload in the
// lowest bit, goes through encode, decode and at_path with every bit; a conversion of the processor would set the
// quiet bit.
TEST_CASE("schema: a NaN keeps its bits through encode, decode and at_path")
{
    nan_fields const value{std::bit_cast<float>(0x7f800001u), std::bit_cast<double>(0x7ff0000000000001ull)};
    std::string const bytes = *form<nan_fields>::encode(value);
    auto const back = form<nan_fields>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>((*back)->single), 0x7f800001u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*back)->twice), 0x7ff0000000000001ull);
    CHECK_EQ(std::bit_cast<std::uint32_t>(*form<nan_fields>::at_path<"$.single">(bytes)), 0x7f800001u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*form<nan_fields>::at_path<"$.twice">(bytes)), 0x7ff0000000000001ull);
}

#if defined(__STDCPP_BFLOAT16_T__)

namespace
{

struct brain_float {
    std::bfloat16_t value;
};

} // namespace

CBOR_DEFINE_TYPE(brain_float, cbor::tag{1737}, CBOR_MEMBER(brain_float, value));

// A bfloat16 is the top 16 bits of a binary32, so the wire takes its bits shifted, not converted. A conversion through
// float set the quiet bit of the signaling NaN 7f81 (7fc10000), and a read through float lost its payload.
TEST_CASE("schema: a bfloat16 NaN keeps its bits")
{
    brain_float const value{std::bit_cast<std::bfloat16_t>(std::uint16_t{0x7f81})};
    std::string const bytes = *form<brain_float>::encode(value);
    CHECK_NE(bytes.find("\xfa\x7f\x81\x00\x00"s), std::string::npos);
    auto const back = form<brain_float>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ(std::bit_cast<std::uint16_t>((*back)->value), 0x7f81u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(*form<brain_float>::at_path<"$.value">(bytes)), 0x7f81u);
}

#endif

#include "schema_types.hpp"
#include "schema_golden.hpp"

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

namespace
{

template <class C>
C chain_of(std::uint8_t const v)
{
    if constexpr (requires { C::next; })
        return C{v, chain_of<decltype(C::next)>(static_cast<std::uint8_t>(v + 1))};
    else
        return C{v, "ab", {"x", "yz"}};
}

} // namespace

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

template <class T>
void check_description_apart(T const &value)
{
    std::string const inside = schema_bytes(value);
    std::string const data = apart_bytes(value);
    auto const description = form<T>::description();
    CHECK_EQ(data.size() + description.size(), inside.size());
    CHECK(std::ranges::equal(description, std::span(inside).subspan(8, description.size())));
    check_one_item(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::string(data))), inside);
    CHECK_EQ(reencoded(form<T>::template unverified_decode<apart>(std::string(data))), inside);
    auto const owner = std::make_shared<std::string const>(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner)), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::shared_ptr<void const>(owner), std::string_view(*owner))), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner, T{})), inside);
    CHECK(form<T>::template path<apart>(std::string(data)).has_value());
    CHECK(form<T>::template path<apart>(owner).has_value());
    CHECK(form<T>::template unverified_path<apart>(owner).has_value());
}

} // namespace

// Owner decision 2026-10-20: the description of a struct is stored once, apart from the data of each key, so the
// data does not repeat it. The data form is the message without the table entries of the description; this
// library reads it with the type, which knows the description at compile time.
TEST_CASE("description apart: the data and the description together hold what the message holds")
{
    check_description_apart(sample_everything);
    check_description_apart(sample_vehicle);
    check_description_apart(empty{});
    check_description_apart(gauges{});
    check_description_apart(fifteen_entries{});
    eighteen_entries points{};
    points.p = {{1, 2}, {3, 4}};
    check_description_apart(points);
}

// Every read form gives on the data form what it gives on the message.
TEST_CASE("description apart: each read form reads the data form")
{
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(data)), -5);
    CHECK_EQ((form<everything>::at_path<"$.gauges[].at.x", apart>(data, 1uz)), -6);
    CHECK_EQ((form<everything>::unverified_at_path<"$.tires[].airPressure", apart>(data, 1uz)), 3.0f);
    auto const root = form<everything>::path<apart>(std::string(data));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    CHECK_EQ(root->view<"$.track">()->at(1), 2.0);
    CHECK_EQ(root->at_path<"$.owners">()->size(), 2u);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    std::vector<int> xs;
    for (auto const g : *root->at_path<"$.gauges">())
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18u);
    auto const unverified = form<everything>::unverified_path<apart>(std::string(data));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->at_path<"$.names[0]">(), "a"sv);
}

// Each target of encode writes the same data form.
TEST_CASE("description apart: every target of encode writes the same bytes")
{
    std::string const data = apart_bytes(sample_everything);
    std::string appended = "ab";
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, appended), data.size());
    CHECK_EQ(appended, "ab" + data);
    std::vector<char> chars;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, chars), data.size());
    CHECK(std::ranges::equal(chars, data));
    std::vector<char> room(data.size() + 64);
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(room)), data.size());
    CHECK(std::ranges::equal(std::span(room).first(data.size()), data));
    std::vector<char> exact(data.size());
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(exact)), data.size());
    CHECK(std::ranges::equal(exact, data));
    string_writer w;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, w), data.size());
    CHECK_EQ(w.encoded, data);
}

// A message read as the data form and the data form read as a message are refused: the byte after the head of
// the table is the head of the directory in one form and the first entry of the description in the other. A short
// input ends as too_little_data before that byte is read.
TEST_CASE("description apart: one form read as the other is refused")
{
    for (std::string const &inside : {schema_bytes(sample_everything), schema_bytes(empty{}), schema_bytes(eighteen_entries{})}) {
        auto const as_data = form<everything>::path<apart>(std::string(inside));
        REQUIRE_FALSE(as_data.has_value());
    }
    std::string const inside = schema_bytes(sample_everything);
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ(form<everything>::path<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(inside)).error(), error::incorrect_type);
    for (auto const e : {form<everything>::path(std::string(data)).error(),
                         form<everything>::decode(std::string(data)).error()})
        CHECK((e == error::incorrect_type || e == error::too_little_data));
    std::string const small = apart_bytes(empty{});
    CHECK_EQ(form<empty>::path(std::string(small)).error(), error::too_little_data);
}

// The data form keeps every check of the message: a cut gives too_little_data, a forged directory entry and a
// forged filler give an error in the verified forms.
TEST_CASE("description apart: the checks of the message hold for the data form")
{
    std::string const data = apart_bytes(sample_everything);
    for (std::size_t n = 0; n < data.size(); ++n) {
        CHECK_FALSE(form<everything>::decode<apart>(data.substr(0, n)).has_value());
        CHECK_FALSE(form<everything>::path<apart>(data.substr(0, n)).has_value());
    }
    std::size_t const directory = data.find('\x5a');
    REQUIRE_EQ(directory, 8u);
    std::string entry = data;
    for (std::size_t k = 0; k < 4; ++k)
        entry[directory + 5 + k] = '\xff';
    CHECK_FALSE(form<everything>::decode<apart>(std::string(entry)).has_value());
    std::uint32_t const length = (std::uint32_t{static_cast<unsigned char>(data[directory + 3])} << 8) |
                                 static_cast<unsigned char>(data[directory + 4]);
    std::string filler = data;
    REQUIRE_EQ(filler[directory + 5 + length], '\xf7');
    filler[directory + 5 + length] = '\xf6';
    CHECK_EQ(form<everything>::path<apart>(std::string(filler)).error(), error::incorrect_type);
}

// The unverified forms read the data form inside its bounds: every changed byte and every cut, and a message read
// as the data form, stay inside the input under AddressSanitizer.
TEST_CASE("description apart: the unverified forms stay inside the input")
{
    std::string const data = apart_bytes(sample_everything);
    auto const reads = [](std::string const &bytes) {
        (void)form<everything>::unverified_at_path<"$.gauges[].at.x", apart>(bytes, 1uz);
        (void)form<everything>::unverified_decode<apart>(std::string(bytes));
        if (auto const root = form<everything>::unverified_path<apart>(std::string(bytes))) {
            (void)root->at_path<"$.names[1]">();
            (void)root->at_path<"$.tires[1].diameter">();
            if (auto const track = root->view<"$.track">())
                for (double const d : *track)
                    (void)d;
        }
    };
    for (std::size_t i = 0; i < data.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~data[i])}) {
            std::string changed = data;
            changed[i] = to;
            reads(changed);
        }
    for (std::size_t n = 0; n < data.size(); ++n)
        reads(data.substr(0, n));
    reads(schema_bytes(sample_everything));
}

namespace
{

template <class T>
void golden_fill(T &value, unsigned &seed)
{
    seed += 1;
    if constexpr (cbor::has_describe_members<T>) {
        std::apply([&](auto const &...m) { (golden_fill(value.*m.pointer, seed), ...); }, cbor::describe_members<T>::members);
    } else if constexpr (std::same_as<T, bool>) {
        value = seed % 2 == 1;
    } else if constexpr (std::is_enum_v<T>) {
        value = static_cast<T>(seed % 2);
    } else if constexpr (std::same_as<T, cbor::int128>) {
        value = -static_cast<cbor::int128>(seed) * 1000003;
    } else if constexpr (std::is_arithmetic_v<T>) {
        value = static_cast<T>(seed * 37u);
#if defined(__STDCPP_FLOAT16_T__)
    } else if constexpr (std::same_as<T, std::float16_t>) {
        value = static_cast<std::float16_t>(seed) + std::float16_t{0.5f};
#endif
    } else if constexpr (std::same_as<T, std::byte>) {
        value = static_cast<std::byte>(seed);
    } else if constexpr (std::same_as<T, std::string_view>) {
        value = std::string_view("golden text").substr(seed % 7);
    } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::pmr::string>) {
        value.assign(seed % 5 + 1, static_cast<char>('a' + seed % 26));
    } else if constexpr (std::is_array_v<T> || requires { std::tuple_size<T>::value; }) {
        for (auto &e : value)
            golden_fill(e, seed);
    } else if constexpr (requires { value.has_value(); }) {
        golden_fill(value.emplace(), seed);
    } else if constexpr (requires { typename T::mapped_type; }) {
        for (unsigned i = 0; i < 2; ++i) {
            typename T::key_type k{};
            typename T::mapped_type v{};
            golden_fill(k, seed);
            golden_fill(v, seed);
            value.try_emplace(std::move(k), std::move(v));
        }
    } else {
        for (unsigned i = 0; i < 2; ++i)
            golden_fill(value.emplace_back(), seed);
    }
}

std::string golden_hex(std::string_view const bytes)
{
    std::string out;
    for (char const c : bytes) {
        constexpr std::string_view digits = "0123456789abcdef";
        auto const byte = static_cast<std::size_t>(static_cast<unsigned char>(c));
        out += digits[byte >> 4];
        out += digits[byte & 0xf];
    }
    return out;
}

template <class T>
std::string golden_line(std::string_view const name)
{
    T value{};
    unsigned seed = 0;
    golden_fill(value, seed);
    auto const inside = form<T>::encode(value);
    auto const data = form<T>::template encode<cbor::description_place::apart>(value);
    if (!inside || !data)
        return std::string(name) + " no encoding\n";
    auto const description = form<T>::description();
    return std::string(name) + " " + golden_hex(*inside) + " " + golden_hex(*data) + " " +
           golden_hex(std::string_view(description.data(), description.size())) + "\n";
}

std::string golden_report()
{
    std::string report;
    report += golden_line<wheel>("wheel");
    report += golden_line<engine>("engine");
    report += golden_line<car>("car");
    report += golden_line<empty>("empty");
    report += golden_line<thirty>("thirty");
    report += golden_line<long_name>("long_name");
    report += golden_line<größe>("größe");
    report += golden_line<login>("login");
    report += golden_line<measures>("measures");
    report += golden_line<named>("named");
    report += golden_line<people>("people");
    report += golden_line<pmr_named>("pmr_named");
    report += golden_line<pmr_people>("pmr_people");
    report += golden_line<unsorted>("unsorted");
    report += golden_line<unsorted_other_order>("unsorted_other_order");
    report += golden_line<renamed>("renamed");
    report += golden_line<listed_other_order>("listed_other_order");
    report += golden_line<sorted_inner>("sorted_inner");
    report += golden_line<sorted_outer>("sorted_outer");
    report += golden_line<tire>("tire");
    report += golden_line<vehicle>("vehicle");
    report += golden_line<badge>("badge");
#if defined(__STDCPP_FLOAT16_T__)
    report += golden_line<numbers>("numbers");
#endif
    report += golden_line<doubles>("doubles");
    report += golden_line<garage>("garage");
    report += golden_line<keyed>("keyed");
    report += golden_line<passkey_user>("passkey_user");
    report += golden_line<passkey_login>("passkey_login");
    report += golden_line<chain19>("chain19");
    report += golden_line<chain10>("chain10");
    report += golden_line<chain0>("chain0");
    report += golden_line<point_a>("point_a");
    report += golden_line<point_b>("point_b");
    report += golden_line<ticket>("ticket");
    report += golden_line<probe>("probe");
    report += golden_line<lot_wheel>("lot_wheel");
    report += golden_line<lot_engine>("lot_engine");
    report += golden_line<lot_car>("lot_car");
    report += golden_line<parking_lot>("parking_lot");
    report += golden_line<point>("point");
    report += golden_line<cloud>("cloud");
    report += golden_line<gauge>("gauge");
    report += golden_line<gauges>("gauges");
    report += golden_line<filler_type<3>>("filler_type<3>");
    report += golden_line<fifteen_entries>("fifteen_entries");
    report += golden_line<eighteen_entries>("eighteen_entries");
    report += golden_line<everything>("everything");
    return report;
}

} // namespace

// The bytes of every described type of this file, with values that differ in each leaf, as the commit before
// cbor::packed wrote them (dc787da): the message with the description inside, the data form apart, and the
// description. A change of the walk that changes one byte fails here.
TEST_CASE("golden bytes: every type of this file encodes as before")
{
    std::string expected(golden_expected);
#if !defined(__STDCPP_FLOAT16_T__)
    std::size_t const at = expected.find("\nnumbers ") + 1;
    expected.erase(at, expected.find('\n', at) + 1 - at);
#endif
    CHECK_EQ(golden_report(), expected);
}

namespace
{

template <class T, cbor::fixed_string Path, class... Index>
concept message_path_reads = requires(std::string_view const s, Index const... i) { form<T>::template at_path<Path>(s, i...); };

} // namespace

// A user reads one byte of an array of bytes with [n], as one number of a list. The array is one byte string in
// the message: inline in the root for a fixed size, a part for a vector. The byte is the end of the path.
TEST_CASE("at_path: [n] reads one byte of a byte string")
{
    std::string const bytes = *form<byte_fields>::encode(byte_fields_value());
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[2]">(bytes), std::uint8_t{3});
    CHECK_EQ(form<byte_fields>::at_path<"$.plain[1]">(bytes), std::uint8_t{5});
    CHECK_EQ(form<byte_fields>::at_path<"$.bytes[1]">(bytes), std::byte{7});
    CHECK_EQ(form<byte_fields>::at_path<"$.list[1]">(bytes), std::uint8_t{9});
    CHECK_EQ(form<byte_fields>::at_path<"$.byte_list[0]">(bytes), std::byte{10});
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[]">(bytes, 1uz), std::uint8_t{2});
    CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(bytes, 0uz), std::uint8_t{8});
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[]">(bytes, 3uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::at_path<"$.none[]">(bytes, 0uz).error(), error::index_out_of_bounds);
    CHECK_EQ(form<byte_fields>::unverified_at_path<"$.fixed[2]">(bytes), std::uint8_t{3});
    CHECK_EQ(form<byte_fields>::unverified_at_path<"$.list[1]">(bytes), std::uint8_t{9});
    auto const root = form<byte_fields>::path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.fixed[0]">(), std::uint8_t{1});
    CHECK_EQ(root->at_path<"$.list[0]">(), std::uint8_t{8});
    std::string const lists = *form<byte_lists>::encode(byte_lists{{{"x", {1, 2, 3, 4}}, {"y", {5, 6, 7, 8}}}, {{9, 10, 11, 12}}});
    CHECK_EQ(form<byte_lists>::at_path<"$.records[1].code[2]">(lists), std::uint8_t{7});
    CHECK_EQ(form<byte_lists>::at_path<"$.codes[0][3]">(lists), std::uint8_t{12});
    auto const records = form<byte_lists>::path(std::string(lists));
    REQUIRE(records.has_value());
    auto const second = records->at_path<"$.records[1]">();
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.code[0]">(), std::uint8_t{5});
}

// A path ends at the byte, and a literal index lies inside a byte string of fixed size. A text stays a text.
TEST_CASE("at_path: [n] into a byte string does not compile past its end or into a text")
{
    CHECK(message_path_reads<byte_fields, "$.fixed[2]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.fixed[3]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.none[0]">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.fixed[0].x">);
    CHECK_FALSE(message_path_reads<byte_fields, "$.list[0][0]">);
    CHECK_FALSE(message_path_reads<byte_record, "$.name[0]">);
}

// A verified read checks the head of a byte string before it reads a byte of it.
TEST_CASE("at_path: [n] into a byte string with a wrong head is incorrect_type")
{
    std::string bytes = *form<byte_fields>::encode(byte_fields_value());
    std::size_t const head = bytes.find("\x43\x01\x02\x03"sv);
    REQUIRE_NE(head, std::string::npos);
    bytes[head] = '\x63';
    CHECK_EQ(form<byte_fields>::at_path<"$.fixed[2]">(bytes).error(), error::incorrect_type);
}

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

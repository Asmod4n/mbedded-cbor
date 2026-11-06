#include "types.hpp"

// draft-ietf-cbor-packed-19 3.1 and 4.2: tag 113 holds an array of the table and the rump. The table holds the
// record function 114 over the keys at index 0, the directory at index 1 (a byte string of the u32 offsets of the
// shared items), f7 up to index 15, and the shared items from index 16 on, each with a head of fixed width. The rump
// is the class tag 1507 = d9 05 e3 (RFC 8949 3.4), then the record d8 80 with the count 9a 00 00 00 03, every number
// in the width of its type, and the string as 6(0) = c6 1a 00 00 00 00, which 2.2 resolves to index 16. The table
// head counts 17 entries. The string starts at byte 45 (2d). The bytes are written from the draft and RFC 8949 3 by
// hand.
TEST_CASE("encode: a struct with a number, a bool and a string")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x11\xd8\x72\x83\x62id\x62ok\x64name"
                    "\x5a\x00\x00\x00\x04\x00\x00\x00\x2d"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x7a\x00\x00\x00\x02"
                    "ab"
                    "\xd9\x05\xe3\xd8\x80\x9a\x00\x00\x00\x03\x19\x00\x05\xf5\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// packed is deterministic: RFC 8949 4.2.1 sorts the keys of a map "in the bytewise lexicographic order of their
// deterministic encodings", so a shorter key comes first. The encodings are 61 62 ("b"), 62 61 61 ("aa") and
// 62 7a 7a ("zz"); the key list of tag 114 and the values of the record follow that order, whatever the order of
// declaration. The bytes are written from the RFC by hand.
TEST_CASE("encode: members stand in the key order of RFC 8949 4.2.1, not in the order of declaration")
{
    std::string const bytes = schema_bytes(unsorted{3, 1, 2});
    CHECK_NE(bytes.find("\xd8\x72\x83\x61" "b\x62" "aa\x62zz"s), std::string::npos);
    CHECK_NE(bytes.find("\xd9\x06\x54\xd8\x80\x9a\x00\x00\x00\x03\x18\x01\x18\x02\x18\x03"s), std::string::npos);
    CHECK_EQ(at_path<unsorted, "$.zz">(bytes), 3);
    CHECK_EQ(at_path<unsorted, "$.b">(bytes), 1);
    CHECK_EQ(at_path<unsorted, "$.aa">(bytes), 2);
}

// The description lists aa, zz, b: neither the order of declaration (zz, b, aa) nor the key order (b, aa, zz).
// The test exists because the key order alone decides the bytes, so the order of the list changes nothing.
TEST_CASE("encode: the order of the description does not change the bytes")
{
    CHECK_EQ(schema_bytes(listed_other_order{3, 1, 2}), schema_bytes(unsorted{3, 1, 2}));
    CHECK_EQ(at_path<listed_other_order, "$.zz">(schema_bytes(listed_other_order{3, 1, 2})), 3);
}

// The same members declared in another order give the same bytes, so a peer gets one encoding for one value.
TEST_CASE("encode: the order of declaration does not change the bytes")
{
    CHECK_EQ(schema_bytes(unsorted{3, 1, 2}), schema_bytes(unsorted_other_order{2, 3, 1}));
}

// A key that the description gives takes part in the order: "a" (61 61) comes before "first" (65 ...).
TEST_CASE("encode: a key given in cbor::describe_members is sorted by that key")
{
    std::string const bytes = schema_bytes(renamed{7, 9});
    CHECK_NE(bytes.find("\xd8\x72\x82\x61" "a\x65" "first"s), std::string::npos);
    CHECK_EQ(at_path<renamed, "$.a">(bytes), 7);
    CHECK_EQ(at_path<renamed, "$.first">(bytes), 9);
    auto const back = form<renamed>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->last, 7);
    CHECK_EQ((*back)->first, 9);
}

// A negative int16 is major type 1 with -1 - n in two bytes (RFC 8949 3.1). A float stays binary32 (fa). The values
// stand in the key order of RFC 8949 4.2.1: f, o, t, v. The empty optional is shared item 16, an empty array. The
// list of uint16 is shared item 17, a typed array of RFC 8746: tag 69 (uint16, little endian) = d8 45 over a byte
// string of four bytes. Item 17 is 6(-1) = c6 3a 00 00 00 00, because 2.2 maps a negative N to index 16 - 2N - 1.
// The items start at 46 (2e) and 51 (33). The rump starts with the class tag 1508 = d9 05 e4.
TEST_CASE("encode: signed numbers, floats, a list and an empty optional")
{
    std::string const bytes = schema_bytes(measures{-5, 1.5f, {7, 8}, std::nullopt});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x12\xd8\x72\x84\x61\x66\x61o\x61t\x61v"
                    "\x5a\x00\x00\x00\x08\x00\x00\x00\x2e\x00\x00\x00\x33"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x00"
                    "\xd8\x45\x5a\x00\x00\x00\x04\x07\x00\x08\x00"
                    "\xd9\x05\xe4\xd8\x80\x9a\x00\x00\x00\x04\xfa\x3f\xc0\x00\x00"
                    "\xc6\x1a\x00\x00\x00\x00\x39\x00\x04\xc6\x3a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// Two record functions take index 0 and 1, the directory index 2. The list is item 16; its elements are records
// of fixed size, each the class tag 1509 = d9 05 e5 over d8 81 for the second type. The strings of the elements
// follow the list as items 17 and 18, in the order the encoder meets them: 6(-1) and 6(1). The items start at
// 53 (35), 90 (5a) and 96 (60). The root is the class tag 1510 = d9 05 e6 over d8 80.
TEST_CASE("encode: a list of structs that hold strings")
{
    std::string const bytes = schema_bytes(people{{{"x"}, {"yz"}}});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x13\xd8\x72\x81\x66people\xd8\x72\x81\x61n"
                    "\x5a\x00\x00\x00\x0c\x00\x00\x00\x35\x00\x00\x00\x5a\x00\x00\x00\x60"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\x9a\x00\x00\x00\x02"
                    "\xd9\x05\xe5\xd8\x81\x9a\x00\x00\x00\x01\xc6\x3a\x00\x00\x00\x00"
                    "\xd9\x05\xe5\xd8\x81\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x01"
                    "\x7a\x00\x00\x00\x01x\x7a\x00\x00\x00\x02yz"
                    "\xd9\x05\xe6\xd8\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// The schema writes a list of a fixed-width number as a typed array of RFC 8746 in little endian. The tag numbers
// come from RFC 8746 figure 6: uint16le 69, uint32le 70, uint64le 71, sint8 72, sint16le 77, sint32le 78,
// sint64le 79, float16le 84, float32le 85, float64le 86. The byte string holds the elements with the lowest byte
// first. The test exists because the tag tells every other reader the type of the elements.
TEST_CASE("encode: a list of a fixed-width number is a typed array of RFC 8746")
{
    std::string const bytes = schema_bytes(sample_numbers);
    check_one_item(bytes);
    CHECK_NE(bytes.find("\xd8\x45\x5a\x00\x00\x00\x02\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x46\x5a\x00\x00\x00\x04\x04\x03\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x47\x5a\x00\x00\x00\x08\x08\x07\x06\x05\x04\x03\x02\x01"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x48\x5a\x00\x00\x00\x01\xfe"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4d\x5a\x00\x00\x00\x02\xfe\xff"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4e\x5a\x00\x00\x00\x04\xfe\xff\xff\xff"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x4f\x5a\x00\x00\x00\x08\xfe\xff\xff\xff\xff\xff\xff\xff"sv), std::string::npos);
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_NE(bytes.find("\xd8\x54\x5a\x00\x00\x00\x02\x00\x3c"sv), std::string::npos);
#endif
    CHECK_NE(bytes.find("\xd8\x55\x5a\x00\x00\x00\x04\x00\x00\x80\x3f"sv), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x56\x5a\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\xf0\x3f"sv), std::string::npos);
}

// encode writes into a target of the caller: a growing container gets the message appended, a fixed span takes
// it if it fits, and any other target gives an object with append and done through allocate. done carries the
// final length of the message. A span that holds the message but not the padding of a head still takes it.
TEST_CASE("encode: into a string, a vector, a span and a writer of the caller")
{
    std::string const expected = *form<garage>::encode(sample_garage);
    std::string text = "x";
    CHECK_EQ(*form<garage>::encode(sample_garage, text), expected.size());
    CHECK_EQ(text, "x" + expected);
    std::vector<std::byte> bytes;
    REQUIRE(form<garage>::encode(sample_garage, bytes).has_value());
    CHECK_EQ(bytes.size(), expected.size());
    std::array<char, 4096> buffer{};
    auto const fits = form<garage>::encode(sample_garage, std::span(buffer));
    REQUIRE(fits.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *fits), expected);
    std::vector<char> chars{'x'};
    CHECK_EQ(*form<garage>::encode(sample_garage, chars), expected.size());
    CHECK_EQ(std::string_view(chars.data(), chars.size()), "x" + expected);
    std::vector<char> exact(expected.size());
    auto const fits_exactly = form<garage>::encode(sample_garage, std::span(exact));
    REQUIRE(fits_exactly.has_value());
    CHECK_EQ(std::string_view(exact.data(), *fits_exactly), expected);
    std::array<char, 8> small{};
    auto const too_small = form<garage>::encode(sample_garage, std::span(small));
    REQUIRE_FALSE(too_small.has_value());
    CHECK_EQ(too_small.error(), cbor::error::no_buffer_space);

    struct counting {
        std::string sent;
        std::size_t finished = 0;
        std::size_t hint = 0;

        struct message {
            counting &to;
            std::expected<void, std::errc> append(std::string_view const part)
            {
                to.sent.append(part);
                return {};
            }
            std::expected<void, std::errc> done(std::size_t const size)
            {
                to.finished = size;
                return {};
            }
        };

        message allocate(std::size_t const n)
        {
            hint = n;
            return message{*this};
        }
    } writer;
    REQUIRE(form<garage>::encode(sample_garage, writer).has_value());
    CHECK_EQ(writer.sent, expected);
    CHECK_EQ(writer.finished, expected.size());
    CHECK_EQ(writer.hint, expected.size());
}

// A store such as LMDB hands out exactly the bytes it was asked for, and the next record may sit right
// behind them. A writer whose allocate returns a span gets the message written straight into it, and no
// byte behind the span changes, also when the last item is a head with no content after it.
TEST_CASE("encode: into the exact span that a writer allocates")
{
    struct reserving {
        std::array<char, 4096> store{};
        std::size_t asked = 0;
        std::size_t finished = 0;

        struct message {
            reserving &to;
            explicit operator std::span<char>() const
            {
                return std::span(to.store).first(to.asked);
            }
            std::expected<void, std::errc> done(std::size_t const size)
            {
                to.finished = size;
                return {};
            }
        };

        message allocate(std::size_t const n)
        {
            asked = n;
            return message{*this};
        }
    };

    auto const check = [](auto const &value) {
        std::string const expected = *form<std::remove_cvref_t<decltype(value)>>::encode(value);
        reserving writer;
        writer.store.fill('#');
        REQUIRE_EQ(*form<std::remove_cvref_t<decltype(value)>>::encode(value, writer), expected.size());
        CHECK_EQ(writer.asked, expected.size());
        CHECK_EQ(writer.finished, expected.size());
        CHECK_EQ(std::string_view(writer.store.data(), expected.size()), expected);
        auto const behind = std::span(writer.store).subspan(expected.size());
        CHECK(std::ranges::all_of(behind, [](char const c) { return c == '#'; }));
    };
    check(sample_garage);
    check(passkey_login{{std::byte{1}, std::byte{2}}, std::nullopt, std::string{}});
    check(passkey_login{{}, passkey_user{{}, ""}, std::nullopt});
}

// Owner decision 2026-10-19: a vector of structs whose members all have a fixed width is one byte string of its
// elements, little endian and without padding, under tag 32873 (the first unassigned number of the first-come
// range of the IANA registry, provisional). The header, the table of tag 113 (draft-ietf-cbor-packed-19 3.1),
// describes the element once: [class tag, [[key, RFC 8746 tag], ...]]. The description of point stands at table
// index 2, after the two record entries: 82 19 06 5e 82 82 61 78 18 55 82 61 79 18 55 (tag 85 is float32 little
// endian, RFC 8746 2.1). The vector refers to it with simple(2) = e2 (draft Table 1), so a reader that unpacks
// finds the description in its place. The directory takes index 3, the fillers 4 to 15. The vector is item 16 at
// 61 (3d): d9 80 69 82 e2 and a byte string of 16 bytes. 1.0f is 3f800000, 2.0f is 40000000, 0.5f is 3f000000,
// -1.0f is bf800000 (IEEE 754), low byte first. The bytes are written from the draft and the RFCs by hand.
TEST_CASE("encode: a vector of fixed-size structs is one byte string under tag 32873")
{
    std::string const bytes = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    CHECK_EQ(bytes, "\xd8\x71\x82\x9a\x00\x00\x00\x11\xd8\x72\x81\x66points\xd8\x72\x82\x61x\x61y"
                    "\x82\x19\x06\x5e\x82\x82\x61x\x18\x55\x82\x61y\x18\x55"
                    "\x5a\x00\x00\x00\x04\x00\x00\x00\x3d"
                    "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7"
                    "\xd9\x80\x69\x82\xe2\x5a\x00\x00\x00\x10"
                    "\x00\x00\x80\x3f\x00\x00\x00\x3f\x00\x00\x00\x40\x00\x00\x80\xbf"
                    "\xd9\x06\x5f\xd8\x80\x9a\x00\x00\x00\x01\xc6\x1a\x00\x00\x00\x00"s);
    check_one_item(bytes);
}

// The description names every leaf in the key order of RFC 8949 4.2.1: "at" (62 61 74) before "on" (62 6f 6e),
// "range" before "shade". An inner struct is described in place with its own class tag 1622 = 19 06 56; a
// std::array takes [key, tag, count]. bool takes uint8 (tag 64), the enum color the tag of its underlying uint16
// (69, little endian), int8 the tag 72. An element is at.x, at.y, on, range[0], range[1], shade: 9 bytes. The table
// holds gauges, gauge and sorted_inner, so the description is index 3 and the reference simple(3) = e3.
TEST_CASE("encode: a fixed-size struct with bool, enum, an inner struct and an array")
{
    std::string const bytes = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    CHECK_NE(bytes.find("\x82\x19\x06\x66\x84"
                        "\x82\x62" "at\x82\x19\x06\x56\x82\x82\x61x\x18\x48\x82\x61y\x18\x48"
                        "\x82\x62" "on\x18\x40"
                        "\x83\x65" "range\x18\x45\x02"
                        "\x82\x65" "shade\x18\x45"s),
             std::string::npos);
    CHECK_NE(bytes.find("\xd9\x80\x69\x82\xe3\x5a\x00\x00\x00\x09\x03\xfe\x01\x01\x00\x03\x02\x01\x00"s), std::string::npos);
    check_one_item(bytes);
}

// A variant member is a reference field. Its item is the record of the held alternative, so the class tag of
// the alternative is the head of the item: 1740 = d9 06 cc for dog, 1741 = d9 06 cd for cat.
TEST_CASE("encode: a variant member writes the record of the held alternative as its item")
{
    auto const with_dog = form<home>::encode(home_with_dog());
    REQUIRE(with_dog);
    CHECK_NE(with_dog->find("\xd9\x06\xcc"sv), std::string::npos);
    CHECK_EQ(with_dog->find("\xd9\x06\xcd"sv), std::string::npos);
    auto const with_cat = form<home>::encode(home_with_cat());
    REQUIRE(with_cat);
    CHECK_NE(with_cat->find("\xd9\x06\xcd"sv), std::string::npos);
    CHECK_EQ(with_cat->find("\xd9\x06\xcc"sv), std::string::npos);
}

// A variant that holds no value has no alternative to write. The encoder cannot repair that, so it is a wrong
// use and raises std::logic_error before it writes a byte.
TEST_CASE("encode: a variant that holds no value is a wrong use")
{
    struct boom {
        operator cat() const
        {
            throw std::runtime_error("boom");
        }
    };
    home h = home_with_dog();
    CHECK_THROWS_AS(h.pet.emplace<cat>(boom{}), std::runtime_error);
    REQUIRE(h.pet.valueless_by_exception());
    CHECK_THROWS_AS((void)form<home>::encode(h), std::logic_error);
    std::string out;
    CHECK_THROWS_AS((void)form<home>::encode(h, out), std::logic_error);
    CHECK(out.empty());
}

namespace schema_types
{

// Writes one_value<T>{value}, checks that its item starts with the head that RFC 8949 or RFC 8746 gives, and that
// decode and the accessor give the value back.
template <class T>
void check_value_alternative(T const value, std::string_view const head)
{
    CAPTURE(head.size());
    auto const bytes = form<one_value<T>>::encode(one_value<T>{value});
    REQUIRE(bytes.has_value());
    auto const item = variant_item_of<one_value<T>>(*bytes, form<one_value<T>>::template fixed_size<T>());
    CHECK_EQ(item.substr(0, head.size()), head);
    auto const back = form<one_value<T>>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    REQUIRE_EQ((*back)->value.index(), 0u);
    CHECK(std::get<0>((*back)->value) == value);
    auto const doc = form<one_value<T>>::path(std::string(*bytes));
    REQUIRE(doc.has_value());
    auto const v = doc->template at_path<"$.value">();
    REQUIRE(v.has_value());
    CHECK(v->template get<T>().value() == value);
    CHECK_EQ(v->template get<dog>().error(), error::incorrect_type);
}

} // namespace schema_types

// Each fixed kind as an alternative is the field that encode_value writes, as an item of its own. The expected
// first bytes come from RFC 8949 3.1 and 3.3 (major type and additional information 24 to 27 for a head of
// fixed width), RFC 8949 3.4.3 for tags 2 and 3 (bignum, 16 bytes: 50), and RFC 8746 tag 83 (d8 53) for binary128.
TEST_CASE("encode: each fixed kind as a variant alternative starts with its CBOR head")
{
    check_value_alternative<bool>(false, "\xf4"sv);
    check_value_alternative<bool>(true, "\xf5"sv);
    check_value_alternative<std::uint8_t>(200, "\x18"sv);
    check_value_alternative<std::uint16_t>(60000, "\x19"sv);
    check_value_alternative<std::uint32_t>(4000000000u, "\x1a"sv);
    check_value_alternative<std::uint64_t>(1ull << 60, "\x1b"sv);
    check_value_alternative<std::int8_t>(5, "\x18"sv);
    check_value_alternative<std::int8_t>(-5, "\x38"sv);
    check_value_alternative<std::int16_t>(300, "\x19"sv);
    check_value_alternative<std::int16_t>(-300, "\x39"sv);
    check_value_alternative<std::int32_t>(70000, "\x1a"sv);
    check_value_alternative<std::int32_t>(-70000, "\x3a"sv);
    check_value_alternative<std::int64_t>(1ll << 40, "\x1b"sv);
    check_value_alternative<std::int64_t>(-(1ll << 40), "\x3b"sv);
    check_value_alternative<float>(1.5f, "\xfa"sv);
    check_value_alternative<double>(-2.25, "\xfb"sv);
#if defined(__STDCPP_FLOAT16_T__)
    check_value_alternative<std::float16_t>(0.5f16, "\xf9"sv);
#endif
#if defined(__STDCPP_BFLOAT16_T__)
    check_value_alternative<std::bfloat16_t>(0.5bf16, "\xfa"sv);
#endif
#if defined(__STDCPP_FLOAT128_T__)
    check_value_alternative<std::float128_t>(0.5f128, "\xd8\x53\x50"sv);
    check_value_alternative<long double>(0.5L, "\xd8\x53\x50"sv);
#endif
#ifdef __SIZEOF_INT128__
    check_value_alternative<cbor::int128>(cbor::int128{1} << 100, "\xc2\x50"sv);
    check_value_alternative<cbor::int128>(-(cbor::int128{1} << 100), "\xc3\x50"sv);
    check_value_alternative<cbor::uint128>(cbor::uint128{1} << 120, "\xc2\x50"sv);
#endif
    check_value_alternative<level>(level::high, "\x19"sv);
}

// A text or byte string alternative is the string item that a std::string member writes: head 7a or 5a with a
// 4-byte length, then the bytes. A class alternative is its record, with its class tag first.
TEST_CASE("encode: a text, a byte string and a class as alternatives of one variant")
{
    for (setting const &s : sample_settings()) {
        auto const bytes = form<setting>::encode(s);
        REQUIRE(bytes.has_value());
        if (auto const *t = std::get_if<std::string>(&s.value)) {
            CHECK_EQ(variant_item_of<setting>(*bytes, 5 + t->size()), "\x7a\x00\x00\x00"s + static_cast<char>(t->size()) + *t);
        }
        if (auto const *b = std::get_if<std::vector<std::byte>>(&s.value))
            CHECK_EQ(variant_item_of<setting>(*bytes, 5 + b->size()), "\x5a\x00\x00\x00\x02\x01\x02"sv);
    }
    auto const with_dog = form<shelter>::encode(shelter{dog{{"rex"}, 3}, "n"});
    REQUIRE(with_dog.has_value());
    CHECK_NE(with_dog->find("\xd9\x06\xcc"sv), std::string::npos);
}

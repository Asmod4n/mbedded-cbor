#include "schema_types.hpp"

// Each kind of field has the size that RFC 8949 3 gives its head plus its argument, so a reader finds every
// field at an offset the compiler knows. The expectations are counted from the specification by hand.
TEST_CASE("fixed_size: numbers and simple values have the size of head and argument")
{
    // 0xf4 or 0xf5, one initial byte (RFC 8949 3.3).
    CHECK_EQ(form<wheel>::fixed_size<bool>(), 1u);
    CHECK_EQ(form<wheel>::fixed_size<bool const>(), 1u);
    // An integer of n bytes is 0x18 + log2(n) followed by n bytes (RFC 8949 3).
    CHECK_EQ(form<wheel>::fixed_size<std::uint8_t>(), 2u);
    CHECK_EQ(form<wheel>::fixed_size<std::int16_t>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::uint32_t>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<std::int64_t>(), 9u);
    // An enum has the size of its underlying type.
    CHECK_EQ(form<wheel>::fixed_size<color>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::byte>(), 2u);
#ifdef __SIZEOF_INT128__
    // A 128-bit integer is tag 2 or 3 (one byte) and a byte string of 16 bytes (RFC 8949 3.4.3).
    CHECK_EQ(form<wheel>::fixed_size<cbor::int128>(), 18u);
    CHECK_EQ(form<wheel>::fixed_size<cbor::uint128>(), 18u);
#endif
}

// RFC 8949 3.3 has binary16, binary32 and binary64. binary128 is a typed array of one element under tag 83
// (RFC 8746 2.1): two bytes of tag, one byte of byte string head, 16 bytes. The 80-bit extended precision of
// x87 is widened to binary128, and bfloat16 to binary32, without loss.
TEST_CASE("fixed_size: floats by their digits")
{
#if defined(__STDCPP_FLOAT16_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::float16_t>(), 3u);
#endif
#if defined(__STDCPP_BFLOAT16_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::bfloat16_t>(), 5u);
#endif
    CHECK_EQ(form<wheel>::fixed_size<float>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<double>(), 9u);
#if defined(__STDCPP_FLOAT128_T__)
    CHECK_EQ(form<wheel>::fixed_size<std::float128_t>(), 19u);
#endif
#if defined(__STDCPP_FLOAT128_T__)
    CHECK_EQ(form<wheel>::fixed_size<long double>(), std::numeric_limits<long double>::digits == 53 ? 9u : 19u);
#endif
}

// A text or a byte string of fixed length has its head and its bytes. An array of fixed length has its head and
// its elements, each of fixed size.
TEST_CASE("fixed_size: text, bytes and arrays of fixed length")
{
    CHECK_EQ(form<wheel>::fixed_size<char[32]>(), 34u);
    CHECK_EQ(form<wheel>::fixed_size<char8_t[3]>(), 4u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<char, 24>>(), 26u);
    CHECK_EQ(form<wheel>::fixed_size<std::byte[4]>(), 5u);
    CHECK_EQ(form<wheel>::fixed_size<unsigned char[300]>(), 303u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<std::uint16_t, 4>>(), 13u);
    // std::uint8_t is unsigned char, so a span of it is a byte string.
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint8_t, 2>>(), 3u);
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint16_t, 2>>(), 7u);
    CHECK_EQ(form<wheel>::fixed_size<std::uint32_t[2]>(), 11u);
}

// draft-ietf-cbor-packed-19 4.2: a struct is a record, its values behind a straight reference to the table entry
// that holds its keys. The tag of the class stands outside the reference; tag 1500 is d9 05 dc in preferred
// serialization (RFC 8949 3.4 and 4.1). The reference is d8 80 to d8 87, the value array has a head of 9a and 4
// bytes, so 10 bytes come before the values. wheel: 10, then 3 + 5 + 1.
TEST_CASE("fixed_size: a struct is a record of its values under the tag of its class")
{
    CHECK_EQ(form<wheel>::fixed_size(), 19u);
    CHECK_EQ(form<wheel>::fixed_size<std::array<wheel, 4>>(), 1u + 4u * 19u);
}

// A part of variable size is a shared item in the table of tag 113. The record holds the reference c6 1a <N> or
// c6 3a <N>: tag 6 with an argument of fixed width (draft-ietf-cbor-packed-19 2.2), so 6 bytes.
TEST_CASE("fixed_size: a part of variable size takes 6 bytes")
{
    CHECK_EQ(form<wheel>::fixed_size<std::string>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::vector<wheel>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::map<int, int>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::optional<int>>(), 6u);
    CHECK_EQ(form<wheel>::fixed_size<std::span<std::uint8_t>>(), 6u);
}

// The offset points at the head of a value. The keys are in the table, so the 3 bytes of the class tag and the 7
// bytes of the record head come first. The values follow the key order of RFC 8949 4.2.1, a shorter key first:
// diameter (68 ...) at 10, snowTires (69 ...) behind its 3 bytes at 13, airPressure (6b ...) behind 1 byte at 14.
TEST_CASE("member_offset: the head of each value, behind the class tag and the head of the record")
{
    CHECK_EQ(form<wheel>::member_offset<&wheel::diameter>(), 10u);
    CHECK_EQ(form<wheel>::member_offset<&wheel::snowTires>(), 13u);
    CHECK_EQ(form<wheel>::member_offset<&wheel::airPressure>(), 14u);
    CHECK_EQ(form<wheel const>::member_offset<&wheel::airPressure>(), 14u);
}

// A nested struct is a record at the offset of its member, so offsets add up. car in key order: motor (65 6d ...)
// at 10, seats (65 73 ...) behind the engine record, hasNavSystem (6c ...) behind the 2 bytes of seats. engine in key
// order: cc (62 ...) at 10, horsepower (6a ...) behind its 5 bytes at 15.
TEST_CASE("member_offset: offsets of nested structs add up")
{
    CHECK_EQ(form<car>::member_offset<&car::motor>(), 10u);
    CHECK_EQ(form<engine>::member_offset<&engine::cc>(), 10u);
    CHECK_EQ(form<engine>::member_offset<&engine::horsepower>(), 15u);
    CHECK_EQ(form<car>::member_offset<&car::seats>(), 10u + form<engine>::fixed_size());
    CHECK_EQ(form<car>::member_offset<&car::hasNavSystem>(), 12u + form<engine>::fixed_size());
}

// The count of values has a head of 9a and 4 bytes whatever the count, so a member added later moves no offset, and
// a long name or a name in UTF-8 takes no place in the record.
TEST_CASE("member_offset: the record head has one width for any count and any name")
{
    CHECK_EQ(form<thirty>::member_offset<&thirty::m00>(), 10u);
    CHECK_EQ(form<thirty>::member_offset<&thirty::m29>(), 10u + 29u * 2u);
    CHECK_EQ(form<thirty>::fixed_size(), 10u + 30u * 2u);
    CHECK_EQ(form<long_name>::member_offset<&long_name::b>(), 10u);
    CHECK_EQ(form<long_name>::member_offset<&long_name::a_member_name_of_twenty_nine_b>(), 12u);
    CHECK_EQ(form<empty>::fixed_size(), 10u);
    CHECK_EQ(form<größe>::member_offset<&größe::b>(), 10u);
    CHECK_EQ(form<größe>::member_offset<&größe::höhe>(), 12u);
}

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

// The order holds at every depth: the outer keys are "B" (61 42), "a" (61 61) and "z"
// (61 7a), keys of one length ordered by their bytes; the inner record holds x before y. A decode gives every value
// back to its member, so the order of the bytes never mixes up members.
TEST_CASE("encode and decode: the key order holds in an inner struct")
{
    sorted_outer const value{5, {2, 1}, 4};
    std::string const bytes = schema_bytes(value);
    check_one_item(bytes);
    CHECK_NE(bytes.find("\xd8\x72\x83\x61" "B\x61" "a\x61z"s), std::string::npos);
    CHECK_NE(bytes.find("\xd8\x72\x82\x61x\x61y"s), std::string::npos);
    CHECK_EQ(at_path<sorted_outer, "$.a.y">(bytes), 2);
    CHECK_EQ(at_path<sorted_outer, "$.a.x">(bytes), 1);
    CHECK_EQ(at_path<sorted_outer, "$.B">(bytes), 4);
    auto const back = form<sorted_outer>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->z, 5);
    CHECK_EQ((*back)->a.y, 2);
    CHECK_EQ((*back)->a.x, 1);
    CHECK_EQ((*back)->B, 4);
    auto const unsorted_back = form<unsorted>::decode(schema_bytes(unsorted{3, 1, 2}));
    REQUIRE(unsorted_back.has_value());
    CHECK_EQ((*unsorted_back)->zz, 3);
    CHECK_EQ((*unsorted_back)->b, 1);
    CHECK_EQ((*unsorted_back)->aa, 2);
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

// The simple form: encode gives the bytes, decode gives the whole struct back. Every field takes the value it
// went in with, the lists, the map and the optional included.
TEST_CASE("encode and decode: a struct goes in and comes back whole")
{
    std::string const bytes = *form<garage>::encode(sample_garage);
    auto const decoded_back = *form<garage>::decode(std::string(bytes));
    garage const &back = *decoded_back;
    REQUIRE_EQ(back.tires.size(), 3u);
    CHECK_EQ(back.tires.at(2).diameter, 19u);
    CHECK_EQ(back.tires.at(1).airPressure, 3.0f);
    CHECK_EQ(back.names, sample_garage.names);
    CHECK_EQ(back.rows, sample_garage.rows);
    CHECK_EQ(back.owners, sample_garage.owners);

    std::string const encoded = *form<vehicle>::encode(sample_vehicle);
    auto const decoded_v = *form<vehicle>::decode(std::string(encoded));
    vehicle const &v = *decoded_v;
    CHECK_EQ(v.make, "Tesla");
    CHECK_EQ(v.balance, -7);
    CHECK_EQ(v.spare.at(1).diameter, 16u);
    CHECK_EQ(v.motor.cc, 1800u);
    CHECK_EQ(std::string_view(v.code, 4), "ABCD"sv);
    CHECK_EQ(v.owner, std::optional<std::uint8_t>{9});
    CHECK_FALSE(v.none.has_value());
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

// A decode gives every leaf of every element back, also for no element and one.
TEST_CASE("encode and decode: a fixed-size vector round trip")
{
    for (std::size_t const n : {0uz, 1uz, 3uz}) {
        gauges value;
        for (std::size_t i = 0; i < n; ++i)
            value.g.push_back({i % 2 == 0, i % 2 == 0 ? color::black : color::white,
                               {static_cast<std::int8_t>(-static_cast<int>(i)), static_cast<std::int8_t>(i)},
                               {static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(i * 300)}});
        std::string const bytes = schema_bytes(value);
        check_one_item(bytes);
        auto const back = form<gauges>::decode(std::string(bytes));
        REQUIRE(back.has_value());
        REQUIRE_EQ((*back)->g.size(), n);
        for (std::size_t i = 0; i < n; ++i)
            CHECK(same((*back)->g[i], value.g[i]));
    }
}

// The table index of the description depends on how many record entries come before it. With 14 record entries the
// description is index 14, simple(14) = ee, and no filler is left. With 17 record entries the description is index
// 17, beyond the 16 one-byte references, so the reference is 6(-1) = c6 20 (draft Table 1: 16 - 2N - 1 = 17).
TEST_CASE("encode and decode: the reference to a description at every table index")
{
    fifteen_entries fifteen{};
    fifteen.p = {{1.0f, 2.0f}};
    fifteen.e1.v = 9;
    std::string const a = schema_bytes(fifteen);
    check_one_item(a);
    CHECK_NE(a.find("\xd9\x80\x69\x82\xee\x5a"s), std::string::npos);
    CHECK_EQ(a.find('\xf7'), std::string::npos);
    auto const a_back = form<fifteen_entries>::decode(std::string(a));
    REQUIRE(a_back.has_value());
    REQUIRE_EQ((*a_back)->p.size(), 1u);
    CHECK_EQ((*a_back)->p[0].y, 2.0f);
    CHECK_EQ((*a_back)->e1.v, 9);
    CHECK_EQ(at_path<fifteen_entries, "$.p[0].x">(a), 1.0f);
    eighteen_entries eighteen{};
    eighteen.p = {{3.0f, 4.0f}};
    eighteen.e4.v = -3;
    std::string const b = schema_bytes(eighteen);
    check_one_item(b);
    CHECK_NE(b.find("\xd9\x80\x69\x82\xc6\x20\x5a"s), std::string::npos);
    auto const b_back = form<eighteen_entries>::decode(std::string(b));
    REQUIRE(b_back.has_value());
    CHECK_EQ((*b_back)->p[0].y, 4.0f);
    CHECK_EQ((*b_back)->e4.v, -3);
    CHECK_EQ(at_path<eighteen_entries, "$.p[0].y">(b), 4.0f);
}

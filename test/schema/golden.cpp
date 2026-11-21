#include "types.hpp"

namespace schema_types
{


template <class T>
std::string golden_line(std::string_view const name)
{
    T value{};
    unsigned seed = 0;
    golden_fill(value, seed);
    auto const inside = form<T>::encode(value);
    auto const data = cbor::static_dictionary<T>::encode(value);
    if (!inside || !data)
        return std::string(name) + " no encoding\n";
    auto const description = form<T>::description();
    return std::string(name) + " " + test::hexlify(*inside) + " " + test::hexlify(*data) + " " +
           test::hexlify(std::string_view(description.data(), description.size())) + "\n";
}

template <class = void>
std::string golden_report()
{
    std::string report;
    report += golden_line<wheel>("wheel");
    report += golden_line<engine>("engine");
    report += golden_line<car>("car");
    report += golden_line<empty>("empty");
    report += golden_line<thirty>("thirty");
    report += golden_line<long_name>("long_name");
    report += golden_line<utf8_key>("utf8_key");
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
#ifdef CBOR_HAS_INT128
    report += golden_line<probe>("probe");
#endif
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
#ifdef CBOR_HAS_INT128
    report += golden_line<everything>("everything");
#endif
    return report;
}

template <class = void>
std::string golden_expected_line(std::string_view const name)
{
    std::string_view const all = golden_expected;
    std::string const key = std::string(name) + " ";
    std::size_t first = 0;
    if (!all.starts_with(key)) {
        std::size_t const at = all.find("\n" + key);
        if (at == std::string_view::npos)
            return {};
        first = at + 1;
    }
    std::size_t const last = all.find('\n', first);
    if (last == std::string_view::npos)
        return {};
    return std::string(all.substr(first, last + 1 - first));
}

} // namespace schema_types

// These are the bytes of every described type of this file, with values that differ in each leaf. The library wrote
// them when the root moved to the start of the message. There are three parts: the message with the description inside, the data form
// apart, and the description. A change of the walk that changes one byte fails here.
TEST_CASE("golden bytes: every type of this file encodes as before")
{
    std::string expected(golden_expected);
#if !defined(__STDCPP_FLOAT16_T__)
    std::size_t const at = expected.find("\nnumbers ") + 1;
    expected.erase(at, expected.find('\n', at) + 1 - at);
#endif
    // probe and everything hold an int128; a platform without it writes neither line.
#ifndef CBOR_HAS_INT128
    for (char const *const name : {"\nprobe ", "\neverything "}) {
        std::size_t const line = expected.find(name) + 1;
        expected.erase(line, expected.find('\n', line) + 1 - line);
    }
#endif
    CHECK_EQ(golden_report(), expected);
}

// The bytes of every golden line start as the specifications fix them, whatever the library writes after them. RFC 8949
// 3.4 writes tag 113 as d8 71. RFC 8949 3.1 gives the head 82 to an array of two items. The draft
// draft-ietf-cbor-packed-19 defines tag 113. Tag 113 is an array of the table and the rump. See
// refs/draft-ietf-cbor-packed-19.txt:901. It defines tag 114 as the record function over an array (:1026). The message
// starts d8 71 82. This holds with the description inside and with the data apart. The description starts d8 72 and a
// head of major type 4.
TEST_CASE("golden bytes: each line starts as RFC 8949 and Packed CBOR define it")
{
    constexpr std::string_view packed_message_head = "d87182";
    constexpr std::string_view packed_record_tag = "d872";
    constexpr std::size_t hex_digits_of_two_bytes = 4;
    constexpr char array_major_type_first_hex_digit_low = '8';
    constexpr char array_major_type_first_hex_digit_high = '9';
    std::string_view all = golden_expected;
    while (!all.empty()) {
        std::size_t const end = all.find('\n');
        std::string_view const line = all.substr(0, end);
        all.remove_prefix(end == std::string_view::npos ? all.size() : end + 1);
        std::size_t const first_space = line.find(' ');
        std::size_t const second_space = line.find(' ', first_space + 1);
        std::size_t const third_space = line.find(' ', second_space + 1);
        REQUIRE_NE(third_space, std::string_view::npos);
        CAPTURE(line.substr(0, first_space));
        CHECK(line.substr(first_space + 1).starts_with(packed_message_head));
        CHECK(line.substr(second_space + 1).starts_with(packed_message_head));
        std::string_view const description = line.substr(third_space + 1);
        REQUIRE(description.starts_with(packed_record_tag));
        REQUIRE_GT(description.size(), hex_digits_of_two_bytes + 1);
        char const head_digit = description[hex_digits_of_two_bytes];
        CHECK_GE(head_digit, array_major_type_first_hex_digit_low);
        CHECK_LE(head_digit, array_major_type_first_hex_digit_high);
    }
}

// Each case below checks one type alone. The test above fails for all types at once; these name the type whose bytes
// changed.
// The report above fails for all types at once. This case fails alone when the bytes of wheel change.
TEST_CASE("golden bytes: wheel encodes as before")
{
    CHECK_FALSE(golden_expected_line("wheel").empty());
    CHECK_EQ(golden_line<wheel>("wheel"), golden_expected_line("wheel"));
}

// The report above fails for all types at once. This case fails alone when the bytes of engine change.
TEST_CASE("golden bytes: engine encodes as before")
{
    CHECK_FALSE(golden_expected_line("engine").empty());
    CHECK_EQ(golden_line<engine>("engine"), golden_expected_line("engine"));
}

// The report above fails for all types at once. This case fails alone when the bytes of car change.
TEST_CASE("golden bytes: car encodes as before")
{
    CHECK_FALSE(golden_expected_line("car").empty());
    CHECK_EQ(golden_line<car>("car"), golden_expected_line("car"));
}

// The report above fails for all types at once. This case fails alone when the bytes of empty change.
TEST_CASE("golden bytes: empty encodes as before")
{
    CHECK_FALSE(golden_expected_line("empty").empty());
    CHECK_EQ(golden_line<empty>("empty"), golden_expected_line("empty"));
}

// The report above fails for all types at once. This case fails alone when the bytes of thirty change.
TEST_CASE("golden bytes: thirty encodes as before")
{
    CHECK_FALSE(golden_expected_line("thirty").empty());
    CHECK_EQ(golden_line<thirty>("thirty"), golden_expected_line("thirty"));
}

// The report above fails for all types at once. This case fails alone when the bytes of long_name change.
TEST_CASE("golden bytes: long_name encodes as before")
{
    CHECK_FALSE(golden_expected_line("long_name").empty());
    CHECK_EQ(golden_line<long_name>("long_name"), golden_expected_line("long_name"));
}

// The report above fails for all types at once. This case fails alone when the bytes of utf8_key change.
TEST_CASE("golden bytes: utf8_key encodes as before")
{
    CHECK_FALSE(golden_expected_line("utf8_key").empty());
    CHECK_EQ(golden_line<utf8_key>("utf8_key"), golden_expected_line("utf8_key"));
}

// The report above fails for all types at once. This case fails alone when the bytes of login change.
TEST_CASE("golden bytes: login encodes as before")
{
    CHECK_FALSE(golden_expected_line("login").empty());
    CHECK_EQ(golden_line<login>("login"), golden_expected_line("login"));
}

// The report above fails for all types at once. This case fails alone when the bytes of measures change.
TEST_CASE("golden bytes: measures encodes as before")
{
    CHECK_FALSE(golden_expected_line("measures").empty());
    CHECK_EQ(golden_line<measures>("measures"), golden_expected_line("measures"));
}

// The report above fails for all types at once. This case fails alone when the bytes of named change.
TEST_CASE("golden bytes: named encodes as before")
{
    CHECK_FALSE(golden_expected_line("named").empty());
    CHECK_EQ(golden_line<named>("named"), golden_expected_line("named"));
}

// The report above fails for all types at once. This case fails alone when the bytes of people change.
TEST_CASE("golden bytes: people encodes as before")
{
    CHECK_FALSE(golden_expected_line("people").empty());
    CHECK_EQ(golden_line<people>("people"), golden_expected_line("people"));
}

// The report above fails for all types at once. This case fails alone when the bytes of pmr_named change.
TEST_CASE("golden bytes: pmr_named encodes as before")
{
    CHECK_FALSE(golden_expected_line("pmr_named").empty());
    CHECK_EQ(golden_line<pmr_named>("pmr_named"), golden_expected_line("pmr_named"));
}

// The report above fails for all types at once. This case fails alone when the bytes of pmr_people change.
TEST_CASE("golden bytes: pmr_people encodes as before")
{
    CHECK_FALSE(golden_expected_line("pmr_people").empty());
    CHECK_EQ(golden_line<pmr_people>("pmr_people"), golden_expected_line("pmr_people"));
}

// The report above fails for all types at once. This case fails alone when the bytes of unsorted change.
TEST_CASE("golden bytes: unsorted encodes as before")
{
    CHECK_FALSE(golden_expected_line("unsorted").empty());
    CHECK_EQ(golden_line<unsorted>("unsorted"), golden_expected_line("unsorted"));
}

// The report above fails for all types at once. This case fails alone when the bytes of unsorted_other_order change.
TEST_CASE("golden bytes: unsorted_other_order encodes as before")
{
    CHECK_FALSE(golden_expected_line("unsorted_other_order").empty());
    CHECK_EQ(golden_line<unsorted_other_order>("unsorted_other_order"),
             golden_expected_line("unsorted_other_order"));
}

// The report above fails for all types at once. This case fails alone when the bytes of renamed change.
TEST_CASE("golden bytes: renamed encodes as before")
{
    CHECK_FALSE(golden_expected_line("renamed").empty());
    CHECK_EQ(golden_line<renamed>("renamed"), golden_expected_line("renamed"));
}

// The report above fails for all types at once. This case fails alone when the bytes of listed_other_order change.
TEST_CASE("golden bytes: listed_other_order encodes as before")
{
    CHECK_FALSE(golden_expected_line("listed_other_order").empty());
    CHECK_EQ(golden_line<listed_other_order>("listed_other_order"),
             golden_expected_line("listed_other_order"));
}

// The report above fails for all types at once. This case fails alone when the bytes of sorted_inner change.
TEST_CASE("golden bytes: sorted_inner encodes as before")
{
    CHECK_FALSE(golden_expected_line("sorted_inner").empty());
    CHECK_EQ(golden_line<sorted_inner>("sorted_inner"), golden_expected_line("sorted_inner"));
}

// The report above fails for all types at once. This case fails alone when the bytes of sorted_outer change.
TEST_CASE("golden bytes: sorted_outer encodes as before")
{
    CHECK_FALSE(golden_expected_line("sorted_outer").empty());
    CHECK_EQ(golden_line<sorted_outer>("sorted_outer"), golden_expected_line("sorted_outer"));
}

// The report above fails for all types at once. This case fails alone when the bytes of tire change.
TEST_CASE("golden bytes: tire encodes as before")
{
    CHECK_FALSE(golden_expected_line("tire").empty());
    CHECK_EQ(golden_line<tire>("tire"), golden_expected_line("tire"));
}

// The report above fails for all types at once. This case fails alone when the bytes of vehicle change.
TEST_CASE("golden bytes: vehicle encodes as before")
{
    CHECK_FALSE(golden_expected_line("vehicle").empty());
    CHECK_EQ(golden_line<vehicle>("vehicle"), golden_expected_line("vehicle"));
}

// The report above fails for all types at once. This case fails alone when the bytes of badge change.
TEST_CASE("golden bytes: badge encodes as before")
{
    CHECK_FALSE(golden_expected_line("badge").empty());
    CHECK_EQ(golden_line<badge>("badge"), golden_expected_line("badge"));
}

#if defined(__STDCPP_FLOAT16_T__)
// The report above fails for all types at once. This case fails alone when the bytes of numbers change.
TEST_CASE("golden bytes: numbers encodes as before")
{
    CHECK_FALSE(golden_expected_line("numbers").empty());
    CHECK_EQ(golden_line<numbers>("numbers"), golden_expected_line("numbers"));
}
#endif

// The report above fails for all types at once. This case fails alone when the bytes of doubles change.
TEST_CASE("golden bytes: doubles encodes as before")
{
    CHECK_FALSE(golden_expected_line("doubles").empty());
    CHECK_EQ(golden_line<doubles>("doubles"), golden_expected_line("doubles"));
}

// The report above fails for all types at once. This case fails alone when the bytes of garage change.
TEST_CASE("golden bytes: garage encodes as before")
{
    CHECK_FALSE(golden_expected_line("garage").empty());
    CHECK_EQ(golden_line<garage>("garage"), golden_expected_line("garage"));
}

// The report above fails for all types at once. This case fails alone when the bytes of keyed change.
TEST_CASE("golden bytes: keyed encodes as before")
{
    CHECK_FALSE(golden_expected_line("keyed").empty());
    CHECK_EQ(golden_line<keyed>("keyed"), golden_expected_line("keyed"));
}

// The report above fails for all types at once. This case fails alone when the bytes of passkey_user change.
TEST_CASE("golden bytes: passkey_user encodes as before")
{
    CHECK_FALSE(golden_expected_line("passkey_user").empty());
    CHECK_EQ(golden_line<passkey_user>("passkey_user"), golden_expected_line("passkey_user"));
}

// The report above fails for all types at once. This case fails alone when the bytes of passkey_login change.
TEST_CASE("golden bytes: passkey_login encodes as before")
{
    CHECK_FALSE(golden_expected_line("passkey_login").empty());
    CHECK_EQ(golden_line<passkey_login>("passkey_login"), golden_expected_line("passkey_login"));
}

// The report above fails for all types at once. This case fails alone when the bytes of chain19 change.
TEST_CASE("golden bytes: chain19 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain19").empty());
    CHECK_EQ(golden_line<chain19>("chain19"), golden_expected_line("chain19"));
}

// The report above fails for all types at once. This case fails alone when the bytes of chain10 change.
TEST_CASE("golden bytes: chain10 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain10").empty());
    CHECK_EQ(golden_line<chain10>("chain10"), golden_expected_line("chain10"));
}

// The report above fails for all types at once. This case fails alone when the bytes of chain0 change.
TEST_CASE("golden bytes: chain0 encodes as before")
{
    CHECK_FALSE(golden_expected_line("chain0").empty());
    CHECK_EQ(golden_line<chain0>("chain0"), golden_expected_line("chain0"));
}

// The report above fails for all types at once. This case fails alone when the bytes of point_a change.
TEST_CASE("golden bytes: point_a encodes as before")
{
    CHECK_FALSE(golden_expected_line("point_a").empty());
    CHECK_EQ(golden_line<point_a>("point_a"), golden_expected_line("point_a"));
}

// The report above fails for all types at once. This case fails alone when the bytes of point_b change.
TEST_CASE("golden bytes: point_b encodes as before")
{
    CHECK_FALSE(golden_expected_line("point_b").empty());
    CHECK_EQ(golden_line<point_b>("point_b"), golden_expected_line("point_b"));
}

// The report above fails for all types at once. This case fails alone when the bytes of ticket change.
TEST_CASE("golden bytes: ticket encodes as before")
{
    CHECK_FALSE(golden_expected_line("ticket").empty());
    CHECK_EQ(golden_line<ticket>("ticket"), golden_expected_line("ticket"));
}

// The golden bytes of this type hold an int128 member; a platform without int128 writes an int64 there.
#ifdef CBOR_HAS_INT128
// The report above fails for all types at once. This case fails alone when the bytes of probe change.
TEST_CASE("golden bytes: probe encodes as before")
{
    CHECK_FALSE(golden_expected_line("probe").empty());
    CHECK_EQ(golden_line<probe>("probe"), golden_expected_line("probe"));
}
#endif

// The report above fails for all types at once. This case fails alone when the bytes of lot_wheel change.
TEST_CASE("golden bytes: lot_wheel encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_wheel").empty());
    CHECK_EQ(golden_line<lot_wheel>("lot_wheel"), golden_expected_line("lot_wheel"));
}

// The report above fails for all types at once. This case fails alone when the bytes of lot_engine change.
TEST_CASE("golden bytes: lot_engine encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_engine").empty());
    CHECK_EQ(golden_line<lot_engine>("lot_engine"), golden_expected_line("lot_engine"));
}

// The report above fails for all types at once. This case fails alone when the bytes of lot_car change.
TEST_CASE("golden bytes: lot_car encodes as before")
{
    CHECK_FALSE(golden_expected_line("lot_car").empty());
    CHECK_EQ(golden_line<lot_car>("lot_car"), golden_expected_line("lot_car"));
}

// The report above fails for all types at once. This case fails alone when the bytes of parking_lot change.
TEST_CASE("golden bytes: parking_lot encodes as before")
{
    CHECK_FALSE(golden_expected_line("parking_lot").empty());
    CHECK_EQ(golden_line<parking_lot>("parking_lot"), golden_expected_line("parking_lot"));
}

// The report above fails for all types at once. This case fails alone when the bytes of point change.
TEST_CASE("golden bytes: point encodes as before")
{
    CHECK_FALSE(golden_expected_line("point").empty());
    CHECK_EQ(golden_line<point>("point"), golden_expected_line("point"));
}

// The report above fails for all types at once. This case fails alone when the bytes of cloud change.
TEST_CASE("golden bytes: cloud encodes as before")
{
    CHECK_FALSE(golden_expected_line("cloud").empty());
    CHECK_EQ(golden_line<cloud>("cloud"), golden_expected_line("cloud"));
}

// The report above fails for all types at once. This case fails alone when the bytes of gauge change.
TEST_CASE("golden bytes: gauge encodes as before")
{
    CHECK_FALSE(golden_expected_line("gauge").empty());
    CHECK_EQ(golden_line<gauge>("gauge"), golden_expected_line("gauge"));
}

// The report above fails for all types at once. This case fails alone when the bytes of gauges change.
TEST_CASE("golden bytes: gauges encodes as before")
{
    CHECK_FALSE(golden_expected_line("gauges").empty());
    CHECK_EQ(golden_line<gauges>("gauges"), golden_expected_line("gauges"));
}

// The report above fails for all types at once. This case fails alone when the bytes of filler_type<3> change.
TEST_CASE("golden bytes: filler_type<3> encodes as before")
{
    CHECK_FALSE(golden_expected_line("filler_type<3>").empty());
    CHECK_EQ(golden_line<filler_type<3>>("filler_type<3>"), golden_expected_line("filler_type<3>"));
}

// The report above fails for all types at once. This case fails alone when the bytes of fifteen_entries change.
TEST_CASE("golden bytes: fifteen_entries encodes as before")
{
    CHECK_FALSE(golden_expected_line("fifteen_entries").empty());
    CHECK_EQ(golden_line<fifteen_entries>("fifteen_entries"), golden_expected_line("fifteen_entries"));
}

// The report above fails for all types at once. This case fails alone when the bytes of eighteen_entries change.
TEST_CASE("golden bytes: eighteen_entries encodes as before")
{
    CHECK_FALSE(golden_expected_line("eighteen_entries").empty());
    CHECK_EQ(golden_line<eighteen_entries>("eighteen_entries"), golden_expected_line("eighteen_entries"));
}

// The golden bytes of this type hold an int128 member; a platform without int128 writes an int64 there.
#ifdef CBOR_HAS_INT128
// The report above fails for all types at once. This case fails alone when the bytes of everything change.
TEST_CASE("golden bytes: everything encodes as before")
{
    CHECK_FALSE(golden_expected_line("everything").empty());
    CHECK_EQ(golden_line<everything>("everything"), golden_expected_line("everything"));
}
#endif

// The item of a variant member is the record of the held alternative. draft-ietf-cbor-packed-19 shares a record as an
// item in the same way. The class tag is 1740 (d9 06 cc) or 1741 (d9 06 cd). The straight reference is d8 81 or d8 82.
// It names the record function of the alternative. The values in key order. The reference c6 1a 00 00 00 01 of the root
// names that item, the first item. The test exists so that a change of this layout shows.
TEST_CASE("golden bytes: a variant member encodes as before")
{
    CHECK_EQ(*form<home>::encode(make_home_with_dog()),
             "\xd8\x71\x82\x9a\x00\x00\x00\x15\xd8\x72\x82\x63\x70\x65\x74\x66\x73\x74\x72\x65\x65\x74\xd8\x72\x82\x64"
             "\x62\x61\x72\x6b\x64\x6e\x61\x6d\x65\xd8\x72\x82\x64\x6e\x61\x6d\x65\x64\x74\x6f\x79\x73\xf7\xf7\xf7\xf7"
             "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xd9\x06\xce\xd8\x80\x9a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x01\xc6"
             "\x3a\x00\x00\x00\x01\x5a\x00\x00\x00\x10\x00\x00\x00\x68\x00\x00\x00\x7d\x00\x00\x00\x85\x00\x00\x00\x8d"
             "\xd9\x06\xcc\xd8\x81\x9a\x00\x00\x00\x02\x1a\x00\x00\x00\x03\xc6\x1a\x00\x00\x00\x02\x7a\x00\x00\x00\x03"
             "\x65\x6c\x6d\x7a\x00\x00\x00\x03\x72\x65\x78\xc6\x00"s);
    CHECK_EQ(*form<home>::encode(make_home_with_cat()),
             "\xd8\x71\x82\x9a\x00\x00\x00\x18\xd8\x72\x82\x63\x70\x65\x74\x66\x73\x74\x72\x65\x65\x74\xd8\x72\x82\x64"
             "\x62\x61\x72\x6b\x64\x6e\x61\x6d\x65\xd8\x72\x82\x64\x6e\x61\x6d\x65\x64\x74\x6f\x79\x73\xf7\xf7\xf7\xf7"
             "\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xf7\xd9\x06\xce\xd8\x80\x9a\x00\x00\x00\x02\xc6\x1a\x00\x00\x00\x01\xc6"
             "\x3a\x00\x00\x00\x01\x5a\x00\x00\x00\x1c\x00\x00\x00\x74\x00\x00\x00\x8a\x00\x00\x00\x92\x00\x00\x00\x9a"
             "\x00\x00\x00\xab\x00\x00\x00\xb4\x00\x00\x00\xbe\xd9\x06\xcd\xd8\x82\x9a\x00\x00\x00\x02\xc6\x1a\x00\x00"
             "\x00\x02\xc6\x3a\x00\x00\x00\x02\x7a\x00\x00\x00\x03\x65\x6c\x6d\x7a\x00\x00\x00\x03\x74\x6f\x6d\x9a\x00"
             "\x00\x00\x02\xc6\x1a\x00\x00\x00\x03\xc6\x3a\x00\x00\x00\x03\x7a\x00\x00\x00\x04\x62\x61\x6c\x6c\x7a\x00"
             "\x00\x00\x05\x6d\x6f\x75\x73\x65\xc6\x00"s);
}

#include "types.hpp"

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

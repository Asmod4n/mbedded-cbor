#include "types.hpp"

namespace schema_types
{



template <class T, cbor::fixed_string Path, class... Index>
concept message_path_reads = requires(std::string_view const s, Index const... i) { form<T>::template at_path<Path>(s, i...); };

} // namespace schema_types

// A path over fixed fields becomes one offset at compile time. A text is copied into a std::string, so it cannot
// outlive the bytes it came from.
TEST_CASE("at_path: fixed fields give the value")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.balance">(bytes), -7);
    CHECK_EQ(at_path<vehicle, "$.motor.cc">(bytes), 1800u);
    CHECK_EQ(at_path<vehicle, "$.motor.horsepower">(bytes), 300u);
    CHECK_EQ(at_path<vehicle, "$.spare[1].diameter">(bytes), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[0].airPressure">(bytes), 2.5f);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 1uz), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.code">(bytes), "ABCD"s);
}

// A step over a part of variable size reads an offset and a length from the wire. An empty pair of brackets takes
// its index from the arguments, in the order of the path.
TEST_CASE("at_path: parts of variable size")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.make">(bytes), "Tesla"s);
    CHECK_EQ(at_path<vehicle, "$.wheels[2].diameter">(bytes), 19u);
    CHECK_EQ(at_path<vehicle, "$.wheels[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[3].diameter">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.wheels[].diameter">(bytes, 3uz).error(), error::index_out_of_bounds);
    CHECK_EQ(*at_path<vehicle, "$.owner">(bytes), std::optional<std::uint8_t>{9});
    CHECK_FALSE(at_path<vehicle, "$.none">(bytes)->has_value());
}

// A directory offset from the wire that points at bytes of another kind, or a length past the start of the next
// item, is refused and reads nothing. The directory of vehicle starts behind the prefix and its head 5a.
TEST_CASE("at_path: a broken offset is an error")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const head = bytes.find("\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string backward = bytes;
    backward.replace(head + 5, 4, "\x00\x00\x00\x01"s);
    CHECK_FALSE(at_path<vehicle, "$.make">(backward).has_value());
    std::size_t const make = bytes.find("\x7a\x00\x00\x00\x05Tesla"s);
    REQUIRE_NE(make, std::string::npos);
    std::string past = bytes;
    past.replace(make + 1, 4, "\x00\x00\xff\xff"s);
    CHECK_FALSE(at_path<vehicle, "$.make">(past).has_value());
    CHECK_FALSE(form<vehicle>::decode(std::string(backward)).has_value());
    CHECK_FALSE(form<vehicle>::decode(std::string(past)).has_value());
}

// A fixed-length array of bytes is a byte string laid out inline (RFC 8949 3.1), the same as a fixed text.
// The reader gives the bytes themselves, not an offset and a length read out of them.
TEST_CASE("at_path: a fixed byte array is read inline")
{
    badge const b{{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, shade::light, "x"};
    std::string const bytes = schema_bytes(b);
    CHECK_EQ(at_path<badge, "$.mac">(bytes), "\x01\x02\x03\x04"s);
    CHECK_EQ(at_path<badge, "$.tone">(bytes), shade::light);
    std::string zeros = bytes;
    std::size_t const mac = bytes.size() - form<badge>::fixed_size() + form<badge>::member_offset<&badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(at_path<badge, "$.mac">(zeros), "\x00\x00\x00\x00"s);
}

// RFC 8949 3.1 major type 4: a list is an array. Each element has a fixed size, so element i lies at i times
// that size behind the data, and a path reads it without walking the ones before. A list of lists takes one
// index for each level.
TEST_CASE("at_path: a list of structs, strings and lists, by index")
{
    std::string const bytes = schema_bytes(sample_garage);
    std::uint32_t sum = 0;
    for (std::size_t i = 0;; ++i) {
        auto const d = at_path<garage, "$.tires[].diameter">(bytes, i);
        if (!d) {
            CHECK_EQ(d.error(), error::index_out_of_bounds);
            CHECK_EQ(i, 3u);
            break;
        }
        sum += *d;
    }
    CHECK_EQ(sum, 17u + 18u + 19u);
    CHECK_EQ(at_path<garage, "$.tires[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<garage, "$.names[1]">(bytes), "bc"s);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 2uz, 0uz), 3u);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 0uz, 1uz), 2u);
    CHECK_EQ(at_path<garage, "$.rows[1][0]">(bytes).error(), error::index_out_of_bounds);
}

// A path reads one leaf of one element at an offset the compiler knows, after the vector was checked once.
TEST_CASE("at_path: a leaf of one element of a fixed-size vector")
{
    std::string const bytes = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}});
    CHECK_EQ(at_path<gauges, "$.g[0].on">(bytes), true);
    CHECK_EQ(at_path<gauges, "$.g[1].on">(bytes), false);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(bytes), color::white);
    CHECK_EQ(at_path<gauges, "$.g[0].at.x">(bytes), 3);
    CHECK_EQ(at_path<gauges, "$.g[1].at.y">(bytes), 5);
    CHECK_EQ(at_path<gauges, "$.g[0].range[1]">(bytes), 0x0203u);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 1uz, 0uz), 7u);
    CHECK_EQ(at_path<gauges, "$.g[2].on">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 0uz, 2uz).error(), error::index_out_of_bounds);
    std::string const points = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    CHECK_EQ(at_path<cloud, "$.points[1].y">(points), -1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(points), 1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(schema_bytes(cloud{})).error(), error::index_out_of_bounds);
}

// An accessor of the vector gives its size; an accessor of one element or of its inner struct reads the leaves
// relative to it.
TEST_CASE("at_path: an accessor of a fixed-size vector and of one element")
{
    auto const owner = std::make_shared<std::string const>(
        schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK_EQ(g->size(), 2u);
    CHECK_EQ(g->at_path<"@[1].at.x">(), -6);
    CHECK_EQ(g->at_path<"@[]">(2uz).error(), error::index_out_of_bounds);
    auto const second = g->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK(std::same_as<std::remove_cvref_t<decltype(*second)>, form<gauges>::accessor<cbor::fixed_element<gauge>>>);
    CHECK_EQ(second->at_path<"@.range[0]">(), 7u);
    CHECK_EQ(second->at_path<"@.shade">(), color::black);
    auto const inner = root->at_path<"$.g[0].at">();
    REQUIRE(inner.has_value());
    CHECK_EQ(inner->at_path<"@.y">(), -2);
}

// The bytes of the vector come from the wire. The head of the vector, the description in the header, the length
// of the byte string and every bool are checked before a value is read.
TEST_CASE("at_path and decode: a broken fixed-size vector is an error")
{
    std::string const bytes = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    std::size_t const head = bytes.find("\xd9\x80\x69\x82\xe2\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string reference = bytes;
    reference[head + 4] = '\xe3';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(reference).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(reference)).error(), error::incorrect_type);
    std::string description = bytes;
    description[description.find("\x18\x55"s) + 1] = '\x56';
    CHECK_EQ(form<cloud>::path(std::string(description)).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(description)).error(), error::incorrect_type);
    std::string partial = bytes;
    partial[head + 9] = '\x0f';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(partial).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(form<cloud>::decode(std::string(partial)).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[head + 9] = '\x18';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(longer).error(), error::too_little_data);
    CHECK_EQ(form<cloud>::decode(std::string(longer)).error(), error::too_little_data);
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(two), color::white);
    CHECK_EQ(form<gauges>::decode(std::string(two)).error(), error::incorrect_type);
}

// The accessor of a fixed-size vector is a random access range of element accessors, so a loop, a page through
// std::views::drop and std::views::take, and a count work as with any std range. A page reads only its elements.
TEST_CASE("at_path: a fixed-size vector is a range of its elements")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK(std::ranges::random_access_range<decltype(*g)>);
    CHECK(std::ranges::sized_range<decltype(*g)>);
    std::vector<int> xs;
    for (auto const e : *g)
        xs.push_back(*e.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6, 9}));
    CHECK_EQ(std::ranges::size(*g), 3u);
    auto page = *g | std::views::drop(1) | std::views::take(1);
    CHECK_EQ(std::ranges::distance(page), 1);
    auto const first = *std::ranges::begin(page);
    CHECK_EQ(first.at_path<"@.range[1]">(), 8u);
    auto const third = g->begin()[2];
    CHECK_EQ(third.at_path<"@.on">(), true);
    std::string const empty = schema_bytes(gauges{});
    auto const none = form<gauges>::path(std::string(empty));
    REQUIRE(none.has_value());
    auto const nothing = none->at_path<"$.g">();
    REQUIRE(nothing.has_value());
    CHECK(std::ranges::empty(*nothing));
}

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

template <class T, cbor::fixed_string Path>
concept accessor_path_reads = requires(typename form<T>::template accessor<> const &a) { a.template at_path<Path>(); };

// A path ends at a variant member. The accessor tells which alternative the record holds and gives an
// accessor of that alternative. A path into the variant does not compile, because the member of a variant
// has no single type.
TEST_CASE("at_path: a variant member gives its index and an accessor of the held alternative")
{
    auto const doc = form<home>::path(*form<home>::encode(home_with_dog()));
    REQUIRE(doc);
    auto const pet = doc->at_path<"$.pet">();
    REQUIRE(pet);
    CHECK_EQ(pet->index(), 0u);
    auto const d = pet->get<dog>();
    REQUIRE(d);
    CHECK_EQ(d->at_path<"@.name">().value(), "rex"sv);
    CHECK_EQ(d->at_path<"@.bark">().value(), 3);
    CHECK_EQ(d->at_path<"$.street">().value(), "elm"sv);
    CHECK_EQ(pet->get<cat>().error(), error::incorrect_type);
    CHECK(accessor_path_reads<home, "$.pet">);
    CHECK_FALSE(accessor_path_reads<home, "$.pet.name">);
    CHECK_FALSE(accessor_path_reads<home, "$.pet[0]">);
    CHECK_FALSE(message_path_reads<home, "$.pet">);
}

// An accessor of a variant keeps the message alive as every accessor does, and a moved-from one is a wrong use.
TEST_CASE("at_path: a variant accessor owns the message and refuses a use after a move")
{
    auto pet = [] {
        auto const doc = form<home>::path(*form<home>::encode(home_with_cat()));
        return *doc->at_path<"$.pet">();
    }();
    CHECK_EQ(pet.index(), 1u);
    CHECK_EQ(pet.get<cat>()->at_path<"@.name">().value(), "tom"sv);
    auto moved = std::move(pet);
    CHECK_EQ(moved.index(), 1u);
    CHECK_THROWS_AS((void)pet.index(), std::logic_error);
    CHECK_THROWS_AS((void)pet.get<cat>(), std::logic_error);
}

// get gives a value alternative by value, a text or byte string alternative as a view into the message, and a
// class alternative as an accessor; any other alternative is incorrect_type.
TEST_CASE("at_path: get of a value, a text and a byte string alternative")
{
    for (setting const &s : sample_settings()) {
        auto const doc = form<setting>::path(*form<setting>::encode(s));
        REQUIRE(doc.has_value());
        auto const v = doc->at_path<"$.value">();
        REQUIRE(v.has_value());
        CHECK_EQ(v->index(), s.value.index());
        if (auto const *b = std::get_if<bool>(&s.value))
            CHECK_EQ(v->get<bool>().value(), *b);
        if (auto const *n = std::get_if<std::int32_t>(&s.value))
            CHECK_EQ(v->get<std::int32_t>().value(), *n);
        if (auto const *d = std::get_if<double>(&s.value))
            CHECK_EQ(v->get<double>().value(), *d);
        if (auto const *t = std::get_if<std::string>(&s.value))
            CHECK_EQ(v->get<std::string>().value(), *t);
        if (std::holds_alternative<std::vector<std::byte>>(s.value))
            CHECK_EQ(v->get<std::vector<std::byte>>().value(), "\x01\x02"sv);
        if (s.value.index() != 0)
            CHECK_EQ(v->get<bool>().error(), error::incorrect_type);
    }
    auto const doc = form<shelter>::path(*form<shelter>::encode(shelter{std::int64_t{-7}, "n"}));
    auto const pet = doc->at_path<"$.pet">();
    CHECK_EQ(pet->get<std::int64_t>().value(), -7);
    CHECK_EQ(pet->get<dog>().error(), error::incorrect_type);
    CHECK_FALSE(accessor_path_reads<setting, "$.value.name">);
}

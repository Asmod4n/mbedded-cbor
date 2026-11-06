#include "types.hpp"

namespace schema_types
{



class counting_resource : public std::pmr::memory_resource
{
    void *do_allocate(std::size_t const bytes, std::size_t const alignment) override
    {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *const p, std::size_t const bytes, std::size_t const alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(std::pmr::memory_resource const &other) const noexcept override
    {
        return this == &other;
    }

public:
    std::size_t allocations = 0;
};










class reading
{
public:
    reading(std::uint16_t const v, std::string u) : value(v), unit(std::move(u))
    {
    }

    std::uint16_t value;
    std::string unit;
};

struct reading_plain {
    std::uint16_t value;
    std::string unit;
};

struct station {
    std::vector<reading> readings;
    std::optional<reading> first;
    std::map<std::uint16_t, reading> by_hour;
};

class pmr_reading
{
public:
    explicit pmr_reading(std::pmr::string u) : unit(std::move(u))
    {
    }

    std::pmr::string unit;
};

struct pmr_station {
    std::pmr::vector<pmr_reading> readings;
};
} // namespace schema_types



template <>
struct cbor::describe_members<reading> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1670};
    static constexpr std::tuple members{
        CBOR_MEMBER(reading, value),
        CBOR_MEMBER(reading, unit),
    };
};

template <>
struct cbor::describe_members<reading_plain> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1670};
    static constexpr std::tuple members{
        CBOR_MEMBER(reading_plain, value),
        CBOR_MEMBER(reading_plain, unit),
    };
};

template <>
struct cbor::describe_members<station> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1671};
    static constexpr std::tuple members{
        CBOR_MEMBER(station, readings),
        CBOR_MEMBER(station, first),
        CBOR_MEMBER(station, by_hour),
    };
};

template <>
struct cbor::describe_members<pmr_reading> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1672};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_reading, unit),
    };
};

template <>
struct cbor::describe_members<pmr_station> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1673};
    static constexpr std::tuple members{
        CBOR_MEMBER(pmr_station, readings),
    };
};

// decode and path read every element type back from its typed array. The floats are compared by their bits.
TEST_CASE("decode and at_path: every typed array gives its elements back")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto const back = form<numbers>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    numbers const &n = **back;
    CHECK_EQ(n.u16, sample_numbers.u16);
    CHECK_EQ(n.u32, sample_numbers.u32);
    CHECK_EQ(n.u64, sample_numbers.u64);
    CHECK_EQ(n.s8, sample_numbers.s8);
    CHECK_EQ(n.s16, sample_numbers.s16);
    CHECK_EQ(n.s32, sample_numbers.s32);
    CHECK_EQ(n.s64, sample_numbers.s64);
#if defined(__STDCPP_FLOAT16_T__)
    REQUIRE_EQ(n.f16.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(n.f16[0]), 0x3c00u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(*at_path<numbers, "$.f16[0]">(bytes)), 0x3c00u);
#endif
    REQUIRE_EQ(n.f32.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint32_t>(n.f32[0]), 0x3f800000u);
    REQUIRE_EQ(n.f64.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(n.f64[0]), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.u16[0]">(bytes), 0x0102u);
    CHECK_EQ(at_path<numbers, "$.u32[]">(bytes, 0uz), 0x01020304u);
    CHECK_EQ(at_path<numbers, "$.u64[0]">(bytes), 0x0102030405060708u);
    CHECK_EQ(at_path<numbers, "$.s8[0]">(bytes), std::int8_t{-2});
    CHECK_EQ(at_path<numbers, "$.s16[0]">(bytes), std::int16_t{-2});
    CHECK_EQ(at_path<numbers, "$.s32[0]">(bytes), -2);
    CHECK_EQ(at_path<numbers, "$.s64[]">(bytes, 0uz), std::int64_t{-2});
    CHECK_EQ(std::bit_cast<std::uint32_t>(*at_path<numbers, "$.f32[0]">(bytes)), 0x3f800000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*at_path<numbers, "$.f64[0]">(bytes)), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.f64[1]">(bytes).error(), error::index_out_of_bounds);
    auto const root = form<numbers>::path(std::string(bytes));
    REQUIRE(root.has_value());
    auto const list = root->at_path<"$.u32">();
    REQUIRE(list.has_value());
    CHECK_EQ(list->size(), 1u);
    CHECK_EQ(list->at_path<"@[0]">(), 0x01020304u);
    CHECK_EQ(list->at_path<"@[]">(1uz).error(), error::index_out_of_bounds);
}

// A list of doubles that does not carry tag 86, or whose byte string is not a multiple of eight bytes, is refused
// as an error value by decode and by path. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746).
TEST_CASE("decode and at_path: a typed array with a wrong tag or a wrong length is refused")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    REQUIRE(form<doubles>::decode(std::string(bytes)).has_value());
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(form<doubles>::decode(std::string(wrong)).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(wrong).error(), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(form<doubles>::decode(std::string(odd)).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(at_path<doubles, "$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(form<doubles>::decode(std::string(longer)).error(), error::too_little_data);
    CHECK_EQ(at_path<doubles, "$.v[0]">(longer).error(), error::too_little_data);
}

// A plain struct has no allocator, so a std::pmr::vector cannot give its resource to it, but it can give it to each
// member that takes an allocator, as uses-allocator construction ([allocator.uses.construction]) does for one
// object. The test puts a counting resource as the default resource, so that a member that took the default
// resource shows as a count above zero. Each name is longer than any short string buffer, so each one allocates.
TEST_CASE("decode: a plain struct in a std::pmr::vector takes the resource of the vector for its members")
{
    std::string const long_name(40, 'n');
    std::string const bytes = schema_bytes(pmr_people{{{std::pmr::string(long_name)}, {std::pmr::string(long_name)}}});
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_people>::decode(owner, pmr_people{std::pmr::vector<pmr_named>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).people.size(), 2u);
    for (pmr_named const &p : (**decoded).people) {
        CHECK_EQ(p.n, long_name);
        CHECK_EQ(p.n.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
    CHECK_GT(target.allocations, 0u);
}

// Broken bytes give the error as a value, as with std::expected.
TEST_CASE("decode: an error is read as a value")
{
    std::string const bytes = *form<vehicle>::encode(sample_vehicle);
    std::string_view const cut = std::string_view(bytes).substr(0, 3);
    auto const r = form<vehicle>::decode(std::string(cut));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::too_little_data);
}

// decode(bytes) copies the message once, and every view in the result points into that copy. The message here is a
// temporary that is destroyed at the end of the full expression. The test exists because a view into the bytes of
// the caller would read freed memory, and the address sanitizer reports that read.
TEST_CASE("decode: the views of the result outlive the bytes of the caller")
{
    std::expected<cbor::ref<ticket>, cbor::error> const t = form<ticket>::decode(ticket_bytes());
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
    CHECK_EQ((*t)->row, 12u);
}

// decode(owner) copies nothing and keeps the owner. The caller releases its own pointer to the owner before
// it reads. The test exists because the result alone must keep the bytes alive.
TEST_CASE("decode: the result keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    auto const t = form<ticket>::decode(std::move(owner));
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
}

// The verified decode checks the head of a fixed string and of a std::array as the verified path does. Before,
// only the path checked them, so a forged head gave incorrect_type to a path and a value to decode.
TEST_CASE("decode: the head of a fixed string and of a std::array is checked")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const code = bytes.find("\x64" "ABCD"s);
    REQUIRE_NE(code, std::string::npos);
    std::string text_head = bytes;
    text_head[code] = '\x44';
    CHECK_EQ(at_path<vehicle, "$.code">(text_head).error(), error::incorrect_type);
    CHECK_EQ(form<vehicle>::decode(std::string(text_head)).error(), error::incorrect_type);
    std::string const spare = schema_bytes(sample_everything);
    std::size_t const engines = spare.find("\x82\xd9\x05\xdd"s);
    REQUIRE_NE(engines, std::string::npos);
    std::string array_head = spare;
    array_head[engines] = '\x83';
    CHECK_EQ(at_path<everything, "$.engines[1].cc">(array_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode(std::string(array_head)).error(), error::incorrect_type);
}

// A class with no default constructor is built by its constructor, which takes the described members in the order
// of the description, as mrb_cpp_new of mruby-c-ext-helpers forwards its arguments to new T(...). The wire does not
// show how a class is built, so the same description gives the bytes of an aggregate. The key order (unit before
// value) differs from the order of the description, so a mix-up of the two orders shows here.
TEST_CASE("decode: a class with a constructor takes its described members in the order of the description")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    CHECK_EQ(bytes, schema_bytes(reading_plain{7, "kg"}));
    auto const back = form<reading>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((**back).value, 7u);
    CHECK_EQ((**back).unit, "kg");
    auto const unverified = form<reading>::unverified_decode(std::make_shared<std::string const>(bytes));
    REQUIRE(unverified.has_value());
    CHECK_EQ((**unverified).unit, "kg");
    CHECK_EQ(at_path<reading, "$.unit">(bytes), "kg");
}

// Each place that makes an element builds a class with a constructor the same way: an element of a vector, the
// value of an optional and the value of a map.
TEST_CASE("decode: a class with a constructor in a vector, an optional and a map")
{
    station const full{{reading{1, "a"}, reading{2, "bb"}}, reading{3, "ccc"}, {{9, reading{4, "dddd"}}}};
    std::string const bytes = schema_bytes(full);
    auto const back = form<station>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    REQUIRE_EQ((**back).readings.size(), 2u);
    CHECK_EQ((**back).readings[1].value, 2u);
    CHECK_EQ((**back).readings[1].unit, "bb");
    REQUIRE((**back).first.has_value());
    CHECK_EQ((**back).first->unit, "ccc");
    REQUIRE_EQ((**back).by_hour.size(), 1u);
    CHECK_EQ((**back).by_hour.at(9).unit, "dddd");
    CHECK_EQ(at_path<station, "$.readings[1].unit">(bytes), "bb");
    CHECK_EQ(reencoded(form<station>::decode(std::string(bytes))), bytes);
    auto const empty = form<station>::decode(schema_bytes(station{{}, std::nullopt, {}}));
    REQUIRE(empty.has_value());
    CHECK((**empty).readings.empty());
    CHECK_FALSE((**empty).first.has_value());
}

// The arguments of the constructor are made with the allocator of the vector (std::tuple with std::allocator_arg,
// [tuple.cnstr]), so a std::pmr member keeps the resource of the vector through the move into the class.
TEST_CASE("decode: a class with a constructor in a std::pmr::vector takes the resource of the vector")
{
    std::string const long_unit(40, 'u');
    pmr_station value;
    value.readings.emplace_back(std::pmr::string(long_unit));
    value.readings.emplace_back(std::pmr::string(long_unit));
    std::string const bytes = schema_bytes(value);
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_station>::decode(owner, pmr_station{std::pmr::vector<pmr_reading>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).readings.size(), 2u);
    for (pmr_reading const &r : (**decoded).readings) {
        CHECK_EQ(r.unit, long_unit);
        CHECK_EQ(r.unit.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
}

// The root of a message stands at its end. A message with a missing end gave incorrect_type, because the last bytes
// are not the root. Every verified entry gives too_little_data for each length that cuts the message.
TEST_CASE("a message cut at any length is too_little_data")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    std::string const apart_bytes = *form<byte_fields>::template encode<cbor::description_place::apart>(byte_fields_value());
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    for (std::size_t n = 0; n < whole.size(); ++n) {
        CAPTURE(n);
        std::string const cut = whole.substr(0, n);
        CHECK_EQ(form<byte_fields>::decode(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::decode(std::make_shared<std::string const>(cut), byte_fields{}).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::path(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[1]">(cut).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(cut, 1uz).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CAPTURE(n);
        CHECK_EQ(form<reading>::decode(bytes.substr(0, n)).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < apart_bytes.size(); ++n) {
        CAPTURE(n);
        std::string const cut = apart_bytes.substr(0, n);
        CHECK_EQ(form<byte_fields>::template decode<cbor::description_place::apart>(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::template at_path<"$.list[1]", cbor::description_place::apart>(cut).error(), error::too_little_data);
    }
}

// Only a cut message of the type is too_little_data: a message of another type, a message with a byte after it and
// text that is not CBOR keep their error.
TEST_CASE("a whole message of another type keeps its error")
{
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    CHECK_EQ(form<reading>::decode(std::string(whole)).error(), error::incorrect_type);
    CHECK_EQ(form<byte_fields>::decode(whole + "\x01").error(), error::incorrect_type);
    std::string const text(whole.size(), '{');
    CHECK_EQ(form<byte_fields>::decode(std::string(text)).error(), error::incorrect_type);
}

// The tag of the record chooses the alternative of a variant member. Each alternative comes back with the
// values of its own members and of its base class.
TEST_CASE("decode: a variant member holds the alternative that the tag of its record names")
{
    auto const with_dog = form<home>::decode(*form<home>::encode(home_with_dog()));
    REQUIRE(with_dog);
    REQUIRE(std::holds_alternative<dog>((*with_dog)->pet));
    CHECK_EQ(std::get<dog>((*with_dog)->pet).name, "rex");
    CHECK_EQ(std::get<dog>((*with_dog)->pet).bark, 3);
    CHECK_EQ((*with_dog)->street, "elm");
    auto const with_cat = form<home>::decode(*form<home>::encode(home_with_cat()));
    REQUIRE(with_cat);
    REQUIRE(std::holds_alternative<cat>((*with_cat)->pet));
    CHECK_EQ(std::get<cat>((*with_cat)->pet).name, "tom");
    CHECK_EQ(std::get<cat>((*with_cat)->pet).toys, std::vector<std::string>{"ball", "mouse"});
    CHECK_EQ((*with_cat)->street, "elm");
}

// fox has no default constructor. decode builds it through its constructor from the described members, as it
// builds such a class in any other place.
TEST_CASE("decode: an alternative with no default constructor is built through its constructor")
{
    auto const bytes = form<den>::encode(den{fox{"vix", 7}});
    REQUIRE(bytes);
    auto const back = form<den>::decode(std::string(*bytes));
    REQUIRE(back);
    REQUIRE(std::holds_alternative<fox>((*back)->pet));
    CHECK_EQ(std::get<fox>((*back)->pet).name, "vix");
    CHECK_EQ(std::get<fox>((*back)->pet).speed, 7);
}

// decode with a target replaces the alternative that the target holds with the one of the message.
TEST_CASE("decode: a target that holds another alternative gets the alternative of the message")
{
    auto const bytes = std::make_shared<std::string const>(*form<home>::encode(home_with_cat()));
    auto const back = form<home>::decode(bytes, home_with_dog());
    REQUIRE(back);
    REQUIRE(std::holds_alternative<cat>((*back)->pet));
    CHECK_EQ(std::get<cat>((*back)->pet).toys.size(), 2u);
}

#include "reference.hpp"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

using namespace std::string_literals;

namespace describe_test
{

struct base_id {
    std::uint8_t id = 0;
};

struct empty_base {};

struct aggregate {
    std::uint8_t a;
};

struct aggregate_derived : base_id {
    std::uint8_t a;
};

struct with_constructor {
    with_constructor() = default;
    explicit with_constructor(std::uint8_t const v) : a(v)
    {
    }
    std::uint8_t a = 0;
};

class with_private
{
    std::uint8_t p = 9;

public:
    std::uint8_t a = 0;

    std::uint8_t p_of() const
    {
        return p;
    }
};

class with_protected
{
protected:
    std::uint8_t p = 9;

public:
    std::uint8_t a = 0;

    std::uint8_t p_of() const
    {
        return p;
    }
};

class with_base : public base_id
{
public:
    with_base() = default;
    std::uint8_t a = 0;
};

class with_protected_base : protected base_id
{
public:
    std::uint8_t a = 0;

    std::uint8_t id_of() const
    {
        return id;
    }
};

class with_private_base : private base_id
{
public:
    std::uint8_t a = 0;

    std::uint8_t id_of() const
    {
        return id;
    }
};

struct with_empty_base : empty_base {
    std::uint8_t a = 0;
};

struct with_virtual_base : virtual base_id {
    std::uint8_t a = 0;
};

struct no_default {
    explicit no_default(std::uint8_t const v) : a(v)
    {
    }
    std::uint8_t a;
};

template <int N>
struct key_form {
    std::uint8_t v;
};

template <int N>
struct tag_form {
    std::uint8_t v;
};

struct mixed {
    std::uint8_t text;
    std::uint8_t positive;
    std::uint8_t negative;
};

struct doors {
    std::uint8_t count;
};

struct car {
    describe_test::doors doors;
};

struct nothing {};

struct claims {
    std::string iss;
    std::int64_t exp;
};

struct plain {
    std::uint8_t b;
    std::string aa;
};

using pair_alias = std::pair<std::uint8_t, std::uint8_t>;

class senml_value
{
public:
    senml_value() = default;
    std::string n;
    std::int64_t v = 0;
};

} // namespace describe_test

template <>
struct cbor::describe_members<describe_test::aggregate> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160010};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::aggregate, a),
    };
};

template <>
struct cbor::describe_members<describe_test::aggregate_derived> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160011};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::aggregate_derived, id),
        CBOR_MEMBER(describe_test::aggregate_derived, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_constructor> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160012};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_constructor, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_private> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160013};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_private, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_protected> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160014};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_protected, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_base> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160015};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_base, id),
        CBOR_MEMBER(describe_test::with_base, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_protected_base> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160016};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_protected_base, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_private_base> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160017};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_private_base, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_empty_base> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160018};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_empty_base, a),
    };
};

template <>
struct cbor::describe_members<describe_test::with_virtual_base> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160019};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::with_virtual_base, id),
        CBOR_MEMBER(describe_test::with_virtual_base, a),
    };
};

template <>
struct cbor::describe_members<describe_test::no_default> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160020};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::no_default, a),
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<0>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160030};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::key_form<0>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<1>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160031};
    static constexpr std::tuple members{
        cbor::member{"text", &describe_test::key_form<1>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<2>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160032};
    static constexpr std::tuple members{
        cbor::member{0, &describe_test::key_form<2>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<3>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160033};
    static constexpr std::tuple members{
        cbor::member{23, &describe_test::key_form<3>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<4>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160034};
    static constexpr std::tuple members{
        cbor::member{24, &describe_test::key_form<4>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<5>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160035};
    static constexpr std::tuple members{
        cbor::member{-1, &describe_test::key_form<5>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<6>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160036};
    static constexpr std::tuple members{
        cbor::member{-24, &describe_test::key_form<6>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<7>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160037};
    static constexpr std::tuple members{
        cbor::member{-25, &describe_test::key_form<7>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<8>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160038};
    static constexpr std::tuple members{
        cbor::member{std::numeric_limits<std::uint64_t>::max(), &describe_test::key_form<8>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::key_form<9>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160039};
    static constexpr std::tuple members{
        cbor::member{std::numeric_limits<std::int64_t>::min(), &describe_test::key_form<9>::v},
    };
};

template <>
struct cbor::describe_members<describe_test::mixed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160040};
    static constexpr std::tuple members{
        cbor::member{"a", &describe_test::mixed::text},
        cbor::member{10, &describe_test::mixed::positive},
        cbor::member{-1, &describe_test::mixed::negative},
    };
};

template <>
struct cbor::describe_members<describe_test::doors> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::doors, count),
    };
};

template <>
struct cbor::describe_members<describe_test::car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160002};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::car, doors),
    };
};

template <>
struct cbor::describe_members<describe_test::nothing> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160041};
    static constexpr std::tuple members{};
};

template <>
struct cbor::describe_members<describe_test::claims> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160043};
    static constexpr std::tuple members{
        cbor::member{1, &describe_test::claims::iss},
        cbor::member{4, &describe_test::claims::exp},
    };
};

template <>
struct cbor::describe_members<describe_test::pair_alias> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160042};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::pair_alias, first),
        cbor::member{"s", &describe_test::pair_alias::second},
    };
};

template <>
struct cbor::describe_members<describe_test::senml_value> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &describe_test::senml_value::n},
        cbor::member{2, &describe_test::senml_value::v},
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<0>> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<0>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<1>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{23};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<1>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<2>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{25};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<2>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<3>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{255};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<3>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<4>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{256};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<4>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<5>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{65534};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<5>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<6>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{65536};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<6>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<7>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{4294967294};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<7>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<8>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{4294967296};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<8>, v),
    };
};

template <>
struct cbor::describe_members<describe_test::tag_form<9>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{18446744073709551614u};
    static constexpr std::tuple members{
        CBOR_MEMBER(describe_test::tag_form<9>, v),
    };
};

template <>
struct cbor::describe_members<std::pair<int, long>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160044};
    static constexpr std::tuple members{
        cbor::member{"first", &std::pair<int, long>::first},
        cbor::member{"second", &std::pair<int, long>::second},
    };
};

namespace describe_test
{

template <class T>
void round_trip(std::string const &bytes, T const &value)
{
    auto const written = cbor::structs<T>::encode(value);
    REQUIRE(written.has_value());
    CHECK_EQ(*written, bytes);
    auto const read = cbor::structs<T>::decode(std::string(bytes));
    REQUIRE(read.has_value());
    CHECK_EQ(*cbor::structs<T>::encode(**read), bytes);
}

template <class T>
T with_a(std::uint8_t const a)
{
    T t;
    t.a = a;
    return t;
}

} // namespace describe_test

// Each kind of class that cbor::describe_members takes, written as the tag of the description (RFC 8949 3.4,
// 160010 = 0x0002710a in four bytes, DA) over a map whose keys come in the order of RFC 8949 4.2.1. Every
// kind gives the same bytes on every build, because the description is the only list of members. A member
// that the description leaves out keeps the value of the default constructor, also when the map holds its
// name.
TEST_CASE("describe: every kind of class reads and writes through its description")
{
    using namespace describe_test;
    describe_test::round_trip("\xda\x00\x02\x71\x0a\xa1\x61"
                              "a\x01"s,
                              aggregate{1});
    aggregate_derived derived{};
    derived.id = 2;
    derived.a = 1;
    describe_test::round_trip("\xda\x00\x02\x71\x0b\xa2\x61"
                              "a\x01\x62"
                              "id\x02"s,
                              derived);
    describe_test::round_trip("\xda\x00\x02\x71\x0c\xa1\x61"
                              "a\x05"s,
                              with_constructor(5));
    describe_test::round_trip("\xda\x00\x02\x71\x0d\xa1\x61"
                              "a\x01"s,
                              with_a<with_private>(1));
    auto const not_set = cbor::structs<with_private>::decode("\xda\x00\x02\x71\x0d\xa2\x61"
                                                             "a\x01\x61p\x05"s);
    REQUIRE(not_set.has_value());
    CHECK_EQ((*not_set)->p_of(), 9u);
    describe_test::round_trip("\xda\x00\x02\x71\x0e\xa1\x61"
                              "a\x01"s,
                              with_a<with_protected>(1));
    with_base public_base;
    public_base.id = 2;
    public_base.a = 1;
    describe_test::round_trip("\xda\x00\x02\x71\x0f\xa2\x61"
                              "a\x01\x62"
                              "id\x02"s,
                              public_base);
    describe_test::round_trip("\xda\x00\x02\x71\x10\xa1\x61"
                              "a\x01"s,
                              with_a<with_protected_base>(1));
    describe_test::round_trip("\xda\x00\x02\x71\x11\xa1\x61"
                              "a\x01"s,
                              with_a<with_private_base>(1));
    describe_test::round_trip("\xda\x00\x02\x71\x12\xa1\x61"
                              "a\x01"s,
                              with_a<with_empty_base>(1));
    with_virtual_base virtual_base;
    virtual_base.id = 2;
    virtual_base.a = 1;
    describe_test::round_trip("\xda\x00\x02\x71\x13\xa2\x61"
                              "a\x01\x62"
                              "id\x02"s,
                              virtual_base);
}

// D11: decode constructs T{} and sets the members. A type with no default constructor is written as any
// other, and it is read only into an object that the caller gives; decode(shared_ptr) of it does not compile
// (compile_fail_structs_no_default_constructor).
TEST_CASE("describe: a type with no default constructor decodes into an object of the caller")
{
    using namespace describe_test;
    std::string const bytes = "\xda\x00\x02\x71\x14\xa1\x61"
                              "a\x07"s;
    CHECK_EQ(*cbor::structs<no_default>::encode(no_default(7)), bytes);
    auto const read =
        cbor::structs<no_default>::decode(std::make_shared<std::string const>(bytes), no_default(0));
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->a, 7u);
}

// Each key form of an entry, written from RFC 8949 3: the identifier as text, an explicit text, and integers
// at the edges of the additional information (23 in the initial byte, 24 in one more byte; -24 and -25
// likewise), 2^64-1 with eight bytes of major type 0, and -2^63 as major type 1 with the argument 2^63-1.
TEST_CASE("describe: every key form of an entry")
{
    using namespace describe_test;
    describe_test::round_trip("\xda\x00\x02\x71\x1e\xa1\x61v\x01"s, key_form<0>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x1f\xa1\x64text\x01"s, key_form<1>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x20\xa1\x00\x01"s, key_form<2>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x21\xa1\x17\x01"s, key_form<3>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x22\xa1\x18\x18\x01"s, key_form<4>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x23\xa1\x20\x01"s, key_form<5>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x24\xa1\x37\x01"s, key_form<6>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x25\xa1\x38\x18\x01"s, key_form<7>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x26\xa1\x1b\xff\xff\xff\xff\xff\xff\xff\xff\x01"s,
                              key_form<8>{1});
    describe_test::round_trip("\xda\x00\x02\x71\x27\xa1\x3b\x7f\xff\xff\xff\xff\xff\xff\xff\x01"s,
                              key_form<9>{1});
}

// Every width of the tag head of RFC 8949 3 (major type 6, C0): no tag, 23 in the initial byte (d7), 25 and
// 255 in one more byte (d8), 256 and 65534 in two (d9), 65536 and 2^32-2 in four (da), 2^32 and 2^64-2 in
// eight (db). The top of each width (65535, 2^32-1, 2^64-1) is a tag that the IANA registry calls always
// invalid, so the value one below stands for it. Tag 24 takes one more byte too, but RFC 8949 3.4.5.1 gives
// it to the encoded data item, so 25 shows that width.
TEST_CASE("describe: every width of the tag of a description")
{
    using namespace describe_test;
    describe_test::round_trip("\xa1\x61v\x01"s, tag_form<0>{1});
    describe_test::round_trip("\xd7\xa1\x61v\x01"s, tag_form<1>{1});
    describe_test::round_trip("\xd8\x19\xa1\x61v\x01"s, tag_form<2>{1});
    describe_test::round_trip("\xd8\xff\xa1\x61v\x01"s, tag_form<3>{1});
    describe_test::round_trip("\xd9\x01\x00\xa1\x61v\x01"s, tag_form<4>{1});
    describe_test::round_trip("\xd9\xff\xfe\xa1\x61v\x01"s, tag_form<5>{1});
    describe_test::round_trip("\xda\x00\x01\x00\x00\xa1\x61v\x01"s, tag_form<6>{1});
    describe_test::round_trip("\xda\xff\xff\xff\xfe\xa1\x61v\x01"s, tag_form<7>{1});
    describe_test::round_trip("\xdb\x00\x00\x00\x01\x00\x00\x00\x00\xa1\x61v\x01"s, tag_form<8>{1});
    describe_test::round_trip("\xdb\xff\xff\xff\xff\xff\xff\xff\xfe\xa1\x61v\x01"s, tag_form<9>{1});
}

// A description needs no macro, so it names a type whose name holds a comma directly, with no alias. The
// tag is 160044 = 0x0002712c; "first" (65) comes before "second" (66) by RFC 8949 4.2.1.
TEST_CASE("describe: a description of a type whose name holds a comma")
{
    describe_test::round_trip("\xda\x00\x02\x71\x2c\xa2\x65"
                              "first\x01\x66"
                              "second\x02"s,
                              std::pair<int, long>{1, 2});
}

// RFC 8949 4.2.1 sorts the keys by their encoded bytes, across kinds: 10 (0a) before -1 (20) before "a" (61
// 61). The order of the description differs on purpose.
TEST_CASE("describe: keys of three kinds come out in the order of RFC 8949 4.2.1")
{
    describe_test::round_trip("\xda\x00\x02\x71\x28\xa3\x0a\x02\x20\x03\x61"
                              "a\x01"s,
                              describe_test::mixed{1, 2, 3});
}

// The owner example: doors carries tag 160001 and car tag 160002; a struct inside a struct carries its own
// tag.
TEST_CASE("describe: a described struct inside a described struct carries its own tag")
{
    describe_test::round_trip("\xda\x00\x02\x71\x02\xa1\x65"
                              "doors\xda\x00\x02\x71\x01\xa1\x65"
                              "count\x04"s,
                              describe_test::car{{4}});
}

// D9: a key that no entry has is skipped. D8: a member that is not std::optional and is absent gives
// key_not_found. A described struct reads tag(map) only: another tag, and a map with no tag, give
// incorrect_type.
TEST_CASE("describe: decode of an unknown key, a missing key, a wrong tag and no tag")
{
    using describe_test::doors;
    auto const unknown = cbor::structs<doors>::decode("\xda\x00\x02\x71\x01\xa2\x65"
                                                      "count\x04\x61x\xf5"s);
    REQUIRE(unknown.has_value());
    CHECK_EQ((*unknown)->count, 4u);
    CHECK_EQ(cbor::structs<doors>::decode("\xda\x00\x02\x71\x01\xa0"s).error(), cbor::error::key_not_found);
    CHECK_EQ(cbor::structs<doors>::decode("\xda\x00\x02\x71\x03\xa1\x65"
                                          "count\x04"s)
                 .error(),
             cbor::error::incorrect_type);
    CHECK_EQ(cbor::structs<doors>::decode("\xa1\x65"
                                          "count\x04"s)
                 .error(),
             cbor::error::incorrect_type);
}

// RFC 8392 3: "all claims that are not understood by implementations MUST be ignored", and mruby-cbor reads
// only the members it lists (test.rb:624-638). The claims iss (1) and exp (4) of RFC 8392 3.1 are listed; an
// unknown claim of each kind of key that RFC 8949 allows in a map of a struct - an integer, a negative
// integer, a text, a byte string and a simple value - is skipped and not read.
TEST_CASE("describe: a key that no entry has is skipped")
{
    auto const read =
        cbor::structs<describe_test::claims>::decode("\xda\x00\x02\x71\x2b\xa7\x01\x61"
                                                     "a\x02\x82\x01\x02\x20\xa0\x61x\xf6\x41\x00\x01"
                                                     "\xf5\x80\x04\x1a\x56\x12\xae\xb0"s);
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->iss, "a");
    CHECK_EQ((*read)->exp, 1444064944);
}

// RFC 8949 5.6: a repeated key in a map of a described struct gives duplicate_key, as the owner decided on
// 2026-10-14 for every eager decode.
TEST_CASE("describe: a repeated key of a member is duplicate_key")
{
    CHECK_EQ(cbor::structs<describe_test::doors>::decode("\xda\x00\x02\x71\x01\xa2\x65"
                                                         "count\x04\x65"
                                                         "count\x05"s)
                 .error(),
             cbor::error::duplicate_key);
}

// A description with no entry is a tag over an empty map. A type alias names a type whose name holds a comma,
// here std::pair, which is no aggregate and is read through its public members.
TEST_CASE("describe: a description with no entry and a description through a type alias")
{
    describe_test::round_trip("\xda\x00\x02\x71\x29\xa0"s, describe_test::nothing{});
    describe_test::round_trip("\xda\x00\x02\x71\x2a\xa2\x61s\x02\x65"
                              "first\x01"s,
                              describe_test::pair_alias{1, 2});
}

// D16: an aggregate with no description is read by its member names, through reflection on gcc and through
// Boost.PFR elsewhere. Both give these bytes, written from RFC 8949 4.2.1: "b" (61 62) before "aa" (62 61
// 61).
TEST_CASE("describe: an aggregate with no description gives the same bytes on every build")
{
    describe_test::round_trip("\xa2\x61"
                              "b\x01\x62"
                              "aa\x61x"s,
                              describe_test::plain{1, "x"});
}

// RFC 9052 2 calls a message with no CBOR tag an untagged message. SenML (RFC 8428 6), CTAP2 and the COSE
// header are such messages with integer keys. A description with cbor::untagged writes the map with no tag in
// front, and a decode refuses a tag in front of the map, because the type has no tag that the tag could name.
TEST_CASE("describe: cbor::untagged writes and reads a map with no tag")
{
    describe_test::senml_value v;
    v.n = "t";
    v.v = 5;
    describe_test::round_trip("\xa2\x00\x61t\x02\x05"s, v);
    auto const read = cbor::structs<describe_test::senml_value>::decode("\xa2\x02\x05\x00\x61t"s);
    REQUIRE(read.has_value());
    CHECK_EQ((*read)->n, "t");
    CHECK_EQ((*read)->v, 5);
    CHECK_EQ(cbor::structs<describe_test::senml_value>::decode("\xda\x00\x02\x71\x0a\xa2\x00\x61t\x02\x05"s)
                 .error(),
             cbor::error::incorrect_type);
}

namespace define_type_test
{

struct point {
    std::uint8_t x;
    std::uint8_t y;
};

template <class A, class B>
struct two {
    A a;
    B b;
};

using two_ints = two<int, int>;

class sample
{
public:
    sample(std::uint8_t const v, std::string n) : value(v), name(std::move(n))
    {
    }

    std::uint8_t value;
    std::string name;
};

struct renamed {
    std::uint8_t user_id;
};

} // namespace define_type_test

namespace
{

struct hidden {
    std::uint8_t h;
};

} // namespace

CBOR_DEFINE_TYPE(define_type_test::point, cbor::tag{160100}, CBOR_MEMBER(define_type_test::point, x),
                 CBOR_MEMBER(define_type_test::point, y));
CBOR_DEFINE_TYPE(define_type_test::two_ints, cbor::tag{160101}, CBOR_MEMBER(define_type_test::two_ints, a),
                 CBOR_MEMBER(define_type_test::two_ints, b));
CBOR_DEFINE_TYPE(define_type_test::sample, cbor::tag{160102}, CBOR_MEMBER(define_type_test::sample, value),
                 CBOR_MEMBER(define_type_test::sample, name));
CBOR_DEFINE_TYPE(define_type_test::renamed, cbor::untagged,
                 cbor::member{cbor::map_key("user-id"), &define_type_test::renamed::user_id});
CBOR_DEFINE_TYPE(hidden, cbor::tag{160103}, CBOR_MEMBER(hidden, h));

// CBOR_DEFINE_TYPE writes the specialization of cbor::describe_members that a user writes by hand: the tag and a
// std::tuple of the members, so cbor::structs and cbor::schema read it alike. The tag 160100 is 0x00027164 (RFC
// 8949 3.4); "x" (61 78) comes before "y" (61 79) by RFC 8949 4.2.1.
TEST_CASE("CBOR_DEFINE_TYPE: the macro writes the description that structs and schema read")
{
    CHECK(std::is_same_v<std::remove_cvref_t<decltype(cbor::describe_members<define_type_test::point>::tag)>,
                         std::optional<cbor::tag>>);
    CHECK_EQ(cbor::describe_members<define_type_test::point>::tag->number, 160100u);
    describe_test::round_trip("\xda\x00\x02\x71\x64\xa2\x61x\x01\x61y\x02"s, define_type_test::point{1, 2});
    auto const bytes = cbor::schema<define_type_test::point>::encode(define_type_test::point{1, 2});
    REQUIRE(bytes.has_value());
    auto const back = cbor::schema<define_type_test::point>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((**back).y, 2u);
}

// A type whose name holds a comma passes through an alias, because the preprocessor splits macro arguments at
// each comma outside parentheses. The explicit specialization needs no alias.
TEST_CASE("CBOR_DEFINE_TYPE: a template instance through an alias")
{
    describe_test::round_trip("\xda\x00\x02\x71\x65\xa2\x61"
                              "a\x01\x61"
                              "b\x02"s,
                              define_type_test::two_ints{1, 2});
}

// A rename is an explicit cbor::member, a class with a constructor takes the described members in their order, a
// type in an anonymous namespace is described at global scope as any other.
TEST_CASE("CBOR_DEFINE_TYPE: a rename, a class with a constructor and a type in an anonymous namespace")
{
    describe_test::round_trip("\xa1\x67user-id\x05"s, define_type_test::renamed{5});
    auto const bytes = cbor::schema<define_type_test::sample>::encode(define_type_test::sample{3, "abc"});
    REQUIRE(bytes.has_value());
    auto const back = cbor::schema<define_type_test::sample>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((**back).value, 3u);
    CHECK_EQ((**back).name, "abc");
    describe_test::round_trip("\xda\x00\x02\x71\x67\xa1\x61h\x07"s, hidden{7});
}


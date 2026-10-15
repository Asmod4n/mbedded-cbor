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

struct plain {
    std::uint8_t b;
    std::string aa;
};

using pair_alias = std::pair<std::uint8_t, std::uint8_t>;

} // namespace describe_test

CBOR_DESCRIBE_STRUCT(describe_test::aggregate, (160010, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::aggregate_derived, (160011, 0), (id), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_constructor, (160012, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_private, (160013, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_protected, (160014, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_base, (160015, 0), (id), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_protected_base, (160016, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_private_base, (160017, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_empty_base, (160018, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::with_virtual_base, (160019, 0), (id), (a))
CBOR_DESCRIBE_STRUCT(describe_test::no_default, (160020, 0), (a))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<0>, (160030, 0), (v))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<1>, (160031, 0), (v, "text"))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<2>, (160032, 0), (v, 0))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<3>, (160033, 0), (v, 23))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<4>, (160034, 0), (v, 24))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<5>, (160035, 0), (v, -1))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<6>, (160036, 0), (v, -24))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<7>, (160037, 0), (v, -25))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<8>, (160038, 0), (v, std::numeric_limits<std::uint64_t>::max()))
CBOR_DESCRIBE_STRUCT(describe_test::key_form<9>, (160039, 0), (v, std::numeric_limits<std::int64_t>::min()))
CBOR_DESCRIBE_STRUCT(describe_test::mixed, (160040, 0), (text, "a"), (positive, 10), (negative, -1))
CBOR_DESCRIBE_STRUCT(describe_test::doors, (160001, 0), (count))
CBOR_DESCRIBE_STRUCT(describe_test::car, (160002, 0), (doors))
CBOR_DESCRIBE_STRUCT(describe_test::nothing, (160041, 0))
CBOR_DESCRIBE_STRUCT(describe_test::pair_alias, (160042, 0), (first), (second, "s"))

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

// Each kind of class that CBOR_DESCRIBE_STRUCT takes, written as the tag of the description (RFC 8949 3.4,
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

// RFC 8949 4.2.1 sorts the keys by their encoded bytes, across kinds: 10 (0a) before -1 (20) before "a" (61
// 61). The order of the macro differs on purpose.
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

#include "reference.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

template <class T>
void round_trip(std::string const &bytes, T const &expected)
{
    auto const back = cbor::structs<T>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK(**back == expected);
    auto const again = cbor::structs<T>::encode(expected);
    REQUIRE(again.has_value());
    CHECK_EQ(*again, bytes);
}

struct pair_ab {
    std::uint64_t a;
    std::vector<std::uint64_t> b;
    bool operator==(pair_ab const &) const = default;
};

// RFC 8428 6: SenML in CBOR names its labels with integers: bn -2, bt -3, n 0, u 1, v 2, t 6.
struct senml_record {
    std::optional<std::string> bn;
    std::optional<double> bt;
    std::optional<std::string> n;
    std::optional<std::string> u;
    std::optional<double> v;
    std::optional<std::int64_t> t;
    static constexpr std::array<std::int64_t, 6> keys{-2, -3, 0, 1, 2, 6};
    bool operator==(senml_record const &) const = default;
};

struct cose_header {
    std::optional<std::span<std::byte const>> kid;
    static constexpr std::array<std::int64_t, 1> keys{4};
};

// RFC 9052 4.2: COSE_Sign1 is tag 18 on an array of protected header, unprotected header, payload and signature.
using cose_sign1 = cbor::tagged<18, std::tuple<std::span<std::byte const>, cose_header, std::span<std::byte const>,
                                               std::span<std::byte const>>>;

struct credential {
    std::span<std::byte const> id;
    std::string_view type;
};

struct user_entity {
    std::span<std::byte const> id;
};

// CTAP 2.1 6.2.2: the response of authenticatorGetAssertion is a map with the keys 1 to 4.
struct get_assertion_response {
    credential credential_id;
    std::span<std::byte const> auth_data;
    std::span<std::byte const> signature;
    std::optional<user_entity> user;
    static constexpr std::array<std::int64_t, 4> keys{1, 2, 3, 4};
};

struct text_order {
    int aa;
    int b;
    bool operator==(text_order const &) const = default;
};

struct integer_order {
    int first;
    int second;
    int third;
    static constexpr std::array<std::int64_t, 3> keys{24, -1, 1};
    bool operator==(integer_order const &) const = default;
};

} // namespace

// The expectations are the examples of RFC 8949 Appendix A, each read into a C++ type and written back.
TEST_CASE("structs: the examples of RFC 8949 Appendix A")
{
    round_trip<std::uint64_t>("\x00"s, 0);
    round_trip<std::uint64_t>("\x17"s, 23);
    round_trip<std::uint64_t>("\x18\x18"s, 24);
    round_trip<std::uint64_t>("\x18\x64"s, 100);
    round_trip<std::uint64_t>("\x1a\x00\x0f\x42\x40"s, 1000000);
    round_trip<std::uint64_t>("\x1b\xff\xff\xff\xff\xff\xff\xff\xff"s, 18446744073709551615u);
    round_trip<std::int64_t>("\x20"s, -1);
    round_trip<std::int64_t>("\x39\x03\xe7"s, -1000);
    round_trip<double>("\xf9\x3e\x00"s, 1.5);
    round_trip<double>("\xfa\x47\xc3\x50\x00"s, 100000.0);
    round_trip<double>("\xfb\x3f\xf1\x99\x99\x99\x99\x99\x9a"s, 1.1);
    round_trip<bool>("\xf4"s, false);
    round_trip<bool>("\xf5"s, true);
    round_trip<std::optional<std::uint64_t>>("\xf6"s, std::nullopt);
    round_trip<std::string>("\x64IETF"s, "IETF");
    round_trip<std::vector<std::byte>>("\x44\x01\x02\x03\x04"s,
                                       {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    round_trip<std::tuple<std::uint64_t, std::array<std::uint64_t, 2>, std::vector<std::uint64_t>>>(
        "\x83\x01\x82\x02\x03\x82\x04\x05"s, {1, {2, 3}, {4, 5}});
    round_trip<pair_ab>("\xa2\x61\x61\x01\x61\x62\x82\x02\x03"s, {1, {2, 3}});
    round_trip<std::map<std::string, std::string>>("\xa2\x61\x61\x61\x41\x61\x62\x61\x42"s,
                                                   {{"a", "A"}, {"b", "B"}});
}

// A map key that the struct does not name is skipped, and a key that a member needs and the map lacks is an
// error; an optional member may be absent.
TEST_CASE("structs: an unknown key is skipped, a missing key is an error")
{
    auto const extra = cbor::structs<pair_ab>::decode("\xa3\x61\x63\x82\x01\x02\x61\x61\x01\x61\x62\x80"s);
    REQUIRE(extra.has_value());
    CHECK_EQ((*extra)->a, 1u);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa1\x61\x61\x01"s).error(), error::key_not_found);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa2\x61\x61\x61\x78\x61\x62\x80"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x19\x01\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(cbor::structs<std::uint64_t>::decode("\x00\x00"s).error(), error::syntax_error);
}

// RFC 8949 5.6 names two ways for a repeated key; the owner decided on 2026-10-14 that an eager decode takes
// the first: it rejects the map with duplicate_key. A struct member took the first entry with no error before
// this test changed, and the last one before that. A key that no member has is not read, so it is no
// duplicate. A std::map takes the first entry of a repeated key.
TEST_CASE("structs: a repeated key of a member is duplicate_key")
{
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa3\x61\x61\x01\x61\x61\x02\x61\x62\x80"s).error(),
             error::duplicate_key);
    auto const map =
        cbor::structs<std::map<std::string, std::string>>::decode("\xa2\x61\x61\x61\x41\x61\x61\x61\x42"s);
    REQUIRE(map.has_value());
    CHECK_EQ((*map)->at("a"), "A");
    CHECK(cbor::structs<pair_ab>::decode("\xa4\x61\x61\x01\x61\x62\x80\x61x\x01\x61x\x02"s).has_value());
    auto const widths = cbor::structs<std::map<std::uint64_t, std::uint64_t>>::decode("\xa2\x01\x00\x18\x01\x05"s);
    REQUIRE(widths.has_value());
    CHECK_EQ((*widths)->size(), 1u);
    CHECK_EQ((*widths)->at(1), 0u);
}

// RFC 8428 6, the CBOR form of the example in 5.1.2: the integer labels come from the keys of the struct.
TEST_CASE("structs: a SenML pack with integer labels")
{
    std::vector<senml_record> const pack{
        {"urn:dev:ow:10e2073a01080063:", 1.320067464e9, "voltage", "V", 120.1, std::nullopt},
        {std::nullopt, std::nullopt, "current", "A", 1.2, -5}};
    auto const bytes = cbor::structs<std::vector<senml_record>>::encode(pack);
    REQUIRE(bytes.has_value());
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(0)), 0x82);
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(1)), 0xa5);
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(2)), 0x00);
    round_trip(*bytes, pack);
}

// RFC 8392 A.3 carries a COSE_Sign1 under tag 18 with a kid in the unprotected header. A moved string becomes the
// owner; decode(owner, bytes) copies nothing and keeps the owner.
TEST_CASE("structs: a COSE_Sign1 under tag 18 reads as views into the message")
{
    std::string const message = "\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s;
    auto const sign1 = cbor::structs<cose_sign1>::decode(std::string(message));
    REQUIRE(sign1.has_value());
    auto const &[protected_header, unprotected, payload, signature] = (*sign1)->content;
    CHECK_EQ(protected_header.size(), 3u);
    REQUIRE(unprotected.kid.has_value());
    CHECK_EQ(static_cast<char>(unprotected.kid->front()), '1');
    CHECK_EQ(payload.size(), 1u);
    CHECK_EQ(signature.size(), 2u);
    CHECK_NE(reinterpret_cast<char const *>(payload.data()), message.data() + 12);
    auto const owner = std::make_shared<std::string const>(message);
    auto const held = cbor::structs<cose_sign1>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(reinterpret_cast<char const *>(std::get<2>((*held)->content).data()), owner->data() + 12);
    CHECK_EQ(*cbor::structs<cose_sign1>::encode(**sign1), message);
}

// Owner decision: structs::decode has the view form decode(std::shared_ptr<std::string const>). It copies nothing
// and keeps the owner.
TEST_CASE("structs: the view form reads the bytes of the owner")
{
    auto const owner = std::make_shared<std::string const>(
        "\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s);
    auto const held = cbor::structs<cose_sign1>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(reinterpret_cast<char const *>(std::get<2>((*held)->content).data()), owner->data() + 12);
    CHECK_THROWS_AS((void)cbor::structs<cose_sign1>::decode(std::shared_ptr<std::string const>()), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. An owner beside separate bytes does not
// compile, because the owner may not hold them. The test exists because each of these let a view outlive its bytes,
// and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("structs: decode checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s);
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o, std::string(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o, std::string_view(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o); }; }(bytes)));
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, bytes.get());
    CHECK_THROWS_AS((void)cbor::structs<cose_sign1>::decode(holds_nothing), std::logic_error);
    std::shared_ptr<std::string const> const holds_bytes(std::shared_ptr<void const>(bytes), bytes.get());
    auto const held = cbor::structs<cose_sign1>::decode(holds_bytes);
    REQUIRE(held.has_value());
    CHECK_EQ(reinterpret_cast<char const *>(std::get<2>((*held)->content).data()), bytes->data() + 12);
}

// CTAP 2.1 6.2.2: a nested struct under an integer key, text keys inside it, and an absent optional user.
TEST_CASE("structs: a CTAP2 getAssertion response")
{
    std::string const message =
        "\xa3\x01\xa2\x62\x69\x64\x42\x0a\x0b\x64\x74\x79\x70\x65\x6a\x70\x75\x62\x6c\x69\x63\x2d\x6b\x65\x79"
        "\x02\x41\x25\x03\x41\x30"s;
    auto const r = cbor::structs<get_assertion_response>::decode(std::string(message));
    REQUIRE(r.has_value());
    CHECK_EQ((*r)->credential_id.type, "public-key"sv);
    CHECK_EQ((*r)->credential_id.id.size(), 2u);
    CHECK_FALSE((*r)->user.has_value());
    CHECK_EQ(*cbor::structs<get_assertion_response>::encode(**r), message);
    std::array<char, 64> room{};
    auto const placed = cbor::structs<get_assertion_response>::encode(**r, std::span(room));
    REQUIRE(placed.has_value());
    CHECK_EQ(std::string_view(room.data(), *placed), message);
    std::vector<char> tight(message.size());
    auto const exact = cbor::structs<get_assertion_response>::encode(**r, std::span(tight));
    REQUIRE(exact.has_value());
    CHECK_EQ(std::string_view(tight.data(), *exact), message);
    std::array<char, 8> small{};
    CHECK_EQ(cbor::structs<get_assertion_response>::encode(**r, std::span(small)).error(), cbor::error::no_buffer_space);
    std::string text = "x";
    CHECK_EQ(*cbor::structs<get_assertion_response>::encode(**r, text), message.size());
    CHECK_EQ(text, "x" + message);
}

// RFC 8949 Appendix A: 2^64 and -2^64-1 need a bignum, tag 2 or 3 on the magnitude; simple(16) and simple(255)
// are simple values of one and two bytes.
TEST_CASE("structs: bignums and simple values of RFC 8949 Appendix A")
{
#ifdef __SIZEOF_INT128__
    round_trip<cbor::uint128>("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s,
                                  static_cast<cbor::uint128>(1) << 64);
    round_trip<cbor::int128>("\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s,
                         -1 - (static_cast<cbor::int128>(1) << 64));
    round_trip<cbor::int128>("\x39\x03\xe7"s, -1000);
#endif
    round_trip<cbor::simple_value>("\xf0"s, static_cast<cbor::simple_value>(16));
    round_trip<cbor::simple_value>("\xf8\xff"s, static_cast<cbor::simple_value>(255));
    CHECK_EQ(cbor::structs<cbor::simple_value>::decode("\xf8\x10"s).error(), error::syntax_error);
}

// A std::variant takes the first alternative whose kind matches the item, so one array can hold several kinds,
// as RFC 8949 allows.
TEST_CASE("structs: a variant takes the alternative that matches the item")
{
    using any = std::variant<std::uint64_t, std::int64_t, std::string, double, bool, std::nullptr_t,
                             std::vector<std::byte>>;
    std::vector<any> const values{std::uint64_t{1}, std::int64_t{-1}, std::string("a"), 1.5, true, nullptr,
                                  std::vector<std::byte>{std::byte{7}}};
    round_trip<std::vector<any>>("\x87\x01\x20\x61\x61\xf9\x3e\x00\xf5\xf6\x41\x07"s, values);
    CHECK_EQ(cbor::structs<std::variant<std::string, bool>>::decode("\x01"s).error(), error::incorrect_type);
}

namespace
{

struct renamed {
    std::uint8_t m0;
    bool m1;
};

} // namespace

CBOR_DESCRIBE_STRUCT(renamed, (160050, 0), (m0, "x-user-id"), (m1, "EOF"))

// A generator gives each member a fixed name and carries the key of the other language in the description, so
// a key need not be a C++ identifier and never meets a macro such as EOF. The map comes from RFC 8949 3.1 by
// hand, under the tag of the description, 160050 = 0x00027132.
TEST_CASE("structs: the description gives the key of a member")
{
    std::string const bytes = "\xda\x00\x02\x71\x32\xa2\x69x-user-id\x07\x63"
                              "EOF\xf5"s;
    auto const back = cbor::structs<renamed>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->m0, 7u);
    CHECK((*back)->m1);
    CHECK_EQ(*cbor::structs<renamed>::encode(renamed{7, true}), "\xda\x00\x02\x71\x32\xa2\x63"
                                                                "EOF\xf5\x69x-user-id\x07"s);
    CHECK_EQ(cbor::structs<renamed>::decode("\xda\x00\x02\x71\x32\xa2\x62m0\x07\x62m1\xf5"s).error(),
             error::key_not_found);
}

namespace
{

class account {
public:
    std::string name;
    std::uint8_t id = 0;

    void hidden_set(std::uint8_t const s, std::uint8_t const k)
    {
        secret = s;
        kept = k;
    }

    std::uint8_t secret_of() const
    {
        return secret;
    }

    std::uint8_t kept_of() const
    {
        return kept;
    }

protected:
    std::uint8_t kept = 0;

private:
    std::uint8_t secret = 0;
};

} // namespace

CBOR_DESCRIBE_STRUCT(account, (160051, 0), (name), (id))

// A private or protected member is state the class keeps to itself. It never goes on the wire, and a key of
// its name on the wire is an unknown key, so the bytes of a sender cannot set it. A class with private
// members is no aggregate, so it is read through its description, under its tag 160051 = 0x00027133.
TEST_CASE("structs: only public members are written and read")
{
    account a;
    a.name = "ann";
    a.id = 7;
    a.hidden_set(42, 9);
    std::string const bytes = *cbor::structs<account>::encode(a);
    CHECK_EQ(bytes, "\xda\x00\x02\x71\x33\xa2\x62id\x07\x64name\x63"
                    "ann"s);
    auto const back = cbor::structs<account>::decode("\xda\x00\x02\x71\x33\xa4\x64name\x63"
                                                     "bob\x62id\x05\x66secret\x18\x2a\x64kept\x09"s);
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->name, "bob");
    CHECK_EQ((*back)->id, 5u);
    CHECK_EQ((*back)->secret_of(), 0u);
    CHECK_EQ((*back)->kept_of(), 0u);
}

namespace
{

struct named_pair {
    std::vector<std::string> b;
    std::uint64_t a;
    bool operator==(named_pair const &) const = default;
};

} // namespace

// The owner decided on 2026-10-13 that an eager decode keeps one table of the tags 28 of one call. A tag 28 on a
// plain member is allowed and keeps the value. A tag 29 needs a member that can share a value; no plain member
// can, so it gives incorrect_type. A tag 29 to a mark in a skipped member gives sharedref_not_complete, a forward
// index sharedref_index_not_marked, and a tag 28 or 29 on a key unsupported_value.
TEST_CASE("structs: the table of tags 28 and 29 of one decode")
{
    auto const marked = cbor::structs<pair_ab>::decode("\xa2\x61\x61\xd8\x1c\x02\x61\x62\x81\xd8\x1c\x03"s);
    REQUIRE(marked.has_value());
    CHECK(**marked == pair_ab{2, {3}});
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa2\xd8\x1c\x61\x61\xd8\x1c\x01\x61\x62\x82\xd8\x1d\x01\xd8\x1d\x01"s).error(),
             error::unsupported_value);
    CHECK_EQ(cbor::structs<named_pair>::decode("\xa2\x61\x62\x81\xd8\x1c\x61\x61\xd8\x1d\x00\x07"s).error(),
             error::unsupported_value);
    CHECK_EQ(cbor::structs<std::vector<std::uint64_t>>::decode("\x81\xd8\x1d\x00"s).error(), error::sharedref_index_not_marked);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa3\x61\x63\xd8\x1c\x00\x61\x61\x02\x61\x62\x81\xd8\x1d\x00"s).error(),
             error::sharedref_not_complete);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa3\x61\x63\xd8\x1c\x00\x61\x61\xd8\x1c\x02\x61\x62\x81\xd8\x1d\x01"s).error(),
             error::incorrect_type);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa2\x61\x61\x02\x61\x62\x81\xd8\x1d\x18\x1d"s).error(),
             error::sharedref_index_not_marked);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa2\x61\x61\x02\x61\x62\x81\xd8\x1d\x61x"s).error(),
             error::inadmissible_type_for_tag_content);
}

template <class Encoded>
concept decodable = requires(Encoded &&e) { cbor::structs<std::string_view>::decode(std::forward<Encoded>(e)); };

// The value category of the bytes says what decode does, as for packed::decode and lazy::copy. A moved std::string
// becomes the owner and is not copied: the view points into the buffer that was moved. Every other form is copied
// once, so no view points into bytes that nothing keeps alive.
TEST_CASE("structs: decode takes a moved string as the owner and copies every other form")
{
    std::string const text(40, 'x');
    std::string const message = "\x78\x28"s + text;
    auto buffer = std::make_unique<std::string>(message);
    char const *const data = buffer->data();
    auto const moved = cbor::structs<std::string_view>::decode(std::move(*buffer));
    REQUIRE(moved.has_value());
    CHECK_EQ(static_cast<void const *>((*moved)->data()), static_cast<void const *>(data + 2));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(**moved, text);

    CHECK(decodable<std::string>);
    CHECK(decodable<std::string &>);
    CHECK(decodable<std::string const &>);
    CHECK(decodable<std::string const>);
    CHECK(decodable<char const *>);
    CHECK(decodable<std::string_view>);

    std::string lvalue = message;
    auto const copied = cbor::structs<std::string_view>::decode(lvalue);
    REQUIRE(copied.has_value());
    CHECK_NE(static_cast<void const *>((*copied)->data()), static_cast<void const *>(lvalue.data() + 2));
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(**copied, text);

    auto const owner = std::make_shared<std::string const>(message);
    auto const held = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>((*held)->data()), static_cast<void const *>(owner->data() + 2));
}

struct float_member {
    float f;
};

// A double that a float cannot hold exactly gives number_out_of_range, as an integer that does not fit does.
// Before the fix 1e300 and DBL_MAX read as inf and 0.1 lost its precision, all with no error.
TEST_CASE("structs: a double that a float cannot hold is number_out_of_range")
{
    auto const huge = cbor::structs<float>::decode("\xfb\x7e\x37\xe4\x3c\x88\x00\x75\x9c"s);
    REQUIRE_FALSE(huge.has_value());
    CHECK_EQ(huge.error(), error::number_out_of_range);
    auto const max = cbor::structs<float_member>::decode("\xa1\x61"
                                                          "f\xfb\x7f\xef\xff\xff\xff\xff\xff\xff"s);
    REQUIRE_FALSE(max.has_value());
    CHECK_EQ(max.error(), error::number_out_of_range);
    auto const tenth = cbor::structs<float>::decode("\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a"s);
    REQUIRE_FALSE(tenth.has_value());
    CHECK_EQ(tenth.error(), error::number_out_of_range);
    auto const payload = cbor::structs<float>::decode("\xfb\x7f\xf8\x00\x00\x00\x00\x00\x01"s);
    REQUIRE_FALSE(payload.has_value());
    CHECK_EQ(payload.error(), error::number_out_of_range);

    auto const exact = cbor::structs<float_member>::decode("\xa1\x61"
                                                            "f\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"s);
    REQUIRE(exact.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>((*exact)->f), 0x3fc00000u);
    auto const infinite = cbor::structs<float>::decode("\xfb\xff\xf0\x00\x00\x00\x00\x00\x00"s);
    REQUIRE(infinite.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>(**infinite), 0xff800000u);
    auto const nan = cbor::structs<float>::decode("\xfb\xff\xf0\x00\x00\x20\x00\x00\x00"s);
    REQUIRE(nan.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>(**nan), 0xff800001u);
    auto const half = cbor::structs<float>::decode("\xf9\x7c\x01"s);
    REQUIRE(half.has_value());
    CHECK_EQ(std::bit_cast<std::uint32_t>(**half), 0x7f802000u);
}

struct standard_containers {
    std::list<int> l;
    std::deque<int> q;
    std::vector<bool> b;
    std::set<int> s;
    std::multiset<int> m;
    std::multimap<int, int> mm;
    bool operator==(standard_containers const &) const = default;
};

// decode reads every standard container that encode writes. Before the fix std::list, std::deque, std::set
// and std::vector<bool> encoded but did not compile in decode: the branch tested push_back and called reserve
// and emplace_back, and a std::set fell through to the struct reader.
TEST_CASE("structs: every standard container that encode writes decodes")
{
    round_trip("\x82\xf5\xf4"s, std::vector<bool>{true, false});
    round_trip("\x82\x01\x02"s, std::set<int>{1, 2});
    round_trip("\x83\x01\x02\x03"s, std::list<int>{1, 2, 3});
    round_trip("\x82\x01\x02"s, std::deque<int>{1, 2});
    round_trip("\xa6\x61"
               "b\x81\xf5\x61l\x82\x01\x02\x61m\x82\x06\x06\x61q\x81\x03\x61s\x82\x04\x05\x62mm\xa2\x01\x02\x01\x03"s,
               standard_containers{{1, 2}, {3}, {true}, {4, 5}, {6, 6}, {{1, 2}, {1, 3}}});
}

template <class T>
struct bounded_allocator {
    using value_type = T;
    static constexpr std::size_t limit = std::size_t{1} << 21;
    bounded_allocator() = default;
    template <class U>
    constexpr bounded_allocator(bounded_allocator<U> const &) noexcept
    {
    }
    T *allocate(std::size_t const n)
    {
        if (n > limit / sizeof(T))
            throw std::bad_alloc();
        return std::allocator<T>{}.allocate(n);
    }
    void deallocate(T *const p, std::size_t const n) noexcept
    {
        std::allocator<T>{}.deallocate(p, n);
    }
    bool operator==(bounded_allocator const &) const = default;
};

struct large_elements {
    std::vector<std::array<std::uint64_t, 512>, bounded_allocator<std::array<std::uint64_t, 512>>> v;
};

// An array head of 2^20 elements before 1 MiB of input made decode reserve 2^20 elements of 4096 bytes, 4
// GiB, and std::bad_alloc escaped decode. The reserve is now at most what the remaining bytes hold, so the
// allocator that refuses more than 2 MiB is not reached, and the read ends at the first element that is no
// array. A std::bad_alloc that does happen inside decode comes back as not_enough_memory.
TEST_CASE("structs: decode reserves no more than the remaining bytes and returns std::bad_alloc as an error")
{
    std::string message = "\xa1\x61v\x9a\x00\x10\x00\x00"s;
    message += std::string(std::size_t{1} << 20, '\xf6');
    auto const r = cbor::structs<large_elements>::decode(std::move(message));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::incorrect_type);

    std::string many = "\xa1\x61v\x9a\x00\x00\x02\x01"s;
    std::string const element = "\x99\x02\x00"s + std::string(512, '\x00');
    for (int i = 0; i < 0x201; ++i)
        many += element;
    auto const full = cbor::structs<large_elements>::decode(std::move(many));
    REQUIRE_FALSE(full.has_value());
    CHECK_EQ(full.error(), error::not_enough_memory);
}

// An array head of 2^20 elements before 4 MiB of input made decode reserve min(2^20, 4 MiB / 4096) = 1024
// elements of 4096 bytes before the first element was read. The allocator refuses more than 512 such
// elements, so the decode ended with not_enough_memory. Memory now grows only with the elements that were
// read, and the first element, a null, ends the read with incorrect_type.
TEST_CASE("structs: decode allocates nothing for the count of an array head")
{
    std::string message = "\xa1\x61v\x9a\x00\x10\x00\x00"s;
    message += std::string(std::size_t{1} << 22, '\xf6');
    auto const r = cbor::structs<large_elements>::decode(std::move(message));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::incorrect_type);
}

struct one_int {
    int a;
};

// A key is read with the same well-formedness checks as a value: f8 10 is simple value 16 in two bytes, which
// RFC 8949 3.3 forbids. Before the fix the key reader accepted it.
TEST_CASE("structs: a map key that is not well-formed is a syntax error")
{
    auto const r = cbor::structs<one_int>::decode("\xa2\xf8\x10\x00\x61"
                                                   "a\x05"s);
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::syntax_error);
    auto const alone = cbor::structs<cbor::simple_value>::decode("\xf8\x10"s);
    REQUIRE_FALSE(alone.has_value());
    CHECK_EQ(alone.error(), error::syntax_error);
}

// A std::pmr target that a caller names was copied into the parameter
// of decode, and the copy took the default resource. The decode then
// ignored the limit of the caller's resource. The target is now taken
// only as an rvalue, so the named form does not compile, and the moved
// form keeps the resource.
template <class T>
concept decodes_named_target = requires(std::shared_ptr<std::string const> owner, T target) {
    cbor::structs<T>::decode(owner, target);
};

TEST_CASE("structs decode keeps the resource of a moved pmr target")
{
    using texts = std::pmr::vector<std::pmr::string>;
    CHECK((!decodes_named_target<texts>));

    std::vector<std::string> const value(3, std::string(40, 'x'));
    auto const encoded = cbor::structs<std::vector<std::string>>::encode(value);
    REQUIRE(encoded.has_value());
    auto const owner = std::make_shared<std::string const>(*encoded);
    std::array<std::byte, 64> buffer;
    std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
    texts target(&budget);
    auto const decoded = cbor::structs<texts>::decode(owner, std::move(target));
    REQUIRE_FALSE(decoded.has_value());
    CHECK_EQ(decoded.error(), error::not_enough_memory);
}

// A resource that counts the allocations it gives, so that a test sees which resource a member took.
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

struct pmr_person {
    std::pmr::string name;
    std::uint64_t age;
    bool operator==(pmr_person const &) const = default;
};

// The caller bounds the memory of a decode with the resource of the target. A plain struct has no allocator, so
// the vector cannot give its resource to the struct, but it can give it to each member that takes an allocator,
// as uses-allocator construction ([allocator.uses.construction]) does for one object. The test puts a counting
// resource as the default resource, so that a member that took the default resource shows as a count above zero.
// Each name is longer than any short string buffer, so each one allocates.
TEST_CASE("structs: a plain struct in a std::pmr::vector takes the resource of the vector for its members")
{
    using people = std::pmr::vector<pmr_person>;
    std::string const long_name(40, 'n');
    auto const encoded = cbor::structs<people>::encode(people{{std::pmr::string(long_name), 1}, {std::pmr::string(long_name), 2}});
    REQUIRE(encoded.has_value());
    counting_resource fallback;
    counting_resource target;
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded =
        cbor::structs<people>::decode(std::make_shared<std::string const>(*encoded), people(&target));
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).size(), 2u);
    for (pmr_person const &p : **decoded) {
        CHECK_EQ(p.name, long_name);
        CHECK_EQ(p.name.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
    CHECK_GT(target.allocations, 0u);
}

// RFC 8949 4.2.1: deterministic encoding orders the keys of a map by the bytewise lexicographic order
// of their encoded forms. A shorter text key has a shorter head, so "b" (61 62) comes before "aa"
// (62 61 61); an unsigned integer comes before a negative one, 1 (01) and 24 (18 18) before -1 (20).
// The declaration order differs on purpose.
TEST_CASE("structs: keys come out in the deterministic order of RFC 8949 4.2.1")
{
    auto const t = cbor::structs<text_order>::encode(text_order{1, 2});
    REQUIRE(t.has_value());
    CHECK_EQ(*t, "\xa2\x61\x62\x02\x62\x61\x61\x01"sv);
    auto const i = cbor::structs<integer_order>::encode(integer_order{10, 20, 30});
    REQUIRE(i.has_value());
    CHECK_EQ(*i, "\xa3\x01\x18\x1e\x18\x18\x0a\x20\x14"sv);
    auto const back = cbor::structs<integer_order>::decode(std::string(*i));
    REQUIRE(back.has_value());
    CHECK(**back == integer_order{10, 20, 30});
}

// RFC 8949 3.4.3 and 5.6.1: a key written as a bignum that fits 64 bits is the integer key of the same value, so
// structs finds each member of integer_order under 2(h'18') for 24, 3(h'00') for -1 and 2(h'0001') for 1. A
// bignum key that does not fit 64 bits names no member and is skipped.
TEST_CASE("structs: an integer key written as a bignum that fits")
{
    auto const back = cbor::structs<integer_order>::decode(
        "\xa4\xc2\x41\x18\x0a\xc3\x41\x00\x14\xc2\x42\x00\x01\x18\x1e\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"s);
    REQUIRE(back.has_value());
    CHECK(**back == integer_order{10, 20, 30});
}



// A tag 29 into a member that is no shared pointer gives incorrect_type, also inside the value that its mark is still
// reading. Nothing is decoded twice. A forward index names no mark. A tag 28 counts one level of nesting, as in a
// tree decode.
TEST_CASE("structs: a tag 29 into a plain member is incorrect_type")
{
    using numbers = std::vector<std::uint64_t>;
    CHECK_EQ(cbor::structs<std::vector<numbers>>::decode("\xd8\x1c\x81\xd8\x1d\x00"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::map<std::uint64_t, numbers>>::decode("\xd8\x1c\xa1\x01\xd8\x1d\x00"s).error(),
             error::incorrect_type);
    CHECK_EQ(cbor::structs<std::vector<std::vector<numbers>>>::decode(
                 "\xd8\x1c\x81\xd8\x1c\x82\xd8\x1d\x00\xd8\x1d\x01"s)
                 .error(),
             error::incorrect_type);
    CHECK_EQ(cbor::structs<std::uint64_t>::decode("\xd8\x1c\xd8\x1d\x00"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<numbers>::decode("\x82\xd8\x1d\x00\xd8\x1c\x01"s).error(), error::sharedref_index_not_marked);
    CHECK_EQ(cbor::structs<std::vector<numbers>>::decode("\x83\xd8\x1c\x82\x01\x02\xd8\x1d\x00\xd8\x1d\x00"s).error(),
             error::incorrect_type);
    test::limits_guard const depth{{.nesting_depth = 16}};
    std::string chain;
    for (int i = 0; i < 16; ++i)
        chain += "\xd8\x1c"s;
    CHECK_EQ(cbor::structs<std::uint64_t>::decode(chain + "\x05"s), 5u);
    CHECK_EQ(cbor::structs<std::uint64_t>::decode("\xd8\x1c"s + chain + "\x05"s).error(), error::nesting_depth_exceeded);
}

#include "rfc8746.hpp"
#include "reference.hpp"
#include <cstddef>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/encode.hpp>
#include <cbor/schema.hpp>
#include <cbor/structs.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <deque>
#if __has_include(<flat_map>)
#include <flat_map>
#endif
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
#include <type_traits>
#include <unordered_map>
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
    CHECK((**back == expected));
    auto const again = cbor::encode<T>(expected);
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
    bool operator==(senml_record const &) const = default;
};

struct cose_header {
    std::optional<std::span<std::byte const>> kid;
};

// RFC 9052 4.2: COSE_Sign1 is tag 18 on an array of protected header, unprotected header, payload and signature.
using cose_sign1 = cbor::tagged_item<18, std::tuple<std::span<std::byte const>, cose_header, std::span<std::byte const>,
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
};

struct text_order {
    int aa;
    int b;
    bool operator==(text_order const &) const = default;
};

struct long_text_order {
    int aaaaaaaaaaaaaaaaaaaaaaaa;
    int bbbbbbbbbbbbbbbbbbbbbbb;
    bool operator==(long_text_order const &) const = default;
};

struct integer_order {
    int first;
    int second;
    int third;
    bool operator==(integer_order const &) const = default;
};

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(user_entity, cbor::untagged, id);

CBOR_DEFINE_TYPE_MEMBERS(credential, cbor::untagged, id, type);

CBOR_DEFINE_TYPE_MEMBERS(pair_ab, cbor::untagged, a, b);
CBOR_DEFINE_TYPE_MEMBERS(text_order, cbor::untagged, aa, b);
CBOR_DEFINE_TYPE_MEMBERS(long_text_order, cbor::untagged, aaaaaaaaaaaaaaaaaaaaaaaa, bbbbbbbbbbbbbbbbbbbbbbb);

template <>
struct cbor::describe_members<senml_record> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{-2, &senml_record::bn},
        cbor::member{-3, &senml_record::bt},
        cbor::member{0, &senml_record::n},
        cbor::member{1, &senml_record::u},
        cbor::member{2, &senml_record::v},
        cbor::member{6, &senml_record::t},
    };
};

template <>
struct cbor::describe_members<cose_header> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{4, &cose_header::kid},
    };
};

template <>
struct cbor::describe_members<get_assertion_response> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{1, &get_assertion_response::credential_id},
        cbor::member{2, &get_assertion_response::auth_data},
        cbor::member{3, &get_assertion_response::signature},
        cbor::member{4, &get_assertion_response::user},
    };
};

template <>
struct cbor::describe_members<integer_order> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{24, &integer_order::first},
        cbor::member{-1, &integer_order::second},
        cbor::member{1, &integer_order::third},
    };
};

// The expectations are the examples of RFC 8949 Appendix A, each read into a C++ type and written back. A vector or an
// array of numbers is an RFC 8746 typed array since the owner decision of 2026-10-15. Tag 64 is for bytes. Tag 71 is for
// uint64 little endian.
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
    round_trip<std::vector<std::byte>>("\xd8\x40\x44\x01\x02\x03\x04"s,
                                       {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}});
    round_trip<std::tuple<std::uint64_t, std::array<std::uint64_t, 2>, std::vector<std::uint64_t>>>(
        "\x83\x01\xd8\x47\x50\x02\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\xd8\x47\x50\x04\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00"s,
        {1, {2, 3}, {4, 5}});
    round_trip<pair_ab>(
        "\xa2\x61\x61\x01\x61\x62\xd8\x47\x50\x02\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00"s,
        {1, {2, 3}});
    round_trip<std::map<std::string, std::string>>("\xa2\x61\x61\x61\x41\x61\x62\x61\x42"s,
                                                   {{"a", "A"}, {"b", "B"}});
}

// A map key that the struct does not name is skipped. A key that a member needs and the map lacks is an error; an
// optional member may be absent.
TEST_CASE("structs: an unknown key is skipped, a missing key is an error")
{
    auto const extra =
        cbor::structs<pair_ab>::decode("\xa3\x61\x63\x82\x01\x02\x61\x61\x01\x61\x62\xd8\x47\x40"s);
    REQUIRE(extra.has_value());
    CHECK_EQ((*extra)->a, 1u);
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa1\x61\x61\x01"s), std::unexpected(error::key_not_found));
    CHECK_EQ(cbor::structs<pair_ab>::decode("\xa2\x61\x61\x61\x78\x61\x62\x80"s), std::unexpected(error::incorrect_type));
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x19\x01\x00"s), std::unexpected(error::number_out_of_range));
    auto const zero = cbor::structs<std::uint64_t>::decode("\x00\x00"s);
    REQUIRE(zero.has_value());
    CHECK_EQ(**zero, 0u);
}

struct inner_ab {
    std::uint64_t a;
    std::optional<std::uint64_t> b;
    bool operator==(inner_ab const &) const = default;
};

struct outer_m {
    inner_ab m;
    bool operator==(outer_m const &) const = default;
};

// RFC 8949 5.6, the first way: a decoder that does not accept maps with duplicate keys. The owner decided on
// 2026-11-16 that structs takes this way when the check costs nothing measurable. A key that no member has is not
// read, so it is no duplicate. The key 1 in one byte and the key 1 in two bytes are the same key (RFC 8949 5.6).
TEST_CASE("structs: a repeated key is duplicate_key")
{
    auto edn_bytes = cbor::diag2cbor(R"({"a": 1, "a": 2, "b": 71(h'')})");
    CHECK_EQ(cbor::structs<pair_ab>::decode(std::move(edn_bytes.value())),
             std::unexpected(error::duplicate_key));
    auto edn_bytes_2 = cbor::diag2cbor(R"({"m": {"a": 1, "b": 2}, "m": {"a": 3}})");
    CHECK_EQ(cbor::structs<outer_m>::decode(std::move(edn_bytes_2.value())),
             std::unexpected(error::duplicate_key));
    auto edn_bytes_3 = cbor::diag2cbor(R"({"a": "A", "a": "B"})");
    CHECK_EQ(cbor::structs<std::map<std::string, std::string>>::decode(std::move(edn_bytes_3.value())),
             std::unexpected(error::duplicate_key));
    auto edn_bytes_4 = cbor::diag2cbor(R"({"a": 1, "b": 71(h''), "x": 1, "x": 2})");
    CHECK(cbor::structs<pair_ab>::decode(std::move(edn_bytes_4.value())).has_value());
    auto edn_bytes_5 = cbor::diag2cbor("{1: 0, 1_0: 5}");
    CHECK_EQ(cbor::structs<std::map<std::uint64_t, std::uint64_t>>::decode(std::move(edn_bytes_5.value())),
             std::unexpected(error::duplicate_key));
}

// RFC 8428 6, the CBOR form of the example in 5.1.2: the integer labels come from the keys of the struct.
TEST_CASE("structs: a SenML pack with integer labels")
{
    std::vector<senml_record> const pack{
        {"urn:dev:ow:10e2073a01080063:", 1.320067464e9, "voltage", "V", 120.1, std::nullopt},
        {std::nullopt, std::nullopt, "current", "A", 1.2, -5}};
    auto const bytes = cbor::encode<std::vector<senml_record>>(pack);
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
    CHECK_NE(static_cast<void const *>(payload.data()), static_cast<void const *>(std::span(message).subspan(12).data()));
    auto const owner = std::make_shared<std::string const>(message);
    auto const held = cbor::structs<cose_sign1>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>(std::get<2>((*held)->content).data()), static_cast<void const *>(std::span(*owner).subspan(12).data()));
    CHECK_EQ(cbor::encode<cose_sign1>(**sign1), message);
}

// Owner decision: structs::decode has the view form decode(std::shared_ptr<std::string const>). It copies nothing
// and keeps the owner.
TEST_CASE("structs: the view form reads the bytes of the owner")
{
    auto const owner = std::make_shared<std::string const>(
        "\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s);
    auto const held = cbor::structs<cose_sign1>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>(std::get<2>((*held)->content).data()), static_cast<void const *>(std::span(*owner).subspan(12).data()));
    CHECK_THROWS_AS((void)cbor::structs<cose_sign1>::decode(std::shared_ptr<std::string const>()), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. An owner beside borrowed bytes compiles (owner
// decision 2026-10-17); an owner beside a string does not, because the string may die first. The test exists because
// each of these let a view outlive its bytes. A check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("structs: decode checks that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>("\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s);
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o, std::string(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o, std::string_view(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o, *o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { cbor::structs<cose_sign1>::decode(o); }; }(bytes)));
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, bytes.get());
    CHECK_THROWS_AS((void)cbor::structs<cose_sign1>::decode(holds_nothing), std::logic_error);
    std::shared_ptr<std::string const> const holds_bytes(std::shared_ptr<void const>(bytes), bytes.get());
    auto const held = cbor::structs<cose_sign1>::decode(holds_bytes);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>(std::get<2>((*held)->content).data()), static_cast<void const *>(std::span(*bytes).subspan(12).data()));
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
    CHECK_EQ(cbor::encode<get_assertion_response>(**r), message);
    std::array<char, 64> room{};
    auto const placed = cbor::encode<get_assertion_response>(std::span(room), **r);
    REQUIRE(placed.has_value());
    CHECK_EQ(std::string_view(room.data(), *placed), message);
    std::vector<char> tight(message.size());
    auto const exact = cbor::encode<get_assertion_response>(std::span(tight), **r);
    REQUIRE(exact.has_value());
    CHECK_EQ(std::string_view(tight.data(), *exact), message);
    std::array<char, 8> small{};
    CHECK_EQ(cbor::encode<get_assertion_response>(std::span(small), **r), std::unexpected(cbor::error::no_buffer_space));
    std::string text = "x";
    CHECK_EQ(cbor::encode<get_assertion_response>(text, **r), message.size());
    CHECK_EQ(text, "x" + message);
}

// RFC 8949 Appendix A: 2^64 and -2^64-1 need a bignum, tag 2 or 3 on the magnitude. Simple(16) and simple(255) are
// simple values of one and two bytes.
TEST_CASE("structs: bignums and simple values of RFC 8949 Appendix A")
{
#ifdef CBOR_HAS_INT128
    round_trip<cbor::uint128>("\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s,
                                  static_cast<cbor::uint128>(1) << 64);
    round_trip<cbor::int128>("\xc3\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00"s,
                         -1 - (static_cast<cbor::int128>(1) << 64));
    round_trip<cbor::int128>("\x39\x03\xe7"s, -1000);
#endif
    round_trip<cbor::simple_value>("\xf0"s, static_cast<cbor::simple_value>(16));
    round_trip<cbor::simple_value>("\xf8\xff"s, static_cast<cbor::simple_value>(255));
    CHECK_EQ(cbor::structs<cbor::simple_value>::decode("\xf8\x10"s), std::unexpected(error::syntax_error));
}

// A std::variant takes the first alternative whose kind matches the item, so one array can hold several
// kinds, as RFC 8949 allows. A vector of bytes is the typed array of tag 64 (RFC 8746).
TEST_CASE("structs: a variant takes the alternative that matches the item")
{
    using any = std::variant<std::uint64_t, std::int64_t, std::string, double, bool, std::nullptr_t,
                             std::vector<std::byte>>;
    std::vector<any> const values{std::uint64_t{1}, std::int64_t{-1}, std::string("a"), 1.5, true, nullptr,
                                  std::vector<std::byte>{std::byte{7}}};
    round_trip<std::vector<any>>("\x87\x01\x20\x61\x61\xf9\x3e\x00\xf5\xf6\xd8\x40\x41\x07"s, values);
    CHECK_EQ(cbor::structs<std::variant<std::string, bool>>::decode("\x01"s), std::unexpected(error::incorrect_type));
}

namespace
{

struct renamed {
    std::uint8_t m0;
    bool m1;
};

} // namespace

template <>
struct cbor::describe_members<renamed> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160050};
    static constexpr std::tuple members{
        cbor::member{"x-user-id", &renamed::m0},
        cbor::member{"EOF", &renamed::m1},
    };
};

// A generator gives each member a fixed name and carries the key of the other language in the description. So a key need
// not be a C++ identifier and never meets a macro such as EOF. The map comes from RFC 8949 3.1 by hand, under the tag of
// the description, 160050 = 0x00027132.
TEST_CASE("structs: the description gives the key of a member")
{
    std::string const bytes = "\xda\x00\x02\x71\x32\xa2\x69x-user-id\x07\x63"
                              "EOF\xf5"s;
    auto const back = cbor::structs<renamed>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->m0, 7u);
    CHECK((*back)->m1);
    CHECK_EQ(cbor::encode<renamed>(renamed{7, true}), "\xda\x00\x02\x71\x32\xa2\x63"
                                                                "EOF\xf5\x69x-user-id\x07"s);
    CHECK_EQ(cbor::structs<renamed>::decode("\xda\x00\x02\x71\x32\xa2\x62m0\x07\x62m1\xf5"s),
             std::unexpected(error::key_not_found));
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

template <>
struct cbor::describe_members<account> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160051};
    static constexpr std::tuple members{
        CBOR_MEMBER(account, name),
        CBOR_MEMBER(account, id),
    };
};

// A private or protected member is state the class keeps to itself. It never goes on the wire, and a key of its name on
// the wire is an unknown key. So the bytes of a sender cannot set it. A class with private members is no aggregate, so
// it is read through its description, under its tag 160051 = 0x00027133.
TEST_CASE("structs: only public members are written and read")
{
    account a;
    a.name = "ann";
    a.id = 7;
    a.hidden_set(42, 9);
    auto const encoded = cbor::encode<account>(a);
    std::string const bytes = *encoded;
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

struct value_kinds {
    std::uint64_t a;
    std::vector<std::optional<std::uint64_t>> b;
    bool operator==(value_kinds const &) const = default;
};

struct inner_number {
    std::uint64_t n;
    bool operator==(inner_number const &) const = default;
};

struct seconds_member {
    std::chrono::sys_seconds a;
};

} // namespace

// Tags 28 and 29 mark and name a shared value (value sharing, cbor.schmorp.de). A C++ value has no form for
// either, so structs refuses both where it reads a value, with invalid_tag (owner, 2026-11-16). RFC 8949 3
// lets the argument of a head take 1, 2, 4 or 8 bytes, and each width must give the same error. The member
// kinds are a number, a text, an optional, an element of an array, a key and a value of a map, a variant, a
// nested struct and the root.
TEST_CASE("structs: a value under tag 28 or tag 29 is invalid_tag")
{
    for (std::string const width : {"_0", "_1", "_2", "_3"}) {
        CAPTURE(width);
        auto const bytes = [&width](std::string const &before, std::string const &after) {
            auto const edn_bytes = cbor::diag2cbor(before + "28" + width + after);
            return edn_bytes.value();
        };
        auto const reference = [&width](std::string const &before, std::string const &after) {
            auto const edn_bytes_2 = cbor::diag2cbor(before + "29" + width + after);
            return edn_bytes_2.value();
        };
        CHECK_EQ(cbor::structs<value_kinds>::decode(bytes(R"({"a": )", R"((1), "b": []})")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<value_kinds>::decode(reference(R"({"a": )", R"((0), "b": []})")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<value_kinds>::decode(bytes(R"({"a": 1, "b": [)", "(2)]}")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<value_kinds>::decode(reference(R"({"a": 1, "b": [)", "(0)]}")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<value_kinds>::decode(bytes(R"({"a": 1, "b": [)", "(null)]}")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<std::string>::decode(bytes("", R"(("x"))")), std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<std::string>::decode(reference("", R"(("x"))")), std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<std::uint64_t>::decode(bytes("", "(5)")), std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<std::uint64_t>::decode(bytes("", "(28(5))")), std::unexpected(error::invalid_tag));
        CHECK_EQ((cbor::structs<std::map<std::string, std::uint64_t>>::decode(bytes("{", R"(("k"): 1})"))),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(
            (cbor::structs<std::map<std::string, std::uint64_t>>::decode(reference(R"({"k": )", "(0)}"))),
            std::unexpected(error::invalid_tag));
        CHECK_EQ(
            (cbor::structs<std::variant<std::uint64_t, std::string>>::decode(bytes("", "(1)"))),
            std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<std::vector<inner_number>>::decode(bytes("[", R"(({"n": 1})])")),
                 std::unexpected(error::invalid_tag));
        CHECK_EQ(cbor::structs<seconds_member>::decode(bytes(R"({"a": )", "(1(5))}")),
                 std::unexpected(error::invalid_tag));
    }
}

// The owner decided on 2026-11-16 that a tag 28 or 29 is an error only where structs reads a value. A key
// that no member has is skipped with its value, whatever its type (owner, 2026-11-08). So a key 28("a"), a
// key 29(0) and the values under a key "x" that no member has are skipped in every width of the head, and the
// members read their values.
TEST_CASE("structs: a tag 28 or tag 29 in a skipped entry is skipped")
{
    for (std::string const width : {"_0", "_1", "_2", "_3"}) {
        CAPTURE(width);
        std::string const edn = "{28" + width + R"(("a"): 28)" + width + "(1), 29" + width + "(0): 29" +
                                width + R"((0), "x": [28)" + width + "(1), 29" + width +
                                R"((0)], "a": 2, "b": [3]})";
        auto edn_bytes = cbor::diag2cbor(edn);
        auto const read = cbor::structs<value_kinds>::decode(std::move(edn_bytes.value()));
        REQUIRE(read.has_value());
        CHECK(**read == value_kinds{2, {3}});
    }
}

// The check for tags 28 and 29 stands where structs reads a value and meets a tag head. The content of a tag
// that structs reads, such as tag 1, is read by the reader of that tag, which keeps its own error. So a tag
// 28 under tag 1 gives incorrect_type into a number and inadmissible_type_for_tag_content into
// std::chrono::sys_seconds.
TEST_CASE("structs: a tag 28 inside the content of tag 1 keeps the error of tag 1")
{
    auto const edn_bytes = cbor::diag2cbor(R"({"a": 1(28(5))})");
    std::string const bytes = edn_bytes.value();
    CHECK_EQ(cbor::structs<value_kinds>::decode(std::string(bytes)), std::unexpected(error::incorrect_type));
    CHECK_EQ(cbor::structs<seconds_member>::decode(std::string(bytes)),
             std::unexpected(error::inadmissible_type_for_tag_content));
}

template <class Encoded>
concept decodable = requires(Encoded &&e) { cbor::structs<std::string_view>::decode(std::forward<Encoded>(e)); };

// A moved std::string becomes the owner and is not copied: the view points into the buffer that was moved. Owner
// decision 2026-10-17: no form copies the input. So a named string, a const string, a pointer and a view do not compile;
// a caller who wants a copy writes it.
TEST_CASE("structs: decode takes a moved string as the owner and takes no other bare form")
{
    std::string const text(40, 'x');
    std::string const message = "\x78\x28"s + text;
    auto buffer = std::make_unique<std::string>(message);
    std::span<char const> const data(*buffer);
    auto const moved = cbor::structs<std::string_view>::decode(std::move(*buffer));
    REQUIRE(moved.has_value());
    CHECK_EQ(static_cast<void const *>((*moved)->data()), static_cast<void const *>(data.subspan(2).data()));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(**moved, text);

    CHECK(decodable<std::string>);
    CHECK_FALSE(decodable<std::string &>);
    CHECK_FALSE(decodable<std::string const &>);
    CHECK_FALSE(decodable<std::string const>);
    CHECK_FALSE(decodable<char const *>);
    CHECK_FALSE(decodable<std::string_view>);

    std::string lvalue = message;
    auto const copied = cbor::structs<std::string_view>::decode(std::string(lvalue));
    REQUIRE(copied.has_value());
    CHECK_NE(static_cast<void const *>((*copied)->data()), static_cast<void const *>(std::span(lvalue).subspan(2).data()));
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(**copied, text);

    auto const owner = std::make_shared<std::string const>(message);
    auto const held = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>((*held)->data()), static_cast<void const *>(std::span(*owner).subspan(2).data()));
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

template <class M>
void map_keys_check()
{
    auto edn_bytes = cbor::diag2cbor("{1: 5, 2: 6, 1: 7}");
    CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes.value())), std::unexpected(error::duplicate_key));
    auto edn_bytes_2 = cbor::diag2cbor("{2: 6, 2: 6}");
    CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes_2.value())), std::unexpected(error::duplicate_key));
    for (std::string_view const edn : {"{1: 5, 2: 6, 3: 7}", "{3: 7, 1: 5, 2: 6}", "{2: 6, 3: 7, 1: 5}",
                                       "{1: 5, 3: 7, 2: 6}", "{2: 6, 1: 5, 3: 7}", "{3: 7, 2: 6, 1: 5}"}) {
        auto edn_bytes_3 = cbor::diag2cbor(edn);
        auto const read = cbor::structs<M>::decode(std::move(edn_bytes_3.value()));
        REQUIRE(read.has_value());
        CHECK_EQ((*read)->size(), 3u);
        CHECK_EQ((*read)->at(1), 5);
        CHECK_EQ((*read)->at(2), 6);
        CHECK_EQ((*read)->at(3), 7);
    }
}

// A map type with no try_emplace: decode reads the result of emplace, so it gives duplicate_key too.
struct emplace_map {
    using key_type = int;
    using mapped_type = int;
    std::map<int, int> entries;
    auto begin() const { return entries.begin(); }
    auto end() const { return entries.end(); }
    std::size_t size() const { return entries.size(); }
    int at(int const key) const { return entries.at(key); }
    void clear() { entries.clear(); }
    std::size_t erase(int const key) { return entries.erase(key); }
    template <class K, class V>
    auto emplace(K &&key, V &&value)
    {
        return entries.emplace(std::forward<K>(key), std::forward<V>(value));
    }
};

// RFC 8949 5.6: every map container gives duplicate_key on a repeated key. The keys 1, 2 and 3 in every order are
// distinct and decode.
TEST_CASE("structs: a map container gives duplicate_key on a repeated key")
{
    map_keys_check<emplace_map>();
    map_keys_check<std::map<int, int>>();
    map_keys_check<std::unordered_map<int, int>>();
#if __has_include(<flat_map>)
    map_keys_check<std::flat_map<int, int>>();
#endif
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

CBOR_DEFINE_TYPE_MEMBERS(standard_containers, cbor::untagged, l, q, b, s, m, mm);

// decode reads every standard container that encode writes. Before the fix std::list, std::deque, std::set and
// std::vector<bool> encoded but did not compile in decode: the branch tested push_back and called reserve and
// emplace_back. A std::set fell through to the struct reader.
TEST_CASE("structs: every standard container that encode writes decodes")
{
    round_trip("\x82\xf5\xf4"s, std::vector<bool>{true, false});
    round_trip("\xd9\x01\x02\x82\x01\x02"s, std::set<int>{1, 2});
    round_trip("\x83\x01\x02\x03"s, std::list<int>{1, 2, 3});
    round_trip("\x82\x01\x02"s, std::deque<int>{1, 2});
    round_trip(
        "\xa6\x61"
        "b\x81\xf5\x61l\x82\x01\x02\x61m\x82\x06\x06\x61q\x81\x03\x61s\xd9\x01\x02\x82\x04\x05\x62mm\xa1\x01\x82\x02\x03"s,
        standard_containers{{1, 2}, {3}, {true}, {4, 5}, {6, 6}, {{1, 2}, {1, 3}}});
}

// A multimap is a map from each key to the array of its values. A repeated key gives duplicate_key in an ordered
// multimap, as for std::map; an unordered multimap appends the values, because the check there costs time (owner,
// 2026-11-16). A value that is not an array, and an empty array, which encode never writes, give incorrect_type.
template <class M>
void multimap_check()
{
    auto const edn_bytes = cbor::diag2cbor("{}");
    round_trip(edn_bytes.value(), M{});
    auto const edn_bytes_2 = cbor::diag2cbor("{1: [2]}");
    round_trip(edn_bytes_2.value(), M{{1, 2}});
    auto edn_bytes_3 = cbor::diag2cbor("{1: [2, 3], 2: [4]}");
    auto const back = cbor::structs<M>::decode(std::move(edn_bytes_3.value()));
    REQUIRE(back.has_value());
    CHECK_EQ((**back), M{{1, 2}, {1, 3}, {2, 4}});
    auto const again = cbor::encode<M>(M{{1, 2}, {1, 3}});
    REQUIRE(again.has_value());
    CHECK((*again == cbor::diag2cbor("{1: [2, 3]}") || *again == cbor::diag2cbor("{1: [3, 2]}")));
    if constexpr (requires(M const &m) { m.lower_bound(1); }) {
        auto edn_bytes_4 = cbor::diag2cbor("{1: [2], 1: [3]}");
        CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes_4.value())), std::unexpected(error::duplicate_key));
    } else {
        auto edn_bytes_5 = cbor::diag2cbor("{1: [2], 1: [3]}");
        auto const appended = cbor::structs<M>::decode(std::move(edn_bytes_5.value()));
        REQUIRE(appended.has_value());
        CHECK_EQ(**appended, M{{1, 2}, {1, 3}});
    }
    auto edn_bytes_6 = cbor::diag2cbor("{1: 2}");
    CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes_6.value())), std::unexpected(error::incorrect_type));
    auto edn_bytes_7 = cbor::diag2cbor("{1: []}");
    CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes_7.value())), std::unexpected(error::incorrect_type));
    auto edn_bytes_8 = cbor::diag2cbor("[[1, 2]]");
    CHECK_EQ(cbor::structs<M>::decode(std::move(edn_bytes_8.value())), std::unexpected(error::incorrect_type));
}

// RFC 8949 5.6 refuses two equal keys in a map, so a std::multimap goes as one key with the array of its values. The
// test exists so that a multimap goes and comes back with no duplicate key.
TEST_CASE("structs: a multimap is a map from each key to the array of its values")
{
    multimap_check<std::multimap<int, int>>();
    multimap_check<std::unordered_multimap<int, int>>();
    // std::multimap keeps the keys sorted and equal keys in insertion order, so its bytes are fixed.
    round_trip("\xa2\x01\x82\x02\x03\x02\x81\x04"s, std::multimap<int, int>{{1, 2}, {1, 3}, {2, 4}});
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

// An array head of 2^20 elements before 1 MiB of input made decode reserve 2^20 elements of 4096 bytes, 4 GiB, and
// std::bad_alloc escaped decode. The reserve is now at most what the remaining bytes hold. So the allocator that refuses
// more than 2 MiB is not reached, and the read ends at the first element that is no array. A std::bad_alloc that does
// happen inside decode reaches the caller.
TEST_CASE("structs: decode reserves no more than the remaining bytes and lets std::bad_alloc through")
{
    std::string message = "\xa1\x61v\x9a\x00\x10\x00\x00"s;
    message += std::string(std::size_t{1} << 20, '\xf6');
    auto const r = cbor::structs<large_elements>::decode(std::move(message));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::incorrect_type);

    std::string many = "\xa1\x61v\x9a\x00\x00\x02\x01"s;
    std::string const element = "\xd8\x47\x59\x10\x00"s + std::string(4096, '\x00');
    for (int i = 0; i < 0x201; ++i)
        many += element;
    CHECK_THROWS_AS(static_cast<void>(cbor::structs<large_elements>::decode(std::move(many))), std::bad_alloc);
}

// A count reserves as many elements as the bytes left can hold (RULES.md, owner 2026-11-16). An array head of 2^20
// elements of std::array<std::uint64_t, 512> stands before 4 MiB of nulls. One element is at least 4101 bytes: the
// typed array tag, the head of 4096 bytes and the bytes. So the bytes left can hold 1022 elements, and decode reserves
// them. The allocator of the test refuses more than 512 elements, and the std::bad_alloc reaches the caller.
TEST_CASE("structs: decode reserves the elements that the bytes left can hold")
{
    constexpr std::size_t claimed = std::size_t{1} << 20;
    constexpr std::size_t nulls = std::size_t{1} << 22;
    std::string claim = "{\"v\": [null";
    for (std::size_t i = 1; i < claimed; ++i)
        claim += ", null";
    claim += "]}";
    auto const edn_bytes = cbor::diag2cbor(claim);
    std::string const full = edn_bytes.value();
    std::string message = full.substr(0, full.size() - claimed);
    auto const edn_bytes_2 = cbor::diag2cbor("null");
    message.append(nulls, edn_bytes_2.value().front());
    REQUIRE_EQ(message.size(), full.size() - claimed + nulls);
    CHECK_THROWS_AS(static_cast<void>(cbor::structs<large_elements>::decode(std::move(message))), std::bad_alloc);
}

struct one_int {
    int a;
};

// A key is read with the same well-formedness checks as a value. f8 10 is simple value 16 in two bytes. RFC 8949 3.3
// forbids that form. Before the fix the key reader accepted it.
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

// A caller sets a limit with a pmr resource. The test fails if decode ignores that resource.
TEST_CASE("structs decode keeps the resource of a moved pmr target")
{
    using texts = std::pmr::vector<std::pmr::string>;
    CHECK((!decodes_named_target<texts>));

    std::vector<std::string> const value(3, std::string(40, 'x'));
    auto const encoded = cbor::encode<std::vector<std::string>>(value);
    REQUIRE(encoded.has_value());
    auto const owner = std::make_shared<std::string const>(*encoded);
    std::array<std::byte, 64> buffer;
    std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
    texts target(&budget);
    CHECK_THROWS_AS(static_cast<void>(cbor::structs<texts>::decode(owner, std::move(target))), std::bad_alloc);
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

// A resource that records the largest single allocation it gives.
class largest_allocation_resource : public std::pmr::memory_resource
{
    void *do_allocate(std::size_t const bytes, std::size_t const alignment) override
    {
        largest = std::max(largest, bytes);
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
    std::size_t largest = 0;
};

struct point_xy {
    std::uint64_t x;
    std::uint64_t y;
};

// The declaration of decode bounds the storage that a container reserves: the count of an array head reserves at
// most as many elements as the bytes left can hold (RULES.md). A point_xy is at least 7 bytes: a map head and two
// pairs of a key head, one key byte and a value byte. The head claims 70 points and 70 bytes follow, so the head
// passes the check of the bytes left, but the bytes hold only 10 points. The vector may then hold storage for 10
// points and no more. The test reads the largest allocation that the vector asks of its resource; libstdc++ asks for
// the count that reserve gets.
TEST_CASE("structs: an array head reserves no more elements than the bytes left can hold")
{
    constexpr std::size_t claimed = 70;
    constexpr std::size_t present = 10;
    std::string all_zeros = "[0";
    for (std::size_t i = 1; i < claimed; ++i)
        all_zeros += ", 0";
    all_zeros += "]";
    auto const edn_bytes = cbor::diag2cbor(all_zeros);
    std::string const claim = edn_bytes.value();
    std::string const head_of_claim = claim.substr(0, claim.size() - claimed);
    std::string input = head_of_claim;
    auto const point = cbor::diag2cbor(R"({"x": 0, "y": 0})");
    for (std::size_t i = 0; i < present; ++i)
        input += point.value();
    REQUIRE_EQ(input.size() - head_of_claim.size(), claimed);
    largest_allocation_resource resource;
    std::pmr::vector<point_xy> target(&resource);
    auto const owner = std::make_shared<std::string const>(input);
    auto const read = cbor::structs<std::pmr::vector<point_xy>>::decode(owner, std::string_view(*owner), std::move(target));
    REQUIRE_FALSE(read.has_value());
    CHECK_EQ(read.error(), error::too_little_data);
    CHECK_GT(resource.largest, 0u);
    CHECK_LE(resource.largest, present * sizeof(point_xy));
}

struct tree_node {
    std::vector<tree_node> children;
    bool operator==(tree_node const &) const = default;
};

// A recursive type nests its own vector. Each level reserves by the bytes left of that level, and a decode that
// succeeds gives the same tree. A tree whose last byte is cut gives too_little_data.
TEST_CASE("structs: a recursive type decodes and a cut tree gives too_little_data")
{
    auto const edn_bytes = cbor::diag2cbor(
        R"({"children": [{"children": []}, {"children": [{"children": []}, {"children": []}]}]})");
    std::string const bytes = edn_bytes.value();
    tree_node const expected{{tree_node{}, tree_node{{tree_node{}, tree_node{}}}}};
    auto const read = cbor::structs<tree_node>::decode(std::string(bytes));
    REQUIRE(read.has_value());
    CHECK_EQ(**read, expected);
    CHECK_EQ(cbor::structs<tree_node>::decode(bytes.substr(0, bytes.size() - 1)), std::unexpected(error::too_little_data));
}

struct pmr_person {
    std::pmr::string name;
    std::uint64_t age;
    bool operator==(pmr_person const &) const = default;
};

CBOR_DEFINE_TYPE_MEMBERS(pmr_person, cbor::untagged, name, age);

// The caller bounds the memory of a decode with the resource of the target. A plain struct has no allocator, so the
// vector cannot give its resource to the struct. But it can give it to each member that takes an allocator, as
// uses-allocator construction ([allocator.uses.construction]) does for one object. The test puts a counting resource as
// the default resource, so that a member that took the default resource shows as a count above zero. Each name is longer
// than any short string buffer, so each one allocates.
TEST_CASE("structs: a plain struct in a std::pmr::vector takes the resource of the vector for its members")
{
    using people = std::pmr::vector<pmr_person>;
    std::string const long_name(40, 'n');
    auto const encoded = cbor::encode<people>(people{{std::pmr::string(long_name), 1}, {std::pmr::string(long_name), 2}});
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
        CHECK_EQ(std::string_view(p.name), long_name);
        CHECK_EQ(p.name.get_allocator().resource(), &target);
    }
    // A debug build of the C++ library of MSVC gives each container a proxy object from its allocator. So a temporary
    // container with the default resource takes blocks there; the count holds without that proxy.
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    CHECK_EQ(fallback.allocations, 0u);
#endif
    CHECK_GT(target.allocations, 0u);
}

// RFC 8949 4.2.1: deterministic encoding orders the keys of a map by the bytewise lexicographic order of their encoded
// forms. A shorter text key has a shorter head, so "b" (61 62) comes before "aa" (62 61 61). An unsigned integer comes
// before a negative one, 1 (01) and 24 (18 18) before -1 (20). The declaration order differs on purpose.
TEST_CASE("structs: keys come out in the deterministic order of RFC 8949 4.2.1")
{
    auto const t = cbor::encode<text_order>(text_order{1, 2});
    REQUIRE(t.has_value());
    CHECK_EQ(*t, "\xa2\x61\x62\x02\x62\x61\x61\x01"sv);
    auto const i = cbor::encode<integer_order>(integer_order{10, 20, 30});
    REQUIRE(i.has_value());
    CHECK_EQ(*i, "\xa3\x01\x18\x1e\x18\x18\x0a\x20\x14"sv);
    auto const back = cbor::structs<integer_order>::decode(std::string(*i));
    REQUIRE(back.has_value());
    CHECK(**back == integer_order{10, 20, 30});
}

// RFC 8949 4.2.1 and 3: a text of 23 bytes has the head 77, a text of 24 bytes the head 78 18. So the key of 23 "b"
// comes before the key of 24 "a", though "a" sorts first by its characters. The test exists because the order of the
// members compares the initial byte first. A text of 24 bytes or more has a head of two bytes.
TEST_CASE("structs: a key of 23 bytes comes before a key of 24 bytes")
{
    auto const t = cbor::encode<long_text_order>(long_text_order{1, 2});
    REQUIRE(t.has_value());
    CHECK_EQ(*t, "\xa2\x77" + std::string(23, 'b') + "\x02\x78\x18" + std::string(24, 'a') + "\x01");
}

// RFC 8949 3.4.3 and 5.6.1: a key written as a bignum that fits 64 bits is the integer key of the same value. So structs
// finds each member of integer_order under 2(h'18') for 24, 3(h'00') for -1 and 2(h'0001') for 1. A bignum key that does
// not fit 64 bits names no member and is skipped.
TEST_CASE("structs: an integer key written as a bignum that fits")
{
    auto const back = cbor::structs<integer_order>::decode(
        "\xa4\xc2\x41\x18\x0a\xc3\x41\x00\x14\xc2\x42\x00\x01\x18\x1e\xc2\x49\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00"s);
    REQUIRE(back.has_value());
    CHECK(**back == integer_order{10, 20, 30});
}

// RFC 8949 3.4.3: 2(h'18') is the integer 24. The key reader of structs computes the value of a bignum in its own
// place, so the same key written in EDN must name the member of 24.
TEST_CASE("structs: the bignum key 2(h'18') names the member of 24")
{
    auto edn_bytes = cbor::diag2cbor("{2(h'18'): 10, -1: 20, 1: 30}");
    auto const back = cbor::structs<integer_order>::decode(std::move(edn_bytes.value()));
    REQUIRE(back.has_value());
    CHECK(**back == integer_order{10, 20, 30});
}

#ifdef CBOR_HAS_INT128
namespace
{

struct wide_member {
    cbor::uint128 v;
};

} // namespace

// RFC 8949 3.4.3: a member of 128 bits reads a bignum up to 16 significant bytes. 2^64 is the first value past 64
// bits, and 2^128 is the first value past 128 bits.
TEST_CASE("structs: a member of 128 bits reads a bignum up to 16 significant bytes")
{
    auto edn_bytes = cbor::diag2cbor(R"({"v": 2(h'010000000000000000')})");
    auto const fits = cbor::structs<wide_member>::decode(std::move(edn_bytes.value()));
    REQUIRE(fits.has_value());
    CHECK((**fits).v == static_cast<cbor::uint128>(1) << 64);
    auto edn_bytes_2 = cbor::diag2cbor(R"({"v": 2(h'0100000000000000000000000000000000')})");
    CHECK_EQ(cbor::structs<wide_member>::decode(
                 std::move(edn_bytes_2.value())),
             std::unexpected(error::number_out_of_range));
}
#endif



namespace
{

// RFC 8949 3: the head of a tag, written by hand for the tag numbers below 2^16.
std::string tag_head(std::uint64_t const tag)
{
    if (tag < 24)
        return std::string(1, static_cast<char>(0xc0 + tag));
    if (tag < 256)
        return "\xd8"s + static_cast<char>(tag);
    return "\xd9"s + static_cast<char>(tag >> 8) + static_cast<char>(tag & 0xff);
}

// There is one content for each tag of the cross product. The registry names the content for the tag
// (refs/cbor-tags.xml). The value is one that every type which accepts the tag can hold.
std::string content_of(std::uint64_t const tag)
{
    switch (tag) {
    case 0:
        return "\x74"
               "1970-01-01T00:00:00Z"s;
    case 1:
    case 100:
        return "\x00"s;
    case 258:
        return "\x80"s;
    case 1004:
        return "\x6a"
               "1970-01-01"s;
    default:
        return "\x40"s;
    }
}

template <class T>
void tags_accepted(std::vector<std::uint64_t> const &accepted)
{
    std::vector<std::uint64_t> tags{0, 1, 24, 100, 258, 1004};
    for (std::uint64_t t = test::rfc8746::first_typed_array_tag; t <= test::rfc8746::last_typed_array_tag; ++t)
        tags.push_back(t);
    REQUIRE_EQ(tags.size(), 30u);
    for (std::uint64_t const t : tags) {
        CAPTURE(t);
        auto const r = cbor::structs<T>::decode(tag_head(t) + content_of(t));
        if (std::ranges::find(accepted, t) != accepted.end())
            CHECK(r.has_value());
        else
            CHECK_EQ(r.error(), error::incorrect_type);
    }
}

} // namespace

// The owner decided on 2026-10-15 that a standard library type takes the tag that CBOR assigns to it. The type
// sys_seconds has tag 1 (RFC 8949 3.4.2). The type year_month_day has tag 100 (RFC 8943 2). The type std::set has tag
// 258 (CBOR_SETS.md). A std::vector of a fixed-width number the RFC 8746 typed array of its element, little endian;
// std::byte and std::uint8_t take tag 64. Each cell of tags x types is one case. A decode of sys_seconds also takes tag
// 0 and a decode of year_month_day tag 1004, through std::chrono::parse. Libc++ has no std::chrono::parse, and the owner
// decided that these tags then give incorrect_type. The tag of each element type comes from the table of RFC 8746 2.1
// (refs/rfc8746.txt:473-539).
TEST_CASE("structs: the tags that each standard library type accepts")
{
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    tags_accepted<std::chrono::sys_seconds>({0, 1, 100});
    tags_accepted<std::chrono::sys_time<std::chrono::milliseconds>>({0, 1, 100});
    tags_accepted<std::chrono::year_month_day>({100, 1004});
#else
    tags_accepted<std::chrono::sys_seconds>({1, 100});
    tags_accepted<std::chrono::sys_time<std::chrono::milliseconds>>({1, 100});
    tags_accepted<std::chrono::year_month_day>({100});
#endif
    tags_accepted<std::set<int>>({258});
    tags_accepted<std::vector<std::byte>>({64});
    tags_accepted<std::vector<std::uint8_t>>({64});
    tags_accepted<std::vector<std::uint16_t>>({69});
    tags_accepted<std::vector<std::uint32_t>>({70});
    tags_accepted<std::vector<std::uint64_t>>({71});
    tags_accepted<std::vector<std::int8_t>>({72});
    tags_accepted<std::vector<std::int16_t>>({77});
    tags_accepted<std::vector<std::int32_t>>({78});
    tags_accepted<std::vector<std::int64_t>>({79});
    tags_accepted<std::vector<float>>({85});
    tags_accepted<std::vector<double>>({86});
}

// RFC 8949 3.4.2: tag 1 holds the seconds since 1970-01-01T00:00Z; 1700000000 is 0x6553f100. RFC 8943 Table 1
// (refs/rfc8943.txt:135-141): 1940-10-09 is day -10676 and 1980-12-08 is day 3994 under tag 100 ('d'). The content of
// tag 1 is an integer or a float (RFC 8949 3.4.2). So a text, a byte string, a tag, true or an array under tag 1 is
// inadmissible_type_for_tag_content. The doc of structs decode names it so (RFC 8949 5.3.2).
TEST_CASE("structs: sys_seconds is tag 1 and year_month_day is tag 100")
{
    using namespace std::chrono;
    round_trip("\xc1\x1a\x65\x53\xf1\x00"s, sys_seconds{seconds{1700000000}});
    round_trip("\xc1\x20"s, sys_seconds{seconds{-1}});
    round_trip("\xd8\x64\x39\x29\xb3"s, year_month_day{year{1940}, October, day{9}});
    round_trip("\xd8\x64\x19\x0f\x9a"s, year_month_day{year{1980}, December, day{8}});
    CHECK_EQ(cbor::structs<sys_seconds>::decode("\xc1\x1b\x80\x00\x00\x00\x00\x00\x00\x00"s),
             std::unexpected(error::number_out_of_range));
    for (std::string const &content : {"\x61x"s, "\x41\x33"s, "\xc0\x00"s, "\xf5"s, "\x80"s})
        CHECK_EQ(cbor::structs<sys_seconds>::decode("\xc1" + content), std::unexpected(error::inadmissible_type_for_tag_content));
}

// RFC 8949 3.4.2: tag 1 holds an integer or a float of seconds. The owner decided on 2026-11-17 that a
// std::chrono::sys_time of any period reads tag 1 when its duration holds the value exactly.
// Each case of the owner's doc is one check. An inexact fraction, an integer out of range and a float that is not
// finite give number_out_of_range.
TEST_CASE("structs: tag 1 with a fraction of a second")
{
    using namespace std::chrono;
    auto const tag1 = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    auto const ms = cbor::structs<sys_time<milliseconds>>::decode(tag1("1(1.5)"));
    REQUIRE(ms.has_value());
    CHECK_EQ((*ms)->time_since_epoch().count(), 1500);
    CHECK_EQ(cbor::structs<sys_seconds>::decode(tag1("1(1.5)")), std::unexpected(error::number_out_of_range));
    auto const fractional = cbor::structs<sys_time<duration<double>>>::decode(tag1("1(1.5)"));
    REQUIRE(fractional.has_value());
    CHECK_EQ((*fractional)->time_since_epoch().count(), 1.5);
    auto const ns = cbor::structs<sys_time<nanoseconds>>::decode(tag1("1(1)"));
    REQUIRE(ns.has_value());
    CHECK_EQ((*ns)->time_since_epoch().count(), 1000000000);
    CHECK_EQ(cbor::structs<sys_time<nanoseconds>>::decode(tag1("1(9223372037)")), std::unexpected(error::number_out_of_range));
    for (std::string_view const diag : {"1(NaN)", "1(Infinity)", "1(-Infinity)"}) {
        CAPTURE(diag);
        CHECK_EQ(cbor::structs<sys_time<milliseconds>>::decode(tag1(diag)), std::unexpected(error::number_out_of_range));
        CHECK_EQ(cbor::structs<sys_time<duration<double>>>::decode(tag1(diag)), std::unexpected(error::number_out_of_range));
    }
}

// The owner's plan 2 of 2026-11-17: a whole number of seconds is written as tag 1 with the integer. A sys_time with an
// integral rep and a period of 10^-3k s writes the rest as tag 1001 {1: s, -3k: f} with 0 <= f < 10^3k (RFC 9581
// 3.3, refs/rfc9581.txt:269-312). A floating rep writes tag 1 with the double. What encode writes, decode reads back to
// the same value. Nanoseconds near the present time with a fraction have no exact double, and tag 1001 carries them.
TEST_CASE("structs: sys_time of each precision round trips through tag 1 and tag 1001")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    round_trip(item("1(1700000000)"), sys_seconds{seconds{1700000000}});
    round_trip(item("1001({1: 1, -3: 500})"), sys_time<milliseconds>{milliseconds{1500}});
    round_trip(item("1001({1: 1, -3: 1})"), sys_time<milliseconds>{milliseconds{1001}});
    round_trip(item("1001({1: -2, -3: 500})"), sys_time<milliseconds>{milliseconds{-1500}});
    round_trip(item("1(-2)"), sys_time<milliseconds>{milliseconds{-2000}});
    round_trip(item("1(1700000000)"), sys_time<nanoseconds>{nanoseconds{1700000000000000000}});
    round_trip(item("1001({1: 0, -9: 500000000})"), sys_time<nanoseconds>{nanoseconds{500000000}});
    round_trip(item("1001({1: 1700000000, -9: 1})"), sys_time<nanoseconds>{nanoseconds{1700000000000000001}});
    round_trip(item("1001({1: 1, -6: 2})"), sys_time<microseconds>{microseconds{1000002}});
    round_trip(item("1(1.5)"), sys_time<duration<double>>{duration<double>{1.5}});
    round_trip(item("1(2)"), sys_time<duration<double>>{duration<double>{2.0}});
    round_trip(item("1(180)"), sys_time<minutes>{minutes{3}});
    round_trip(item("1(-3600)"), sys_time<hours>{hours{-1}});
    round_trip(item("1(172800)"), sys_days{days{2}});
}

// The owner's review of plan 2: an integral rep with a period that is no decimal fraction of a second has only whole
// seconds to write. Its other values give number_out_of_range on encode.
TEST_CASE("structs: sys_time with a period that tag 1001 cannot carry gives number_out_of_range on encode")
{
    using namespace std::chrono;
    using thirds = duration<std::int64_t, std::ratio<1, 3>>;
    CHECK_EQ(cbor::encode<sys_time<thirds>>(sys_time<thirds>{thirds{1}}), std::unexpected(error::number_out_of_range));
    auto const whole = cbor::encode<sys_time<thirds>>(sys_time<thirds>{thirds{3}});
    REQUIRE(whole.has_value());
    CHECK_EQ(*whole, cbor::diag2cbor("1(1)"));
}

// RFC 9581 3 (refs/rfc9581.txt:180-247): key 1 is the base time, a fraction key adds to it, and a fraction of 10^3k or
// more carries into the seconds (RFC 9581 3.3 names the invariant of C timespec). Key 13, -1 or -13 with the value 0 is
// UTC. A negative key or a text key that the library does not read is elective and ignored.
TEST_CASE("structs: tag 1001 into sys_time")
{
    using namespace std::chrono;
    auto const decode_ms = [](std::string_view const diag) {
        auto edn_bytes = cbor::diag2cbor(diag);
        return cbor::structs<sys_time<milliseconds>>::decode(std::move(edn_bytes.value()));
    };
    auto const carried = decode_ms("1001({1: 1, -3: 1500})");
    REQUIRE(carried.has_value());
    CHECK_EQ((*carried)->time_since_epoch().count(), 2500);
    for (std::string_view const diag : {"1001({1: 1, 13: 0})", "1001({1: 1, -1: 0})", "1001({1: 1, -13: 0})",
                                        "1001({1: 1, -2: 5, \"x\": [1]})", "1001({-9: 0, 1: 1})"}) {
        CAPTURE(diag);
        auto const r = decode_ms(diag);
        REQUIRE(r.has_value());
        CHECK_EQ((*r)->time_since_epoch().count(), 1000);
    }
    auto edn_bytes_2 = cbor::diag2cbor("1001({1: 1, -3: 5})");
    auto const fine = cbor::structs<sys_time<nanoseconds>>::decode(std::move(edn_bytes_2.value()));
    REQUIRE(fine.has_value());
    CHECK_EQ((*fine)->time_since_epoch().count(), 1005000000);
    auto edn_bytes_3 = cbor::diag2cbor("1001({1: 1.5})");
    auto const fractional =
        cbor::structs<sys_time<duration<double>>>::decode(std::move(edn_bytes_3.value()));
    REQUIRE(fractional.has_value());
    CHECK_EQ((*fractional)->time_since_epoch().count(), 1.5);
    CHECK_EQ(decode_ms("1001({1: 1, -6: 1})"), std::unexpected(error::number_out_of_range));
    auto edn_bytes_4 = cbor::diag2cbor("1001({1: 1, -3: 1})");
    CHECK_EQ(cbor::structs<sys_seconds>::decode(std::move(edn_bytes_4.value())),
             std::unexpected(error::number_out_of_range));
    CHECK_EQ(decode_ms("1001({1: 9223372036854775807, -3: 1000})"), std::unexpected(error::number_out_of_range));
}

// RFC 9581 3: the map has exactly one base time, at most one fraction key and at most one timescale key (3.3, 3.4). A
// fraction needs an integer under key 1. An unsigned key that the library does not implement (4, 5, 11 and unknown
// ones) is critical and is an error. Key 10 is a critical time zone hint (3.6), and a sys_time cannot use it. TAI
// (timescale 1) needs the leap seconds of the tzdb; without the tzdb it is an error.
TEST_CASE("structs: tag 1001 that the library cannot read gives inadmissible_type_for_tag_content")
{
    using namespace std::chrono;
#if !(__cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream))
    for (std::string_view const diag : {"1001({1: 1, 13: 1})", "1001({1: 1, -1: 1})"}) {
        CAPTURE(diag);
        auto edn_bytes = cbor::diag2cbor(diag);
        CHECK_EQ(cbor::structs<sys_time<milliseconds>>::decode(std::move(edn_bytes.value())),
                 std::unexpected(error::inadmissible_type_for_tag_content));
    }
#endif
    for (std::string_view const diag :
         {"1001({1: 1, 4: 0})", "1001({1: 1, 5: 0})", "1001({1: 1, 10: 0})", "1001({1: 1, 11: 0})",
          "1001({1: 1, 10: \"UTC\"})", "1001({1: 1, 99: 0})", "1001({1: 1, 13: 2})", "1001({1: 1, 13: \"TAI\"})",
          "1001({1: 1, -3: 1, -6: 1})", "1001({1: 1.5, -3: 1})", "1001({-3: 1})", "1001({})",
          "1001({1: 1, 1: 2})", "1001({1: 1, 13: 0, -1: 0})", "1001({1: 1, -3: -1})", "1001([1])",
          "1001({1: \"1\"})", "1001({h'01': 1, 1: 1})"}) {
        CAPTURE(diag);
        auto edn_bytes = cbor::diag2cbor(diag);
        CHECK_EQ(cbor::structs<sys_time<milliseconds>>::decode(std::move(edn_bytes.value())),
                 std::unexpected(error::inadmissible_type_for_tag_content));
    }
}

#if __cpp_lib_chrono >= 201907L
// RFC 9581 3.4 (refs/rfc9581.txt:315-347): timescale 1 is TAI with the PTP epoch 1970-01-01 TAI, and t_tai = t_gps +
// 315964819. The epoch of tai_clock is 1958-01-01 TAI, 4383 days before the PTP epoch. A tai_time and a gps_time are
// written as TAI under key 13, with no leap second table.
TEST_CASE("structs: tai_time and gps_time are tag 1001 with timescale 1")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    round_trip(item("1001({1: 0, 13: 1})"), tai_seconds{seconds{378691200}});
    round_trip(item("1001({1: 0, -3: 500, 13: 1})"), tai_time<milliseconds>{milliseconds{378691200500}});
    round_trip(item("1001({1: -378691200, 13: 1})"), tai_seconds{seconds{0}});
    round_trip(item("1001({1: 315964819, 13: 1})"), gps_seconds{seconds{0}});
    round_trip(item("1001({1: 315964820, -3: 250, 13: 1})"), gps_time<milliseconds>{milliseconds{1250}});
    CHECK_EQ(cbor::encode<gps_time<minutes>>(gps_time<minutes>{}), std::unexpected(error::number_out_of_range));
    CHECK_EQ(cbor::structs<gps_time<minutes>>::decode(item("1001({1: 0, 13: 1})")), std::unexpected(error::number_out_of_range));
    CHECK_EQ(cbor::encode<tai_time<duration<std::int64_t, std::atto>>>(
                 tai_time<duration<std::int64_t, std::atto>>{}),
             std::unexpected(error::number_out_of_range));
    CHECK_EQ(cbor::structs<tai_seconds>::decode(item("1002({1: 0})")), std::unexpected(error::incorrect_type));
}
#endif

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
// The owner's review of the plan of 2026-11-18: every conversion through utc_clock reads the leap seconds of the tzdb.
// TAI - UTC is 37 s from 2017-01-01 on. A UTC timescale or tag 1 into a tai_time goes through clock_cast.
TEST_CASE("structs: timescale 1 into sys_time and UTC forms into tai_time convert with the leap seconds")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    auto const utc = cbor::structs<sys_seconds>::decode(item("1001({1: 1500000000, 13: 1})"));
    REQUIRE(utc.has_value());
    CHECK_EQ((*utc)->time_since_epoch().count(), 1499999963);
    auto const elective = cbor::structs<sys_seconds>::decode(item("1001({1: 1500000000, -1: 1})"));
    REQUIRE(elective.has_value());
    CHECK_EQ((*elective)->time_since_epoch().count(), 1499999963);
    for (std::string_view const diag : {"1(1499999963)", "1001({1: 1499999963})", "1001({1: 1499999963, 13: 0})"}) {
        CAPTURE(diag);
        auto const tai = cbor::structs<tai_seconds>::decode(item(diag));
        REQUIRE(tai.has_value());
        CHECK_EQ((*tai)->time_since_epoch().count(), 1500000000 + 378691200);
        auto const gps = cbor::structs<gps_seconds>::decode(item(diag));
        REQUIRE(gps.has_value());
        CHECK_EQ((*gps)->time_since_epoch().count(), 1500000000 - 315964819);
    }
    CHECK_EQ(cbor::structs<sys_seconds>::decode(item("1001({1: 9223372036854775807, 13: 1})")),
             std::unexpected(error::number_out_of_range));
}

// A utc_time counts the leap second 2016-12-31 23:59:60, which a sys_time cannot hold. It round trips as utc_time.
// A decode into sys_time maps it to 23:59:59, as utc_clock::to_sys does.
TEST_CASE("structs: utc_time is TAI and keeps a leap second")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    auto const before = clock_cast<utc_clock>(sys_seconds{seconds{1483228799}});
    utc_seconds const leap = before + seconds{1};
    CHECK(get_leap_second_info(leap).is_leap_second);
    round_trip(item("1001({1: 1483228836, 13: 1})"), leap);
    round_trip(item("1001({1: 1483228837, 13: 1})"), leap + seconds{1});
    round_trip(item("1001({1: 1483228836, -3: 5, 13: 1})"), utc_time<milliseconds>{leap} + milliseconds{5});
    auto const sys = cbor::structs<sys_seconds>::decode(item("1001({1: 1483228836, 13: 1})"));
    REQUIRE(sys.has_value());
    CHECK_EQ((*sys)->time_since_epoch().count(), 1483228799);
    auto const tagged = cbor::structs<utc_seconds>::decode(item("1(1483228800)"));
    REQUIRE(tagged.has_value());
    CHECK((**tagged == leap + seconds{1}));
}

// A file_time is converted to sys_time with clock_cast and is written as a sys_time.
TEST_CASE("structs: file_time is written as its sys_time")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    round_trip(item("1(1700000000)"), clock_cast<file_clock>(sys_seconds{seconds{1700000000}}));
    round_trip(item("1001({1: 1700000000, -3: 500})"),
               clock_cast<file_clock>(sys_time<milliseconds>{milliseconds{1700000000500}}));
}

// RFC 9581 3.6 (refs/rfc9581.txt:468-495): key -10 or 10 holds a time-zone-name or a time-numoffset. A zoned_time
// with a const time_zone * holds a zone of the tzdb, and the tzdb has no zone "+01:00". Keys -10 and 10 must not both
// stand in one map. A map with no hint gives the zone UTC.
TEST_CASE("structs: zoned_time is tag 1001 with a time zone hint")
{
    using namespace std::chrono;
    using zoned = zoned_time<seconds>;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    zoned const berlin{locate_zone("Europe/Berlin"), sys_seconds{seconds{1700000000}}};
    round_trip(item(R"(1001({1: 1700000000, -10: "Europe/Berlin"}))"), berlin);
    round_trip(item(R"(1001({1: 1700000000, -3: 5, -10: "Europe/Berlin"}))"),
               zoned_time<milliseconds>{locate_zone("Europe/Berlin"), sys_time<milliseconds>{milliseconds{1700000000005}}});
    auto const critical = cbor::structs<zoned>::decode(item(R"(1001({1: 1700000000, 10: "Europe/Berlin"}))"));
    REQUIRE(critical.has_value());
    CHECK((**critical == berlin));
    auto const plain = cbor::structs<zoned>::decode(item("1001({1: 1700000000})"));
    REQUIRE(plain.has_value());
    CHECK_EQ((*plain)->get_time_zone(), locate_zone("UTC"));
    auto const tai = cbor::structs<zoned>::decode(item(R"(1001({1: 1700000037, 13: 1, -10: "Europe/Berlin"}))"));
    REQUIRE(tai.has_value());
    CHECK((**tai == berlin));
    auto const hint = cbor::structs<sys_seconds>::decode(item(R"(1001({1: 1700000000, -10: "Europe/Berlin"}))"));
    REQUIRE(hint.has_value());
    CHECK_EQ((*hint)->time_since_epoch().count(), 1700000000);
    for (std::string_view const diag :
         {R"(1001({1: 1, -10: "+01:00"}))", R"(1001({1: 1, -10: "Nowhere/None"}))", R"(1001({1: 1, -10: 60}))",
          R"(1001({1: 1, -10: "UTC", 10: "UTC"}))"}) {
        CAPTURE(diag);
        CHECK_EQ(cbor::structs<zoned>::decode(item(diag)), std::unexpected(error::inadmissible_type_for_tag_content));
    }
    CHECK_EQ(cbor::structs<zoned>::decode(item("1(1)")), std::unexpected(error::incorrect_type));
}
#endif

// RFC 8943 2: tag 100 holds days since 1970-01-01. A sys_time takes it as days * 86400 seconds. A count of days whose
// seconds do not fit 64 bits gives number_out_of_range.
TEST_CASE("structs: tag 100 into sys_time")
{
    using namespace std::chrono;
    auto edn_bytes = cbor::diag2cbor("100(-3)");
    auto const days_in = cbor::structs<sys_days>::decode(std::move(edn_bytes.value()));
    REQUIRE(days_in.has_value());
    CHECK_EQ((*days_in)->time_since_epoch().count(), -3);
    auto edn_bytes_2 = cbor::diag2cbor("100(2)");
    auto const seconds_in = cbor::structs<sys_seconds>::decode(std::move(edn_bytes_2.value()));
    REQUIRE(seconds_in.has_value());
    CHECK_EQ((*seconds_in)->time_since_epoch().count(), 172800);
    auto edn_bytes_3 = cbor::diag2cbor("100(9223372036854775807)");
    CHECK_EQ(cbor::structs<sys_seconds>::decode(std::move(edn_bytes_3.value())),
             std::unexpected(error::number_out_of_range));
    auto edn_bytes_4 = cbor::diag2cbor("100(-106751991167301)");
    CHECK_EQ(cbor::structs<sys_seconds>::decode(std::move(edn_bytes_4.value())),
             std::unexpected(error::number_out_of_range));
    auto edn_bytes_5 = cbor::diag2cbor("1(86401)");
    CHECK_EQ(cbor::structs<sys_days>::decode(std::move(edn_bytes_5.value())), std::unexpected(error::number_out_of_range));
}

struct duration_member {
    std::chrono::milliseconds a;
    bool operator==(duration_member const &) const = default;
};

CBOR_DEFINE_TYPE_MEMBERS(duration_member, cbor::untagged, a);

// RFC 9581 4 (refs/rfc9581.txt:558-591): a duration is tag 1002 with the map of tag 1001. The owner's review of plan 2:
// a duration is always tag 1002, and decode takes only tag 1002. A std::chrono::duration has a private member, so it
// is read as a duration and never as a struct.
TEST_CASE("structs: std::chrono::duration is tag 1002")
{
    using namespace std::chrono;
    auto const item = [](std::string_view const diag) {
        auto const bytes = cbor::diag2cbor(diag);
        return bytes.value();
    };
    round_trip(item("1002({1: 5})"), seconds{5});
    round_trip(item("1002({1: 1, -3: 500})"), milliseconds{1500});
    round_trip(item("1002({1: -2, -3: 500})"), milliseconds{-1500});
    round_trip(item("1002({1: 1, -9: 1})"), nanoseconds{1000000001});
    round_trip(item("1002({1: 1.5})"), duration<double>{1.5});
    round_trip(item("1002({1: 120})"), minutes{2});
    round_trip(item(R"({"a": 1002({1: 1, -3: 500})})"), duration_member{milliseconds{1500}});
    for (std::string_view const diag : {"1(5)", "5", "{1: 5}", "1001({1: 5})"}) {
        CAPTURE(diag);
        CHECK_EQ(cbor::structs<seconds>::decode(item(diag)), std::unexpected(error::incorrect_type));
    }
    CHECK_EQ(cbor::structs<seconds>::decode(item("1002({1: 1, -3: 1})")), std::unexpected(error::number_out_of_range));
    CHECK_EQ(cbor::structs<seconds>::decode(item("1002({1: 1, 4: 0})")),
             std::unexpected(error::inadmissible_type_for_tag_content));
}

// RFC 8949 3.4.2 reads a nonnegative value of tag 1 as POSIX time. It leaves a negative value and a value that is not
// finite to the application (refs/rfc8949.txt:1047-1069). The owner decided on 2026-10-15 that a float with a whole
// value goes into sys_seconds. Any other float gives number_out_of_range. -0.0 has the value 0. Each value stands in
// each width of RFC 8949 3.3 that holds it: 2^63 does not fit binary16.
TEST_CASE("structs: tag 1 with a float into sys_seconds")
{
    using namespace std::chrono;
    auto const read = [](std::string bytes) { return cbor::structs<sys_seconds>::decode("\xc1"s + bytes); };
    for (std::string const &one :
         {"\xf9\x3c\x00"s, "\xfa\x3f\x80\x00\x00"s, "\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"s}) {
        auto const r = read(one);
        REQUIRE(r.has_value());
        CHECK_EQ((*r)->time_since_epoch().count(), 1);
    }
    for (std::string const &zero :
         {"\xf9\x80\x00"s, "\xfa\x80\x00\x00\x00"s, "\xfb\x80\x00\x00\x00\x00\x00\x00\x00"s}) {
        auto const r = read(zero);
        REQUIRE(r.has_value());
        CHECK_EQ((*r)->time_since_epoch().count(), 0);
    }
    for (std::string const &refused :
         {"\xf9\x3e\x00"s, "\xfa\x3f\xc0\x00\x00"s, "\xfb\x3f\xf8\x00\x00\x00\x00\x00\x00"s, "\xf9\x7e\x00"s,
          "\xfa\x7f\xc0\x00\x00"s, "\xfb\x7f\xf8\x00\x00\x00\x00\x00\x00"s, "\xf9\x7c\x00"s,
          "\xfa\x7f\x80\x00\x00"s, "\xfb\x7f\xf0\x00\x00\x00\x00\x00\x00"s, "\xf9\xfc\x00"s,
          "\xfa\xff\x80\x00\x00"s, "\xfb\xff\xf0\x00\x00\x00\x00\x00\x00"s, "\xfa\x5f\x00\x00\x00"s,
          "\xfb\x43\xe0\x00\x00\x00\x00\x00\x00"s}) {
        CAPTURE(refused.size());
        CHECK_EQ(read(refused), std::unexpected(error::number_out_of_range));
    }
}

// CBOR_SETS.md (refs/CBOR_SETS.md): tag 258 on an array is a set. The library writes the order of the
// container, as it never sorts at run time; std::set holds -1 before 1 and "aa" before "b". A decode takes
// any order. A repeated item makes the set not valid and gives duplicate_key, as a repeated key of a map does.
TEST_CASE("structs: std::set is tag 258")
{
    auto const edn_bytes = cbor::diag2cbor("258([-1, 1])");
    round_trip(edn_bytes.value(), std::set<int>{-1, 1});
    auto const edn_bytes_2 = cbor::diag2cbor(R"(258(["aa", "b"]))");
    round_trip(edn_bytes_2.value(), std::set<std::string>{"aa", "b"});
    auto edn_bytes_3 = cbor::diag2cbor("258([2, 1])");
    auto const any_order = cbor::structs<std::set<int>>::decode(std::move(edn_bytes_3.value()));
    REQUIRE(any_order.has_value());
    CHECK(**any_order == std::set<int>{1, 2});
    auto edn_bytes_4 = cbor::diag2cbor("258([1, 1])");
    CHECK_EQ(cbor::structs<std::set<int>>::decode(std::move(edn_bytes_4.value())), std::unexpected(error::duplicate_key));
    auto edn_bytes_5 = cbor::diag2cbor("[1, 2]");
    CHECK_EQ(cbor::structs<std::set<int>>::decode(std::move(edn_bytes_5.value())), std::unexpected(error::incorrect_type));
}

// RFC 8746 2.1: a typed array is its tag over a byte string of the elements, here little endian. The owner decided on
// 2026-10-15 that every std::vector and std::array of a fixed-width number, std::byte and std::uint8_t included, is a
// typed array. A byte string comes from std::span<std::byte const>. A std::array takes exactly its count; a byte string
// whose length is no multiple of the element size is no typed array (RFC 8746 2).
TEST_CASE("structs: a vector or an array of numbers is an RFC 8746 typed array")
{
    round_trip("\xd8\x40\x42\x01\x02"s, std::vector<std::byte>{std::byte{1}, std::byte{2}});
    round_trip("\xd8\x40\x42\x01\xff"s, std::vector<std::uint8_t>{1, 255});
    round_trip("\xd8\x45\x44\x01\x00\x02\x01"s, std::vector<std::uint16_t>{1, 0x102});
    round_trip("\xd8\x46\x44\x04\x03\x02\x01"s, std::vector<std::uint32_t>{0x01020304});
    round_trip("\xd8\x47\x48\x08\x07\x06\x05\x04\x03\x02\x01"s,
               std::vector<std::uint64_t>{0x0102030405060708});
    round_trip("\xd8\x48\x42\xff\x7f"s, std::vector<std::int8_t>{-1, 127});
    round_trip("\xd8\x4d\x42\xfe\xff"s, std::vector<std::int16_t>{-2});
    round_trip("\xd8\x4e\x44\xfd\xff\xff\xff"s, std::vector<std::int32_t>{-3});
    round_trip("\xd8\x4f\x48\xfc\xff\xff\xff\xff\xff\xff\xff"s, std::vector<std::int64_t>{-4});
    round_trip("\xd8\x55\x44\x00\x00\xc0\x3f"s, std::vector<float>{1.5f});
    round_trip("\xd8\x56\x48\x00\x00\x00\x00\x00\x00\xf8\x3f"s, std::vector<double>{1.5});
    round_trip("\xd8\x45\x40"s, std::vector<std::uint16_t>{});
    round_trip("\xd8\x45\x44\x01\x00\x02\x00"s, std::array<std::uint16_t, 2>{1, 2});
    round_trip("\xd8\x40\x42\x01\x02"s, std::array<std::byte, 2>{std::byte{1}, std::byte{2}});
    CHECK_EQ((cbor::structs<std::array<std::uint16_t, 2>>::decode("\xd8\x45\x42\x01\x00"s)),
             std::unexpected(error::incorrect_type));
    CHECK_EQ(cbor::structs<std::vector<std::uint16_t>>::decode("\xd8\x45\x43\x01\x00\x02"s),
             std::unexpected(error::inadmissible_type_for_tag_content));
    CHECK_EQ(cbor::structs<std::vector<std::uint16_t>>::decode("\xd8\x45\x82\x01\x02"s),
             std::unexpected(error::inadmissible_type_for_tag_content));
    CHECK_EQ(cbor::structs<std::vector<std::uint16_t>>::decode("\xd8\x41\x44\x00\x01\x00\x02"s),
             std::unexpected(error::incorrect_type));
    round_trip("\x82\x61"
               "a\x61"
               "b"s,
               std::vector<std::string>{"a", "b"});
    std::array const three{std::byte{1}, std::byte{2}, std::byte{3}};
    CHECK_EQ(cbor::encode<std::span<std::byte const>>(std::span<std::byte const>(three)),
             "\x43\x01\x02\x03"s);
    auto const view = cbor::structs<std::span<std::byte const>>::decode("\x43\x01\x02\x03"s);
    REQUIRE(view.has_value());
    CHECK_EQ((*view)->size(), 3u);
}

namespace
{

// RFC 8949 3: the head of a text string, written by hand for a length below 256.
template <class T>
std::expected<std::int64_t, error> since_epoch(std::string_view const text)
{
    std::string bytes = "\xc0"s;
    if (text.size() < 24)
        bytes += static_cast<char>(0x60 + text.size());
    else
        bytes += "\x78"s + static_cast<char>(text.size());
    bytes += text;
    auto const r = cbor::structs<T>::decode(std::move(bytes));
    if (!r)
        return std::unexpected(r.error());
    return (*r)->time_since_epoch().count();
}

std::expected<std::int64_t, error> day_of(std::string_view const text)
{
    std::string bytes = "\xd9\x03\xec"s;
    if (text.size() < 24)
        bytes += static_cast<char>(0x60 + text.size());
    else
        bytes += "\x78"s + static_cast<char>(text.size());
    bytes += text;
    auto const r = cbor::structs<std::chrono::year_month_day>::decode(std::move(bytes));
    if (!r)
        return std::unexpected(r.error());
    return std::chrono::sys_days{**r}.time_since_epoch().count();
}

} // namespace

// RFC 8943 2: tag 100 holds the number of days since 1970-01-01, with no bound. std::chrono::year_month_day holds the
// years from -32767 to 32767 (C++ [time.cal.year]). Their first and last days are -32767-01-01 and 32767-12-31. They are
// -12687428 and 11248737 days from the epoch. The days are counted year by year in the proleptic Gregorian calendar
// outside std::chrono. A count outside them is no date that year_month_day holds. decode gives number_out_of_range and
// gives no other date. This holds also for the counts at the ends of 32 and 64 bits.
TEST_CASE("structs: tag 100 outside the years of year_month_day is number_out_of_range")
{
    using namespace std::chrono;
    auto const days_tag = [](std::int64_t const n) {
        std::uint64_t const argument = n < 0 ? static_cast<std::uint64_t>(-(n + 1)) : static_cast<std::uint64_t>(n);
        std::string bytes = "\xd8\x64"s;
        bytes += static_cast<char>(n < 0 ? 0x3b : 0x1b);
        for (int shift = 56; shift >= 0; shift -= 8)
            bytes += static_cast<char>((argument >> shift) & 0xff);
        return cbor::structs<year_month_day>::decode(std::move(bytes));
    };
    constexpr std::int64_t first_day = -12687428;
    constexpr std::int64_t last_day = 11248737;
    auto const lowest = days_tag(first_day);
    REQUIRE(lowest.has_value());
    CHECK(**lowest == year{-32767} / January / 1);
    auto const highest = days_tag(last_day);
    REQUIRE(highest.has_value());
    CHECK(**highest == year{32767} / December / 31);
    for (std::int64_t const n : {first_day - 1, last_day + 1, std::int64_t{2146764180}, std::int64_t{2147483647},
                                 std::int64_t{-2147483648}, std::numeric_limits<std::int64_t>::max(),
                                 std::numeric_limits<std::int64_t>::min()}) {
        CAPTURE(n);
        auto const r = days_tag(n);
        REQUIRE_FALSE(r.has_value());
        CHECK_EQ(r.error(), error::number_out_of_range);
    }
}

// Tag 0 holds an RFC 3339 date-time and tag 1004 an RFC 3339 full-date (RFC 8949 3.4.1, RFC 8943 2). The inputs are the
// examples of RFC 3339 5.8 (refs/rfc3339.txt:515-536) and forms that the grammar of 5.6 (405-437) allows or refuses. The
// epoch values are computed from the calendar with Python calendar.timegm, which is not the code under test. The owner
// decided on 2026-10-15: a fraction goes into a sys_time of finer precision. A time that cannot hold it gives
// incorrect_type; second 60 gives incorrect_type; t and z may be lower case. Where the standard library has no
// std::chrono::parse (libc++), every input gives incorrect_type.
TEST_CASE("structs: RFC 3339 text under tags 0 and 1004")
{
    using namespace std::chrono;
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    auto const value = [](std::expected<std::int64_t, error> const r) { return r.value_or(-1); };
    CHECK_EQ(value(since_epoch<sys_seconds>("1985-04-12T23:20:50Z")), 482196050);
    CHECK_EQ(since_epoch<sys_seconds>("1985-04-12T23:20:50.52Z").error(), error::incorrect_type);
    CHECK_EQ(value(since_epoch<sys_time<milliseconds>>("1985-04-12T23:20:50.52Z")), 482196050520);
    CHECK_EQ(value(since_epoch<sys_seconds>("1996-12-19T16:39:57-08:00")), 851042397);
    CHECK_EQ(since_epoch<sys_seconds>("1990-12-31T23:59:60Z").error(), error::incorrect_type);
    CHECK_EQ(since_epoch<sys_seconds>("1990-12-31T15:59:60-08:00").error(), error::incorrect_type);
    CHECK_EQ(since_epoch<sys_seconds>("1937-01-01T12:00:27.87+00:20").error(), error::incorrect_type);
    CHECK_EQ(value(since_epoch<sys_time<milliseconds>>("1937-01-01T12:00:27.87+00:20")), -1041337172130);
    CHECK_EQ(value(since_epoch<sys_seconds>("1937-01-01T12:00:27+00:20")), -1041337173);
    CHECK_EQ(value(since_epoch<sys_seconds>("1985-04-12t23:20:50Z")), 482196050);
    CHECK_EQ(value(since_epoch<sys_seconds>("1985-04-12T23:20:50z")), 482196050);
    CHECK_EQ(value(since_epoch<sys_seconds>("1985-04-12t23:20:50z")), 482196050);
    CHECK_EQ(value(since_epoch<sys_seconds>("1996-12-19t16:39:57-08:00")), 851042397);
    CHECK_EQ(since_epoch<sys_seconds>("1985-04-12 23:20:50Z").error(), error::incorrect_type);
    CHECK_EQ(since_epoch<sys_seconds>("1985-04-12T23:20:50").error(), error::incorrect_type);
    CHECK_EQ(since_epoch<sys_seconds>("1985-04-12T23:20:50.5Z").error(), error::incorrect_type);
    CHECK_EQ(value(since_epoch<sys_time<milliseconds>>("1985-04-12T23:20:50.5Z")), 482196050500);
    CHECK_EQ(value(since_epoch<sys_time<microseconds>>("1985-04-12T23:20:50.123456Z")), 482196050123456);
    CHECK_EQ(since_epoch<sys_time<milliseconds>>("1985-04-12T23:20:50.123456Z").error(),
             error::incorrect_type);
    CHECK_EQ(value(day_of("1940-10-09")), -10676);
    CHECK_EQ(value(day_of("1980-12-08")), 3994);
    CHECK_EQ(day_of("1940-02-30").error(), error::incorrect_type);
    CHECK_EQ(day_of("1940-10-09T00:00:00Z").error(), error::incorrect_type);
#else
    CHECK_EQ(since_epoch<sys_seconds>("1985-04-12T23:20:50Z").error(), error::incorrect_type);
    CHECK_EQ(since_epoch<sys_time<milliseconds>>("1985-04-12T23:20:50.52Z").error(), error::incorrect_type);
    CHECK_EQ(day_of("1940-10-09").error(), error::incorrect_type);
#endif
}

namespace
{

struct clone_probe {
    std::string_view name;
    std::optional<std::span<std::byte const>> key;
    std::vector<std::string_view> tags;
    std::map<std::string_view, std::string_view> labels;
};

}

CBOR_DEFINE_TYPE_MEMBERS(clone_probe, cbor::untagged, name, key, tags, labels);

// Owner decision 2026-10-17: clone() of a ref gives an object that owns all its memory, recursively (rule: a clone is
// recursive). Each view of the struct is copied into the zone of the result. So the result stays valid after the input
// and the ref are gone; the address sanitizer build sees any read of the freed input.
TEST_CASE("structs: clone of a ref copies every view of the struct, recursively")
{
    std::array<std::byte, 2> const key{std::byte{1}, std::byte{2}};
    clone_probe const probe{"ann", std::span<std::byte const>(key), {"a", "bc"}, {{"k", "v"}, {"x", "yz"}}};
    auto encoded = cbor::encode<clone_probe>(probe);
    REQUIRE(encoded.has_value());
    auto read = cbor::structs<clone_probe>::decode(std::move(*encoded));
    REQUIRE(read.has_value());
    std::optional<cbor::ref<clone_probe>> decoded(std::move(*read));
    auto const cloned = decoded->clone();
    decoded.reset();
    read = cbor::structs<clone_probe>::decode(std::string());
    CHECK_EQ(cloned->name, "ann"sv);
    REQUIRE(cloned->key.has_value());
    CHECK_EQ(cloned->key->size(), 2u);
    CHECK_EQ((*cloned->key)[1], std::byte{2});
    REQUIRE_EQ(cloned->tags.size(), 2u);
    CHECK_EQ(cloned->tags[1], "bc"sv);
    REQUIRE_EQ(cloned->labels.size(), 2u);
    CHECK_EQ(cloned->labels.at("x"), "yz"sv);
    auto const text = cbor::at_path<"$", std::string_view>(std::make_shared<std::string const>("\x63xyz"));
    REQUIRE(text.has_value());
    std::string const own = text->clone();
    CHECK_EQ(own, "xyz"s);
}

// A map whose count claims more pairs than its bytes can hold gives too_little_data before any pair is read. A duplicate
// key in the first pairs is not reached.
TEST_CASE("structs: a count longer than the bytes is too_little_data before the first pair")
{
    CHECK_EQ(cbor::structs<std::map<int, int>>::decode("\xa2\x01\x01\x01"s), std::unexpected(error::too_little_data));
    // A vector of numbers is an RFC 8746 typed array, so a list of texts shows the array.
    CHECK_EQ(cbor::structs<std::vector<std::string>>::decode("\x83\x61\x61"s), std::unexpected(error::too_little_data));
}

// RFC 8742: a program writes a CBOR sequence into one buffer of its own. It keeps the bytes behind each item.
// cbor::encode into a span changes no byte behind the item. This holds also where the item ends in a head of one byte
// (the empty typed array). It writes the bytes of the std::string form.
TEST_CASE("encode: a span keeps every byte behind the struct")
{
    constexpr char sentinel = 'Z';
    pair_ab const value{1, {}};
    auto const expected = cbor::encode<pair_ab>(value);
    REQUIRE(expected.has_value());
    std::array<char, 64> buffer;
    buffer.fill(sentinel);
    auto const n = cbor::encode<pair_ab>(std::span<char>(buffer), value);
    REQUIRE(n.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *n), *expected);
    CHECK(std::ranges::all_of(std::span(buffer).subspan(*n), [](char const c) { return c == sentinel; }));
}

// cbor::encode with a target counts the bytes that it appended to memory. Such a count is a std::size_t, as
// std::string::size gives it. The expectation comes from that decision, not from the header.
TEST_CASE("structs: the count of bytes appended is a std::size_t")
{
    CHECK((std::is_same_v<decltype(cbor::encode<pair_ab>(std::declval<std::string &>(), std::declval<pair_ab const &>())),
                          cbor::result<std::size_t>>));
}

// A byte range reads as the std::string of the same bytes. The bytes come from the encoder of the library, and the
// test reads them as std::vector<std::uint8_t> and as std::span<std::byte const>, the forms that other CBOR libraries take.
TEST_CASE("structs: decode reads a range of bytes as the std::string of the same bytes")
{
    pair_ab const value{1, {2, 3}};
    auto const encoded = cbor::encode<pair_ab>(value);
    REQUIRE(encoded.has_value());
    std::vector<std::uint8_t> u(encoded->size());
    std::ranges::transform(std::as_bytes(std::span(*encoded)), u.begin(),
                           [](std::byte const b) { return std::to_integer<std::uint8_t>(b); });
    auto const from_u = cbor::structs<pair_ab>::decode(std::move(u));
    REQUIRE(from_u.has_value());
    CHECK((**from_u == value));
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const from_b = cbor::structs<pair_ab>::decode(owner, std::as_bytes(std::span(*owner)));
    REQUIRE(from_b.has_value());
    CHECK((**from_b == value));
}

// cbor::encode takes the target first, as the standard library does, so the second argument is always the value. A
// value of a type that could itself be a target, a std::string or a std::vector of bytes, is written as that value.
TEST_CASE("cbor::encode: a value of a type that is also a target is written as the value")
{
    std::string out;
    CHECK_EQ(cbor::encode(out, std::string("ab")), 3u);
    CHECK_EQ(out, cbor::diag2cbor("\"ab\""));
    std::string typed;
    CHECK_EQ(cbor::encode(typed, std::vector<std::byte>{std::byte{1}}), 4u);
    CHECK_EQ(typed, cbor::diag2cbor("64(h'01')"));
}

namespace
{

template <class T>
void check_edn_of_value(T const &value)
{
    auto const bytes = cbor::encode(value);
    REQUIRE(bytes.has_value());
    auto const text = cbor::diagnostic_notation(*bytes);
    REQUIRE(text.has_value());
    CAPTURE(*text);
    CHECK_EQ(cbor::to_edn(value), text);
}

} // namespace

// cbor::to_edn of a value is declared as the text that diagnostic_notation gives for the bytes of cbor::encode. The
// value has one row of each kind of the table of cbor::structs, and the edges of the text: a NaN with a payload, a
// float that needs its width, an escaped text, an empty string and container, a multimap and a tagged struct.
TEST_CASE("cbor::to_edn: a value gives the text of the bytes that cbor::encode writes")
{
    using namespace std::chrono;
    check_edn_of_value(0);
    check_edn_of_value(-25);
    check_edn_of_value(std::numeric_limits<std::uint64_t>::max());
    check_edn_of_value(std::numeric_limits<std::int64_t>::min());
    check_edn_of_value(true);
    check_edn_of_value(nullptr);
    check_edn_of_value(cbor::simple_value{99});
    check_edn_of_value(1.5);
    check_edn_of_value(1.1);
    check_edn_of_value(100000.0f);
    check_edn_of_value(std::bit_cast<double>(std::uint64_t{0x7ff8000000000001}));
    check_edn_of_value(std::string("a\"b\n\x01"));
    check_edn_of_value(std::string());
    std::array<std::byte, 2> const raw{std::byte{1}, std::byte{0xab}};
    check_edn_of_value(std::span<std::byte const>(raw));
    check_edn_of_value(std::vector<std::int32_t>{1, -2});
    check_edn_of_value(std::vector<std::string>{"a", "b"});
    check_edn_of_value(std::vector<std::string>{});
    check_edn_of_value(std::set<int>{3, 1});
    check_edn_of_value(std::list<int>{1, 2});
    check_edn_of_value(std::map<std::string, int>{{"a", 1}, {"b", 2}});
    check_edn_of_value(std::multimap<int, std::string>{{1, "a"}, {1, "b"}, {2, "c"}});
    check_edn_of_value(std::optional<int>{});
    check_edn_of_value(std::variant<int, std::string>{"v"});
    check_edn_of_value(std::tuple<int, bool, std::string>{1, true, "x"});
    check_edn_of_value(sys_seconds{seconds{1700000000}});
    check_edn_of_value(sys_time<milliseconds>{milliseconds{1700000000123}});
    check_edn_of_value(milliseconds{-1500});
    check_edn_of_value(year_month_day{year{1970}, month{1}, day{2}});
#if __cpp_lib_chrono >= 201907L
    check_edn_of_value(tai_time<seconds>{seconds{100}});
#endif
    check_edn_of_value(cbor::tagged_item<99, std::string>{"t"});
    check_edn_of_value(integer_order{10, 20, 30});
    check_edn_of_value(std::vector<pair_ab>{{1, {2, 3}}, {4, {}}});
}

namespace
{

struct plain_point {
    std::int64_t x;
    std::string name;
    bool operator==(plain_point const &) const = default;
};

} // namespace

#if defined(__cpp_impl_reflection) || (defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED)
// structs<T>::encode reads the members of an aggregate with no description through reflection or Boost.PFR, as
// structs<T>::decode reads them. cbor::encode does not take such a type; structs<T>::encode is the form that does.
// The bytes are the map with the member names as keys in the order of RFC 8949 4.2.1, and decode reads them back.
TEST_CASE("structs: encode writes an aggregate with no description and decode reads it back")
{
    auto const bytes = cbor::structs<plain_point>::encode(plain_point{-3, "a"});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*bytes), "{\"x\": -3, \"name\": \"a\"}");
    auto const back = cbor::structs<plain_point>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ(**back, (plain_point{-3, "a"}));
}
#endif

// structs<T>::encode of a described struct gives the bytes of cbor::encode, and the form with a target appends them
// and gives their count.
TEST_CASE("structs: encode of a described struct equals cbor::encode, and appends to a target")
{
    auto const bytes = cbor::structs<pmr_person>::encode(pmr_person{std::pmr::string("ann"), 30});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::encode(pmr_person{std::pmr::string("ann"), 30}), *bytes);
    std::string out = "x";
    auto const n = cbor::structs<pmr_person>::encode(out, pmr_person{std::pmr::string("ann"), 30});
    REQUIRE(n.has_value());
    CHECK_EQ(*n, bytes->size());
    CHECK_EQ(out, "x" + *bytes);
}

namespace
{

// A binding holds objects whose kind is known only at run time. This handle stands for such an object: the
// test builds a tree of handles and lets cbor::encode read it through describe_members<handle>::value.
struct handle {
    enum class kind { number, text, array, map, failing };
    kind k = kind::number;
    long long number = 0;
    std::string text;
    std::vector<handle> elements;
    std::shared_ptr<std::vector<std::pair<std::string, handle>> const> pairs;
};

struct handle_failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

handle number_handle(long long const n)
{
    return {handle::kind::number, n, {}, {}, {}};
}

handle text_handle(std::string t)
{
    return {handle::kind::text, 0, std::move(t), {}, {}};
}

handle array_handle(std::vector<handle> e)
{
    return {handle::kind::array, 0, {}, std::move(e), {}};
}

handle map_handle(std::vector<std::pair<std::string, handle>> p)
{
    return {handle::kind::map, 0, {}, {}, std::make_shared<std::vector<std::pair<std::string, handle>> const>(std::move(p))};
}

// A type that is a range itself, but whose description gives a number: the description decides.
struct counted_range {
    std::vector<int> items;
    auto begin() const
    {
        return items.begin();
    }
    auto end() const
    {
        return items.end();
    }
    std::size_t size() const
    {
        return items.size();
    }
};

struct integer_keys {
    std::vector<std::pair<long long, handle>> pairs;
};

struct handle_keys {
    std::vector<std::pair<handle, handle>> pairs;
};

}

template <>
struct cbor::describe_members<handle> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static std::variant<long long, std::string_view, std::span<handle const>,
                        std::span<std::pair<std::string, handle> const>>
    value(handle const &h)
    {
        switch (h.k) {
        case handle::kind::number:
            return h.number;
        case handle::kind::text:
            return std::string_view(h.text);
        case handle::kind::array:
            return std::span<handle const>(h.elements);
        case handle::kind::map:
            return std::span<std::pair<std::string, handle> const>(*h.pairs);
        case handle::kind::failing:
            break;
        }
        throw handle_failure("the object cannot give its value");
    }
};

template <>
struct cbor::describe_members<counted_range> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static std::size_t value(counted_range const &r)
    {
        return r.size();
    }
};

template <>
struct cbor::describe_members<integer_keys> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static std::span<std::pair<long long, handle> const> value(integer_keys const &k)
    {
        return k.pairs;
    }
};

template <>
struct cbor::describe_members<handle_keys> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static std::span<std::pair<handle, handle> const> value(handle_keys const &k)
    {
        return k.pairs;
    }
};

namespace
{

std::string bytes_of_edn(std::string_view const text)
{
    auto const tree = cbor::parse_edn(text);
    REQUIRE(tree.has_value());
    std::string out;
    REQUIRE(cbor::encode(out, **tree).has_value());
    return out;
}

handle sample_handle()
{
    return map_handle({{"a", number_handle(7)}, {"b", array_handle({text_handle("x"), number_handle(-3)})}});
}

}

// A binding writes no byte itself: cbor::encode asks each handle for its value and writes it. The bytes must
// be those of the same data as EDN, in every target form that encode takes.
TEST_CASE("structs: a type described by its value is written as that value, for each target")
{
    std::string const expected = bytes_of_edn(R"({"a": 7, "b": ["x", -3]})");
    handle const h = sample_handle();
    auto const fresh = cbor::encode(h);
    REQUIRE(fresh.has_value());
    CHECK_EQ(*fresh, expected);

    std::string appended = "z";
    REQUIRE(cbor::encode(appended, h).has_value());
    CHECK_EQ(appended, "z" + expected);

    std::vector<std::byte> bytes;
    REQUIRE(cbor::encode(bytes, h).has_value());
    CHECK_EQ(std::string(reinterpret_cast<char const *>(bytes.data()), bytes.size()), expected);

    std::string room(expected.size(), '\0');
    auto const n = cbor::encode(std::span<char>(room), h);
    REQUIRE(n.has_value());
    CHECK_EQ(*n, expected.size());
    CHECK_EQ(room, expected);
    CHECK_EQ(cbor::to_edn(h), R"({"a": 7, "b": ["x", -3]})");
}

// RFC 8949 3.1: the head of an array or a map holds the count, and a key is any data item. Empty containers,
// integer keys and keys that are handles themselves must be written with their count.
TEST_CASE("structs: a range of pairs is a map with any key, and an empty range is an empty container")
{
    CHECK_EQ(cbor::encode(array_handle({})), bytes_of_edn("[]"));
    CHECK_EQ(cbor::encode(map_handle({})), bytes_of_edn("{}"));
    integer_keys const ints{{{1, text_handle("one")}, {-2, number_handle(2)}}};
    CHECK_EQ(cbor::encode(ints), bytes_of_edn(R"({1: "one", -2: 2})"));
    handle_keys const keyed{{{array_handle({number_handle(1)}), text_handle("v")}}};
    CHECK_EQ(cbor::encode(keyed), bytes_of_edn(R"({[1]: "v"})"));
    std::vector<std::pair<int const, int>> const constant_keys{{1, 2}, {3, 4}};
    CHECK_EQ(cbor::encode(constant_keys), bytes_of_edn("{1: 2, 3: 4}"));
}

// A type that has a description with a value is written through it, even where the type is also a range.
TEST_CASE("structs: the value of a description wins over the range form of the type")
{
    counted_range const r{{5, 6, 7}};
    CHECK_EQ(cbor::encode(r), bytes_of_edn("3"));
}

// A std::span target has a fixed size. A value that does not fit gives no_buffer_space, and the span holds
// the bytes that came before the item that did not fit.
TEST_CASE("structs: a span that is too small gives no_buffer_space and keeps the bytes before")
{
    handle const h = sample_handle();
    auto const encoded = cbor::encode(h);
    std::string const expected = encoded.value();
    std::string room(expected.size() - 1, '\0');
    auto const n = cbor::encode(std::span<char>(room), h);
    REQUIRE_FALSE(n.has_value());
    CHECK_EQ(n.error(), cbor::error::no_buffer_space);
    CHECK_EQ(room.substr(0, 3), expected.substr(0, 3));
}

// An object of a binding may fail while it gives its value; that exception belongs to the binding and passes
// through cbor::encode unchanged, and a string target keeps what it held.
TEST_CASE("structs: an exception from a value passes through encode")
{
    handle const h = array_handle({number_handle(1), handle{handle::kind::failing, 0, {}, {}, {}}});
    CHECK_THROWS_AS(static_cast<void>(cbor::encode(h)), handle_failure);
    std::string out = "keep";
    CHECK_THROWS_AS(static_cast<void>(cbor::encode(out, h)), handle_failure);
    CHECK_EQ(out, "keep");
}

// The nesting of a binding's objects is not known at compile time; a depth over the stack of the thread gives
// nesting_depth_exceeded and not a crash. The tree is built and taken apart one level at a time, so that the
// test itself does not recurse as deep as it nests.
TEST_CASE("structs: a value nested deeper than the stack gives nesting_depth_exceeded")
{
    constexpr std::size_t depth = 1'000'000;
    handle deep = number_handle(0);
    for (std::size_t i = 0; i < depth; ++i) {
        handle outer = array_handle({});
        outer.elements.push_back(std::move(deep));
        deep = std::move(outer);
    }
    auto const e = cbor::encode(deep);
    REQUIRE_FALSE(e.has_value());
    CHECK_EQ(e.error(), cbor::error::nesting_depth_exceeded);
    while (!deep.elements.empty()) {
        handle inner = std::move(deep.elements.front());
        deep = std::move(inner);
    }
}

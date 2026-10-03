#include "host.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#ifdef __cpp_impl_reflection

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

namespace
{

template <class T>
void round_trip(std::string const &bytes, T const &expected)
{
    auto const back = cbor::generic::decode<T>(bytes);
    REQUIRE(back.has_value());
    CHECK(*back == expected);
    auto const again = cbor::generic::encode(expected);
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

} // namespace

// The expectations are the examples of RFC 8949 Appendix A, each read into a C++ type and written back.
TEST_CASE("generic: the examples of RFC 8949 Appendix A")
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
TEST_CASE("generic: an unknown key is skipped, a missing key is an error")
{
    auto const extra = cbor::generic::decode<pair_ab>("\xa3\x61\x63\x82\x01\x02\x61\x61\x01\x61\x62\x80"s);
    REQUIRE(extra.has_value());
    CHECK_EQ(extra->a, 1u);
    CHECK_EQ(cbor::generic::decode<pair_ab>("\xa1\x61\x61\x01"s).error(), error::key_not_found);
    CHECK_EQ(cbor::generic::decode<pair_ab>("\xa2\x61\x61\x61\x78\x61\x62\x80"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::generic::decode<std::uint8_t>("\x19\x01\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(cbor::generic::decode<std::uint64_t>("\x00\x00"s).error(), error::syntax_error);
}

// RFC 8428 6, the CBOR form of the example in 5.1.2: the integer labels come from the keys of the struct.
TEST_CASE("generic: a SenML pack with integer labels")
{
    std::vector<senml_record> const pack{
        {"urn:dev:ow:10e2073a01080063:", 1.320067464e9, "voltage", "V", 120.1, std::nullopt},
        {std::nullopt, std::nullopt, "current", "A", 1.2, -5}};
    auto const bytes = cbor::generic::encode(pack);
    REQUIRE(bytes.has_value());
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(0)), 0x82);
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(1)), 0xa5);
    CHECK_EQ(static_cast<std::uint8_t>(bytes->at(2)), 0x21);
    round_trip(*bytes, pack);
}

// RFC 8392 A.3 carries a COSE_Sign1 under tag 18 with a kid in the unprotected header; the views point into
// the bytes, so nothing is copied.
TEST_CASE("generic: a COSE_Sign1 under tag 18 reads as views into the message")
{
    std::string const message = "\xd2\x84\x43\xa1\x01\x26\xa1\x04\x42\x31\x31\x41\x7a\x42\x01\x02"s;
    auto const sign1 = cbor::generic::decode<cose_sign1>(message);
    REQUIRE(sign1.has_value());
    auto const &[protected_header, unprotected, payload, signature] = sign1->content;
    CHECK_EQ(protected_header.size(), 3u);
    REQUIRE(unprotected.kid.has_value());
    CHECK_EQ(static_cast<char>(unprotected.kid->front()), '1');
    CHECK_EQ(payload.size(), 1u);
    CHECK_EQ(signature.size(), 2u);
    CHECK_EQ(reinterpret_cast<char const *>(payload.data()), message.data() + 12);
    CHECK_EQ(*cbor::generic::encode(*sign1), message);
}

// CTAP 2.1 6.2.2: a nested struct under an integer key, text keys inside it, and an absent optional user.
TEST_CASE("generic: a CTAP2 getAssertion response")
{
    std::string const message =
        "\xa3\x01\xa2\x62\x69\x64\x42\x0a\x0b\x64\x74\x79\x70\x65\x6a\x70\x75\x62\x6c\x69\x63\x2d\x6b\x65\x79"
        "\x02\x41\x25\x03\x41\x30"s;
    auto const r = cbor::generic::decode<get_assertion_response>(message);
    REQUIRE(r.has_value());
    CHECK_EQ(r->credential_id.type, "public-key"sv);
    CHECK_EQ(r->credential_id.id.size(), 2u);
    CHECK_FALSE(r->user.has_value());
    CHECK_EQ(*cbor::generic::encode(*r), message);
    std::array<char, 64> room{};
    auto const placed = cbor::generic::encode(*r, std::span(room));
    REQUIRE(placed.has_value());
    CHECK_EQ(std::string_view(room.data(), *placed), message);
    std::vector<char> tight(message.size());
    auto const exact = cbor::generic::encode(*r, std::span(tight));
    REQUIRE(exact.has_value());
    CHECK_EQ(std::string_view(tight.data(), *exact), message);
    std::array<char, 8> small{};
    CHECK_EQ(cbor::generic::encode(*r, std::span(small)).error(), std::errc::no_buffer_space);
    std::string text = "x";
    CHECK_EQ(*cbor::generic::encode(*r, text), message.size());
    CHECK_EQ(text, "x" + message);
}

// RFC 8949 Appendix A: 2^64 and -2^64-1 need a bignum, tag 2 or 3 on the magnitude; simple(16) and simple(255)
// are simple values of one and two bytes.
TEST_CASE("generic: bignums and simple values of RFC 8949 Appendix A")
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
    CHECK_EQ(cbor::generic::decode<cbor::simple_value>("\xf8\x10"s).error(), error::syntax_error);
}

// A std::variant takes the first alternative whose kind matches the item, so one array can hold several kinds,
// as RFC 8949 allows.
TEST_CASE("generic: a variant takes the alternative that matches the item")
{
    using any = std::variant<std::uint64_t, std::int64_t, std::string, double, bool, std::nullptr_t,
                             std::vector<std::byte>>;
    std::vector<any> const values{std::uint64_t{1}, std::int64_t{-1}, std::string("a"), 1.5, true, nullptr,
                                  std::vector<std::byte>{std::byte{7}}};
    round_trip<std::vector<any>>("\x87\x01\x20\x61\x61\xf9\x3e\x00\xf5\xf6\x41\x07"s, values);
    CHECK_EQ(cbor::generic::decode<std::variant<std::string, bool>>("\x01"s).error(), error::incorrect_type);
}

#endif

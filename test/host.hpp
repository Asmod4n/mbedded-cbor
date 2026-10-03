#pragma once

#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// doctest prints an error by its message, because cbor::error converts to std::error_code.
template <>
struct doctest::StringMaker<cbor::error> {
    static doctest::String convert(cbor::error const e)
    {
        return std::error_code(e).message().c_str();
    }
};

// The host of the tests: a dynamic language in C++, reached only through the public interface.
namespace test
{

using cbor::error;

struct value;
using array = std::vector<value>;
struct entry;
using map = std::vector<entry>;

struct negative {
    std::uint64_t argument;
    bool operator==(negative const &) const = default;
};

struct bytes {
    std::string b;
    bool operator==(bytes const &) const = default;
};

// A bignum holds the magnitude of its absolute value, most significant byte first.
struct bignum {
    bool negative;
    std::string magnitude;
    bool operator==(bignum const &) const = default;
};

struct simple {
    std::uint8_t v;
    bool operator==(simple const &) const = default;
};

// The content of a tag is one value; a vector holds it, because a struct cannot hold its own type.
struct tagged {
    std::uint64_t tag;
    array content;
    bool operator==(tagged const &) const = default;
};

struct value {
    std::variant<std::uint64_t, negative, bignum, bytes, std::string, double, simple, array, map, tagged>
        kind;
    bool operator==(value const &) const = default;
};

struct entry {
    value key;
    value val;
    bool operator==(entry const &) const = default;
};

// The host of a test: a dynamic language in C++. Every tag_invoke builds one kind of value.
struct test_host {
    using value = test::value;
};

inline value tag_invoke(cbor::unsigned_integer_decode_t, test_host &, std::uint64_t a)
{
    return {a};
}

inline value tag_invoke(cbor::negative_integer_decode_t, test_host &, std::uint64_t a)
{
    return {negative{a}};
}

inline value tag_invoke(cbor::unsigned_bignum_decode_t, test_host &, std::string_view m)
{
    return {bignum{false, std::string(m)}};
}

inline value tag_invoke(cbor::negative_bignum_decode_t, test_host &, std::string_view m)
{
    return {bignum{true, std::string(m)}};
}

inline value tag_invoke(cbor::byte_string_decode_t, test_host &, std::string_view b)
{
    return {bytes{std::string(b)}};
}

inline value tag_invoke(cbor::text_string_decode_t, test_host &, std::string_view t)
{
    return {std::string(t)};
}

inline value tag_invoke(cbor::float_decode_t, test_host &, double f)
{
    return {f};
}

inline value tag_invoke(cbor::simple_value_decode_t, test_host &, std::uint8_t s)
{
    return {simple{s}};
}

inline value tag_invoke(cbor::array_decode_t, test_host &, std::uint64_t const size)
{
    array a;
    a.reserve(size);
    return {std::move(a)};
}

inline value tag_invoke(cbor::array_append_t, test_host &, value &&a, value &&e)
{
    std::get<array>(a.kind).push_back(std::move(e));
    return std::move(a);
}

inline value tag_invoke(cbor::map_decode_t, test_host &, std::uint64_t const size)
{
    map m;
    m.reserve(size);
    return {std::move(m)};
}

inline value tag_invoke(cbor::map_insert_t, test_host &, value &&m, value &&k, value &&v)
{
    std::get<map>(m.kind).push_back(entry{std::move(k), std::move(v)});
    return std::move(m);
}

inline value tag_invoke(cbor::tag_decode_t, test_host &, std::uint64_t tag, value content)
{
    return {tagged{tag, array{std::move(content)}}};
}

struct string_writer {
    std::string bytes;

    string_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view part)
    {
        bytes.append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

// The way back, as a binding walks its own values.
// The answers of this host to the questions of the encoder. Each answer reads the value and decides
// nothing.
inline cbor::kind tag_invoke(cbor::kind_of_t, test_host &, value const &v)
{
    return std::visit(
        [](auto const &k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, std::uint64_t>)
                return cbor::kind::unsigned_integer;
            else if constexpr (std::is_same_v<K, negative>)
                return cbor::kind::negative_integer;
            else if constexpr (std::is_same_v<K, bignum>)
                return k.negative ? cbor::kind::negative_bignum : cbor::kind::unsigned_bignum;
            else if constexpr (std::is_same_v<K, bytes>)
                return cbor::kind::byte_string;
            else if constexpr (std::is_same_v<K, std::string>)
                return cbor::kind::text_string;
            else if constexpr (std::is_same_v<K, double>)
                return cbor::kind::floating_point;
            else if constexpr (std::is_same_v<K, simple>)
                return cbor::kind::simple_value;
            else if constexpr (std::is_same_v<K, array>)
                return cbor::kind::array;
            else if constexpr (std::is_same_v<K, map>)
                return cbor::kind::map;
            else
                return cbor::kind::registered;
        },
        v.kind);
}

// A negative value -1 - n answers with its absolute value n + 1.
inline std::uint64_t tag_invoke(cbor::unsigned_of_t, test_host &, value const &v)
{
    if (auto const *n = std::get_if<negative>(&v.kind))
        return n->argument + 1;
    return std::get<std::uint64_t>(v.kind);
}

inline std::string_view tag_invoke(cbor::magnitude_of_t, test_host &, value const &v)
{
    return std::get<bignum>(v.kind).magnitude;
}

inline std::string_view tag_invoke(cbor::bytes_of_t, test_host &, value const &v)
{
    return std::get<bytes>(v.kind).b;
}

inline std::string_view tag_invoke(cbor::text_of_t, test_host &, value const &v)
{
    return std::get<std::string>(v.kind);
}

inline double tag_invoke(cbor::float_of_t, test_host &, value const &v)
{
    return std::get<double>(v.kind);
}

inline std::uint8_t tag_invoke(cbor::simple_of_t, test_host &, value const &v)
{
    return std::get<simple>(v.kind).v;
}

inline std::uint64_t tag_invoke(cbor::array_size_t, test_host &, value const &v)
{
    return std::get<array>(v.kind).size();
}

inline value const &tag_invoke(cbor::array_at_t, test_host &, value const &v, std::uint64_t i)
{
    return std::get<array>(v.kind).at(i);
}

inline std::uint64_t tag_invoke(cbor::map_size_t, test_host &, value const &v)
{
    return std::get<map>(v.kind).size();
}

template <class F>
inline void tag_invoke(cbor::map_for_each_t, test_host &, value const &v, F const &f)
{
    for (auto const &[key, val] : std::get<map>(v.kind))
        f(key, val);
}

// In this host a tagged value stands for a registered object: its number is the tag, its content
// is what before_encode gives.
inline std::uint64_t tag_invoke(cbor::registered_tag_t, test_host &, value const &v)
{
    return std::get<tagged>(v.kind).tag;
}

inline value tag_invoke(cbor::before_encode_t, test_host &, value const &v)
{
    return std::get<tagged>(v.kind).content.at(0);
}

inline std::string encoded(value const &v)
{
    test_host host;
    string_writer w;
    REQUIRE(cbor::encode<16>(host, w, v).has_value());
    return w.bytes;
}

template <std::size_t DepthMax = 16>
inline std::expected<value, error> decoded(std::string_view wire)
{
    test_host host;
    return cbor::decode<DepthMax>(host, wire);
}

template <std::size_t DepthMax = 16>
inline error decode_error(std::string_view wire)
{
    auto const v = decoded<DepthMax>(wire);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}

inline value V(int i)
{
    return {static_cast<std::uint64_t>(i)};
}

inline value V(std::string s)
{
    return {std::move(s)};
}

inline value V(double d)
{
    return {d};
}

inline value V(simple s)
{
    return {s};
}

inline value V(value v)
{
    return v;
}

template <class... T>
value A(T... t)
{
    return {array{V(std::move(t))...}};
}

template <class... T>
value M(T... t)
{
    array const flat{V(std::move(t))...};
    map m;
    for (std::size_t i = 0; i < flat.size(); i += 2)
        m.push_back(entry{flat.at(i), flat.at(i + 1)});
    return {std::move(m)};
}

inline void check_both(std::string_view wire, value const &expected)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    CHECK_EQ(encoded(expected), wire);
}

inline std::string repeat(std::string_view part, std::size_t n)
{
    std::string s;
    for (std::size_t i = 0; i < n; ++i)
        s += part;
    return s;
}

} // namespace test

using namespace test;

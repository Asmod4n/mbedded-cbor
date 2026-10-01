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
    std::variant<std::uint64_t, negative, bytes, std::string, double, simple, array, map, tagged> kind;
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

inline value tag_invoke(cbor::array_decode_t, test_host &)
{
    return {array{}};
}

inline value tag_invoke(cbor::array_append_t, test_host &, value a, value e)
{
    std::get<array>(a.kind).push_back(std::move(e));
    return a;
}

inline value tag_invoke(cbor::map_decode_t, test_host &)
{
    return {map{}};
}

inline value tag_invoke(cbor::map_insert_t, test_host &, value m, value k, value v)
{
    std::get<map>(m.kind).push_back(entry{std::move(k), std::move(v)});
    return m;
}

inline value tag_invoke(cbor::tag_decode_t, test_host &, std::uint64_t tag, value content)
{
    return {tagged{tag, array{std::move(content)}}};
}

struct string_writer {
    std::string bytes;

    std::expected<void, std::errc> reserve(std::size_t size)
    {
        bytes.reserve(bytes.size() + size);
        return {};
    }

    std::expected<void, std::errc> append(std::string_view part)
    {
        bytes.append(part);
        return {};
    }
};

// The way back, as a binding walks its own values.
inline void encode_into(cbor::encoder<string_writer> &e, value const &v)
{
    std::visit(
        [&](auto const &k) {
            using K = std::decay_t<decltype(k)>;
            if constexpr (std::is_same_v<K, std::uint64_t>) {
                REQUIRE(e.head_encode(cbor::major_type::unsigned_integer, k).has_value());
            } else if constexpr (std::is_same_v<K, negative>) {
                REQUIRE(e.head_encode(cbor::major_type::negative_integer, k.argument).has_value());
            } else if constexpr (std::is_same_v<K, bytes>) {
                REQUIRE(e.byte_string_encode(k.b).has_value());
            } else if constexpr (std::is_same_v<K, std::string>) {
                REQUIRE(e.text_string_encode(k).has_value());
            } else if constexpr (std::is_same_v<K, double>) {
                REQUIRE(e.float_encode(k).has_value());
            } else if constexpr (std::is_same_v<K, simple>) {
                REQUIRE(e.head_encode(cbor::major_type::simple_float, k.v).has_value());
            } else if constexpr (std::is_same_v<K, array>) {
                REQUIRE(e.head_encode(cbor::major_type::array, k.size()).has_value());
                for (value const &element : k)
                    encode_into(e, element);
            } else if constexpr (std::is_same_v<K, map>) {
                REQUIRE(e.head_encode(cbor::major_type::map, k.size()).has_value());
                for (auto const &[key, val] : k) {
                    encode_into(e, key);
                    encode_into(e, val);
                }
            } else {
                REQUIRE(e.head_encode(cbor::major_type::tag, k.tag).has_value());
                encode_into(e, k.content.at(0));
            }
        },
        v.kind);
}

inline std::string encoded(value const &v)
{
    string_writer w;
    cbor::encoder<string_writer> e{w};
    encode_into(e, v);
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
        m.push_back(entry{flat[i], flat[i + 1]});
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

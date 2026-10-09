#pragma once

#include <cbor/cbor.hpp>
#include "nesting_depth_max_guard.hpp"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// doctest prints an error by its message.
template <>
struct doctest::StringMaker<cbor::error> {
    static doctest::String convert(cbor::error const e)
    {
        return std::string(cbor::message(e)).c_str();
    }
};

// The binding of the tests: a dynamic language in C++, reached only through the public interface.
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

struct tagged;

// A text that fits next to the index of the variant is held in place; a longer text is held on the heap.
struct short_text {
    std::array<char, 23> c;
    std::uint8_t size;

    std::string_view view() const
    {
        return {c.data(), size};
    }

    bool operator==(short_text const &o) const
    {
        return view() == o.view();
    }
};

// Every alternative is trivially copyable, so a move of a value copies 32 bytes. The value owns what each
// pointer points to.
struct value {
    std::variant<std::uint64_t, negative, double, simple, short_text, std::string *, bytes *, bignum *, array *,
                 map *, tagged *>
        kind{std::uint64_t{0}};

    value() = default;

    value(std::uint64_t const u) : kind(u)
    {
    }

    value(negative const n) : kind(n)
    {
    }

    value(double const d) : kind(d)
    {
    }

    value(simple const s) : kind(s)
    {
    }

    value(short_text const t) : kind(t)
    {
    }

    value(std::string t)
    {
        if (t.size() <= short_text{}.c.size()) {
            short_text s{};
            std::ranges::copy(t, s.c.begin());
            s.size = static_cast<std::uint8_t>(t.size());
            kind = s;
        } else {
            kind = new std::string(std::move(t));
        }
    }

    value(bytes b) : kind(new bytes(std::move(b)))
    {
    }

    value(bignum b) : kind(new bignum(std::move(b)))
    {
    }

    value(array a) : kind(new array(std::move(a)))
    {
    }

    value(map m);

    value(tagged t);

    value(value const &o);

    value(value &&o) noexcept : kind(o.kind)
    {
        o.kind = std::uint64_t{0};
    }

    value &operator=(value o) noexcept
    {
        std::swap(kind, o.kind);
        return *this;
    }

    ~value();

    std::string_view text() const
    {
        if (auto const *s = std::get_if<short_text>(&kind))
            return s->view();
        return *std::get<std::string *>(kind);
    }

    bool operator==(value const &o) const;
};

struct entry {
    value key;
    value val;
    bool operator==(entry const &) const = default;
};

inline value::value(map m) : kind(new map(std::move(m)))
{
}

struct tagged {
    std::uint64_t tag;
    value content;
    bool operator==(tagged const &) const = default;
};

inline value::value(tagged t) : kind(new tagged(std::move(t)))
{
}

inline value::~value()
{
    if (auto const *t = std::get_if<std::string *>(&kind))
        delete *t;
    else if (auto const *b = std::get_if<bytes *>(&kind))
        delete *b;
    else if (auto const *n = std::get_if<bignum *>(&kind))
        delete *n;
    else if (auto const *a = std::get_if<array *>(&kind))
        delete *a;
    else if (auto const *m = std::get_if<map *>(&kind))
        delete *m;
    else if (auto const *g = std::get_if<tagged *>(&kind))
        delete *g;
}

inline value::value(value const &o) : kind(o.kind)
{
    if (auto const *t = std::get_if<std::string *>(&o.kind))
        kind = new std::string(**t);
    else if (auto const *b = std::get_if<bytes *>(&o.kind))
        kind = new bytes(**b);
    else if (auto const *n = std::get_if<bignum *>(&o.kind))
        kind = new bignum(**n);
    else if (auto const *a = std::get_if<array *>(&o.kind))
        kind = new array(**a);
    else if (auto const *m = std::get_if<map *>(&o.kind))
        kind = new map(**m);
    else if (auto const *g = std::get_if<tagged *>(&o.kind))
        kind = new tagged(**g);
}

inline bool value::operator==(value const &o) const
{
    if (kind.index() != o.kind.index())
        return false;
    if (auto const *t = std::get_if<std::string *>(&kind))
        return **t == *std::get<std::string *>(o.kind);
    if (auto const *b = std::get_if<bytes *>(&kind))
        return **b == *std::get<bytes *>(o.kind);
    if (auto const *n = std::get_if<bignum *>(&kind))
        return **n == *std::get<bignum *>(o.kind);
    if (auto const *a = std::get_if<array *>(&kind))
        return **a == *std::get<array *>(o.kind);
    if (auto const *m = std::get_if<map *>(&kind))
        return **m == *std::get<map *>(o.kind);
    if (auto const *g = std::get_if<tagged *>(&kind))
        return **g == *std::get<tagged *>(o.kind);
    return kind == o.kind;
}

// The pointee of a heap alternative, or nullptr when the value holds another alternative.
template <class T>
T const *get_if(value const &v)
{
    auto const *p = std::get_if<T *>(&v.kind);
    return p ? *p : nullptr;
}

// The binding of a test: a dynamic language in C++. Every member function builds one kind of value.
struct test_binding : cbor::binding<test::value> {
    value unsigned_integer_decode(std::uint64_t const a)
    {
        return {a};
    }

    value negative_integer_decode(std::uint64_t a)
    {
        return {negative{a}};
    }

    value unsigned_bignum_decode(std::string_view m)
    {
        return {bignum{false, std::string(m)}};
    }

    value negative_bignum_decode(std::string_view m)
    {
        return {bignum{true, std::string(m)}};
    }

    value byte_string_decode(std::string_view b)
    {
        return {bytes{std::string(b)}};
    }

    value text_string_decode(std::string_view t)
    {
        if (t.size() > short_text{}.c.size())
            return {std::string(t)};
        short_text s{};
        std::ranges::copy(t, s.c.begin());
        s.size = static_cast<std::uint8_t>(t.size());
        return {s};
    }

    value float_decode(double f)
    {
        return {f};
    }

    value simple_value_decode(std::uint8_t s)
    {
        return {simple{s}};
    }

    value array_decode(std::uint64_t const size)
    {
        array a;
        a.reserve(size);
        return {std::move(a)};
    }

    value array_append(value &&a, value &&e)
    {
        std::get<array *>(a.kind)->push_back(std::move(e));
        return std::move(a);
    }

    value map_decode(std::uint64_t const size)
    {
        map m;
        m.reserve(size);
        return {std::move(m)};
    }

    value map_insert(value &&m, value &&k, value &&v)
    {
        std::get<map *>(m.kind)->emplace_back(std::move(k), std::move(v));
        return std::move(m);
    }

    value tag_decode(std::uint64_t tag, value content)
    {
        return {tagged{tag, std::move(content)}};
    }

    cbor::kind kind_of(value const &v)
    {
        switch (v.kind.index()) {
        case 0:
            return cbor::kind::unsigned_integer;
        case 1:
            return cbor::kind::negative_integer;
        case 2:
            return cbor::kind::floating_point;
        case 3:
            return cbor::kind::simple_value;
        case 4:
        case 5:
            return cbor::kind::text_string;
        case 6:
            return cbor::kind::byte_string;
        case 7:
            return std::get<bignum *>(v.kind)->negative ? cbor::kind::negative_bignum
                                                        : cbor::kind::unsigned_bignum;
        case 8:
            return cbor::kind::array;
        case 9:
            return cbor::kind::map;
        default:
            return cbor::kind::registered;
        }
    }

    std::uint64_t unsigned_of(value const &v)
    {
        if (auto const *n = std::get_if<negative>(&v.kind))
            return n->argument + 1;
        return std::get<std::uint64_t>(v.kind);
    }

    std::string_view magnitude_of(value const &v)
    {
        return std::get<bignum *>(v.kind)->magnitude;
    }

    std::string_view bytes_of(value const &v)
    {
        return std::get<bytes *>(v.kind)->b;
    }

    std::string_view text_of(value const &v)
    {
        return v.text();
    }

    double float_of(value const &v)
    {
        return std::get<double>(v.kind);
    }

    std::uint8_t simple_of(value const &v)
    {
        return std::get<simple>(v.kind).v;
    }

    std::uint64_t array_size(value const &v)
    {
        return std::get<array *>(v.kind)->size();
    }

    value const & array_at(value const &v, std::uint64_t i)
    {
        return std::get<array *>(v.kind)->at(i);
    }

    std::uint64_t map_size(value const &v)
    {
        return std::get<map *>(v.kind)->size();
    }

    template <class F>
    void map_for_each(value const &v, F const &f)
    {
        for (auto const &[key, val] : *std::get<map *>(v.kind))
            f(key, val);
    }

    std::uint64_t registered_tag(value const &v)
    {
        return std::get<tagged *>(v.kind)->tag;
    }

    value const &before_encode(value const &v)
    {
        return std::get<tagged *>(v.kind)->content;
    }
};

struct string_writer {
    std::string encoded;

    string_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view part)
    {
        encoded.append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

// The way back, as a binding walks its own values.
// The answers of this binding to the questions of the encoder. Each answer reads the value and decides
// nothing.
// A negative value -1 - n answers with its absolute value n + 1.
// In this binding a tagged value stands for a registered object: its number is the tag, its content
// is what before_encode gives.
inline std::string encoded([[maybe_unused]] value const &v)
{
    [[maybe_unused]] test_binding binding;
    string_writer w;
    REQUIRE(cbor::encode(binding, w, v).has_value());
    return w.encoded;
}

inline std::expected<value, error> decoded(std::string_view wire)
{
    test_binding binding;
    return cbor::lazy_decode(binding, *cbor::decode(wire));
}

inline error decode_error(std::string_view wire)
{
    auto const v = decoded(wire);
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

inline void check_both(std::string_view wire, [[maybe_unused]] value const &expected)
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

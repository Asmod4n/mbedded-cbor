#pragma once

#include <cbor/cbor.hpp>
#include "limits_guard.hpp"
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <algorithm>
#include <bit>
#include <cmath>
#include <expected>
#include <span>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif
#include <variant>
#include <vector>

// doctest prints an error by its message.
template <>
struct doctest::StringMaker<cbor::error> {
    static doctest::String convert(cbor::error const e)
    {
        return cbor::make_error_code(e).message().c_str();
    }
};

// The reference model of the tests: a value of a dynamic language in C++, built and compared only in the tests.
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

// The preferred serialization of RFC 8949 4.2.2, written by hand from the specification: the shortest head
// for every argument and the shortest float that keeps the value. Documentation examples read this form,
// because it is what most senders write and what diagnostic notation prints without encoding indicators.
inline void preferred_head(std::string &out, unsigned const major, std::uint64_t const argument)
{
    auto const put = [&](unsigned const info, int const bytes) {
        out.push_back(static_cast<char>(major << 5 | info));
        for (int shift = 8 * (bytes - 1); shift >= 0; shift -= 8)
            out.push_back(static_cast<char>(argument >> shift));
    };
    if (argument < 24)
        out.push_back(static_cast<char>(major << 5 | argument));
    else if (argument <= 0xff)
        put(24, 1);
    else if (argument <= 0xffff)
        put(25, 2);
    else if (argument <= 0xffffffff)
        put(26, 4);
    else
        put(27, 8);
}

inline bool float_fits(double const v, int const precision, int const least_exponent, int const max_exponent)
{
    if (v == 0)
        return true;
    int e = 0;
    std::frexp(v, &e);
    if (e > max_exponent)
        return false;
    int const q = std::max(e - precision, least_exponent);
    double const n = std::ldexp(v, -q);
    return n == std::trunc(n) && std::fabs(n) < std::ldexp(1.0, precision);
}

inline void preferred_float(std::string &out, double const v)
{
    auto const put = [&](unsigned const info, std::uint64_t const bits, int const bytes) {
        out.push_back(static_cast<char>(7u << 5 | info));
        for (int shift = 8 * (bytes - 1); shift >= 0; shift -= 8)
            out.push_back(static_cast<char>(bits >> shift));
    };
    if (std::isnan(v))
        return put(25, 0x7e00, 2);
    std::uint64_t const sign = std::signbit(v) ? 1 : 0;
    if (std::isinf(v))
        return put(25, sign << 15 | 0x7c00, 2);
    double const a = std::fabs(v);
    if (float_fits(a, 11, -24, 16)) {
        std::uint64_t half = 0;
        if (a != 0) {
            int e = 0;
            double const m = std::frexp(a, &e);
            if (e - 1 >= -14)
                half = static_cast<std::uint64_t>(e - 1 + 15) << 10 | static_cast<std::uint64_t>((m * 2 - 1) * 1024);
            else
                half = static_cast<std::uint64_t>(std::ldexp(a, 24));
        }
        return put(25, sign << 15 | half, 2);
    }
    if (float_fits(a, 24, -149, 128))
        return put(26, std::bit_cast<std::uint32_t>(static_cast<float>(v)), 4);
    put(27, std::bit_cast<std::uint64_t>(v), 8);
}

inline void preferred_write(std::string &out, value const &v)
{
    std::visit(
        [&](auto const &k) {
            using K = std::remove_cvref_t<decltype(k)>;
            if constexpr (std::same_as<K, std::uint64_t>) {
                preferred_head(out, 0, k);
            } else if constexpr (std::same_as<K, negative>) {
                preferred_head(out, 1, k.argument);
            } else if constexpr (std::same_as<K, double>) {
                preferred_float(out, k);
            } else if constexpr (std::same_as<K, simple>) {
                preferred_head(out, 7, k.v);
            } else if constexpr (std::same_as<K, short_text>) {
                preferred_head(out, 3, k.size);
                out.append(k.view());
            } else if constexpr (std::same_as<K, std::string *>) {
                preferred_head(out, 3, k->size());
                out.append(*k);
            } else if constexpr (std::same_as<K, bytes *>) {
                preferred_head(out, 2, k->b.size());
                out.append(k->b);
            } else if constexpr (std::same_as<K, bignum *>) {
                preferred_head(out, 6, k->negative ? 3 : 2);
                preferred_head(out, 2, k->magnitude.size());
                out.append(k->magnitude);
            } else if constexpr (std::same_as<K, array *>) {
                preferred_head(out, 4, k->size());
                for (value const &e : *k)
                    preferred_write(out, e);
            } else if constexpr (std::same_as<K, map *>) {
                preferred_head(out, 5, k->size());
                for (entry const &e : *k) {
                    preferred_write(out, e.key);
                    preferred_write(out, e.val);
                }
            } else {
                preferred_head(out, 6, k->tag);
                preferred_write(out, k->content);
            }
        },
        v.kind);
}

inline std::string preferred(value const &v)
{
    std::string out;
    preferred_write(out, v);
    return out;
}

// The encoder writes a run-time integer at its native width: the initial byte with eight argument bytes.
inline std::string native_integer(cbor::major_type const major, std::uint64_t const argument)
{
    std::string s(1, static_cast<char>(std::to_underlying(major) << 5 | 27));
    for (int shift = 56; shift >= 0; shift -= 8)
        s.push_back(static_cast<char>(argument >> shift));
    return s;
}

inline std::string native_unsigned(std::uint64_t const n)
{
    return native_integer(cbor::major_type::unsigned_integer, n);
}

inline std::string repeat(std::string_view part, std::size_t n)
{
    std::string s;
    for (std::size_t i = 0; i < n; ++i)
        s += part;
    return s;
}

// The index selector of RFC 9535 through the public interface of lazy: an index counts from the end when it
// is negative, and on a map it names the integer key, as at_path reads it.
inline std::expected<cbor::lazy, cbor::error> index_select(cbor::lazy const &l, std::int64_t const index)
{
    if (auto const e = l.elements(); !e && e.error() == cbor::error::not_indexable)
        return l.at(cbor::key{index});
    if (index >= 0)
        return l.at(static_cast<std::size_t>(index));
    auto const size = l.size();
    if (!size)
        return std::unexpected(size.error());
    std::uint64_t const back = std::uint64_t{0} - static_cast<std::uint64_t>(index);
    if (back > *size)
        return std::unexpected(cbor::error::index_out_of_bounds);
    return l.at(static_cast<std::size_t>(*size - back));
}

// The magnitude of a bignum without its leading zero bytes, as RFC 8949 3.4.3 lets a sender add them.
inline std::string_view magnitude_without_leading_zeros(std::string_view const m)
{
    std::size_t const first = m.find_first_not_of('\0');
    return first == std::string_view::npos ? std::string_view{} : m.substr(first);
}

// RFC 8949 3.4.3: tag 3 holds n for the value -1 - n, so the absolute value is n + 1.
inline std::string magnitude_plus_one(std::string_view const m)
{
    std::string sum(m);
    for (auto it = sum.rbegin(); it != sum.rend(); ++it) {
        *it = static_cast<char>(static_cast<unsigned char>(*it) + 1);
        if (*it != '\0')
            return sum;
    }
    sum.insert(sum.begin(), '\1');
    return sum;
}

// RFC 8949 3.3 and Appendix D: a float of 16 or 32 bits as a double. A NaN keeps its sign and its payload, which
// RFC 8949 4.1 extends with zeros at the right; a conversion of the processor would set the quiet bit.
inline double double_of(std::uint8_t const info, std::uint64_t const bits)
{
    if (info == 27)
        return std::bit_cast<double>(bits);
    bool const half = info == 25;
    int const exponent_bits = half ? 5 : 8;
    int const significand_bits = half ? 10 : 23;
    std::uint64_t const sign = bits >> (exponent_bits + significand_bits) & 1;
    std::uint64_t const exponent = bits >> significand_bits & ((1u << exponent_bits) - 1);
    std::uint64_t const significand = bits & ((std::uint64_t{1} << significand_bits) - 1);
    if (exponent == (1u << exponent_bits) - 1)
        return std::bit_cast<double>(sign << 63 | std::uint64_t{0x7ff} << 52 | significand << (52 - significand_bits));
    if (!half)
        return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
    double const magnitude = exponent == 0 ? std::ldexp(static_cast<double>(significand), -24)
                                           : std::ldexp(static_cast<double>(1024 + significand),
                                                        static_cast<int>(exponent) - 25);
    return sign != 0 ? -magnitude : magnitude;
}

inline std::expected<value, error> value_of(cbor::item const &it, std::vector<cbor::item const *> &open);

// The reference model of a decoded item tree. Tag 28 is the value it marks, and tag 29 is the value it points to,
// as an application without identity reads them. A bignum that fits 64 bits is an integer. A reference to an item
// whose read is not complete is sharedref_not_complete, as a cycle has no value in this model.
inline std::expected<value, error> value_of(cbor::item const &it, std::vector<cbor::item const *> &open)
{
    if (auto const *t = std::get_if<std::string_view>(&it.content))
        return value{std::string(*t)};
    if (auto const *b = std::get_if<std::span<std::byte const>>(&it.content))
        return value{bytes{std::string(reinterpret_cast<char const *>(b->data()), b->size())}};
    if (it.major_type == cbor::major_type::simple_float && it.additional_information >= 25 &&
        it.additional_information <= 27)
        return value{double_of(it.additional_information, it.argument)};
    if (auto const *v = std::get_if<cbor::simple_value>(&it.content))
        return value{simple{std::to_underlying(*v)}};
    if (auto const *r = std::get_if<cbor::sharedref>(&it.content)) {
        if (std::ranges::find(open, r->shareable) != open.end())
            return std::unexpected(error::sharedref_not_complete);
        open.push_back(&it);
        auto shared = value_of(*r->shareable, open);
        open.pop_back();
        return shared;
    }
    open.push_back(&it);
    std::expected<value, error> made = value{};
    if (auto const *container = std::get_if<cbor::data_items>(&it.content)) {
        auto const children = container->span();
        if (it.major_type == cbor::major_type::array) {
            array a;
            for (cbor::item const *const child : children) {
                auto e = value_of(*child, open);
                if (!e) {
                    made = std::unexpected(e.error());
                    break;
                }
                a.push_back(std::move(*e));
            }
            if (made)
                made = value{std::move(a)};
        } else {
            map m;
            for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
                auto k = value_of(*children[i], open);
                auto v = k ? value_of(*children[i + 1], open) : std::expected<value, error>(std::unexpected(k.error()));
                if (!v) {
                    made = std::unexpected(v.error());
                    break;
                }
                m.push_back(entry{std::move(*k), std::move(*v)});
            }
            if (made)
                made = value{std::move(m)};
        }
    } else if (auto const *content = std::get_if<cbor::item const *>(&it.content)) {
        cbor::item const &inner = **content;
        auto const *magnitude = std::get_if<std::span<std::byte const>>(&inner.content);
        if (it.argument == 28) {
            made = value_of(inner, open);
        } else if ((it.argument == 2 || it.argument == 3) && magnitude != nullptr) {
            bool const negative_value = it.argument == 3;
            std::string_view const m = magnitude_without_leading_zeros(
                std::string_view(reinterpret_cast<char const *>(magnitude->data()), magnitude->size()));
            if (m.size() <= sizeof(std::uint64_t)) {
                std::uint64_t n = 0;
                for (char const c : m)
                    n = n << 8 | static_cast<unsigned char>(c);
                made = negative_value ? value{negative{n}} : value{n};
            } else {
                made = value{bignum{negative_value, negative_value ? magnitude_plus_one(m) : std::string(m)}};
            }
        } else {
            auto c = value_of(inner, open);
            made = c ? std::expected<value, error>(value{tagged{it.argument, std::move(*c)}})
                     : std::unexpected(c.error());
        }
    } else if (it.major_type == cbor::major_type::unsigned_integer) {
        made = value{it.argument};
    } else {
        made = value{negative{it.argument}};
    }
    open.pop_back();
    return made;
}

inline std::expected<value, error> value_of(cbor::lazy const &l)
{
    auto const tree = l.decode();
    if (!tree)
        return std::unexpected(tree.error());
    std::vector<cbor::item const *> open;
    return value_of(**tree, open);
}

template <class E>
inline std::expected<value, error> value_of(std::expected<cbor::lazy, E> const &l)
{
    if (!l)
        return std::unexpected(l.error());
    return value_of(*l);
}

inline std::expected<value, error> decoded(std::string_view const wire)
{
    auto const l = cbor::lazy::from(wire);
    if (!l)
        return std::unexpected(l.error());
    return value_of(*l);
}

inline error decode_error(std::string_view const wire)
{
    auto const v = decoded(wire);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}

// RFC 8949 3.4.3: tag 3 holds n for the value -1 - n, so n is the absolute value less one.
inline std::string magnitude_minus_one(std::string_view const m)
{
    std::string n(m);
    for (auto it = n.rbegin(); it != n.rend(); ++it) {
        *it = static_cast<char>(static_cast<unsigned char>(*it) - 1);
        if (*it != '\xff')
            break;
    }
    return std::string(magnitude_without_leading_zeros(n));
}

// A bignum of the model is an integer where its value fits 64 bits, and a tag over its magnitude otherwise. A
// negative bignum of absolute value zero is no value.
template <class Target>
std::expected<void, error> bignum_encode(cbor::encoder<Target> &e, bignum const &b)
{
    std::string_view const m = magnitude_without_leading_zeros(b.magnitude);
    if (b.negative && m.empty())
        return std::unexpected(error::unsupported_value);
    std::string const n = b.negative ? magnitude_minus_one(m) : std::string(m);
    if (n.size() <= sizeof(std::uint64_t)) {
        std::uint64_t a = 0;
        for (char const c : n)
            a = a << 8 | static_cast<unsigned char>(c);
        return b.negative ? e.negative_integer(a) : e.unsigned_integer(a);
    }
    if (auto const r = e.tag(b.negative ? 3 : 2); !r)
        return r;
    return e.byte_string(std::as_bytes(std::span(n)));
}

// The reference model written through the public encoder: an integer at its native width of 64 bits, a float as
// a double, a bignum as tag 2 or 3 over its magnitude, every other head in its shortest form.
template <class Target>
std::expected<void, error> value_encode(cbor::encoder<Target> &e, value const &v)
{
    return std::visit(
        [&](auto const &k) -> std::expected<void, error> {
            using K = std::remove_cvref_t<decltype(k)>;
            if constexpr (std::same_as<K, std::uint64_t>) {
                return e.unsigned_integer(k);
            } else if constexpr (std::same_as<K, negative>) {
                return e.negative_integer(k.argument);
            } else if constexpr (std::same_as<K, double>) {
                return e.floating_point(k);
            } else if constexpr (std::same_as<K, simple>) {
                return e.simple_value(static_cast<cbor::simple_value>(k.v));
            } else if constexpr (std::same_as<K, short_text>) {
                return e.text_string(k.view());
            } else if constexpr (std::same_as<K, std::string *>) {
                return e.text_string(std::string_view(*k));
            } else if constexpr (std::same_as<K, bytes *>) {
                return e.byte_string(std::as_bytes(std::span(k->b)));
            } else if constexpr (std::same_as<K, bignum *>) {
                return bignum_encode(e, *k);
            } else if constexpr (std::same_as<K, array *>) {
                if (auto const r = e.array(k->size()); !r)
                    return r;
                for (value const &x : *k)
                    if (auto const r = value_encode(e, x); !r)
                        return r;
                return {};
            } else if constexpr (std::same_as<K, map *>) {
                if (auto const r = e.map(k->size()); !r)
                    return r;
                for (entry const &x : *k) {
                    if (auto const r = value_encode(e, x.key); !r)
                        return r;
                    if (auto const r = value_encode(e, x.val); !r)
                        return r;
                }
                return {};
            } else {
                if (auto const r = e.tag(k->tag); !r)
                    return r;
                return value_encode(e, k->content);
            }
        },
        v.kind);
}

template <class Target>
auto encode(Target &&target, value const &v)
    -> decltype(std::declval<cbor::encoder<std::remove_cvref_t<Target>> &>().finish())
{
    cbor::encoder e{std::forward<Target>(target)};
    if (auto const r = value_encode(e, v); !r)
        return std::unexpected(r.error());
    return e.finish();
}

inline std::expected<cbor::owning_ref<std::span<std::string_view const>>, error> encode_iov(value const &v)
{
    cbor::iov_writer w;
    return encode(w, v);
}

struct string_writer {
    std::string encoded;

    string_writer &allocate(std::size_t)
    {
        return *this;
    }

    std::expected<void, std::errc> append(std::string_view const part)
    {
        encoded.append(part);
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

inline std::string encoded(value const &v)
{
    std::string out;
    auto const written = encode(out, v);
    REQUIRE(written.has_value());
    static_cast<void>(written);
    return out;
}

inline void check_both(std::string_view const wire, value const &expected)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    auto const back = decoded(encoded(expected));
    REQUIRE(back.has_value());
    CHECK(*back == expected);
}

// The value of the node that a singular query selects.
inline std::expected<value, error> at_path(std::string_view const path, cbor::lazy const &l)
{
    return value_of(cbor::at_path(path, l));
}

template <cbor::fixed_string Path>
std::expected<value, error> at_path(cbor::lazy const &l)
{
    return value_of(cbor::at_path<Path>(l));
}

// The nodelist of a query as an array of the values of its nodes.
inline std::expected<value, error> values_of(std::expected<std::vector<cbor::lazy>, error> const &nodes)
{
    if (!nodes)
        return std::unexpected(nodes.error());
    array a;
    for (cbor::lazy const &n : *nodes) {
        auto v = value_of(n);
        if (!v)
            return std::unexpected(v.error());
        a.push_back(std::move(*v));
    }
    return value{std::move(a)};
}

inline std::expected<value, error> query(std::string_view const path, cbor::lazy const &l)
{
    return values_of(cbor::query(path, l));
}

template <cbor::fixed_string Path>
std::expected<value, error> query(cbor::lazy const &l)
{
    return values_of(cbor::query<Path>(l));
}

} // namespace test

using namespace test;

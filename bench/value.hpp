#pragma once

#include <cbor/cbor.hpp>

#include <algorithm>
#include <expected>
#include <span>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace bench
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

template <class T>
T const *get_if(value const &v)
{
    auto const *p = std::get_if<T *>(&v.kind);
    return p ? *p : nullptr;
}

// The questions that the writers of the other libraries ask of a value.
struct reader {
    cbor::type kind_of(value const &v)
    {
        switch (v.kind.index()) {
        case 0:
            return cbor::type::unsigned_integer;
        case 1:
            return cbor::type::negative_integer;
        case 2:
            return cbor::type::double_float;
        case 3:
            return cbor::type::simple_value;
        case 4:
        case 5:
            return cbor::type::text_string;
        case 6:
            return cbor::type::byte_string;
        case 7:
            return std::get<bignum *>(v.kind)->negative ? cbor::type::negative_bignum
                                                        : cbor::type::unsigned_bignum;
        case 8:
            return cbor::type::array;
        case 9:
            return cbor::type::map;
        default:
            return cbor::type::tag;
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

inline value text_value(std::string_view const t)
{
    if (t.size() > short_text{}.c.size())
        return {std::string(t)};
    short_text s{};
    std::ranges::copy(t, s.c.begin());
    s.size = static_cast<std::uint8_t>(t.size());
    return {s};
}

inline value value_of(cbor::item const &it);

template <cbor::type K>
value tag_value(cbor::item const &it)
{
    std::pair<std::uint64_t, cbor::item const *> const tag = it.get<K>().value();
    std::uint64_t const number = tag.first;
    cbor::item const *const content = tag.second;
    auto const magnitude = content->get<cbor::type::byte_string>();
    if ((number == 2 || number == 3) && magnitude) {
        std::string m(reinterpret_cast<char const *>(magnitude->data()), magnitude->size());
        m.erase(0, std::min(m.find_first_not_of('\0'), m.size()));
        if (m.size() <= sizeof(std::uint64_t)) {
            std::uint64_t n = 0;
            for (char const ch : m)
                n = n << 8 | static_cast<unsigned char>(ch);
            return number == 3 ? value{negative{n}} : value{n};
        }
        return {bignum{number == 3, std::move(m)}};
    }
    return {tagged{number, value_of(*content)}};
}

// The value of a decoded item tree. A bignum of at most 8 bytes is an integer, as a dynamic language reads it. A
// float is a double. Tags 28 and 29 are plain tags.
inline value value_of(cbor::item const &it)
{
    switch (it.type) {
    case cbor::type::text_string:
        return text_value(it.get<cbor::type::text_string>().value());
    case cbor::type::byte_string: {
        std::span<std::byte const> const b = it.get<cbor::type::byte_string>().value();
        return {bytes{std::string(reinterpret_cast<char const *>(b.data()), b.size())}};
    }
    case cbor::type::half_float:
        return {it.get<cbor::type::half_float>().value()};
    case cbor::type::single_float:
        return {it.get<cbor::type::single_float>().value()};
    case cbor::type::double_float:
        return {it.get<cbor::type::double_float>().value()};
    case cbor::type::false_value:
        return {simple{std::to_underlying(it.get<cbor::type::false_value>().value())}};
    case cbor::type::true_value:
        return {simple{std::to_underlying(it.get<cbor::type::true_value>().value())}};
    case cbor::type::null:
        return {simple{std::to_underlying(it.get<cbor::type::null>().value())}};
    case cbor::type::undefined:
        return {simple{std::to_underlying(it.get<cbor::type::undefined>().value())}};
    case cbor::type::simple_value:
        return {simple{std::to_underlying(it.get<cbor::type::simple_value>().value())}};
    case cbor::type::unsigned_integer:
        return {it.get<cbor::type::unsigned_integer>().value()};
    case cbor::type::negative_integer:
        return {negative{it.get<cbor::type::negative_integer>().value()}};
    case cbor::type::array: {
        std::span<cbor::item const> const children = it.get<cbor::type::array>().value();
        array a;
        a.reserve(children.size());
        for (cbor::item const &child : children)
            a.push_back(value_of(child));
        return {std::move(a)};
    }
    case cbor::type::map: {
        std::span<cbor::item const> const children = it.get<cbor::type::map>().value();
        map m;
        m.reserve(children.size() / 2);
        for (std::size_t i = 0; i + 1 < children.size(); i += 2)
            m.push_back(entry{value_of(children[i]), value_of(children[i + 1])});
        return {std::move(m)};
    }
    case cbor::type::tag:
        return tag_value<cbor::type::tag>(it);
    case cbor::type::unsigned_bignum:
        return tag_value<cbor::type::unsigned_bignum>(it);
    case cbor::type::negative_bignum:
        return tag_value<cbor::type::negative_bignum>(it);
    case cbor::type::typed_array:
        return tag_value<cbor::type::typed_array>(it);
    }
    std::unreachable();
}

inline std::expected<value, error> value_of(cbor::lazy const &l)
{
    auto const tree = l.clone();
    if (!tree)
        return std::unexpected(tree.error());
    return value_of(**tree);
}

// A value written through the public encoder: an integer at 64 bits, a float as a double, a bignum as tag 2 or 3
// over the magnitude that it holds.
template <class Target>
std::expected<void, error> value_encode(cbor::encoder<Target> &e, value const &x)
{
    reader b;
    switch (b.kind_of(x)) {
    case cbor::type::unsigned_integer:
        return e.unsigned_integer(b.unsigned_of(x));
    case cbor::type::negative_integer:
        return e.negative_integer(b.unsigned_of(x) - 1);
    case cbor::type::double_float:
        return e.floating_point(b.float_of(x));
    case cbor::type::simple_value:
        return e.simple_value(static_cast<cbor::simple_value>(b.simple_of(x)));
    case cbor::type::text_string:
        return e.text_string(b.text_of(x));
    case cbor::type::byte_string:
        return e.byte_string(std::as_bytes(std::span(b.bytes_of(x))));
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        if (auto const r = e.tag(b.kind_of(x) == cbor::type::negative_bignum ? 3 : 2); !r)
            return r;
        return e.byte_string(std::as_bytes(std::span(b.magnitude_of(x))));
    case cbor::type::array: {
        if (auto const r = e.array(b.array_size(x)); !r)
            return r;
        for (value const &v : *std::get<array *>(x.kind))
            if (auto const r = value_encode(e, v); !r)
                return r;
        return {};
    }
    case cbor::type::map: {
        if (auto const r = e.map(b.map_size(x)); !r)
            return r;
        for (entry const &v : *std::get<map *>(x.kind)) {
            if (auto const r = value_encode(e, v.key); !r)
                return r;
            if (auto const r = value_encode(e, v.val); !r)
                return r;
        }
        return {};
    }
    default:
        if (auto const r = e.tag(b.registered_tag(x)); !r)
            return r;
        return value_encode(e, b.before_encode(x));
    }
}

template <class Target>
auto encode(Target &&target, value const &x)
    -> decltype(std::declval<cbor::encoder<std::remove_cvref_t<Target>> &>().finish())
{
    cbor::encoder e{std::forward<Target>(target)};
    if (auto const r = value_encode(e, x); !r)
        return std::unexpected(r.error());
    return e.finish();
}

} // namespace bench

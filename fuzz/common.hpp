#pragma once

#include "../test/binding.hpp"
#include "../test/ref_binding.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace fuzz
{

// A finding of the fuzzer is a wrong answer as much as a crash, so every check that fails ends the run.
inline void require(bool const holds, std::source_location const where = std::source_location::current())
{
    if (!holds) {
        std::fprintf(stderr, "fuzz: check failed at line %u\n", static_cast<unsigned>(where.line()));
        std::abort();
    }
}

// RFC 8949 3.4.3: a bignum whose magnitude fits 64 bits is the same number as the integer, and the encoder
// writes the integer. The comparison therefore reads both forms as one.
inline std::string magnitude_without_leading_zeros(std::string_view m)
{
    while (!m.empty() && m.front() == '\0')
        m.remove_prefix(1);
    return std::string(m);
}

inline std::uint64_t magnitude_value(std::string_view const m)
{
    std::uint64_t v = 0;
    for (char const c : m)
        v = v << 8 | static_cast<unsigned char>(c);
    return v;
}

inline std::string magnitude_minus_one(std::string m)
{
    for (auto it = m.rbegin(); it != m.rend(); ++it) {
        if (*it != '\0') {
            *it = static_cast<char>(static_cast<unsigned char>(*it) - 1);
            break;
        }
        *it = '\xff';
    }
    return magnitude_without_leading_zeros(m);
}

inline test::value canonical(test::value const &v)
{
    if (auto const *b = std::get_if<test::bignum *>(&v.kind)) {
        std::string const m = magnitude_without_leading_zeros((*b)->magnitude);
        if (!(*b)->negative)
            return m.size() <= 8 ? test::value{magnitude_value(m)} : test::value{test::bignum{false, m}};
        if (m.empty())
            return v;
        std::string const n = magnitude_minus_one(m);
        return n.size() <= 8 ? test::value{test::negative{magnitude_value(n)}} : test::value{test::bignum{true, m}};
    }
    if (auto const *a = std::get_if<test::array *>(&v.kind)) {
        test::array out;
        for (auto const &e : **a)
            out.push_back(canonical(e));
        return {std::move(out)};
    }
    if (auto const *m = std::get_if<test::map *>(&v.kind)) {
        test::map out;
        for (auto const &[k, x] : **m)
            out.push_back(test::entry{canonical(k), canonical(x)});
        return {std::move(out)};
    }
    if (auto const *t = std::get_if<test::tagged *>(&v.kind))
        return {test::tagged{(*t)->tag, canonical((*t)->content)}};
    return v;
}

// A float compares by its bits, so -0.0 and 0.0 differ; two NaN are the same, as the encoder may give a NaN
// its shortest form (RFC 8949 4.2.2).
inline bool same(test::value const &a, test::value const &b)
{
    if (a.kind.index() != b.kind.index())
        return false;
    if (auto const *x = std::get_if<double>(&a.kind)) {
        double const y = std::get<double>(b.kind);
        if (std::isnan(*x) || std::isnan(y))
            return std::isnan(*x) && std::isnan(y);
        return std::bit_cast<std::uint64_t>(*x) == std::bit_cast<std::uint64_t>(y);
    }
    if (auto const *x = std::get_if<test::array *>(&a.kind)) {
        auto const &y = *std::get<test::array *>(b.kind);
        if ((*x)->size() != y.size())
            return false;
        for (std::size_t i = 0; i < y.size(); ++i)
            if (!same((*x)->at(i), y.at(i)))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::map *>(&a.kind)) {
        auto const &y = *std::get<test::map *>(b.kind);
        if ((*x)->size() != y.size())
            return false;
        for (std::size_t i = 0; i < y.size(); ++i)
            if (!same((*x)->at(i).key, y.at(i).key) || !same((*x)->at(i).val, y.at(i).val))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::tagged *>(&a.kind)) {
        auto const &y = *std::get<test::tagged *>(b.kind);
        return (*x)->tag == y.tag && same(test::value{(*x)->content}, test::value{y.content});
    }
    return a == b;
}

inline bool same_number(test::value const &a, test::value const &b)
{
    return same(canonical(a), canonical(b));
}

// The bytes of the input, taken from the front. When they run out every read gives zero.
struct source {
    std::string_view bytes;

    std::uint8_t byte()
    {
        if (bytes.empty())
            return 0;
        auto const b = static_cast<std::uint8_t>(bytes.front());
        bytes.remove_prefix(1);
        return b;
    }

    std::uint64_t number()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = v << 8 | byte();
        return v;
    }

    std::string string()
    {
        std::size_t const n = byte() % 40;
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
            s.push_back(static_cast<char>(byte()));
        return s;
    }

    std::string text()
    {
        std::string s = string();
        for (char &c : s)
            c = static_cast<char>(c & 0x7f);
        return s;
    }
};

// What the encoder refuses by RFC 8949: a simple value from 24 to 31 (3.3), and a negative bignum of
// magnitude 0, which no CBOR item holds (3.4.3).
struct refusal {
    bool reserved_simple = false;
    bool zero_negative = false;
};

inline bool tag_has_meaning(std::uint64_t const tag)
{
    return tag == 0 || tag == 1 || tag == 2 || tag == 3 || tag == 24 || tag == 28 || tag == 29 || (tag >= 64 && tag <= 87);
}

// A value of the test binding from the bytes of the input, with every kind the encoder knows. A text is ASCII,
// because the binding promises the encoder valid UTF-8.
inline test::value value_from(source &in, refusal &r, int const depth)
{
    std::uint8_t const k = in.byte() % (depth > 6 ? 7 : 10);
    switch (k) {
    case 0:
        return {in.number()};
    case 1:
        return {test::negative{in.number()}};
    case 2: {
        bool const negative = in.byte() & 1;
        std::string m = in.string();
        if (negative && magnitude_without_leading_zeros(m).empty())
            r.zero_negative = true;
        return {test::bignum{negative, std::move(m)}};
    }
    case 3:
        return {test::bytes{in.string()}};
    case 4:
        return {in.text()};
    case 5:
        return {std::bit_cast<double>(in.number())};
    case 6: {
        std::uint8_t const s = in.byte();
        if (s >= 24 && s < 32)
            r.reserved_simple = true;
        return {test::simple{s}};
    }
    case 7: {
        test::array a;
        for (std::size_t n = in.byte() % 5; n > 0; --n)
            a.push_back(value_from(in, r, depth + 1));
        return {std::move(a)};
    }
    case 8: {
        test::map m;
        for (std::size_t n = in.byte() % 4; n > 0; --n) {
            test::value key = value_from(in, r, depth + 1);
            m.push_back(test::entry{std::move(key), value_from(in, r, depth + 1)});
        }
        return {std::move(m)};
    }
    default: {
        std::uint64_t tag = in.number();
        if (tag_has_meaning(tag))
            tag += std::uint64_t{1} << 40;
        return {test::tagged{tag, value_from(in, r, depth + 1)}};
    }
    }
}

// A graph of the reference binding is compared node by node. The map pairs the nodes already seen, so two
// graphs are the same only when their sharing is the same. An integer and a string key have no identity in
// that binding, so the encoder writes them each time; integers and strings compare by value.
inline bool same_graph(shared_test::handle const &a, shared_test::handle const &b,
                       std::map<shared_test::node const *, shared_test::node const *> &pairs)
{
    if (a->kind.index() != b->kind.index())
        return false;
    if (auto const *x = std::get_if<std::uint64_t>(&a->kind))
        return *x == std::get<std::uint64_t>(b->kind);
    if (auto const *x = std::get_if<std::string>(&a->kind))
        return *x == std::get<std::string>(b->kind);
    auto const [at, fresh] = pairs.try_emplace(a.get(), b.get());
    if (!fresh)
        return at->second == b.get();
    if (auto const *x = std::get_if<std::vector<shared_test::handle>>(&a->kind)) {
        auto const &y = std::get<std::vector<shared_test::handle>>(b->kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same_graph(x->at(i), y.at(i), pairs))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(&a->kind)) {
        auto const &y = std::get<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(b->kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same_graph(x->at(i).first, y.at(i).first, pairs) ||
                !same_graph(x->at(i).second, y.at(i).second, pairs))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<shared_test::object>(&a->kind)) {
        auto const &y = std::get<shared_test::object>(b->kind);
        return x->tag == y.tag && same_graph(x->content, y.content, pairs);
    }
    return false;
}

} // namespace fuzz

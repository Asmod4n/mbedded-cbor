#pragma once

#include "reference.hpp"

namespace test
{

// The binding of a test: a dynamic language in C++. Every member function builds one kind of value.
struct test_binding {
    using value = test::value;

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
    return cbor::lazy_decode(binding, *cbor::lazy::from(wire));
}

inline error decode_error(std::string_view wire)
{
    auto const v = decoded(wire);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}

inline void check_both(std::string_view wire, [[maybe_unused]] value const &expected)
{
    auto const v = decoded(wire);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    auto const back = decoded(encoded(expected));
    REQUIRE(back.has_value());
    CHECK(*back == expected);
}

} // namespace test

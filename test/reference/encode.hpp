#pragma once

#include "decode.hpp"
#include "value.hpp"
#include <cbor/encode.hpp>
#include <cbor/ref.hpp>

#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace test
{

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

#if __has_include(<sys/uio.h>)
inline std::expected<cbor::ref<std::span<iovec const>>, error> encode_iov(value const &v)
{
    cbor::iov_writer w;
    return encode(w, v);
}
#endif

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

} // namespace test

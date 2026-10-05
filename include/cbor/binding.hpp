#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace cbor
{

struct typed_array {
    std::uint64_t tag;
    std::span<std::byte const> bytes;
};

enum class kind {
    unsigned_integer,
    negative_integer,
    unsigned_bignum,
    negative_bignum,
    byte_string,
    text_string,
    floating_point,
    simple_value,
    array,
    map,
    typed_array,
    registered,
    unsupported
};

template <class Value>
struct binding {
    using value = Value;
    value unsigned_integer_decode(std::uint64_t argument) = delete;
    value negative_integer_decode(std::uint64_t argument) = delete;
    value unsigned_bignum_decode(std::string_view magnitude) = delete;
    value negative_bignum_decode(std::string_view magnitude) = delete;
    value byte_string_decode(std::string_view bytes) = delete;
    value text_string_decode(std::string_view text) = delete;
    value float_decode(double number) = delete;
    value simple_value_decode(std::uint8_t simple) = delete;
    value array_decode(std::uint64_t size) = delete;
    value array_append(value array, value element) = delete;
    value map_decode(std::uint64_t size) = delete;
    value map_key_decode(std::string_view key) = delete;
    value map_insert(value map, value key, value item) = delete;
    value tag_decode(std::uint64_t tag, value content) = delete;
    std::optional<value> tag_begin(std::uint64_t tag) = delete;
    value registered_decode(value object, value content) = delete;
    value after_decode(value object) = delete;
    bool cyclic_data_structures() = delete;
    kind kind_of(value const &item) = delete;
    value before_encode(value const &item) = delete;
    std::uint64_t unsigned_of(value const &item) = delete;
    std::string_view magnitude_of(value const &item) = delete;
    std::string_view bytes_of(value const &item) = delete;
    std::string_view text_of(value const &item) = delete;
    double float_of(value const &item) = delete;
    std::uint8_t simple_of(value const &item) = delete;
    std::uint64_t array_size(value const &item) = delete;
    value const &array_at(value const &item, std::uint64_t index) = delete;
    std::uint64_t map_size(value const &item) = delete;
    template <class F>
    void map_for_each(value const &item, F const &each) = delete;
    typed_array typed_array_of(value const &item) = delete;
    std::uint64_t registered_tag(value const &item) = delete;
    bool embed_of(value const &item) = delete;
    void value_identity(value const &item) = delete;
    void key_identity(value const &item) = delete;
};

template <class B>
concept language_binding = std::derived_from<B, binding<typename B::value>> &&
                           requires(B &b, typename B::value v, std::uint64_t const n, std::string_view const s, double const f,
                                    std::uint8_t const simple) {
    { b.unsigned_integer_decode(n) } -> std::same_as<typename B::value>;
    { b.negative_integer_decode(n) } -> std::same_as<typename B::value>;
    { b.unsigned_bignum_decode(s) } -> std::same_as<typename B::value>;
    { b.negative_bignum_decode(s) } -> std::same_as<typename B::value>;
    { b.byte_string_decode(s) } -> std::same_as<typename B::value>;
    { b.text_string_decode(s) } -> std::same_as<typename B::value>;
    { b.float_decode(f) } -> std::same_as<typename B::value>;
    { b.simple_value_decode(simple) } -> std::same_as<typename B::value>;
    { b.array_decode(n) } -> std::same_as<typename B::value>;
    { b.array_append(std::move(v), std::move(v)) } -> std::same_as<typename B::value>;
    { b.map_decode(n) } -> std::same_as<typename B::value>;
    { b.map_insert(std::move(v), std::move(v), std::move(v)) } -> std::same_as<typename B::value>;
    { b.tag_decode(n, std::move(v)) } -> std::same_as<typename B::value>;
};

template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    consteval fixed_string(char const (&text)[N])
    {
        std::ranges::copy(text, value.begin());
    }

    consteval std::string_view view() const
    {
        return {value.data(), N - 1};
    }
};

}

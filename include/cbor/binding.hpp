#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#ifdef __cpp_impl_reflection
#include <meta>
#endif

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

struct key {
    char const *text = nullptr;
    std::int64_t number = 0;

#ifdef __cpp_impl_reflection
    template <std::same_as<char> C>
    consteval explicit key(C const *const s) : text(std::define_static_string(std::string_view(s)))
    {
    }
#endif

    constexpr explicit key(std::int64_t const n) : number(n)
    {
    }
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

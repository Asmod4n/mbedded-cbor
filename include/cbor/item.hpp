#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <utility>

#include "error.hpp"
#include "rfc8949.hpp"

namespace cbor
{

template <bool>
struct tree_decoder;

class encoding;

struct item;

template <cbor::type K>
struct item_alternative;

template <cbor::type K>
    requires(K == cbor::type::unsigned_integer || K == cbor::type::negative_integer)
struct item_alternative<K> {
    using type = std::uint64_t;
};

template <cbor::type K>
    requires(K == cbor::type::half_float || K == cbor::type::single_float || K == cbor::type::double_float)
struct item_alternative<K> {
    using type = double;
};

template <>
struct item_alternative<cbor::type::text_string> {
    using type = std::string_view;
};

template <>
struct item_alternative<cbor::type::byte_string> {
    using type = std::span<std::byte const>;
};

template <cbor::type K>
    requires(K == cbor::type::array || K == cbor::type::map)
struct item_alternative<K> {
    using type = std::span<item const>;
};

template <cbor::type K>
    requires(K == cbor::type::tag || K == cbor::type::unsigned_bignum || K == cbor::type::negative_bignum ||
             K == cbor::type::typed_array)
struct item_alternative<K> {
    using type = std::pair<std::uint64_t, item const *>;
};

template <cbor::type K>
    requires(K == cbor::type::false_value || K == cbor::type::true_value || K == cbor::type::null ||
             K == cbor::type::undefined || K == cbor::type::simple_value)
struct item_alternative<K> {
    using type = cbor::simple_value;
};

template <cbor::type K>
using item_alternative_t = typename item_alternative<K>::type;

struct item {
    cbor::type const type;
    std::uint8_t const additional_information;

    std::expected<std::uint64_t, error> argument() const
    {
        if (type != cbor::type::unsigned_integer && type != cbor::type::negative_integer) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.u64;
    }

    std::expected<double, error> floating_point() const
    {
        if (type != cbor::type::half_float && type != cbor::type::single_float && type != cbor::type::double_float)
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.f64;
    }

    std::expected<cbor::simple_value, error> simple_value() const
    {
        if (type != cbor::type::false_value && type != cbor::type::true_value && type != cbor::type::null &&
            type != cbor::type::undefined && type != cbor::type::simple_value) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.simple_value;
    }

    std::expected<std::span<std::byte const>, error> byte_string() const
    {
        if (type != cbor::type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.bin;
    }

    std::expected<std::string_view, error> text_string() const
    {
        if (type != cbor::type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.str;
    }

    std::expected<std::span<item const>, error> array() const
    {
        if (type != cbor::type::array) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.array;
    }

    std::expected<std::span<item const>, error> map() const
    {
        if (type != cbor::type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.map;
    }

    std::expected<std::uint64_t, error> tag_number() const
    {
        if (!holds_tag()) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.tag.number;
    }

    std::expected<item const *, error> tag_content() const
    {
        if (!holds_tag()) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return via.tag.content;
    }

    template <cbor::type K>
    std::expected<item_alternative_t<K>, error> get() const
    {
        if (type != K) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if constexpr (std::same_as<item_alternative_t<K>, std::uint64_t>)
            return via.u64;
        else if constexpr (std::same_as<item_alternative_t<K>, double>)
            return via.f64;
        else if constexpr (K == cbor::type::text_string)
            return via.str;
        else if constexpr (K == cbor::type::byte_string)
            return via.bin;
        else if constexpr (K == cbor::type::array)
            return via.array;
        else if constexpr (K == cbor::type::map)
            return via.map;
        else if constexpr (std::same_as<item_alternative_t<K>, std::pair<std::uint64_t, item const *>>)
            return std::pair<std::uint64_t, item const *>(via.tag.number, via.tag.content);
        else
            return via.simple_value;
    }

private:
    union content {
        std::uint64_t u64;
        double f64;
        std::string_view str;
        std::span<std::byte const> bin;
        std::span<item const> array;
        std::span<item const> map;
        struct {
            std::uint64_t number;
            item const *content;
        } tag;
        cbor::simple_value simple_value;
    } via;

    constexpr item(cbor::type const t, std::uint8_t const info, content const v)
        : type(t), additional_information(info), via(v)
    {
    }

    constexpr bool holds_tag() const
    {
        return type == cbor::type::tag || type == cbor::type::unsigned_bignum || type == cbor::type::negative_bignum ||
               type == cbor::type::typed_array;
    }

    template <bool>
    friend struct tree_decoder;

    friend class encoding;
};

}

#endif

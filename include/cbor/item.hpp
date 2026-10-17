#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include "error.hpp"
#include "rfc8949.hpp"

namespace cbor
{

template <bool>
struct tree_decoder;

class encoding;

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

#pragma once

#include "../owned_copy.hpp"
#include "value.hpp"
#include <cbor/item.hpp>
#include <cbor/lazy.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace test
{

// The index selector of RFC 9535 through the public interface of lazy: an index counts from the end when it
// is negative, and on a map it names the integer key, as at_path reads it.
inline std::expected<cbor::lazy, cbor::error> select_index(cbor::lazy const &l, std::int64_t const index)
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

inline std::expected<value, error> value_of(cbor::item const &it);

template <cbor::type K>
std::expected<value, error> tag_value(cbor::item const &it)
{
    std::pair<std::uint64_t, cbor::item const *> const tag = it.get<K>().value();
    std::uint64_t const number = tag.first;
    cbor::item const &inner = *tag.second;
    auto const magnitude = inner.get<cbor::type::byte_string>();
    if ((number == 2 || number == 3) && magnitude) {
        bool const negative_value = number == 3;
        std::string_view const m =
            magnitude_without_leading_zeros(std::string_view(reinterpret_cast<char const *>(magnitude->data()), magnitude->size()));
        if (m.size() <= sizeof(std::uint64_t)) {
            std::uint64_t n = 0;
            for (char const c : m)
                n = n << 8 | static_cast<unsigned char>(c);
            return negative_value ? value{negative{n}} : value{n};
        }
        return value{bignum{negative_value, negative_value ? magnitude_plus_one(m) : std::string(m)}};
    }
    auto c = value_of(inner);
    if (!c)
        return std::unexpected(c.error());
    return value{tagged{number, std::move(*c)}};
}

// The reference model of a decoded item tree. Tags 28 and 29 are plain tags over their content, as RFC 8949
// 3.4 lets a reader that does not know a tag pass it on. A bignum that fits 64 bits is an integer. Tag 24 is a tag
// over its byte string.
inline std::expected<value, error> value_of(cbor::item const &it)
{
    switch (it.type) {
    case cbor::type::text_string:
        return value{std::string(it.get<cbor::type::text_string>().value())};
    case cbor::type::byte_string: {
        std::span<std::byte const> const b = it.get<cbor::type::byte_string>().value();
        return value{bytes{std::string(reinterpret_cast<char const *>(b.data()), b.size())}};
    }
    case cbor::type::half_float:
        return value{it.get<cbor::type::half_float>().value()};
    case cbor::type::single_float:
        return value{it.get<cbor::type::single_float>().value()};
    case cbor::type::double_float:
        return value{it.get<cbor::type::double_float>().value()};
    case cbor::type::false_value:
        return value{simple{std::to_underlying(it.get<cbor::type::false_value>().value())}};
    case cbor::type::true_value:
        return value{simple{std::to_underlying(it.get<cbor::type::true_value>().value())}};
    case cbor::type::null:
        return value{simple{std::to_underlying(it.get<cbor::type::null>().value())}};
    case cbor::type::undefined:
        return value{simple{std::to_underlying(it.get<cbor::type::undefined>().value())}};
    case cbor::type::simple_value:
        return value{simple{std::to_underlying(it.get<cbor::type::simple_value>().value())}};
    case cbor::type::unsigned_integer:
        return value{it.get<cbor::type::unsigned_integer>().value()};
    case cbor::type::negative_integer:
        return value{negative{it.get<cbor::type::negative_integer>().value()}};
    case cbor::type::array: {
        array a;
        auto const children = it.get<cbor::type::array>().value();
        for (cbor::item const &child : children) {
            auto e = value_of(child);
            if (!e)
                return std::unexpected(e.error());
            a.push_back(std::move(*e));
        }
        return value{std::move(a)};
    }
    case cbor::type::map: {
        std::span<cbor::item const> const children = it.get<cbor::type::map>().value();
        map m;
        for (std::size_t i = 0; i + 1 < children.size(); i += 2) {
            auto k = value_of(children[i]);
            if (!k)
                return std::unexpected(k.error());
            auto v = value_of(children[i + 1]);
            if (!v)
                return std::unexpected(v.error());
            m.push_back(entry{std::move(*k), std::move(*v)});
        }
        return value{std::move(m)};
    }
    case cbor::type::tag:
        return tag_value<cbor::type::tag>(it);
    case cbor::type::unsigned_bignum:
        return tag_value<cbor::type::unsigned_bignum>(it);
    case cbor::type::negative_bignum:
        return tag_value<cbor::type::negative_bignum>(it);
    case cbor::type::typed_array:
        return tag_value<cbor::type::typed_array>(it);
    case cbor::type::shared_reference:
        return value{tagged{std::uint64_t{29},
                            value{it.get<cbor::type::shared_reference>().value().first}}};
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

template <class E>
inline std::expected<value, error> value_of(std::expected<cbor::lazy, E> const &l)
{
    if (!l)
        return std::unexpected(l.error());
    return value_of(*l);
}

inline std::expected<value, error> decoded(std::string_view const wire)
{
    auto const l = test::lazy_of_copy(wire);
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

} // namespace test

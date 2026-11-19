#pragma once

#include "../owned_copy.hpp"
#include "value.hpp"
#include <cbor/decode.hpp>
#include <cbor/encode.hpp>
#include <cbor/item.hpp>
#include <cbor/lazy.hpp>

#include <cstdint>
#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace test
{

// The index selector of RFC 9535 through the public interface of lazy. An index counts from the end when it is negative.
// On a map it names the integer key, as at_path reads it.
inline std::expected<cbor::lazy, cbor::error> select_element_by_index(cbor::lazy const &l, std::int64_t const index)
{
    if (auto const e = l.elements(); !e && e.error() == cbor::error::not_indexable)
        return l.at(index);
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

inline std::expected<value, error> make_value_from_item(cbor::item const it);

// The model keeps a negative integer as its argument n, for the value -1 - n (RFC 8949 3.1). get<std::int64_t> gives
// the value down to -2^63; a smaller value is read back from the head that cbor::encode writes.
inline std::uint64_t argument_of_negative_integer(cbor::item const it)
{
    if (auto const v = it.get<std::int64_t>())
        return static_cast<std::uint64_t>(-1 - *v);
    std::string head;
    if (!cbor::encode(head, it) || head.size() != 1 + sizeof(std::uint64_t))
        return 0;
    std::uint64_t argument = 0;
    for (char const c : std::string_view(head).substr(1))
        argument = argument << 8 | static_cast<std::uint8_t>(c);
    return argument;
}

inline std::expected<value, error> make_value_from_tag_item(cbor::item const it)
{
    std::pair<std::uint64_t, cbor::item> const tag = it.get<std::pair<std::uint64_t, cbor::item>>().value();
    std::uint64_t const number = tag.first;
    cbor::item const inner = tag.second;
    auto const magnitude = inner.get<std::span<std::byte const>>();
    if ((number == 2 || number == 3) && magnitude) {
        bool const negative_value = number == 3;
        std::string_view const m =
            strip_leading_zeros_of_magnitude(std::string_view(reinterpret_cast<char const *>(magnitude->data()), magnitude->size()));
        if (m.size() <= sizeof(std::uint64_t)) {
            std::uint64_t n = 0;
            for (char const c : m)
                n = n << 8 | static_cast<unsigned char>(c);
            return negative_value ? value{negative{n}} : value{n};
        }
        return value{bignum{negative_value, negative_value ? add_one_to_magnitude(m) : std::string(m)}};
    }
    auto c = make_value_from_item(inner);
    if (!c)
        return std::unexpected(c.error());
    return value{tagged{number, std::move(*c)}};
}

// The reference model of a decoded item tree. Tags 28 and 29 are plain tags over their content. RFC 8949 3.4 lets a
// reader that does not know a tag pass it on. A bignum that fits 64 bits is an integer. Tag 24 is a tag over its byte
// string.
inline std::expected<value, error> make_value_from_item(cbor::item const it)
{
    switch (it.type()) {
    case cbor::type::text_string:
        return value{std::string(it.get<std::string_view>().value())};
    case cbor::type::byte_string: {
        std::span<std::byte const> const b = it.get<std::span<std::byte const>>().value();
        return value{bytes{std::string(reinterpret_cast<char const *>(b.data()), b.size())}};
    }
    case cbor::type::half_float:
        return value{it.get<double>().value()};
    case cbor::type::single_float:
        return value{it.get<double>().value()};
    case cbor::type::double_float:
        return value{it.get<double>().value()};
    case cbor::type::false_value:
        return value{simple{std::to_underlying(it.get<cbor::simple_value>().value())}};
    case cbor::type::true_value:
        return value{simple{std::to_underlying(it.get<cbor::simple_value>().value())}};
    case cbor::type::null:
        return value{simple{std::to_underlying(it.get<cbor::simple_value>().value())}};
    case cbor::type::undefined:
        return value{simple{std::to_underlying(it.get<cbor::simple_value>().value())}};
    case cbor::type::simple_value:
        return value{simple{std::to_underlying(it.get<cbor::simple_value>().value())}};
    case cbor::type::unsigned_integer:
        return value{it.get<std::uint64_t>().value()};
    case cbor::type::negative_integer:
        return value{negative{argument_of_negative_integer(it)}};
    case cbor::type::array: {
        array a;
        auto const children = it.get<cbor::array_range>().value();
        for (cbor::item const child : children) {
            auto e = make_value_from_item(child);
            if (!e)
                return std::unexpected(e.error());
            a.push_back(std::move(*e));
        }
        return value{std::move(a)};
    }
    case cbor::type::map: {
        map m;
        for (auto const pair : it.get<cbor::map_range>().value()) {
            auto k = make_value_from_item(std::get<0>(pair));
            if (!k)
                return std::unexpected(k.error());
            auto v = make_value_from_item(std::get<1>(pair));
            if (!v)
                return std::unexpected(v.error());
            m.push_back(entry{std::move(*k), std::move(*v)});
        }
        return value{std::move(m)};
    }
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        return make_value_from_tag_item(it);
    case cbor::type::typed_array: {
        cbor::typed_array const t = it.get<cbor::typed_array>().value();
        return value{tagged{t.tag, value{bytes{std::string(reinterpret_cast<char const *>(t.bytes.data()), t.bytes.size())}}}};
    }
    case cbor::type::shared_reference:
        return value{tagged{std::uint64_t{29},
                            value{it.get<std::pair<std::uint64_t, cbor::item>>().value().first}}};
    }
    std::unreachable();
}

inline std::expected<value, error> make_value_from_item(cbor::lazy const &l)
{
    auto const tree = l.to_item();
    if (!tree)
        return std::unexpected(tree.error());
    return make_value_from_item(**tree);
}

template <class E>
inline std::expected<value, error> make_value_from_item(std::expected<cbor::lazy, E> const &l)
{
    if (!l)
        return std::unexpected(l.error());
    return make_value_from_item(*l);
}

inline std::expected<value, error> decode_wire_into_value(std::string_view const wire)
{
    auto const l = test::make_lazy_from_copy(wire);
    if (!l)
        return std::unexpected(l.error());
    return make_value_from_item(*l);
}

inline error read_decode_error_of_wire(std::string_view const wire)
{
    auto const v = decode_wire_into_value(wire);
    REQUIRE_FALSE(v.has_value());
    return v.error();
}

// A wire and its value in the model. The wire reads into the value. cbor::encode keeps the width of every head, so the
// tree that cbor::decode makes of a definite-length wire writes back to the same bytes.
inline void check_both(std::string_view const wire, [[maybe_unused]] value const &expected)
{
    auto const v = decode_wire_into_value(wire);
    REQUIRE(v.has_value());
    CHECK(*v == expected);
    auto const tree = cbor::decode(wire);
    REQUIRE(tree.has_value());
    std::string back;
    REQUIRE(cbor::encode(back, **tree).has_value());
    CHECK_EQ(back, wire);
}

} // namespace test

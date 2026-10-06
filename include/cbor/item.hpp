#include "shared.hpp"

#ifndef CBOR_ITEM_HPP
#define CBOR_ITEM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "head.hpp"

namespace cbor
{

struct item;

struct negative_integer {
    std::uint64_t argument;
};

struct tag {
    std::uint64_t number;
    item const *content{};
};

struct record {
    std::uint64_t tag;
    std::vector<std::pair<std::string_view, item const *>> fields;
};

struct item {
    std::variant<lazy, std::uint64_t, negative_integer, std::span<std::byte const>, std::string_view,
                 std::vector<item const *>, std::vector<std::pair<item const *, item const *>>, tag, record, bool,
                 std::nullptr_t, simple_value, double>
        content;

    std::uint8_t initial_byte() const;
};

inline std::uint8_t item::initial_byte() const
{
    return std::visit(
        [](auto const &v) -> std::uint8_t {
            using V = std::remove_cvref_t<decltype(v)>;
            auto const byte = [](major_type const major, std::uint64_t const info) {
                return static_cast<std::uint8_t>(heads::initial_byte(major, info));
            };
            if constexpr (std::is_same_v<V, lazy>)
                return byte(major_type::simple_float,
                            std::to_underlying(heads::simple_float_information::reserved));
            else if constexpr (std::is_same_v<V, std::uint64_t>)
                return byte(major_type::unsigned_integer, 0);
            else if constexpr (std::is_same_v<V, negative_integer>)
                return byte(major_type::negative_integer, 0);
            else if constexpr (std::is_same_v<V, std::span<std::byte const>>)
                return byte(major_type::byte_string, 0);
            else if constexpr (std::is_same_v<V, std::string_view>)
                return byte(major_type::text_string, 0);
            else if constexpr (std::is_same_v<V, std::vector<item const *>>)
                return byte(major_type::array, 0);
            else if constexpr (std::is_same_v<V, std::vector<std::pair<item const *, item const *>>>)
                return byte(major_type::map, 0);
            else if constexpr (std::is_same_v<V, tag> || std::is_same_v<V, record>)
                return byte(major_type::tag, 0);
            else if constexpr (std::is_same_v<V, bool>)
                return byte(major_type::simple_float,
                            std::to_underlying(v ? simple_value::true_value : simple_value::false_value));
            else if constexpr (std::is_same_v<V, std::nullptr_t>)
                return byte(major_type::simple_float, std::to_underlying(simple_value::null));
            else if constexpr (std::is_same_v<V, simple_value>)
                return std::to_underlying(v) < std::to_underlying(heads::simple_float_information::simple_value_follows)
                           ? byte(major_type::simple_float, std::to_underlying(v))
                           : byte(major_type::simple_float,
                                  std::to_underlying(heads::simple_float_information::simple_value_follows));
            else
                return byte(major_type::simple_float, std::to_underlying(heads::preferred_float_info(v)));
        },
        content);
}

}

#endif

/**
 * @file
 * Gives cbor::item, one node of a decoded CBOR tree. item::get<K>
 * gives the value of the node as a C++ type, or incorrect_type when
 * the node is not of type K.
 */
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

namespace detail
{

template <bool, bool>
struct tree_decoder;

}

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
             K == cbor::type::typed_array || K == cbor::type::shared_reference)
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

/**
 * @brief One node of a tree from cbor::decode() or `lazy::clone`.
 *
 * `type` gives the kind of the node.
 * `additional_information` gives the low 5 bits of the initial byte (RFC 8949 3).
 * cbor::item::get() gives the value.
 * The constructor is private: only a decode makes an item.
 * An item owns no memory and has no destructor to run.
 * The tree owns every node and every string.
 * An item gives no error.
 * `type` and `additional_information` are data members.
 *
 * @code{.callseq}
 * (*tree)->type                     → cbor::type const
 * (*tree)->additional_information   → std::uint8_t const
 * (*tree)->get<cbor::type::array>() → std::expected<std::span<item const>, error>
 * @endcode
 * @code{.example}
 * message: [1, {"a": 2}]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, {"a": 2}]
 * call: (*tree)->type
 * result: cbor::type::array
 * call: (*tree)->additional_information
 * result: 2
 * call: (*tree)->get<cbor::type::array>()->size()
 * result: 2
 * @endcode
 *
 * @snippet doc_reference_item.cpp item
 * @see cbor::item::get()
 * @see cbor::decode()
 * @see cbor::encode()
 *
 * @ingroup item
 */
struct item {
    cbor::type const type;
    std::uint8_t const additional_information;

    /**
     * @brief Gives the value of the node when `type` is `K`.
     *
     * | K | Result |
     * |---|---|
     * | `unsigned_integer`, `negative_integer` | `std::uint64_t`, the argument |
     * | `half_float`, `single_float`, `double_float` | `double` |
     * | `text_string` | `std::string_view` |
     * | `byte_string` | `std::span<std::byte const>` |
     * | `array` | `std::span<item const>`, the elements |
     * | `map` | `std::span<item const>`, each key and its value one after the other |
     * | `tag`, `unsigned_bignum`, `negative_bignum`, `typed_array` | `std::pair<std::uint64_t, item const *>`: the tag number and the content |
     * | `shared_reference` | `std::pair<std::uint64_t, item const *>`: the index and the item that the tag 28 of that index marks |
     * | `false_value`, `true_value`, `null`, `undefined`, `simple_value` | `cbor::simple_value` |
     * A negative integer gives its argument n, for the value -1 - n.
     * A view and a pointer in the result point into the tree.
     * `incorrect_type`: `type` is not `K`.
     *
     * @code{.callseq}
     * node.get<cbor::type::unsigned_integer>() → std::expected<std::uint64_t, error>
     * node.get<cbor::type::text_string>()      → std::expected<std::string_view, error>
     * node.get<cbor::type::map>()              → std::expected<std::span<item const>, error>
     * node.get<cbor::type::tag>()              → std::expected<std::pair<std::uint64_t, item const *>, error>
     * node.get<cbor::type::null>()             → std::expected<simple_value, error>
     * @endcode
     * @code{.example}
     * message: {"a": -3}
     * call: auto const tree = cbor::decode(bytes)
     * result: owning item of {"a": -3}
     * call: auto const map = *(*tree)->get<cbor::type::map>()
     * result: span of 2 items: "a", -3
     * call: map[0].get<cbor::type::text_string>()
     * result: "a"
     * call: map[1].get<cbor::type::negative_integer>()
     * result: 2
     * call: map[1].type
     * result: cbor::type::negative_integer
     * @endcode
     *
     * @snippet doc_reference_item.cpp get
     * @see cbor::item
     * @see cbor::decode()
     */
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

    template <bool, bool>
    friend struct detail::tree_decoder;

    friend class encoding;
};

}

#endif

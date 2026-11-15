/**
 * @file
 * Gives cbor::item, one node of a decoded CBOR tree. item::get<K>
 * gives the value of the node as a C++ type, or incorrect_type when
 * the node is not of type K.
 */
#pragma once

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include "error.hpp"
#include "head.hpp"
#include "rfc8746.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

class encoding;

template <bool>
struct tree_decoder;

}

struct item;

template <class T>
concept scalar_result = (std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                        std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
                        || std::is_same_v<T, std::float128_t>
#endif
    ;

template <class T>
concept view_result =
    std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>;

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
 * @brief One node of a tree from cbor::decode() or `lazy::to_item`.
 *
 * cbor::item::type() gives the kind of the node.
 * `additional_information` gives the low 5 bits of the initial byte (RFC 8949 3).
 * cbor::item::get() and cbor::item::visit() give the value.
 * The constructor is private: only a decode makes an item.
 * An item owns no memory and has no destructor to run.
 * The tree owns every node and every string.
 * An item gives no error.
 * `additional_information` is a data member.
 *
 * @code{.example}
 * message: [1, {"a": 2}]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, {"a": 2}]
 * call: (*tree)->type()
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
 * @ingroup decode
 */
struct item {
private:
    cbor::type const m_type;

public:
    std::uint8_t const additional_information;

    /**
     * @brief Gives the kind of the node.
     *
     * A tag 2 or 3 whose magnitude fits 64 bits is an integer node, so it gives `unsigned_integer` or
     * `negative_integer`.
     *
     * @code{.example}
     * message: [1, 2(h'010000000000000000')]
     * call: auto const tree = cbor::decode(bytes)
     * result: owning item of [1, 2(h'010000000000000000')]
     * call: (*tree)->type()
     * result: cbor::type::array
     * call: (*tree)->get<std::span<cbor::item const>>().value()[1].type()
     * result: cbor::type::unsigned_bignum
     * @endcode
     *
     * @see cbor::lazy::type()
     */
    constexpr cbor::type type() const
    {
        return m_type;
    }

    /**
     * @brief Gives the value of the node when type() is `K`.
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
     * `incorrect_type`: type() is not `K`.
     *
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
     * call: map[1].type()
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
        if (m_type != K) [[unlikely]]
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

    /**
     * @brief Gives the value of the node as `T`, as cbor::lazy::get() reads it.
     *
     * | T | Reads |
     * |---|---|
     * | an integer type | an integer that fits `T` |
     * | `double` | a float, or an integer that a `double` holds exactly |
     * | `bool`, `std::nullptr_t` | `false` or `true`, `null` |
     * | `cbor::simple_value` | `false`, `true`, `null`, `undefined` or another simple value |
     * | `std::string_view`, `std::span<std::byte const>` | a text, a byte string |
     * | `cbor::typed_array` | a typed array (RFC 8746): its tag number and its bytes |
     * | `std::span<item const>` | an array: its elements |
     * | `std::ranges::chunk_view<std::span<item const>>` | a map: each key and its value as one chunk of two items |
     * | `std::pair<std::uint64_t, item const *>` | a tag, a bignum or a shared reference: the tag number or the index, and the content or the target |
     *
     * A negative integer gives its value, -1 minus the argument (RFC 8949 3.1).
     * Every `T` except the pair passes a tag that the call does not interpret, as cbor::lazy::get() does.
     * A tag 2, 3 or 24 and a typed array are not passed, and a shared reference is not passed.
     * A view and a pointer in the result point into the tree: the result is valid as long as the tree.
     *
     * @code{.example}
     * message: {"a": -500}
     * call: auto const tree = cbor::decode(bytes)
     * result: owning item of {"a": -500}
     * call: auto const map = *(*tree)->get<std::ranges::chunk_view<std::span<cbor::item const>>>()
     * result: chunk_view of 1 pair: "a": -500
     * call: map[0][0].get<std::string_view>()
     * result: "a"
     * call: map[0][1].get<std::int64_t>()
     * result: -500
     * call: map[0][1].get<std::uint64_t>()
     * result: error number_out_of_range
     * @endcode
     *
     * @retval incorrect_type the node is of another kind
     * @retval number_out_of_range the number does not fit `T`, or the node is a bignum: a bignum node holds more than
     * 64 bits, because cbor::decode() makes a smaller bignum an integer node
     * @retval inadmissible_type_for_tag_content the node is a bignum whose content is not a byte string
     * @see cbor::item::visit()
     * @see cbor::lazy::get()
     * @see cbor::decode()
     */
    template <class T>
        requires((scalar_result<T>
#ifdef __STDCPP_FLOAT128_T__
                  && !std::is_same_v<T, std::float128_t>
#endif
                  ) ||
                 view_result<T> || std::same_as<T, std::span<item const>> ||
                 std::same_as<T, std::ranges::chunk_view<std::span<item const>>> ||
                 std::same_as<T, std::pair<std::uint64_t, item const *>>)
    std::expected<T, error> get() const
    {
        item const *at = this;
        if constexpr (!std::same_as<T, std::pair<std::uint64_t, item const *>>)
            while (at->m_type == cbor::type::tag && detail::heads::is_tag_passed(at->via.tag.number))
                at = at->via.tag.content;
        item const &node = *at;
        if constexpr (std::integral<T> && !std::same_as<T, bool>) {
            bool const negative = node.m_type == cbor::type::negative_integer;
            if (node.m_type != cbor::type::unsigned_integer && !negative) [[unlikely]]
                return node.reject_as_number();
            if (error const c = detail::validity::check_number_range<T>(negative, node.via.u64).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            return negative ? detail::heads::subtract_argument_from_minus_one<T>(node.via.u64)
                            : static_cast<T>(node.via.u64);
        } else if constexpr (std::same_as<T, double>) {
            switch (node.m_type) {
            case cbor::type::half_float:
            case cbor::type::single_float:
            case cbor::type::double_float:
                return node.via.f64;
            case cbor::type::unsigned_integer:
            case cbor::type::negative_integer: {
                bool const negative = node.m_type == cbor::type::negative_integer;
                if (error const c =
                        detail::validity::check_number_range<double>(negative, node.via.u64).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                return negative ? detail::heads::subtract_argument_from_minus_one<double>(node.via.u64)
                                : static_cast<double>(node.via.u64);
            }
            [[unlikely]] default:
                return node.reject_as_number();
            }
        } else if constexpr (std::same_as<T, bool>) {
            if (node.m_type != cbor::type::false_value && node.m_type != cbor::type::true_value) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return node.m_type == cbor::type::true_value;
        } else if constexpr (std::same_as<T, std::nullptr_t>) {
            if (node.m_type != cbor::type::null) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return nullptr;
        } else if constexpr (std::same_as<T, simple_value>) {
            switch (node.m_type) {
            case cbor::type::false_value:
            case cbor::type::true_value:
            case cbor::type::null:
            case cbor::type::undefined:
            case cbor::type::simple_value:
                return node.via.simple_value;
            [[unlikely]] default:
                return std::unexpected(error::incorrect_type);
            }
        } else if constexpr (std::same_as<T, std::string_view>) {
            if (node.m_type != cbor::type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return node.via.str;
        } else if constexpr (std::same_as<T, std::span<std::byte const>>) {
            if (node.m_type != cbor::type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return node.via.bin;
        } else if constexpr (std::same_as<T, typed_array>) {
            if (node.m_type != cbor::type::typed_array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return typed_array{node.via.tag.number, node.via.tag.content->via.bin};
        } else if constexpr (std::same_as<T, std::span<item const>>) {
            if (node.m_type != cbor::type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return node.via.array;
        } else if constexpr (std::same_as<T, std::ranges::chunk_view<std::span<item const>>>) {
            if (node.m_type != cbor::type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return std::ranges::chunk_view(node.via.map, rfc8949::data_items_per_pair);
        } else {
            switch (node.m_type) {
            case cbor::type::tag:
            case cbor::type::unsigned_bignum:
            case cbor::type::negative_bignum:
            case cbor::type::shared_reference:
                return std::pair<std::uint64_t, item const *>(node.via.tag.number, node.via.tag.content);
            [[unlikely]] default:
                return std::unexpected(error::incorrect_type);
            }
        }
    }

#ifdef CBOR_HAS_INT128
    /**
     * @brief Calls `f` once with the value of the node, as std::visit calls its visitor, and gives what `f` gives.
     *
     * | Kind | `f` receives |
     * |---|---|
     * | `unsigned_integer` | `std::uint64_t` |
     * | `negative_integer` | `cbor::int128`: the value, -1 minus the argument (RFC 8949 3.1) |
     * | `half_float`, `single_float`, `double_float` | `double` |
     * | `byte_string`, `text_string` | `std::span<std::byte const>`, `std::string_view` |
     * | `array` | `std::span<item const>`: the elements |
     * | `map` | `std::ranges::chunk_view<std::span<item const>>`: each key and its value as one chunk of two items |
     * | `false_value`, `true_value` | `bool` |
     * | `null` | `std::nullptr_t` |
     * | `undefined`, `simple_value` | `cbor::simple_value` |
     * | `tag`, `unsigned_bignum`, `negative_bignum` | `std::pair<std::uint64_t, item const *>`: the tag number and the content; a bignum has the tag number 2 or 3 (RFC 8949 3.4.3) |
     * | `typed_array` | `cbor::typed_array` |
     * | `shared_reference` | `item const *`: the target |
     *
     * `f` gives one return type for every alternative, as std::visit requires; another `f` does not compile.
     * visit exists where cbor::int128 exists.
     * A view and a pointer that `f` receives point into the tree.
     *
     * @code{.example}
     * message: [1, -2]
     * call: auto const tree = cbor::decode(bytes)
     * result: owning item of [1, -2]
     * call: (*tree)->visit([](auto const &v) { return sizeof(v); })
     * result: 16
     * call: auto const elements = *(*tree)->get<std::span<cbor::item const>>()
     * result: span of 2 items: 1, -2
     * call: elements[1].visit([](auto const v) -> bool { if constexpr (std::is_same_v<decltype(v), cbor::int128 const>) return v == -2; else return false; })
     * result: true
     * @endcode
     *
     * @see cbor::item::get()
     */
    template <class F>
    decltype(auto) visit(F &&f) const
    {
        using result = std::invoke_result_t<F, std::uint64_t>;
        static_assert(std::same_as<std::invoke_result_t<F, int128>, result> &&
                          std::same_as<std::invoke_result_t<F, double>, result> &&
                          std::same_as<std::invoke_result_t<F, std::span<std::byte const>>, result> &&
                          std::same_as<std::invoke_result_t<F, std::string_view>, result> &&
                          std::same_as<std::invoke_result_t<F, std::span<item const>>, result> &&
                          std::same_as<std::invoke_result_t<F, std::ranges::chunk_view<std::span<item const>>>, result> &&
                          std::same_as<std::invoke_result_t<F, bool>, result> &&
                          std::same_as<std::invoke_result_t<F, std::nullptr_t>, result> &&
                          std::same_as<std::invoke_result_t<F, simple_value>, result> &&
                          std::same_as<std::invoke_result_t<F, std::pair<std::uint64_t, item const *>>, result> &&
                          std::same_as<std::invoke_result_t<F, typed_array>, result> &&
                          std::same_as<std::invoke_result_t<F, item const *>, result>,
                      "cbor::item::visit: f must give one return type for every kind of node, as std::visit requires");
        switch (m_type) {
        case cbor::type::unsigned_integer:
            return std::invoke(std::forward<F>(f), via.u64);
        case cbor::type::negative_integer:
            return std::invoke(std::forward<F>(f), detail::heads::subtract_argument_from_minus_one<int128>(via.u64));
        case cbor::type::half_float:
        case cbor::type::single_float:
        case cbor::type::double_float:
            return std::invoke(std::forward<F>(f), via.f64);
        case cbor::type::byte_string:
            return std::invoke(std::forward<F>(f), via.bin);
        case cbor::type::text_string:
            return std::invoke(std::forward<F>(f), via.str);
        case cbor::type::array:
            return std::invoke(std::forward<F>(f), via.array);
        case cbor::type::map:
            return std::invoke(std::forward<F>(f), std::ranges::chunk_view(via.map, rfc8949::data_items_per_pair));
        case cbor::type::false_value:
        case cbor::type::true_value:
            return std::invoke(std::forward<F>(f), m_type == cbor::type::true_value);
        case cbor::type::null:
            return std::invoke(std::forward<F>(f), nullptr);
        case cbor::type::undefined:
        case cbor::type::simple_value:
            return std::invoke(std::forward<F>(f), via.simple_value);
        case cbor::type::tag:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum:
            return std::invoke(std::forward<F>(f), std::pair<std::uint64_t, item const *>(via.tag.number, via.tag.content));
        case cbor::type::typed_array:
            return std::invoke(std::forward<F>(f), typed_array{via.tag.number, via.tag.content->via.bin});
        case cbor::type::shared_reference:
            return std::invoke(std::forward<F>(f), via.tag.content);
        }
        detail::validity::throw_logic_error("cbor::item::visit: the type is not a member of cbor::type");
    }
#endif

private:
    std::unexpected<error> reject_as_number() const
    {
        if (m_type != cbor::type::unsigned_bignum && m_type != cbor::type::negative_bignum) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (error const c = detail::validity::check_tag_content(via.tag.number,
                                                                 detail::heads::major_type_of(via.tag.content->m_type))
                                .error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        return std::unexpected(error::number_out_of_range);
    }

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
        : m_type(t), additional_information(info), via(v)
    {
    }

    template <bool>
    friend struct detail::tree_decoder;

    friend class detail::encoding;
};

}

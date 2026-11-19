/**
 * @file
 * Gives cbor::item, one node of a decoded CBOR tree. item::get<T>
 * gives the value of the node as the C++ type T, and item::visit
 * calls a function with the value of the node.
 */
#pragma once

#include <cstddef>
#include <concepts>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <ranges>
#include <span>
#include <string>
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

class extended_diagnostic_notation;

struct tree_owner;

namespace dom
{

struct kind;
struct value_block;
class decoder;
class reader;

}

}

struct item;

std::expected<std::string, error> to_edn(item const &root);

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

namespace detail
{

class array_iterator
{
    std::span<item const> rest;

    friend struct cbor::item;

    explicit array_iterator(std::span<item const> const siblings) : rest(siblings)
    {
    }

public:
    using value_type = item;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::forward_iterator_tag;

    array_iterator() = default;

    item const &operator*() const;

    array_iterator &operator++();

    array_iterator operator++(int)
    {
        array_iterator const before = *this;
        ++*this;
        return before;
    }

    friend bool operator==(array_iterator const &a, array_iterator const &b)
    {
        return a.rest.data() == b.rest.data();
    }
};

class map_iterator
{
    std::span<item const> rest;

    friend struct cbor::item;

    explicit map_iterator(std::span<item const> const siblings) : rest(siblings)
    {
    }

public:
    using value_type = std::pair<item const &, item const &>;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::forward_iterator_tag;

    map_iterator() = default;

    std::pair<item const &, item const &> operator*() const;

    map_iterator &operator++();

    map_iterator operator++(int)
    {
        map_iterator const before = *this;
        ++*this;
        return before;
    }

    friend bool operator==(map_iterator const &a, map_iterator const &b)
    {
        return a.rest.data() == b.rest.data();
    }
};

namespace dom
{

struct kind {
    static constexpr std::uint8_t type_extended = 0;
    static constexpr std::uint8_t type_bytes = 1;
    static constexpr std::uint8_t type_null = 2;
    static constexpr std::uint8_t type_bool = 3;
    static constexpr std::uint8_t type_number = 4;
    static constexpr std::uint8_t type_text = 5;
    static constexpr std::uint8_t type_array = 6;
    static constexpr std::uint8_t type_map = 7;
    static constexpr int subtype_bit = 3;
    static constexpr std::uint8_t type_mask = (1 << subtype_bit) - 1;
    static constexpr int width_bit = 5;
    static constexpr int width_bits = 2;
    static constexpr std::uint8_t subtype_mask = ((1 << width_bit) - 1) & ~type_mask;
    static constexpr int shared_bit = width_bit + width_bits;
    static constexpr std::uint8_t shared_flag = 1 << shared_bit;
    static constexpr int info_bit = 8;
    static constexpr std::uint8_t extended_mask = (shared_flag - 1) & ~type_mask;
    static constexpr std::uint8_t width_mask = ((1 << width_bits) - 1) << width_bit;
    static constexpr int tag_bit = 13;
    static constexpr std::uint64_t info_mask = (std::uint64_t{1} << (tag_bit - info_bit)) - 1;
    static constexpr std::uint64_t high_bound = std::uint64_t{1}
                                                << (std::numeric_limits<std::uint64_t>::digits - tag_bit);

    static constexpr std::uint8_t subtype_false = 0 << subtype_bit;
    static constexpr std::uint8_t subtype_true = 1 << subtype_bit;
    static constexpr std::uint8_t subtype_undefined = 1 << subtype_bit;
    static constexpr std::uint8_t subtype_uint = 0 << subtype_bit;
    static constexpr std::uint8_t subtype_sint = 1 << subtype_bit;
    static constexpr std::uint8_t subtype_real = 2 << subtype_bit;
    static constexpr std::uint8_t subtype_negative = 3 << subtype_bit;

    static constexpr std::uint8_t extended_tag = 1 << subtype_bit;
    static constexpr std::uint8_t extended_simple = 2 << subtype_bit;
    static constexpr std::uint8_t extended_shared_reference = 3 << subtype_bit;
    static constexpr std::uint8_t extended_typed_array = 4 << subtype_bit;

    static constexpr std::uint8_t info_of(std::uint64_t const tag)
    {
        return static_cast<std::uint8_t>(tag >> info_bit & info_mask);
    }

    static constexpr std::uint64_t high_of(std::uint64_t const tag)
    {
        return tag >> tag_bit;
    }

    static constexpr std::uint64_t word(std::uint64_t const high, std::uint8_t const info, std::uint8_t const low)
    {
        return high << tag_bit | std::uint64_t{info} << info_bit | low;
    }

    static constexpr bool is_extended_word(std::uint64_t const tag, std::uint8_t const extended)
    {
        return (tag & type_mask) == type_extended && (tag & extended_mask) == extended;
    }

    static constexpr bool is_tag_word(std::uint64_t const tag)
    {
        return is_extended_word(tag, extended_tag) || is_extended_word(tag, extended_typed_array);
    }

    static constexpr bool is_extended(item const &x, std::uint8_t extended);

    static constexpr bool is_tag(item const &x);

    static constexpr std::size_t width_of(item const &x);
};

}

template <class T, class Iterator>
struct is_sized_range_of : std::false_type {
};

template <class Iterator>
struct is_sized_range_of<std::ranges::subrange<Iterator, Iterator, std::ranges::subrange_kind::sized>, Iterator>
    : std::true_type {
};

}

/**
 * @brief One node of a tree from cbor::decode() or `lazy::to_item`.
 *
 * cbor::item::type() gives the kind of the node.
 * cbor::item::additional_information() gives the low 5 bits of the initial byte (RFC 8949 3).
 * cbor::item::get() and cbor::item::visit() give the value.
 * The constructor is private: only a decode makes an item.
 * An item owns no memory and has no destructor to run.
 * An item takes 16 bytes, as a value of the yyjson DOM: the items of a tree stand in one array, and a container is
 * followed by its descendants. So an item is not copied: it is used where its tree holds it.
 * The tree owns every node and every string.
 * An item gives no error.
 *
 * @code{.example}
 * message: [1, {"a": 2}]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, {"a": 2}]
 * call: (*tree)->type()
 * result: cbor::type::array
 * call: (*tree)->additional_information()
 * result: 2
 * call: (*tree)->get<cbor::array_range>()->size()
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
    std::uint64_t m_tag;
    union {
        std::uint64_t u64;
        std::int64_t i64;
        double f64;
        char const *str;
        std::size_t ofs;
    } m_uni;

    item() = default;

    item(item const &) = default;

    item &operator=(item const &) = default;

public:
    item(item &&) = delete;

    item &operator=(item &&) = delete;

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
     * call: std::ranges::next((*tree)->get<cbor::array_range>()->begin())->type()
     * result: cbor::type::unsigned_bignum
     * @endcode
     *
     * @see cbor::lazy::type()
     */
    constexpr cbor::type type() const
    {
        using kind = detail::dom::kind;
        switch (m_tag & kind::type_mask) {
        case kind::type_bytes:
            return cbor::type::byte_string;
        case kind::type_text:
            return cbor::type::text_string;
        case kind::type_array:
            return cbor::type::array;
        case kind::type_map:
            return cbor::type::map;
        case kind::type_null:
            return (m_tag & kind::subtype_mask) == kind::subtype_undefined ? cbor::type::undefined : cbor::type::null;
        case kind::type_bool:
            return (m_tag & kind::subtype_mask) == kind::subtype_true ? cbor::type::true_value
                                                                      : cbor::type::false_value;
        case kind::type_number:
            switch (m_tag & kind::subtype_mask) {
            case kind::subtype_uint:
                return cbor::type::unsigned_integer;
            case kind::subtype_real:
                return static_cast<cbor::type>(std::to_underlying(cbor::type::half_float) +
                                               ((m_tag & kind::width_mask) >> kind::width_bit));
            default:
                return cbor::type::negative_integer;
            }
        default:
            switch (m_tag & kind::extended_mask) {
            case kind::extended_simple:
                return cbor::type::simple_value;
            case kind::extended_shared_reference:
                return cbor::type::shared_reference;
            case kind::extended_typed_array:
                return cbor::type::typed_array;
            default:
                return detail::heads::type_of(major_type::tag, 0, m_uni.u64);
            }
        }
    }

    /**
     * @brief Gives the additional information of the head of the node: the low 5 bits of its initial byte (RFC 8949
     * 3).
     *
     * It says the width of the argument, so an encode writes the head in the width that the decoded head had.
     *
     * @code{.example}
     * bytes: 19 00 01
     * call: auto const tree = cbor::decode(bytes)
     * result: owning item of 1_1
     * call: (*tree)->additional_information()
     * result: 25
     * @endcode
     */
    constexpr std::uint8_t additional_information() const
    {
        return detail::dom::kind::info_of(m_tag);
    }

    /**
     * @brief Gives the text of the item in the extended diagnostic notation (EDN).
     *
     * The text is the one of cbor::encode() of the item, in the notation of RFC 8949 section 8 and of
     * draft-ietf-cbor-edn-literals. cbor::parse_edn() reads it back. The function is defined in
     * `<cbor/diagnostic_notation.hpp>`.
     *
     * @retval nesting_depth_exceeded the tree, or the path through its references, is deeper than the stack of the
     * thread holds
     * @see cbor::parse_edn()
     */
    std::expected<std::string, error> to_edn() const;

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
     * | `cbor::array_range` | an array: its elements |
     * | `cbor::map_range` | a map: each key and its value as one pair |
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
     * call: auto const [key, value] = (*tree)->get<cbor::map_range>()->front()
     * result: "a": -500
     * call: key.get<std::string_view>()
     * result: "a"
     * call: value.get<std::int64_t>()
     * result: -500
     * call: value.get<std::uint64_t>()
     * result: unexpected cbor::error::number_out_of_range
     * @endcode
     *
     * @retval incorrect_type the node is of another kind
     * @retval number_out_of_range the number does not fit `T`, or the node is a bignum: a bignum node holds more than
     * 64 bits, because cbor::decode() makes a smaller bignum an integer node
     * @retval inadmissible_type_for_tag_content the node is a bignum whose content is not a byte string
     * @snippet doc_reference_item.cpp get
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
                 view_result<T> || detail::is_sized_range_of<T, detail::array_iterator>::value ||
                 detail::is_sized_range_of<T, detail::map_iterator>::value ||
                 std::same_as<T, std::pair<std::uint64_t, item const *>>)
    std::expected<T, error> get() const;

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
     * | `array` | `cbor::array_range`: the elements |
     * | `map` | `cbor::map_range`: each key and its value as one pair |
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
     * @snippet doc_reference_item.cpp visit
     * @see cbor::item::get()
     */
    template <class F>
    decltype(auto) visit(F &&f) const;
#endif

private:
    std::unexpected<error> reject_as_number() const
    {
        cbor::type const t = type();
        if (t != cbor::type::unsigned_bignum && t != cbor::type::negative_bignum) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (error const c =
                detail::validity::check_tag_content(m_uni.u64, detail::heads::major_type_of(content().type()))
                    .error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        return std::unexpected(error::number_out_of_range);
    }

    std::uint64_t argument() const
    {
        if ((m_tag & detail::dom::kind::subtype_mask) == detail::dom::kind::subtype_sint)
            return static_cast<std::uint64_t>(~m_uni.i64);
        return m_uni.u64;
    }

    bool holds_shared_reference() const
    {
        return (m_tag & detail::dom::kind::shared_flag) != 0;
    }

    std::size_t length() const
    {
        return static_cast<std::size_t>(detail::dom::kind::high_of(m_tag));
    }

    std::string_view text() const
    {
        return {m_uni.str, length()};
    }

    std::span<std::byte const> bytes() const
    {
        return std::as_bytes(std::span(text()));
    }

    std::span<item const> descendants() const;

    item const &content() const
    {
        return descendants().subspan(1).front();
    }

    item const &target() const;

    auto elements() const;

    auto entries() const;

    friend struct detail::dom::kind;

    friend struct detail::dom::value_block;

    friend class detail::dom::decoder;

    friend class detail::dom::reader;

    friend class detail::encoding;

    friend class detail::extended_diagnostic_notation;

    friend struct detail::tree_owner;

    friend std::expected<std::string, error> cbor::to_edn(item const &root);
};

static_assert(sizeof(item) == sizeof(std::uint64_t) + sizeof(std::uint64_t));

constexpr bool detail::dom::kind::is_extended(item const &x, std::uint8_t const extended)
{
    return is_extended_word(x.m_tag, extended);
}

constexpr bool detail::dom::kind::is_tag(item const &x)
{
    return is_tag_word(x.m_tag);
}

constexpr std::size_t detail::dom::kind::width_of(item const &x)
{
    switch (x.m_tag & type_mask) {
    case type_array:
    case type_map:
        return x.m_uni.ofs;
    case type_extended:
        if (is_tag(x))
            return static_cast<std::size_t>(high_of(x.m_tag));
        return 1;
    default:
        return 1;
    }
}

#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif

inline std::span<item const> item::descendants() const
{
    return {this, detail::dom::kind::width_of(*this)};
}

inline item const &item::target() const
{
    return *(this + m_uni.i64);
}

#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif

/**
 * @brief The elements of an array, in their order, as a forward range with a size (RFC 8949 3.1, major type 4).
 *
 * cbor::item::get() gives it for an array. Each element is a `cbor::item const &`. The range is a view into the tree.
 *
 * @code{.example}
 * message: [1, "a"]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, "a"]
 * call: auto const elements = *(*tree)->get<cbor::array_range>()
 * result: 2 elements: 1, "a"
 * call: elements.size()
 * result: 2
 * call: elements.front().get<int>()
 * result: 1
 * @endcode
 *
 * @ingroup decode
 */
using array_range =
    std::ranges::subrange<detail::array_iterator, detail::array_iterator, std::ranges::subrange_kind::sized>;

/**
 * @brief The entries of a map, in their order, as a forward range with a size (RFC 8949 3.1, major type 5).
 *
 * cbor::item::get() gives it for a map. Each entry is a `std::pair<item const &, item const &>`: the key and the
 * value. The range is a view into the tree.
 *
 * @code{.example}
 * message: {"a": 1}
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of {"a": 1}
 * call: for (auto const [key, value] : *(*tree)->get<cbor::map_range>())
 * result: key "a", value 1
 * @endcode
 *
 * @ingroup decode
 */
using map_range =
    std::ranges::subrange<detail::map_iterator, detail::map_iterator, std::ranges::subrange_kind::sized>;

static_assert(std::ranges::forward_range<array_range> && std::ranges::view<array_range> &&
              std::ranges::sized_range<array_range>);
static_assert(std::ranges::forward_range<map_range> && std::ranges::view<map_range> &&
              std::ranges::sized_range<map_range>);

inline item const &detail::array_iterator::operator*() const
{
    return rest.front();
}

inline detail::array_iterator &detail::array_iterator::operator++()
{
    rest = rest.subspan(dom::kind::width_of(rest.front()));
    return *this;
}

inline std::pair<item const &, item const &> detail::map_iterator::operator*() const
{
    return {rest.front(), rest.subspan(dom::kind::width_of(rest.front())).front()};
}

inline detail::map_iterator &detail::map_iterator::operator++()
{
    std::span<item const> const value = rest.subspan(dom::kind::width_of(rest.front()));
    rest = value.subspan(dom::kind::width_of(value.front()));
    return *this;
}

inline auto item::elements() const
{
    std::span<item const> const all = descendants();
    return array_range(detail::array_iterator(all.subspan(1)), detail::array_iterator(all.subspan(all.size())),
                       length());
}

inline auto item::entries() const
{
    std::span<item const> const all = descendants();
    return map_range(detail::map_iterator(all.subspan(1)), detail::map_iterator(all.subspan(all.size())), length());
}

template <class T>
    requires((scalar_result<T>
#ifdef __STDCPP_FLOAT128_T__
              && !std::is_same_v<T, std::float128_t>
#endif
              ) ||
             view_result<T> || detail::is_sized_range_of<T, detail::array_iterator>::value ||
             detail::is_sized_range_of<T, detail::map_iterator>::value ||
             std::same_as<T, std::pair<std::uint64_t, item const *>>)
std::expected<T, error> item::get() const
{
    item const *at = this;
    if constexpr (!std::same_as<T, std::pair<std::uint64_t, item const *>>)
        while (at->type() == cbor::type::tag && detail::heads::is_tag_passed(at->m_uni.u64))
            at = &at->content();
    item const &node = *at;
    cbor::type const t = node.type();
    if constexpr (std::integral<T> && !std::same_as<T, bool>) {
        bool const negative = t == cbor::type::negative_integer;
        if (t != cbor::type::unsigned_integer && !negative) [[unlikely]]
            return node.reject_as_number();
        std::uint64_t const argument = node.argument();
        if (error const c = detail::validity::check_number_range<T>(negative, argument).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        return negative ? detail::heads::subtract_argument_from_minus_one<T>(argument) : static_cast<T>(argument);
    } else if constexpr (std::same_as<T, double>) {
        switch (t) {
        case cbor::type::half_float:
        case cbor::type::single_float:
        case cbor::type::double_float:
            return node.m_uni.f64;
        case cbor::type::unsigned_integer:
        case cbor::type::negative_integer: {
            bool const negative = t == cbor::type::negative_integer;
            std::uint64_t const argument = node.argument();
            if (error const c = detail::validity::check_number_range<double>(negative, argument).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            return negative ? detail::heads::subtract_argument_from_minus_one<double>(argument)
                            : static_cast<double>(argument);
        }
        [[unlikely]] default:
            return node.reject_as_number();
        }
    } else if constexpr (std::same_as<T, bool>) {
        if (t != cbor::type::false_value && t != cbor::type::true_value) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return t == cbor::type::true_value;
    } else if constexpr (std::same_as<T, std::nullptr_t>) {
        if (t != cbor::type::null) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return nullptr;
    } else if constexpr (std::same_as<T, simple_value>) {
        switch (t) {
        case cbor::type::false_value:
        case cbor::type::true_value:
        case cbor::type::null:
        case cbor::type::undefined:
            return static_cast<simple_value>(node.additional_information());
        case cbor::type::simple_value:
            return static_cast<simple_value>(node.m_uni.u64);
        [[unlikely]] default:
            return std::unexpected(error::incorrect_type);
        }
    } else if constexpr (std::same_as<T, std::string_view>) {
        if (t != cbor::type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return node.text();
    } else if constexpr (std::same_as<T, std::span<std::byte const>>) {
        if (t != cbor::type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return node.bytes();
    } else if constexpr (std::same_as<T, typed_array>) {
        if (t != cbor::type::typed_array) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return typed_array{node.m_uni.u64, node.content().bytes()};
    } else if constexpr (detail::is_sized_range_of<T, detail::array_iterator>::value) {
        if (t != cbor::type::array) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return node.elements();
    } else if constexpr (detail::is_sized_range_of<T, detail::map_iterator>::value) {
        if (t != cbor::type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return node.entries();
    } else {
        switch (t) {
        case cbor::type::tag:
        case cbor::type::unsigned_bignum:
        case cbor::type::negative_bignum:
            return std::pair<std::uint64_t, item const *>(node.m_uni.u64, &node.content());
        case cbor::type::shared_reference:
            return std::pair<std::uint64_t, item const *>(detail::dom::kind::high_of(node.m_tag), &node.target());
        [[unlikely]] default:
            return std::unexpected(error::incorrect_type);
        }
    }
}

#ifdef CBOR_HAS_INT128
template <class F>
decltype(auto) item::visit(F &&f) const
{
    using result = std::invoke_result_t<F, std::uint64_t>;
    static_assert(std::same_as<std::invoke_result_t<F, int128>, result> &&
                      std::same_as<std::invoke_result_t<F, double>, result> &&
                      std::same_as<std::invoke_result_t<F, std::span<std::byte const>>, result> &&
                      std::same_as<std::invoke_result_t<F, std::string_view>, result> &&
                      std::same_as<std::invoke_result_t<F, array_range>, result> &&
                      std::same_as<std::invoke_result_t<F, map_range>, result> &&
                      std::same_as<std::invoke_result_t<F, bool>, result> &&
                      std::same_as<std::invoke_result_t<F, std::nullptr_t>, result> &&
                      std::same_as<std::invoke_result_t<F, simple_value>, result> &&
                      std::same_as<std::invoke_result_t<F, std::pair<std::uint64_t, item const *>>, result> &&
                      std::same_as<std::invoke_result_t<F, typed_array>, result> &&
                      std::same_as<std::invoke_result_t<F, item const *>, result>,
                  "cbor::item::visit: f must give one return type for every kind of node, as std::visit requires");
    switch (type()) {
    case cbor::type::unsigned_integer:
        return std::invoke(std::forward<F>(f), m_uni.u64);
    case cbor::type::negative_integer:
        return std::invoke(std::forward<F>(f), detail::heads::subtract_argument_from_minus_one<int128>(argument()));
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float:
        return std::invoke(std::forward<F>(f), m_uni.f64);
    case cbor::type::byte_string:
        return std::invoke(std::forward<F>(f), bytes());
    case cbor::type::text_string:
        return std::invoke(std::forward<F>(f), text());
    case cbor::type::array:
        return std::invoke(std::forward<F>(f), elements());
    case cbor::type::map:
        return std::invoke(std::forward<F>(f), entries());
    case cbor::type::false_value:
    case cbor::type::true_value:
        return std::invoke(std::forward<F>(f), type() == cbor::type::true_value);
    case cbor::type::null:
        return std::invoke(std::forward<F>(f), nullptr);
    case cbor::type::undefined:
        return std::invoke(std::forward<F>(f), static_cast<simple_value>(additional_information()));
    case cbor::type::simple_value:
        return std::invoke(std::forward<F>(f), static_cast<simple_value>(m_uni.u64));
    case cbor::type::tag:
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        return std::invoke(std::forward<F>(f), std::pair<std::uint64_t, item const *>(m_uni.u64, &content()));
    case cbor::type::typed_array:
        return std::invoke(std::forward<F>(f), typed_array{m_uni.u64, content().bytes()});
    case cbor::type::shared_reference:
        return std::invoke(std::forward<F>(f), &target());
    }
    detail::validity::throw_logic_error("cbor::item::visit: the type is not a member of cbor::type");
}
#endif

}

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#ifdef __cpp_impl_reflection
#include <meta>
#endif
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include "binding.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "owning_ref.hpp"

namespace cbor
{

#ifdef __cpp_impl_reflection
template <class T>
consteval std::size_t no_fixed_size()
{
    std::unreachable();
}

struct skip {};

struct allow {};

struct allowlist {};

struct key {
    char const *text;

    consteval explicit key(char const *const s) : text(std::define_static_string(std::string_view(s)))
    {
    }
};

struct tag {
    std::uint64_t number;
};

template <std::uint64_t Number, class T>
struct tagged {
    static constexpr std::uint64_t number = Number;
    T content;
};

template <class Root>
consteval bool tags_registered();

template <class T>
class schema;

template <class E>
class typed_array_view;

struct directory {
    std::size_t at;
    std::size_t count;
};

class packed
{
    static constexpr std::size_t dynamic_type_sizes = 2 * heads::initial_byte_size + sizeof(std::uint32_t);
    static constexpr std::size_t item_head = heads::initial_byte_size + sizeof(std::uint32_t);
    static constexpr std::size_t shared_first = 16;

    template <class T>
    struct fixed_length {};

    template <class E, std::size_t N>
    struct fixed_length<E[N]> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    template <class E, std::size_t N>
    struct fixed_length<std::array<E, N>> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    template <class E, std::size_t N>
        requires(N != std::dynamic_extent)
    struct fixed_length<std::span<E, N>> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    static consteval std::string_view key_of(std::meta::info const member)
    {
        for (std::meta::info const a : std::meta::annotations_of(member))
            if (std::meta::remove_const(std::meta::type_of(a)) == ^^cbor::key)
                return std::meta::extract<cbor::key>(a).text;
        auto const name = std::meta::u8identifier_of(member);
        return std::define_static_string(std::string(name.begin(), name.end()));
    }

    static consteval std::optional<std::uint64_t> tag_number_of(std::meta::info const type)
    {
        for (std::meta::info const a : std::meta::annotations_of(std::meta::dealias(type)))
            if (std::meta::remove_const(std::meta::type_of(a)) == ^^cbor::tag)
                return std::meta::extract<cbor::tag>(a).number;
        return std::nullopt;
    }

    static consteval bool keys_unique(std::span<std::meta::info const> const members)
    {
        std::vector<std::string_view> keys;
        for (std::meta::info const m : members)
            keys.push_back(key_of(m));
        std::ranges::sort(keys);
        return validity::keys_unique(keys);
    }

    static consteval bool annotated(std::meta::info const entity, std::meta::info const type)
    {
        return std::ranges::any_of(std::meta::annotations_of(entity), [type](std::meta::info const a) {
            return std::meta::remove_const(std::meta::type_of(a)) == type;
        });
    }

    template <class U>
    static consteval std::span<std::meta::info const> data_members()
    {
        bool const only_allowed = annotated(^^U, ^^cbor::allowlist);
        std::vector<std::meta::info> members;
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(^^U, std::meta::access_context::unprivileged()))
            if (!annotated(m, ^^cbor::skip) && (!only_allowed || annotated(m, ^^cbor::allow)))
                members.push_back(m);
        return std::define_static_array(members);
    }

    template <class Root>
    friend consteval bool cbor::tags_registered();

    template <class T, class Root>
    static consteval std::size_t fixed_size();

    template <class T, std::meta::info Member, class Root>
    static consteval std::size_t member_offset();

    template <class U>
    static constexpr bool is_text_range = requires {
        requires std::ranges::contiguous_range<U>;
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, char> ||
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, char8_t>;
    };

    template <class U>
    static constexpr bool is_byte_range = requires {
        requires std::ranges::contiguous_range<U>;
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, unsigned char> ||
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>;
    };

    template <class U>
    static constexpr bool is_fixed_string = requires {
        typename fixed_length<U>::element;
        requires std::same_as<typename fixed_length<U>::element, char> ||
                     std::same_as<typename fixed_length<U>::element, char8_t> ||
                     std::same_as<typename fixed_length<U>::element, unsigned char> ||
                     std::same_as<typename fixed_length<U>::element, std::byte>;
    };

    template <class U>
    static constexpr bool has_fixed_underlying_type = std::is_enum_v<U> && requires { U{0}; };

    template <class U>
    static constexpr bool is_optional = requires(U const &v) {
        v.has_value();
        *v;
        typename U::value_type;
    } && !requires { typename U::error_type; };

    template <class U>
    static constexpr bool is_inline_optional = is_optional<U> && requires {
        requires std::is_class_v<typename U::value_type> && std::is_aggregate_v<typename U::value_type>;
        requires !requires { fixed_length<typename U::value_type>::value; };
    };

    static constexpr std::size_t inline_optional_head = 2;

    template <class U>
    static constexpr bool is_map = std::ranges::sized_range<U> && requires {
        typename U::key_type;
        typename U::mapped_type;
    };

    template <class U>
    static constexpr bool is_list = std::ranges::sized_range<U> && !is_text_range<U> && !is_byte_range<U> && !is_optional<U> &&
                                    !is_map<U> && !requires { fixed_length<U>::value; };

    template <class E>
    static consteval std::uint64_t typed_array_tag()
    {
        constexpr std::uint64_t f = std::is_floating_point_v<E> ? 1 : 0;
        constexpr std::uint64_t s = std::is_signed_v<E> && !std::is_floating_point_v<E> ? 1 : 0;
        constexpr std::uint64_t e = sizeof(E) > 1 ? 1 : 0;
        constexpr std::uint64_t ll = static_cast<std::uint64_t>(std::countr_zero(sizeof(E))) - f;
        return std::to_underlying(rfc8746::tag_number::typed_array_first) | f << 4 | s << 3 | e << 2 | ll;
    }

    template <class E>
    static constexpr bool is_typed_array_integer = requires {
        requires std::integral<E> && !std::same_as<E, bool> && !std::same_as<E, char> && !std::same_as<E, wchar_t> &&
                     !std::same_as<E, char8_t> && !std::same_as<E, char16_t> && !std::same_as<E, char32_t>;
        requires sizeof(E) <= sizeof(std::uint64_t);
    };

    template <class E>
    static constexpr bool is_typed_array_float = requires {
        requires std::is_floating_point_v<E> && std::numeric_limits<E>::is_iec559;
        requires(sizeof(E) == 2 && std::numeric_limits<E>::digits == 11) || (sizeof(E) == 4 && std::numeric_limits<E>::digits == 24) ||
                    (sizeof(E) == 8 && std::numeric_limits<E>::digits == 53);
    };

    template <class E>
    static constexpr bool is_typed_array_element = is_typed_array_integer<E> || is_typed_array_float<E>;

    template <class U>
    static constexpr bool is_typed_array = requires {
        requires is_list<U>;
        requires is_typed_array_element<std::remove_cv_t<std::ranges::range_value_t<U>>>;
    };

    static constexpr std::size_t typed_array_head = heads::initial_byte_size + sizeof(std::uint8_t) + item_head;

    template <class E>
    CBOR_ALWAYS_INLINE static E typed_array_element_read(std::span<char const, sizeof(E)> const in)
    {
        std::array<char, sizeof(E)> little;
        std::ranges::copy(in, little.begin());
        if constexpr (std::endian::native == std::endian::big)
            std::ranges::reverse(little);
        return std::bit_cast<E>(little);
    }

    template <class E>
    CBOR_ALWAYS_INLINE static void typed_array_element_write(std::span<char, sizeof(E)> const out, E const value)
    {
        auto little = std::bit_cast<std::array<char, sizeof(E)>>(value);
        if constexpr (std::endian::native == std::endian::big)
            std::ranges::reverse(little);
        std::ranges::copy(little, out.begin());
    }

    static constexpr void fixed_width_head_encode(std::vector<char> &encoded, major_type const major, std::size_t const width)
    {
        heads::head_append(
            encoded, major,
            static_cast<std::uint8_t>(std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                      std::countr_zero(width)),
            0);
    }

    template <class Root, class T>
    static consteval void zero_initialized_encode(std::vector<char> &encoded)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            heads::head_append(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
        } else if constexpr (std::is_enum_v<U>) {
            zero_initialized_encode<Root, std::underlying_type_t<U>>(encoded);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            heads::head_append(encoded, major_type::tag,
                               std::to_underlying(rfc8949::tag_number::unsigned_bignum));
            heads::head_append(encoded, major_type::byte_string, sizeof(U));
            encoded.resize(encoded.size() + sizeof(U));
#endif
        } else if constexpr (std::is_integral_v<U>) {
            fixed_width_head_encode(encoded, major_type::unsigned_integer, sizeof(U));
        } else if constexpr (std::is_floating_point_v<U>) {
            constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
            if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float16_t));
            else
#endif
            if constexpr (
#if defined(__STDCPP_BFLOAT16_T__)
                digits == std::numeric_limits<std::bfloat16_t>::digits ||
#endif
                digits == std::numeric_limits<std::float32_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float32_t));
            else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float64_t));
            else {
                heads::head_append(encoded, major_type::tag,
                                   std::to_underlying(rfc8746::tag_number::float128_big_endian));
                heads::head_append(encoded, major_type::byte_string, sizeof(std::float128_t));
                encoded.resize(encoded.size() + sizeof(std::float128_t));
            }
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t>) {
                heads::head_append(encoded, major_type::text_string, n);
                encoded.resize(encoded.size() + n);
            } else if constexpr (std::same_as<E, unsigned char> || std::same_as<E, std::byte>) {
                heads::head_append(encoded, major_type::byte_string, n);
                encoded.resize(encoded.size() + n);
            } else {
                heads::head_append(encoded, major_type::array, n);
                for (std::size_t i = 0; i < n; ++i)
                    zero_initialized_encode<Root, E>(encoded);
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            heads::head_append(encoded, major_type::tag, *tag_number_of(^^U));
            straight_reference_encode<Root, U>(encoded);
            fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
            auto const count = heads::big_endian(static_cast<std::uint32_t>(data_members<U>().size()));
            std::ranges::copy(count, encoded.end() - sizeof(std::uint32_t));
            template for (constexpr auto m : data_members<U>())
                zero_initialized_encode<Root, typename[:std::meta::type_of(m):]>(encoded);
        } else if constexpr (is_inline_optional<U>) {
            heads::head_append(encoded, major_type::array, 2);
            heads::head_append(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
            zero_initialized_encode<Root, typename U::value_type>(encoded);
        } else {
            heads::head_append(encoded, major_type::tag, std::to_underlying(validity::tag_number::reference));
            fixed_width_head_encode(encoded, major_type::unsigned_integer, sizeof(std::uint32_t));
        }
    }

    template <class Root, class T>
    static consteval std::span<char const> zero_initialized()
    {
        std::vector<char> encoded;
        zero_initialized_encode<Root, T>(encoded);
        return std::define_static_array(encoded);
    }

    template <class U>
    static consteval std::span<char const> record_keys()
    {
        std::vector<char> encoded;
        heads::head_append(encoded, major_type::tag,
                           std::to_underlying(validity::tag_number::record_function));
        static constexpr auto members = members_of<U>();
        heads::head_append(encoded, major_type::array, members.size());
        template for (constexpr std::size_t i : std::define_static_array(std::views::iota(0uz, members.size()))) {
            if constexpr (has_integer_keys<U>) {
                constexpr std::int64_t key = std::get<i>(U::keys);
                if (key >= 0)
                    heads::head_append(encoded, major_type::unsigned_integer, static_cast<std::uint64_t>(key));
                else
                    heads::head_append(encoded, major_type::negative_integer, static_cast<std::uint64_t>(-1 - key));
            } else {
                constexpr std::string_view key = key_of(members[i]);
                heads::head_append(encoded, major_type::text_string, key.size());
                encoded.insert(encoded.end(), key.begin(), key.end());
            }
        }
        return std::define_static_array(encoded);
    }

    template <class T>
    static consteval void record_types_collect(std::vector<std::meta::info> &types)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (requires { fixed_length<U>::value; }) {
            record_types_collect<typename fixed_length<U>::element>(types);
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            if (std::ranges::contains(types, std::meta::dealias(^^U)))
                return;
            types.push_back(std::meta::dealias(^^U));
            template for (constexpr auto m : data_members<U>())
                record_types_collect<typename[:std::meta::type_of(m):]>(types);
        } else if constexpr (is_inline_optional<U> || is_optional<U>) {
            record_types_collect<typename U::value_type>(types);
        } else if constexpr (is_map<U>) {
            record_types_collect<typename U::key_type>(types);
            record_types_collect<typename U::mapped_type>(types);
        } else if constexpr (std::ranges::sized_range<U>) {
            record_types_collect<std::ranges::range_value_t<U>>(types);
        }
    }

    template <class Root>
    static consteval std::span<std::meta::info const> packing_table_of()
    {
        std::vector<std::meta::info> types;
        record_types_collect<Root>(types);
        return std::define_static_array(types);
    }

    template <class Root>
    static consteval std::size_t shared_first_of()
    {
        return std::max(shared_first, packing_table_of<Root>().size() + 1);
    }

    static constexpr std::uint64_t straight_argument_count =
        std::to_underlying(validity::tag_number::straight_argument_last) -
        std::to_underlying(validity::tag_number::straight_argument_first) + 1;

    template <class Root, class U>
    static consteval std::uint64_t argument_index_of()
    {
        constexpr auto types = packing_table_of<Root>();
        return static_cast<std::uint64_t>(std::ranges::find(types, std::meta::dealias(^^std::remove_cv_t<U>)) - types.begin());
    }

    template <class Root, class U>
    static consteval void straight_reference_encode(std::vector<char> &encoded)
    {
        constexpr std::uint64_t i = argument_index_of<Root, U>();
        if constexpr (i < straight_argument_count) {
            heads::head_append(encoded, major_type::tag,
                               std::to_underlying(validity::tag_number::straight_argument_first) + i);
        } else {
            heads::head_append(encoded, major_type::tag, std::to_underlying(validity::tag_number::reference));
            heads::head_append(encoded, major_type::array, 2);
            heads::head_append(encoded, major_type::unsigned_integer, i - straight_argument_count);
        }
    }

    template <class Root, class U>
    static consteval std::size_t straight_reference_size()
    {
        std::vector<char> encoded;
        straight_reference_encode<Root, U>(encoded);
        return encoded.size();
    }

    template <class Root>
    static consteval std::span<char const> packing_prefix_of()
    {
        std::vector<char> encoded;
        heads::head_append(encoded, major_type::tag,
                           std::to_underlying(validity::tag_number::basic_packed_cbor));
        heads::head_append(encoded, major_type::array, 2);
        fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
        template for (constexpr std::meta::info type : packing_table_of<Root>()) {
            constexpr auto keys = record_keys<typename[:type:]>();
            encoded.insert(encoded.end(), keys.begin(), keys.end());
        }
        return std::define_static_array(encoded);
    }

    template <class U>
    static constexpr auto float_bits(U const value)
    {
        constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return std::bit_cast<std::uint16_t>(value);
        else
#endif
#if defined(__STDCPP_BFLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits)
            return std::bit_cast<std::uint32_t>(static_cast<std::float32_t>(value));
        else
#endif
        if constexpr (digits == std::numeric_limits<std::float32_t>::digits)
            return std::bit_cast<std::uint32_t>(value);
        else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
            return std::bit_cast<std::uint64_t>(value);
#ifdef __SIZEOF_INT128__
        else
            return std::bit_cast<uint128>(static_cast<std::float128_t>(value));
#endif
    }

    template <class Root>
    struct second_item {
        std::size_t items = 0;
        std::size_t bytes = 0;
        bool overflow = false;

        void bytes_add(std::size_t const n)
        {
            auto const sum = validity::checked_add(bytes, n);
            overflow |= !sum;
            bytes = sum.value_or(bytes);
        }

        void block_add(std::size_t const count, std::size_t const size)
        {
            auto const block = validity::checked_mul(count, size);
            overflow |= !block;
            bytes_add(block.value_or(0));
        }

        template <class E, class R>
        void elements_add(R const &range)
        {
            if constexpr (is_typed_array_element<std::remove_cv_t<E>>) {
                items += 1;
                bytes_add(typed_array_head);
                block_add(std::ranges::size(range), sizeof(E));
                return;
            }
            items += 1;
            bytes_add(item_head);
            block_add(std::ranges::size(range), fixed_size<E, Root>());
            if constexpr (!std::is_arithmetic_v<E> && !std::is_enum_v<E>)
                for (auto const &e : range)
                    add<E>(e);
        }

        template <class T>
        void add(T const &value)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || is_wide_integer<U>) {
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                if constexpr (!std::is_arithmetic_v<E> && !std::same_as<E, std::byte>)
                    for (auto const &e : value)
                        add<E>(e);
            } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
                template for (constexpr auto m : data_members<U>())
                    add<typename[:std::meta::type_of(m):]>(value.[:m:]);
            } else if constexpr (is_inline_optional<U>) {
                if (value.has_value())
                    add<typename U::value_type>(*value);
            } else if constexpr (is_optional<U>) {
                items += 1;
                bytes_add(item_head);
                if (value.has_value()) {
                    bytes_add(fixed_size<typename U::value_type, Root>());
                    add<typename U::value_type>(*value);
                }
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                items += 1;
                bytes_add(item_head + std::ranges::size(value));
            } else if constexpr (is_map<U>) {
                items += 1;
                bytes_add(item_head);
                block_add(std::ranges::size(value),
                          fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
                for (auto const &[k, v] : value) {
                    add<typename U::key_type>(k);
                    add<typename U::mapped_type>(v);
                }
            } else {
                elements_add<std::ranges::range_value_t<U>>(value);
            }
        }
    };

    template <class Root, class E>
    CBOR_ALWAYS_INLINE static void zero_initialized_copy(std::span<char, fixed_size<E, Root>()> const field)
    {
        constexpr std::size_t n = fixed_size<E, Root>();
        static_assert(zero_initialized<Root, E>().size() >= n, "The zero-initialized encoding must hold the whole field.");
        std::span<char const, n> const from{zero_initialized<Root, E>().data(), n};
        std::copy(from.begin(), from.end(), field.begin());
    }

    struct encode_cursor {
        std::size_t position;
        std::size_t index;
    };

    template <class Root>
    static consteval std::size_t directory_at()
    {
        return packing_prefix_of<Root>().size() + item_head;
    }

    template <class Root, class E, bool Exact, class R>
    CBOR_ALWAYS_INLINE static encode_cursor elements_encode(std::span<char> const out, std::size_t const data, R const &range,
                                       encode_cursor position)
    {
        constexpr std::size_t size = fixed_size<E, Root>();
        std::size_t at = data;
        for (auto const &e : range) {
            auto const field = out.subspan(at).template first<size>();
            zero_initialized_copy<Root, E>(field);
            position = value_encode<Root, E, Exact>(out, field, e, position);
            at += size;
        }
        return position;
    }

    template <class Root, bool Exact, class V>
    CBOR_ALWAYS_INLINE static encode_cursor reference_encode(std::span<char> const out, std::span<char, dynamic_type_sizes> const field,
                                        V const &value, encode_cursor c)
    {
        using U = std::remove_cv_t<V>;
        std::size_t const j = c.index++;
        std::size_t const item = c.position;
        std::size_t const m = j + shared_first_of<Root>() - shared_first;
        heads::u32_write(out, directory_at<Root>() + sizeof(std::uint32_t) * j, item);
        field[1] =
            heads::initial_byte(static_cast<major_type>(m & 1),
                                std::to_underlying(rfc8949::additional_information::four_byte_argument));
        auto const n = heads::big_endian(static_cast<std::uint32_t>(m >> 1));
        std::ranges::copy(n, field.template last<sizeof(std::uint32_t)>().begin());
        std::size_t const data = item + item_head;
        if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            heads::item_head_write(out, item, major_type::array, value.has_value() ? 1 : 0);
            c.position = data;
            if (value.has_value()) {
                c.position += fixed_size<E, Root>();
                auto const element = out.subspan(data).template first<fixed_size<E, Root>()>();
                zero_initialized_copy<Root, E>(element);
                c = value_encode<Root, E, Exact>(out, element, *value, c);
            }
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            std::size_t const length = std::ranges::size(value);
            heads::item_head_write(out, item, is_text_range<U> ? major_type::text_string : major_type::byte_string, length);
            auto const from = std::as_bytes(std::span(value));
            std::copy(from.begin(), from.end(), std::as_writable_bytes(out.subspan(data, length)).begin());
            c.position = data + length;
        } else if constexpr (is_map<U>) {
            using K = typename U::key_type;
            using M = typename U::mapped_type;
            std::size_t const length = std::ranges::size(value);
            heads::item_head_write(out, item, major_type::map, length);
            c.position = data + length * (fixed_size<K, Root>() + fixed_size<M, Root>());
            std::size_t at = data;
            for (auto const &[k, v] : value) {
                auto const key = out.subspan(at).template first<fixed_size<K, Root>()>();
                zero_initialized_copy<Root, K>(key);
                c = value_encode<Root, K, Exact>(out, key, k, c);
                at += fixed_size<K, Root>();
                auto const mapped = out.subspan(at).template first<fixed_size<M, Root>()>();
                zero_initialized_copy<Root, M>(mapped);
                c = value_encode<Root, M, Exact>(out, mapped, v, c);
                at += fixed_size<M, Root>();
            }
        } else if constexpr (is_typed_array<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            std::size_t const length = std::ranges::size(value);
            out[item] = heads::initial_byte(
                major_type::tag, std::to_underlying(rfc8949::additional_information::one_byte_argument));
            out[item + 1] = static_cast<char>(typed_array_tag<E>());
            heads::item_head_write(out, item + 2, major_type::byte_string, length * sizeof(E));
            std::size_t const elements = item + typed_array_head;
            c.position = elements + length * sizeof(E);
            if constexpr (std::endian::native == std::endian::little && std::ranges::contiguous_range<U>) {
                auto const from = std::as_bytes(std::span(value));
                std::ranges::copy(from, std::as_writable_bytes(out.subspan(elements, from.size())).begin());
            } else {
                std::size_t at = elements;
                for (E const e : value) {
                    typed_array_element_write<E>(out.subspan(at).template first<sizeof(E)>(), e);
                    at += sizeof(E);
                }
            }
        } else {
            using E = std::ranges::range_value_t<U>;
            std::size_t const length = std::ranges::size(value);
            heads::item_head_write(out, item, major_type::array, length);
            c.position = data + length * fixed_size<E, Root>();
            c = elements_encode<Root, E, Exact>(out, data, value, c);
        }
        return c;
    }

    template <class Root, class T, bool Exact>
    CBOR_ALWAYS_INLINE static encode_cursor value_encode(std::span<char> const out, std::span<char, fixed_size<T, Root>()> const field,
                                    T const &value, encode_cursor position)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            field.front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + value);
        } else if constexpr (std::is_enum_v<U>) {
            position = value_encode<Root, std::underlying_type_t<U>, Exact>(out, field, std::to_underlying(value), position);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 magnitude = static_cast<uint128>(value);
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = static_cast<uint128>(value >> 127);
                field.front() = heads::initial_byte(major_type::tag,
                                                    std::to_underlying(rfc8949::tag_number::unsigned_bignum) +
                                                        static_cast<std::uint64_t>(sign & 1));
                magnitude ^= sign;
            }
            auto const bytes = heads::big_endian(magnitude);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            auto const bytes = heads::big_endian(value);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(value >> (8 * sizeof(U) - 1));
            field.front() =
                heads::initial_byte(static_cast<major_type>(sign & 1),
                                    std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                        std::countr_zero(sizeof(U)));
            auto const bytes = heads::big_endian(static_cast<M>(static_cast<M>(value) ^ sign));
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::is_floating_point_v<U>) {
            auto const bits = heads::big_endian(float_bits(value));
            std::copy(bits.begin(), bits.end(), field.template last<bits.size()>().begin());
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                          std::same_as<E, std::byte>) {
                auto const from = std::as_bytes(std::span(value));
                std::copy(from.begin(), from.end(), std::as_writable_bytes(field.template last<n>()).begin());
            } else {
                constexpr std::size_t head = heads::head_size(n);
                static_assert(validity::checked_mul(n, fixed_size<E, Root>())
                                      .and_then([](std::size_t const elements) {
                                          return validity::checked_add(head, elements);
                                      })
                                      .value_or(std::numeric_limits<std::size_t>::max()) <=
                                  fixed_size<U, Root>(),
                              "The elements must lie inside the field.");
                std::size_t at = head;
                for (auto const &e : value) {
                    position = value_encode<Root, E, Exact>(out, field.subspan(at).template first<fixed_size<E, Root>()>(), e, position);
                    at += fixed_size<E, Root>();
                }
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            template for (constexpr auto m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                position = value_encode<Root, M, Exact>(out, field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(),
                                           value.[:m:], position);
            }
        } else if constexpr (is_inline_optional<U>) {
            if (value.has_value()) {
                using E = typename U::value_type;
                field.template subspan<1, 1>().front() =
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value));
                position = value_encode<Root, E, Exact>(out, field.template subspan<inline_optional_head, fixed_size<E, Root>()>(), *value,
                                           position);
            }
        } else {
            position = reference_encode<Root, Exact>(out, field, value, position);
        }
        return position;
    }

    template <class U>
    static consteval std::meta::info member_named(std::string_view const name)
    {
        constexpr auto members = data_members<U>();
        auto const m = std::ranges::find_if(members, [name](std::meta::info const member) {
            return key_of(member) == name;
        });
        return m == members.end() ? std::meta::info{} : *m;
    }

    static consteval std::size_t step_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find_first_of(path | std::views::drop(at + 1), std::string_view(".[")) -
                                        path.begin());
    }

    static consteval std::size_t index_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find(path | std::views::drop(at + 1), ']') - path.begin());
    }

    template <class T>
    static consteval std::size_t index_of(std::string_view const digits)
    {
        std::size_t value = 0;
        auto const [end, ec] = std::from_chars(digits.data(), std::to_address(digits.end()), value);
        if (ec != std::errc{} || end != std::to_address(digits.end())) [[unlikely]]
            return no_fixed_size<T>();
        return value;
    }

    template <class T>
    static T fixed_value_read(std::span<char const, fixed_size<T, T>()> const field)
    {
        using U = std::remove_cv_t<T>;
        auto const head = static_cast<unsigned char>(field.front());
        if constexpr (std::same_as<U, bool>) {
            return (head & 1) != 0;
        } else if constexpr (has_fixed_underlying_type<U>) {
            return static_cast<U>(fixed_value_read<std::underlying_type_t<U>>(field));
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 const magnitude = heads::unsigned128_read(field.template last<sizeof(U)>());
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = -static_cast<uint128>(head & 1);
                return static_cast<U>(magnitude ^ sign);
            } else {
                return magnitude;
            }
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            return heads::unsigned_read<U>(field.template last<sizeof(U)>());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(-static_cast<M>((head >> 5) & 1));
            return static_cast<U>(heads::unsigned_read<M>(field.template last<sizeof(M)>()) ^ sign);
        } else {
            using B = decltype(float_bits(U{}));
#ifdef __SIZEOF_INT128__
            if constexpr (std::same_as<B, uint128>) {
                auto const bits = heads::unsigned128_read(field.template last<sizeof(B)>());
                return static_cast<U>(std::bit_cast<std::float128_t>(bits));
            } else
#endif
            {
                auto const bits = heads::unsigned_read<B>(field.template last<sizeof(B)>());
#if defined(__STDCPP_FLOAT16_T__)
                using F = std::conditional_t<sizeof(B) == sizeof(std::uint16_t), std::float16_t,
                                             std::conditional_t<sizeof(B) == sizeof(std::uint32_t), std::float32_t,
                                                                std::float64_t>>;
                return static_cast<U>(std::bit_cast<F>(bits));
#else
                if constexpr (sizeof(B) == sizeof(std::uint16_t))
                    return static_cast<U>(heads::float_decode_binary16(bits));
                else
                    return static_cast<U>(
                        std::bit_cast<std::conditional_t<sizeof(B) == sizeof(std::uint32_t), std::float32_t,
                                                         std::float64_t>>(bits));
#endif
            }
        }
    }

    template <class T>
    CBOR_ALWAYS_INLINE static bool fixed_head_valid(std::span<char const, fixed_size<T, T>()> const field)
    {
        using U = std::remove_cv_t<T>;
        auto const head = static_cast<unsigned char>(field.front());
        if constexpr (std::same_as<U, bool>) {
            return (head | 1) == (std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(simple_value::true_value));
        } else if constexpr (has_fixed_underlying_type<U>) {
            return fixed_head_valid<std::underlying_type_t<U>>(field);
        } else {
            constexpr unsigned char expected = static_cast<unsigned char>(zero_initialized<void, U>()[0]);
            if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>)
                static_assert(fixed_size<T, T>() > 1, "The field must hold the second byte that is read.");
            if constexpr (std::signed_integral<U> || std::same_as<U, int128>) {
                if constexpr (std::same_as<U, int128>)
                    return (head & 0xfe) == expected && static_cast<unsigned char>(field[1]) == static_cast<unsigned char>(zero_initialized<void, U>()[1]);
                else
                    return (head & 0xdf) == expected;
            } else if constexpr (std::same_as<U, uint128>) {
                return head == expected && static_cast<unsigned char>(field[1]) == static_cast<unsigned char>(zero_initialized<void, U>()[1]);
            } else {
                return head == expected;
            }
        }
    }

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static bool class_tag_valid(std::span<char const, fixed_size<U, Root>()> const field)
    {
        constexpr std::size_t n = heads::head_size(*tag_number_of(^^U));
        static_assert(zero_initialized<Root, U>().size() >= n, "The zero-initialized encoding must hold the whole tag head.");
        std::span<char const, n> const expected{zero_initialized<Root, U>().data(), n};
        return std::ranges::equal(field.template first<n>(), expected);
    }

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    static constexpr char reference_tag_byte =
        heads::initial_byte(major_type::tag, std::to_underlying(validity::tag_number::reference));

    template <class Root>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> shared_index_read(std::span<char const, dynamic_type_sizes> const field)
    {
        constexpr std::size_t fillers = shared_first_of<Root>() - shared_first;
        auto const info = static_cast<unsigned char>(field[1]);
        if (field[0] != reference_tag_byte ||
            (info & 0xdf) != std::to_underlying(rfc8949::additional_information::four_byte_argument))
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const m = 2 * std::size_t{heads::unsigned_read<std::uint32_t>(field.subspan<2, sizeof(std::uint32_t)>())} + (info >> 5);
        if (m < fillers) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        return m - fillers;
    }

    template <major_type Major, class E>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> item_reference_read(std::string_view const encoded, std::size_t const item,
                                                                                  std::size_t const end, std::size_t const element)
    {
        if constexpr (is_typed_array_element<E>) {
            static_assert(validity::typed_array_element_size(typed_array_tag<E>()) == sizeof(E),
                          "The elements of the typed array tag of E are sizeof(E) bytes.");
            if (end < item + typed_array_head) [[unlikely]]
                return std::unexpected(error::too_little_data);
            static constexpr std::array<char, 3> head{
                heads::initial_byte(major_type::tag,
                                    std::to_underlying(rfc8949::additional_information::one_byte_argument)),
                static_cast<char>(typed_array_tag<E>()),
                heads::initial_byte(major_type::byte_string,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument))};
            if (!std::ranges::equal(std::span<char const>(encoded).subspan(item).template first<head.size()>(), head)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::size_t const size =
                heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(item + 3).template first<sizeof(std::uint32_t)>());
            if (size > end - item - typed_array_head) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (!validity::typed_array_check(typed_array_tag<E>(), size)) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            return reference{item + typed_array_head, size / sizeof(E)};
        } else {
            if (end < item + item_head) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (encoded[item] !=
                heads::initial_byte(Major,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument)))
                [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::size_t const length =
                heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(item + 1).template first<sizeof(std::uint32_t)>());
            auto const size = validity::checked_mul(length, element);
            if (!size || *size > end - item - item_head) [[unlikely]]
                return std::unexpected(error::too_little_data);
            return reference{item + item_head, length};
        }
    }

    CBOR_ALWAYS_INLINE static std::size_t directory_entry(std::string_view const encoded, cbor::directory const dir, std::size_t const j)
    {
        return heads::unsigned_read<std::uint32_t>(
            std::span<char const>(encoded).subspan(dir.at + sizeof(std::uint32_t) * j).template first<sizeof(std::uint32_t)>());
    }

    template <class Root, major_type Major, class E = void>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> reference_read(std::string_view const encoded,
                                                           std::span<char const, dynamic_type_sizes> const field,
                                                           cbor::directory const dir, std::size_t const element)
    {
        auto const j = shared_index_read<Root>(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        return item_read<Root, Major, E>(encoded, dir, *j, element);
    }

    template <class Root, major_type Major, class E = void>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> item_read(std::string_view const encoded, cbor::directory const dir,
                                                                        std::size_t const j, std::size_t const element)
    {
        if (!validity::check_index(j, dir.count)) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        constexpr std::size_t fillers = shared_first_of<Root>() - 1 - packing_table_of<Root>().size();
        std::size_t const items_at = dir.at + sizeof(std::uint32_t) * dir.count + fillers;
        std::size_t const items_end = encoded.size() - fixed_size<Root, Root>();
        std::size_t const item = directory_entry(encoded, dir, j);
        std::size_t const end = j + 1 < dir.count ? directory_entry(encoded, dir, j + 1) : items_end;
        if (item < items_at || end > items_end) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return item_reference_read<Major, E>(encoded, item, end, element);
    }

    template <class T>
    static std::expected<cbor::directory, error> directory_read(std::string_view const encoded)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        static_assert(prefix.size() >= 8, "The packing prefix must hold the eight bytes that are compared and read.");
        constexpr auto least_size = validity::checked_add(directory_at<T>(), fillers).and_then([](std::size_t const head) {
            return validity::checked_add(head, fixed_size<T, T>());
        });
        static_assert(least_size.has_value(), "The least size of an encoded item must fit in std::size_t.");
        constexpr std::size_t least = *least_size;
        if (encoded.size() < least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (!std::ranges::equal(std::span(encoded).template first<4>(),
                                std::span(prefix).template first<4>()) ||
            !std::ranges::equal(std::span(encoded).template subspan<8, prefix.size() - 8>(),
                                std::span(prefix).template subspan<8>()) ||
            encoded[prefix.size()] !=
                heads::initial_byte(major_type::byte_string,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument)))
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length = heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(prefix.size() + 1).template first<4>());
        std::size_t const count = length / 4;
        if (length % 4 != 0 ||
            heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(4).template first<4>()) != shared_first_of<T>() + count) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (length > encoded.size() - least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        static constexpr auto undefined = [] {
            std::array<char, fillers> a{};
            a.fill(static_cast<char>(heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined))));
            return a;
        }();
        if (!std::ranges::equal(std::span(encoded).subspan(directory_at<T>() + length).template first<fillers>(), undefined)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return cbor::directory{directory_at<T>(), count};
    }

    template <fixed_string Path>
    static consteval std::size_t index_slots()
    {
        return static_cast<std::size_t>(std::ranges::count_if(Path.view() | std::views::pairwise, [](auto const pair) {
            return std::get<0>(pair) == '[' && std::get<1>(pair) == ']';
        }));
    }

    template <fixed_string Path, std::size_t At>
    static consteval std::size_t index_slot()
    {
        return index_slots<Path>() - static_cast<std::size_t>(std::ranges::count_if(
                                          Path.view() | std::views::drop(At) | std::views::pairwise, [](auto const pair) {
                                              return std::get<0>(pair) == '[' && std::get<1>(pair) == ']';
                                          }));
    }

    template <class T, fixed_string Path, std::size_t At>
    static consteval bool path_valid()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_optional<U>)
                return path_valid<typename U::value_type, Path, At>();
            else
                return true;
        } else if constexpr (path[At] == '.') {
            if constexpr (!(std::is_class_v<U> && std::is_aggregate_v<U>) || requires { fixed_length<U>::value; } || is_optional<U>) {
                return false;
            } else {
                constexpr std::size_t end = step_end(path, At);
                constexpr std::meta::info m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
                if constexpr (m == std::meta::info{})
                    return false;
                else
                    return path_valid<typename[:std::meta::type_of(m):], Path, end>();
            }
        } else if constexpr (path[At] == '[' && index_end(path, At) < path.size()) {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (is_fixed_string<U> || is_text_range<U> || is_byte_range<U> || is_map<U> || is_optional<U>) {
                return false;
            } else if constexpr (requires { fixed_length<U>::value; }) {
                if constexpr (close != At + 1 && index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1))) >= fixed_length<U>::value)
                    return false;
                else
                    return path_valid<typename fixed_length<U>::element, Path, close + 1>();
            } else if constexpr (std::ranges::sized_range<U>) {
                return path_valid<std::ranges::range_value_t<U>, Path, close + 1>();
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    template <class Root, class T, fixed_string Path, std::size_t At>
    static consteval auto path_result()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_optional<U>)
                return std::type_identity<
                    std::optional<typename decltype(path_result<Root, typename U::value_type, Path, At>())::type>>{};
            else if constexpr (is_fixed_string<U> || is_text_range<U> || is_byte_range<U>)
                return std::type_identity<std::string_view>{};
            else if constexpr ((std::is_class_v<U> || std::is_array_v<U>) && !std::same_as<U, int128> && !std::same_as<U, uint128>)
                return std::type_identity<typename cbor::schema<Root>::template accessor<U>>{};
            else
                return std::type_identity<U>{};
        } else if constexpr (path[At] == '.') {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            return path_result<Root, typename[:std::meta::type_of(m):], Path, end>();
        } else {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (requires { fixed_length<U>::value; })
                return path_result<Root, typename fixed_length<U>::element, Path, close + 1>();
            else
                return path_result<Root, std::ranges::range_value_t<U>, Path, close + 1>();
        }
    }

    template <class Root, class T, fixed_string Path, std::size_t At>
    CBOR_ALWAYS_INLINE static auto path_walk(std::string_view const encoded, std::span<char const, fixed_size<T, Root>()> const field,
                                             cbor::directory const floor,
                                             std::array<std::size_t, index_slots<Path>()> const &indexes)
        -> std::expected<typename decltype(path_result<Root, T, Path, At>())::type, error>
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_fixed_string<U>) {
                constexpr std::size_t n = fixed_length<U>::value;
                constexpr std::size_t head = fixed_size<U, Root>() - n;
                static_assert(zero_initialized<Root, U>().size() >= head, "The zero-initialized encoding must hold the whole head.");
                std::span<char const, head> const expected{zero_initialized<Root, U>().data(), head};
                if (!std::ranges::equal(field.template first<head>(), expected)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return std::string_view(field.template last<n>());
            } else if constexpr (is_inline_optional<U>) {
                using E = typename U::value_type;
                using X = typename decltype(path_result<Root, E, Path, At>())::type;
                if (field.template subspan<1, 1>().front() !=
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value)))
                    return std::optional<X>{};
                auto const x = path_walk<Root, E, Path, At>(encoded, field.template subspan<inline_optional_head, fixed_size<E, Root>()>(),
                                                            floor, indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r = reference_read<Root, major_type::array>(encoded, field, floor, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->length > 1) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                using X = typename decltype(path_result<Root, E, Path, At>())::type;
                if (r->length == 0)
                    return std::optional<X>{};
                auto const x = path_walk<Root, E, Path, At>(
                    encoded, std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>(), floor, indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = reference_read<Root, is_text_range<U> ? major_type::text_string : major_type::byte_string>(encoded, field, floor, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return std::string_view(std::span(encoded).subspan(r->data, r->length));
            } else if constexpr (is_map<U>) {
                auto const r = reference_read<Root, major_type::map>(
                    encoded, field, floor, fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U>(encoded, field, floor, *r);
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = reference_read<Root, major_type::array, E>(encoded, field, floor, sizeof(E));
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U>(encoded, field, floor, *r);
            } else if constexpr (is_list<U>) {
                auto const r = reference_read<Root, major_type::array>(encoded, field, floor, fixed_size<std::ranges::range_value_t<U>, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U>(encoded, field, floor, *r);
            } else if constexpr ((std::is_class_v<U> || std::is_array_v<U>) && !std::same_as<U, int128> && !std::same_as<U, uint128>) {
                return typename cbor::schema<Root>::template accessor<U>(encoded, field, floor);
            } else {
                if (!fixed_head_valid<U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return fixed_value_read<U>(field);
            }
        } else if constexpr (path[At] == '.') {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            using M = typename[:std::meta::type_of(m):];
            if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return path_walk<Root, M, Path, end>(encoded, field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(),
                                                 floor, indexes);
        } else {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                constexpr std::size_t head = heads::head_size(n);
                static_assert(zero_initialized<Root, U>().size() >= head, "The zero-initialized encoding must hold the whole head.");
                static_assert(validity::checked_mul(n, fixed_size<E, Root>())
                                      .and_then([](std::size_t const elements) {
                                          return validity::checked_add(head, elements);
                                      })
                                      .value_or(std::numeric_limits<std::size_t>::max()) <=
                                  fixed_size<U, Root>(),
                              "The elements must lie inside the field.");
                std::span<char const, head> const expected{zero_initialized<Root, U>().data(), head};
                if (!std::ranges::equal(field.template first<head>(), expected)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if constexpr (close == At + 1) {
                    std::size_t const i = std::get<index_slot<Path, At>()>(indexes);
                    if (error const c = validity::check_index(i, n).error_or(error{}); c != error{})
                        [[unlikely]]
                        return std::unexpected(c);
                    return path_walk<Root, E, Path, close + 1>(
                        encoded, field.subspan(head + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(), floor, indexes);
                } else {
                    constexpr std::size_t i = index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1)));
                    return path_walk<Root, E, Path, close + 1>(
                        encoded, field.template subspan<head + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(), floor, indexes);
                }
            } else {
                using E = std::ranges::range_value_t<U>;
                std::size_t i;
                if constexpr (close == At + 1)
                    i = std::get<index_slot<Path, At>()>(indexes);
                else
                    i = index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1)));
                if constexpr (is_typed_array<U>) {
                    using F = std::remove_cv_t<E>;
                    auto const r = reference_read<Root, major_type::array, F>(encoded, field, floor, sizeof(F));
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    if (error const c = validity::check_index(i, r->length).error_or(error{}); c != error{})
                        [[unlikely]]
                        return std::unexpected(c);
                    return typed_array_element_read<F>(std::span<char const>(encoded).subspan(r->data + i * sizeof(F)).template first<sizeof(F)>());
                }
                auto const r = reference_read<Root, major_type::array>(encoded, field, floor, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (error const c = validity::check_index(i, r->length).error_or(error{}); c != error{})
                    [[unlikely]]
                    return std::unexpected(c);
                return path_walk<Root, E, Path, close + 1>(
                    encoded, std::span<char const>(encoded).subspan(r->data + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(),
                    floor, indexes);
            }
        }
    }

    struct decode_cursor {
        std::string_view encoded;
        cbor::directory dir;
        std::size_t index;
        std::size_t at;
        std::size_t end;

        template <class Root, major_type Major, class E = void>
        CBOR_ALWAYS_INLINE std::expected<reference, error> reference_take(std::span<char const, dynamic_type_sizes> const field,
                                                                          std::size_t const element)
        {
            auto const j = shared_index_read<Root>(field);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (*j != index || !validity::check_index(*j, dir.count)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if (directory_entry(encoded, dir, *j) != at) [[unlikely]]
                return std::unexpected(error::syntax_error);
            auto const r = item_reference_read<Major, E>(encoded, at, end, element);
            if (!r) [[unlikely]]
                return r;
            index += 1;
            at = r->data + r->length * element;
            return r;
        }

        template <class Root, class T>
        std::expected<void, error> value_read(T &out, std::span<char const, fixed_size<T, Root>()> const field, std::size_t const depth,
                                              std::size_t const depth_max)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; }) {
                if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                std::expected<void, error> done;
                template for (constexpr std::meta::info m : data_members<U>()) {
                    using M = typename[:std::meta::type_of(m):];
                    if (done)
                        done = value_read<Root>(out.[:m:],
                                                    field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(),
                                                    depth, depth_max);
                }
                return done;
            } else if constexpr (is_fixed_string<U>) {
                auto const text = field.template last<fixed_length<U>::value>();
                std::ranges::transform(text, std::ranges::begin(out), [](char const c) {
                    return static_cast<typename fixed_length<U>::element>(c);
                });
                return {};
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                std::expected<void, error> done;
                template for (constexpr std::size_t i : std::define_static_array(std::views::iota(0uz, n))) {
                    if (done)
                        done = value_read<Root>(
                            std::span(out).template subspan<i, 1>().front(),
                            field.template subspan<heads::head_size(n) + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(), depth, depth_max);
                }
                return done;
            } else if constexpr (is_inline_optional<U>) {
                if (field.template subspan<1, 1>().front() !=
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value))) {
                    out.reset();
                    return {};
                }
                return value_read<Root>(
                    out.emplace(),
                    field.template subspan<inline_optional_head, fixed_size<typename U::value_type, Root>()>(), depth, depth_max);
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r = reference_take<Root, major_type::array>(field, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->length > 1) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                if (r->length == 0) {
                    out.reset();
                    return {};
                }
                if (auto const nesting = validity::check_nesting_depth(depth + 1, depth_max); !nesting) [[unlikely]]
                    return std::unexpected(nesting.error());
                E element{};
                if (auto const e = value_read<Root>(
                        element, std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>(),
                        depth + 1, depth_max);
                    !e) [[unlikely]]
                    return e;
                out = std::move(element);
                return {};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = reference_take<Root, is_text_range<U> ? major_type::text_string : major_type::byte_string>(field, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                std::string_view const part{std::span(encoded).subspan(r->data, r->length)};
                if constexpr (std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>) {
                    auto const raw = std::as_bytes(std::span(part));
                    out = U(raw.begin(), raw.end());
                } else {
                    out = U(part.begin(), part.end());
                }
                return {};
            } else if constexpr (is_map<U>) {
                using K = typename U::key_type;
                using V = typename U::mapped_type;
                constexpr std::size_t pair = fixed_size<K, Root>() + fixed_size<V, Root>();
                auto const r = reference_take<Root, major_type::map>(field, pair);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                out.clear();
                for (std::size_t i = 0; i < r->length; ++i) {
                    auto const entry = std::span<char const>(encoded).subspan(r->data + i * pair).template first<pair>();
                    K key{};
                    V value{};
                    if (auto const e = value_read<Root>(key, entry.template first<fixed_size<K, Root>()>(), depth + 1, depth_max);
                        !e) [[unlikely]]
                        return e;
                    if (auto const e = value_read<Root>(value, entry.template last<fixed_size<V, Root>()>(), depth + 1, depth_max);
                        !e) [[unlikely]]
                        return e;
                    out.try_emplace(std::move(key), std::move(value));
                }
                return {};
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = reference_take<Root, major_type::array, E>(field, sizeof(E));
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                auto const from = std::span<char const>(encoded).subspan(r->data, r->length * sizeof(E));
                if constexpr (std::endian::native == std::endian::little && std::ranges::contiguous_range<U> &&
                              requires { out.resize(std::size_t{}); }) {
                    out.resize(r->length);
                    std::ranges::copy(std::as_bytes(from), std::as_writable_bytes(std::span(out)).begin());
                } else {
                    out.clear();
                    out.reserve(r->length);
                    for (std::size_t i = 0; i < r->length; ++i)
                        out.push_back(typed_array_element_read<E>(from.subspan(i * sizeof(E)).template first<sizeof(E)>()));
                }
                return {};
            } else if constexpr (std::ranges::sized_range<U>) {
                using E = std::ranges::range_value_t<U>;
                auto const r = reference_take<Root, major_type::array>(field, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                out.clear();
                out.reserve(r->length);
                for (std::size_t i = 0; i < r->length; ++i) {
                    E element{};
                    auto const entry =
                        std::span<char const>(encoded).subspan(r->data + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>();
                    if (auto const e = value_read<Root>(element, entry, depth + 1, depth_max); !e) [[unlikely]]
                        return e;
                    out.push_back(std::move(element));
                }
                return {};
            } else {
                if (!fixed_head_valid<U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                out = fixed_value_read<U>(field);
                return {};
            }
        }
    };

    template <class T>
    static std::expected<void, error> root_read(T &out, std::string_view const encoded, std::size_t const depth_max)
    {
        auto const dir = directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        std::size_t const root = encoded.size() - fixed_size<T, T>();
        auto const field = std::span<char const>(encoded).subspan(root).template first<fixed_size<T, T>()>();
        if (!class_tag_valid<T, T>(field)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        decode_cursor c{encoded, *dir, 0, dir->at + 4 * dir->count + fillers, root};
        if (auto const r = c.value_read<T>(out, field, 0, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (c.index != dir->count || c.at != root) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    template <class>
    friend class cbor::schema;

    template <class T>
    static std::expected<std::size_t, std::errc> encoded_size(second_item<T> const &second)
    {
        constexpr auto fixed_part =
            validity::checked_add(directory_at<T>(), shared_first_of<T>() - 1 - packing_table_of<T>().size())
                .and_then([](std::size_t const head) { return validity::checked_add(head, fixed_size<T, T>()); });
        static_assert(fixed_part.has_value(), "The fixed part of an encoded item must fit in std::size_t.");
        constexpr std::size_t fixed = *fixed_part;
        if (second.overflow) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        auto const directory = validity::checked_mul(second.items, sizeof(std::uint32_t));
        if (!directory) [[unlikely]]
            return directory;
        auto const second_size = validity::checked_add(*directory, second.bytes);
        if (!second_size) [[unlikely]]
            return second_size;
        auto const size = validity::checked_add(fixed, *second_size);
        if (!size || !std::in_range<std::uint32_t>(*size)) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return size;
    }

    template <bool Exact, class T>
    CBOR_ALWAYS_INLINE static std::size_t encoded_write(std::span<char> const encoded, T const &value, second_item<T> const &second)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        std::size_t const size = encoded.size() - (Exact ? 0 : heads::head_padding);
        std::ranges::copy(prefix, encoded.template first<prefix.size()>().begin());
        heads::u32_write(encoded, 4, shared_first_of<T>() + second.items);
        heads::item_head_write(encoded, prefix.size(), major_type::byte_string, sizeof(std::uint32_t) * second.items);
        std::size_t const data = directory_at<T>() + sizeof(std::uint32_t) * second.items;
        std::ranges::fill(encoded.subspan(data, fillers),
                          heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined)));
        auto const root = encoded.subspan(size - fixed_size<T, T>()).template first<fixed_size<T, T>()>();
        zero_initialized_copy<T, T>(root);
        value_encode<T, T, Exact>(encoded, root, value, encode_cursor{data + fillers, 0});
        return size;
    }

    template <class U>
#ifdef __SIZEOF_INT128__
    static constexpr bool is_wide_integer = std::same_as<U, int128> || std::same_as<U, uint128>;
#else
    static constexpr bool is_wide_integer = false;
#endif

    template <class U>
    static constexpr bool has_integer_keys = requires { U::keys; };

    template <class U>
    static consteval auto members_of()
    {
        auto const members = data_members<U>();
        if (!has_integer_keys<U> && !keys_unique(members))
            std::unreachable();
        return members;
    }

    friend class generic;

    template <class>
    friend class cbor::typed_array_view;
};

template <class E>
class typed_array_view
{
    static constexpr std::ptrdiff_t stride = sizeof(E);

    std::shared_ptr<void const> owner;
    std::span<char const> bytes;

    typed_array_view(std::shared_ptr<void const> o, std::span<char const> const b) : owner(std::move(o)), bytes(b)
    {
    }

    template <class>
    friend class schema;

public:
    class iterator
    {
        std::span<char const>::iterator at;

        explicit iterator(std::span<char const>::iterator const i) : at(i)
        {
        }

        friend class typed_array_view;

    public:
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = E;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        CBOR_ALWAYS_INLINE E operator*() const
        {
            return packed::typed_array_element_read<E>(std::span<char const, sizeof(E)>(at, sizeof(E)));
        }

        CBOR_ALWAYS_INLINE E operator[](difference_type const n) const
        {
            return *(*this + n);
        }

        CBOR_ALWAYS_INLINE iterator &operator++()
        {
            at += stride;
            return *this;
        }

        CBOR_ALWAYS_INLINE iterator operator++(int)
        {
            iterator const was = *this;
            at += stride;
            return was;
        }

        CBOR_ALWAYS_INLINE iterator &operator--()
        {
            at -= stride;
            return *this;
        }

        CBOR_ALWAYS_INLINE iterator operator--(int)
        {
            iterator const was = *this;
            at -= stride;
            return was;
        }

        CBOR_ALWAYS_INLINE iterator &operator+=(difference_type const n)
        {
            at += n * stride;
            return *this;
        }

        CBOR_ALWAYS_INLINE iterator &operator-=(difference_type const n)
        {
            at -= n * stride;
            return *this;
        }

        CBOR_ALWAYS_INLINE friend iterator operator+(iterator const i, difference_type const n)
        {
            return iterator(i.at + n * stride);
        }

        CBOR_ALWAYS_INLINE friend iterator operator+(difference_type const n, iterator const i)
        {
            return iterator(i.at + n * stride);
        }

        CBOR_ALWAYS_INLINE friend iterator operator-(iterator const i, difference_type const n)
        {
            return iterator(i.at - n * stride);
        }

        CBOR_ALWAYS_INLINE friend difference_type operator-(iterator const a, iterator const b)
        {
            return (a.at - b.at) / stride;
        }

        CBOR_ALWAYS_INLINE friend bool operator==(iterator const a, iterator const b)
        {
            return a.at == b.at;
        }

        CBOR_ALWAYS_INLINE friend auto operator<=>(iterator const a, iterator const b)
        {
            return a.at <=> b.at;
        }
    };

    CBOR_ALWAYS_INLINE std::size_t size() const
    {
        return bytes.size() / sizeof(E);
    }

    CBOR_ALWAYS_INLINE bool empty() const
    {
        return bytes.empty();
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator begin(this Self &&self)
    {
        return iterator(self.bytes.begin());
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator end(this Self &&self)
    {
        return iterator(self.bytes.end());
    }

    CBOR_ALWAYS_INLINE std::expected<E, error> front() const
    {
        if (error const c = validity::check_index(std::size_t{0}, size()).error_or(error{}); c != error{})
            [[unlikely]]
            return std::unexpected(c);
        return packed::typed_array_element_read<E>(bytes.template first<sizeof(E)>());
    }

    CBOR_ALWAYS_INLINE std::expected<E, error> back() const
    {
        if (error const c = validity::check_index(std::size_t{0}, size()).error_or(error{}); c != error{})
            [[unlikely]]
            return std::unexpected(c);
        return packed::typed_array_element_read<E>(bytes.template last<sizeof(E)>());
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> first(std::size_t const n) const &
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(owner, bytes.first(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> first(std::size_t const n) &&
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(std::move(owner), bytes.first(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> last(std::size_t const n) const &
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(owner, bytes.last(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> last(std::size_t const n) &&
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(std::move(owner), bytes.last(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<E, error> operator[](std::size_t const i) const
    {
        if (error const c = validity::check_index(i, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return packed::typed_array_element_read<E>(bytes.subspan(i * sizeof(E)).template first<sizeof(E)>());
    }
};

template <class Root>
consteval bool tags_registered()
{
    std::vector<std::uint64_t> numbers;
    for (std::meta::info const type : packed::packing_table_of<Root>()) {
        auto const number = packed::tag_number_of(type);
        if (!number)
            return false;
        numbers.push_back(*number);
    }
    std::ranges::sort(numbers);
    return validity::keys_unique(numbers);
}

template <class T, class Root>
consteval std::size_t packed::fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = heads::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (packed::has_fixed_underlying_type<U>)
        return fixed_size<std::underlying_type_t<U>, Root>();
#ifdef __SIZEOF_INT128__
    else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>)
        return heads::head_size(std::to_underlying(rfc8949::tag_number::negative_bignum)) +
               heads::head_size(sizeof(U)) + sizeof(U);
#endif
    else if constexpr (std::is_integral_v<U> && std::has_single_bit(sizeof(U)) && sizeof(U) <= sizeof(std::uint64_t))
        return initial_byte_size + sizeof(U);
    else if constexpr (std::is_floating_point_v<U>) {
        constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return initial_byte_size + sizeof(std::float16_t);
        else
#endif
        if constexpr (
#if defined(__STDCPP_BFLOAT16_T__)
            digits == std::numeric_limits<std::bfloat16_t>::digits ||
#endif
            digits == std::numeric_limits<std::float32_t>::digits)
            return initial_byte_size + sizeof(std::float32_t);
        else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
            return initial_byte_size + sizeof(std::float64_t);
        else if constexpr (digits == heads::extended_precision_digits ||
                           digits == std::numeric_limits<std::float128_t>::digits)
            return heads::head_size(std::to_underlying(rfc8746::tag_number::float128_big_endian)) +
                   heads::head_size(sizeof(std::float128_t)) + sizeof(std::float128_t);
        else
            return no_fixed_size<T>();
    } else if constexpr (requires { packed::fixed_length<U>::value; }) {
        using E = typename packed::fixed_length<U>::element;
        constexpr std::size_t n = packed::fixed_length<U>::value;
        if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                      std::same_as<E, std::byte>) {
            constexpr auto size = validity::checked_add(heads::head_size(n), n);
            static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
            return *size;
        } else {
            constexpr std::size_t element = fixed_size<E, Root>();
            constexpr auto size = validity::checked_mul(n, element).and_then([](std::size_t const elements) {
                return validity::checked_add(heads::head_size(n), elements);
            });
            static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
            return *size;
        }
    } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
        if constexpr (!std::meta::bases_of(^^U, std::meta::access_context::unchecked()).empty() ||
                      !packed::keys_unique(packed::data_members<U>())) {
            return no_fixed_size<U>();
        } else {
            std::size_t size = heads::head_size(*packed::tag_number_of(^^U)) +
                               packed::straight_reference_size<Root, U>() + packed::item_head;
            template for (constexpr auto m : packed::data_members<U>()) {
                if constexpr (!std::meta::has_identifier(m) || std::meta::is_bit_field(m) || !std::meta::is_public(m))
                    return no_fixed_size<U>();
                constexpr std::size_t member = fixed_size<typename[:std::meta::type_of(m):], Root>();
                auto const sum = validity::checked_add(size, member);
                if (!sum)
                    return no_fixed_size<U>();
                size = *sum;
            }
            return size;
        }
    } else if constexpr (packed::is_inline_optional<U>) {
        constexpr std::size_t value = fixed_size<typename U::value_type, Root>();
        constexpr auto size = validity::checked_add(packed::inline_optional_head, value);
        static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
        return *size;
    }
    else if constexpr (requires(U const &v) {
                           v.has_value();
                           *v;
                           typename U::value_type;
                       } && !requires { typename U::error_type; })
        return packed::dynamic_type_sizes;
    else if constexpr (std::ranges::sized_range<U>)
        return packed::dynamic_type_sizes;
    else
        return no_fixed_size<T>();
}

template <class T, std::meta::info Member, class Root>
consteval std::size_t packed::member_offset()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::meta::parent_of(Member) != std::meta::dealias(^^U)) {
        return no_fixed_size<T>();
    } else {
        std::size_t offset = heads::head_size(*packed::tag_number_of(^^U)) +
                             packed::straight_reference_size<Root, U>() + packed::item_head;
        template for (constexpr auto m : packed::data_members<U>()) {
            if constexpr (m == Member)
                return offset;
            offset += fixed_size<typename[:std::meta::type_of(m):], Root>();
        }
        return no_fixed_size<T>();
    }
}

template <class T>
class schema
{
public:
    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t fixed_size()
    {
        return packed::fixed_size<U, T>();
    }

    template <std::meta::info Member, class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t member_offset()
    {
        return packed::member_offset<U, Member, T>();
    }

    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    class accessor
    {
        static constexpr bool listed = packed::is_list<U> || packed::is_map<U>;

        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::span<char const, packed::fixed_size<U, T>()> field;
        cbor::directory dir;
        [[no_unique_address]] std::conditional_t<listed, packed::reference, std::monostate> items;

        accessor(std::string_view const b, std::span<char const, packed::fixed_size<U, T>()> const f, cbor::directory const d)
            requires(!listed)
            : encoded(b), field(f), dir(d)
        {
        }

        accessor(std::string_view const b, std::span<char const, packed::fixed_size<U, T>()> const f, cbor::directory const d,
                 packed::reference const r)
            requires(listed)
            : encoded(b), field(f), dir(d), items(r)
        {
        }

        accessor(std::shared_ptr<void const> o, std::string_view const b, std::span<char const, packed::fixed_size<U, T>()> const f,
                 cbor::directory const d)
            requires(!listed)
            : owner(std::move(o)), encoded(b), field(f), dir(d)
        {
        }

        friend class schema;

        friend class packed;

    public:
        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(((Path.view().starts_with('@') && packed::path_valid<U, Path, 1>()) ||
                      (Path.view().starts_with('$') && packed::path_valid<T, Path, 1>())) &&
                     sizeof...(Index) == packed::index_slots<Path>() && std::is_lvalue_reference_v<Self>)
        CBOR_ALWAYS_INLINE auto at(this Self &&self, Index const... indexes)
        {
            std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
            constexpr std::string_view path = Path.view();
            if constexpr (path.starts_with('$') && std::same_as<U, T>) {
                return packed::path_walk<T, T, Path, 1>(self.encoded, self.field, self.dir, i);
            } else if constexpr (path.starts_with('$')) {
                constexpr std::size_t root = packed::fixed_size<T, T>();
                return packed::path_walk<T, T, Path, 1>(
                    self.encoded, std::span<char const>(self.encoded).subspan(self.encoded.size() - root).template first<root>(), self.dir, i);
            } else if constexpr (packed::is_typed_array<U> && path.size() > 1) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                constexpr std::size_t close = packed::index_end(path, 1);
                std::size_t at;
                if constexpr (close == 2)
                    at = std::get<0>(i);
                else
                    at = packed::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                if (error const c = validity::check_index(at, self.items.length).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::expected<E, error>(std::unexpect, c);
                return std::expected<E, error>(packed::typed_array_element_read<E>(
                    std::span<char const>(self.encoded).subspan(self.items.data + at * sizeof(E)).template first<sizeof(E)>()));
            } else if constexpr (packed::is_list<U> && path.size() > 1) {
                using E = std::ranges::range_value_t<U>;
                constexpr std::size_t close = packed::index_end(path, 1);
                using X = typename decltype(packed::path_result<T, E, Path, close + 1>())::type;
                std::size_t at;
                if constexpr (close == 2)
                    at = std::get<0>(i);
                else
                    at = packed::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                if (error const c = validity::check_index(at, self.items.length).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::expected<X, error>(std::unexpect, c);
                return packed::path_walk<T, E, Path, close + 1>(
                    self.encoded, std::span<char const>(self.encoded).subspan(self.items.data + at * packed::fixed_size<E, T>()).template first<packed::fixed_size<E, T>()>(),
                    self.dir, i);
            } else {
                return packed::path_walk<T, U, Path, 1>(self.encoded, self.field, self.dir, i);
            }
        }

        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(std::same_as<U, T> && Path.view().starts_with('$') && packed::path_valid<T, Path, 1>() &&
                     sizeof...(Index) == packed::index_slots<Path>())
        auto view(this Self &&self, Index const... indexes)
        {
            using X = typename decltype(packed::path_result<T, T, Path, 1>())::type;
            using E = typename decltype([]<class V>(std::type_identity<accessor<V>>) {
                static_assert(packed::is_typed_array<V>, "cbor::schema::accessor::view: the path does not end at a typed array");
                return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<V>>>{};
            }(std::type_identity<X>{}))::type;
            auto const list = self.template at<Path>(indexes...);
            if (!list) [[unlikely]]
                return std::expected<typed_array_view<E>, error>(std::unexpect, list.error());
            return std::expected<typed_array_view<E>, error>(
                typed_array_view<E>(std::forward_like<Self>(self.owner), std::span<char const>(self.encoded).subspan(list->items.data, list->items.length * sizeof(E))));
        }

        std::size_t size() const
            requires(listed)
        {
            return items.length;
        }
    };

    static std::expected<accessor<>, error> path(std::shared_ptr<void const> owner, std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::schema::path: the owner of the encoded data item is empty");
        auto const dir = packed::directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        auto const root = std::span<char const>(encoded).subspan(encoded.size() - fixed_size()).template first<fixed_size()>();
        if (!packed::class_tag_valid<T, T>(root)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return accessor<>(std::move(owner), encoded, root, *dir);
    }

    template <class Encoded>
        requires std::same_as<std::remove_const_t<Encoded>, std::string>
    static std::expected<accessor<>, error> path(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

    template <fixed_string Path, std::convertible_to<std::size_t>... Index,
              class X = typename decltype(packed::path_result<T, T, Path, 1>())::type>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>() && Path.view().starts_with('$') &&
                 packed::path_valid<T, Path, 1>() && sizeof...(Index) == packed::index_slots<Path>() &&
                 std::is_trivially_copyable_v<X> && !std::same_as<X, std::string_view> &&
                 !std::same_as<X, std::optional<std::string_view>>)
    static std::expected<X, error> at(std::string_view const encoded, Index const... indexes)
    {
        auto const dir = packed::directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
        return packed::path_walk<T, T, Path, 1>(
            encoded, std::span<char const>(encoded).subspan(encoded.size() - fixed_size()).template first<fixed_size()>(), *dir, i);
    }

    static std::expected<accessor<>, error> path(std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        auto copy = std::make_shared<std::string const>(encoded);
        std::string_view const view = *copy;
        return path(std::move(copy), view);
    }

    template <std::same_as<std::string> Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static std::expected<accessor<>, error> path(Encoded &&encoded)
    {
        auto owner = std::make_shared<std::string const>(std::move(encoded));
        std::string_view const view = *owner;
        return path(std::move(owner), view);
    }

    static std::expected<owning_ref<T>, error> decode(std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        auto copy = std::make_shared<std::string const>(encoded);
        T value{};
        if (auto const r = packed::root_read<T>(value, *copy, validity::nesting_depth_max_read()); !r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(copy), std::move(value));
    }

    template <std::same_as<std::string> Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static std::expected<owning_ref<T>, error> decode(Encoded &&encoded)
    {
        auto owner = std::make_shared<std::string const>(std::move(encoded));
        std::string_view const view = *owner;
        return decode(std::move(owner), view);
    }

    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<void const> owner, std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::schema::decode: the owner of the encoded data item is empty");
        T value{};
        if (auto const r = packed::root_read<T>(value, encoded, validity::nesting_depth_max_read()); !r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(owner), std::move(value));
    }

    template <class Encoded>
        requires std::same_as<std::remove_const_t<Encoded>, std::string>
    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

    CBOR_ALWAYS_INLINE static std::expected<std::string, std::errc> encode(T const &value)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        packed::second_item<T> second;
        second.add(value);
        auto const size = packed::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(size.error());
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(sum.error());
        std::size_t const padded = *sum;
        std::string out;
        out.resize_and_overwrite(padded, [&](char *const p, std::size_t const n) {
            return packed::encoded_write<false>(std::span<char>(p, n), value, second);
        });
        return out;
    }

    template <class Target>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, std::errc> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        packed::second_item<T> second;
        second.add(value);
        auto const size = packed::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(size.error());
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(sum.error());
        std::size_t const padded = *sum;
        CBOR_ASSUME(padded >= fixed_size());
        if constexpr (std::same_as<U, std::string>) {
            std::size_t const at = target.size();
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(total.error());
            target.resize_and_overwrite(*total, [&](char *const p, std::size_t const n) {
                return at + packed::encoded_write<false>(std::span<char>(p, n).subspan(at), value, second);
            });
            return *size;
        } else if constexpr (encoding::byte_container<U> && requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(total.error());
            target.reserve(*total);
            target.insert(target.end(), padded, char{});
            packed::encoded_write<false>(std::span<char>(target).subspan(at), value, second);
            target.resize(at + *size);
            return *size;
        } else if constexpr (!encoding::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (out.size() >= padded) {
                packed::encoded_write<false>(out.first(padded), value, second);
                return *size;
            }
        }
        decltype(auto) message = encoding::message_of(target, *size);
        if constexpr (requires { std::span<char>(message); }) {
            std::span<char> const out(message);
            if (out.size() < *size) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
            packed::encoded_write<true>(out.first(*size), value, second);
            if (auto const r = message.done(*size); !r) [[unlikely]]
                return std::unexpected(r.error());
            return *size;
        } else {
            std::string encoded(padded, '\0');
            encoded.resize(packed::encoded_write<false>(std::span<char>(encoded), value, second));
            if (auto const r = message.append(encoded); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return encoded.size();
        }
    }
};
#endif

}

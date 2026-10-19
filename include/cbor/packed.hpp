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
#include <new>
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

#include "cbor_sets.hpp"
#include "notable_tags.hpp"
#include "describe.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "head.hpp"
#include "ref.hpp"
#include "rfc8746.hpp"
#include "rfc8943.hpp"
#include "rfc9535.hpp"
#include "validity.hpp"

namespace cbor
{

struct skip {};

struct allow {};

struct allowlist {};

struct tag {
    std::uint64_t number;
    std::uint16_t generation = 0;
};

template <std::uint64_t Number, class T>
struct tagged_item {
    static_assert(!has_describe_members<T> && !(std::is_class_v<T> && std::is_aggregate_v<T> && !std::ranges::range<T>),
                  "the tag wrapper takes content that is not a struct; a struct carries its tag in cbor::describe_members.");
    static constexpr std::uint64_t number = Number;
    T content;
};

#ifdef __cpp_impl_reflection
template <class T>
consteval std::size_t no_fixed_size()
{
    std::unreachable();
}

template <class Root>
consteval bool tags_registered();

template <class T>
class packed;

template <class E>
class typed_array_view;

struct directory {
    std::size_t count;
    std::size_t items_at;
    std::size_t items_end;
    std::size_t root;
    std::size_t shared_start;
};

#endif

class reflection
{
    static constexpr std::size_t item_head = heads::initial_byte_size + sizeof(std::uint32_t);
    static constexpr std::size_t dynamic_type_sizes = heads::initial_byte_size + item_head;
    static constexpr std::size_t shared_first = 16;
    static constexpr std::size_t fillers_written = shared_first;
    static constexpr std::size_t shared_start_written = fillers_written + 1;
    static constexpr std::size_t root_and_end_entries = 2;

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
    static constexpr bool is_record =
        std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; };

    template <class U>
    static constexpr bool is_optional_record = is_optional<U> && requires { requires is_record<typename U::value_type>; };

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
        return std::to_underlying(rfc8746::tag_number::typed_array_first) | f << std::to_underlying(rfc8746::bit_field::f) |
               s << std::to_underlying(rfc8746::bit_field::s) | e << std::to_underlying(rfc8746::bit_field::e) |
               ll << std::to_underlying(rfc8746::bit_field::ll);
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
        requires(sizeof(E) == sizeof(std::uint16_t) && std::numeric_limits<E>::digits == heads::half_precision.significand_bits + 1) ||
                    (sizeof(E) == sizeof(float) && std::numeric_limits<E>::digits == std::numeric_limits<float>::digits) ||
                    (sizeof(E) == sizeof(double) &&
                     std::numeric_limits<E>::digits == std::numeric_limits<double>::digits);
    };

    template <class E>
    static constexpr bool is_typed_array_element = is_typed_array_integer<E> || is_typed_array_float<E>;

    template <class U>
    static constexpr bool is_typed_array = requires {
        requires is_list<U>;
        requires is_typed_array_element<std::remove_cv_t<std::ranges::range_value_t<U>>>;
    };

    template <class U>
#ifdef __SIZEOF_INT128__
    static constexpr bool is_wide_integer = std::same_as<U, int128> || std::same_as<U, uint128>;
#else
    static constexpr bool is_wide_integer = false;
#endif

    static consteval std::vector<unsigned char> key_encoded(major_type const major, std::uint64_t const argument,
                                                            std::string_view const text)
    {
        std::uint8_t const info = heads::preferred_argument_info(argument);
        std::size_t const bytes = heads::argument_size(info);
        std::vector<unsigned char> out{static_cast<unsigned char>(heads::initial_byte(major, info))};
        for (std::size_t k = bytes; k > 0; --k)
            out.push_back(static_cast<unsigned char>(argument >> (std::numeric_limits<std::uint8_t>::digits * (k - 1))));
        for (char const c : text)
            out.push_back(static_cast<unsigned char>(c));
        return out;
    }

    static consteval bool keys_unique(std::vector<std::vector<unsigned char>> encoded)
    {
        std::ranges::sort(encoded);
        return validity::keys_unique(encoded);
    }

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
    CBOR_ALWAYS_INLINE static void typed_array_element_write(std::span<char, sizeof(E)> const out,
                                                             E const value)
    {
        auto little = std::bit_cast<std::array<char, sizeof(E)>>(value);
        if constexpr (std::endian::native == std::endian::big)
            std::ranges::reverse(little);
        std::ranges::copy(little, out.begin());
    }

    static consteval bool tag_taken_by_standard_type(std::uint64_t const tag)
    {
        return tag == std::to_underlying(rfc8949::tag_number::standard_date_time_string) ||
               tag == std::to_underlying(rfc8949::tag_number::epoch_based_date_time) ||
               tag == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
               tag == std::to_underlying(rfc8949::tag_number::negative_bignum) ||
               tag == std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item) ||
               tag == std::to_underlying(rfc8943::tag_number::days_since_epoch_date) ||
               tag == std::to_underlying(rfc8943::tag_number::full_date_string) ||
               tag == std::to_underlying(cbor_sets::tag_number::set) ||
               (tag >= std::to_underlying(rfc8746::tag_number::typed_array_first) &&
                tag <= std::to_underlying(rfc8746::tag_number::typed_array_last));
    }

    static consteval bool tag_invalid(std::uint64_t const tag)
    {
        return tag == std::numeric_limits<std::uint16_t>::max() || tag == std::numeric_limits<std::uint32_t>::max() ||
               tag == std::numeric_limits<std::uint64_t>::max() ||
               tag == std::to_underlying(notable_tags::tag_number::programming_aid_for_simple_values);
    }

#ifdef __cpp_impl_reflection
    template <class Root>
    static consteval bool table_tag_taken_by_standard_type()
    {
        if constexpr (std::is_class_v<Root> && std::is_aggregate_v<Root>)
            return std::ranges::any_of(packing_table_of<Root>(), [](std::meta::info const type) {
                auto const number = tag_number_of(type);
                return number && tag_taken_by_standard_type(*number);
            });
        else
            return false;
    }

    template <class Root>
    static consteval bool table_tag_invalid()
    {
        if constexpr (std::is_class_v<Root> && std::is_aggregate_v<Root>)
            return std::ranges::any_of(packing_table_of<Root>(), [](std::meta::info const type) {
                auto const number = tag_number_of(type);
                return number && tag_invalid(*number);
            });
        else
            return false;
    }
#endif

    template <class U>
    static consteval bool tag_declared()
    {
        return requires {
            { describe_members<U>::tag } -> std::same_as<std::optional<cbor::tag> const &>;
        };
    }

    template <class M, class B>
    static std::type_identity<B> class_of(M B::*);

    template <class U, class P>
    static consteval bool member_of()
    {
        using C = typename decltype(class_of(P{}))::type;
        return std::is_base_of_v<C, U> && requires(U &u, P p) { u.*p; };
    }

    template <class U>
    static consteval bool members_of_class()
    {
        return std::apply([](auto const &...m) { return (member_of<U, decltype(m.pointer)>() && ...); },
                          describe_members<U>::members);
    }

    template <class U, class P>
    static consteval bool converts_to_member_of()
    {
        return requires(typename decltype(class_of(P{}))::type *b) { static_cast<U *>(b); };
    }

    template <class U, std::size_t J, std::size_t L>
    static consteval bool pointers_differ()
    {
        constexpr auto a = std::get<J>(describe_members<U>::members).pointer;
        constexpr auto b = std::get<L>(describe_members<U>::members).pointer;
        using PA = std::remove_cvref_t<decltype(a)>;
        using PB = std::remove_cvref_t<decltype(b)>;
        using MA = std::remove_reference_t<decltype(std::declval<U &>().*a)>;
        using MB = std::remove_reference_t<decltype(std::declval<U &>().*b)>;
        if constexpr (J == L || !std::is_same_v<MA, MB>)
            return true;
        else if constexpr (converts_to_member_of<U, PA>() && converts_to_member_of<U, PB>())
            return static_cast<MA U::*>(a) != static_cast<MB U::*>(b);
        else if constexpr (std::is_same_v<PA, PB>)
            return a != b;
        else
            return true;
    }

    template <class U, std::size_t J, std::size_t... L>
    static consteval bool pointer_differs_from_all(std::index_sequence<L...>)
    {
        return (pointers_differ<U, J, L>() && ...);
    }

    template <class U>
    static consteval bool pointers_unique()
    {
        constexpr std::size_t n = std::tuple_size_v<std::remove_cvref_t<decltype(describe_members<U>::members)>>;
        return []<std::size_t... J>(std::index_sequence<J...>) {
            return (pointer_differs_from_all<U, J>(std::make_index_sequence<n>{}) && ...);
        }(std::make_index_sequence<n>{});
    }

    static consteval auto key_encoding_of(map_key const key)
    {
        return std::tuple(heads::initial_byte(key.major, heads::preferred_argument_info(key.argument)),
                          key.argument, key.text);
    }

    static consteval bool key_encoding_less(map_key const a, map_key const b)
    {
        return key_encoding_of(a) < key_encoding_of(b);
    }

    template <class... P>
    static consteval bool keys_unique(std::tuple<member<P>...> const &members)
    {
        std::vector<map_key> keys;
        std::apply([&](auto const &...m) { (keys.push_back(m.key), ...); }, members);
        std::ranges::sort(keys, key_encoding_less);
        return validity::keys_unique(keys, key_encoding_of);
    }

    template <class T>
    static consteval bool annotations_absent()
    {
#ifdef __cpp_impl_reflection
        std::meta::info const type = std::meta::dealias(^^T);
        if (annotated(type, ^^cbor::tag) || annotated(type, ^^cbor::allowlist))
            return false;
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged()))
            if (annotated(m, ^^cbor::key) || annotated(m, ^^cbor::skip) || annotated(m, ^^cbor::allow))
                return false;
#endif
        return true;
    }

#ifdef __cpp_impl_reflection
    static consteval void public_members_add(std::meta::info const type, std::vector<std::meta::info> &out)
    {
        for (std::meta::info const b : std::meta::bases_of(type, std::meta::access_context::unprivileged()))
            public_members_add(std::meta::type_of(b), out);
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged()))
            out.push_back(m);
    }

    template <class U>
    static consteval std::span<std::meta::info const> public_members()
    {
        std::vector<std::meta::info> out;
        public_members_add(std::meta::dealias(^^U), out);
        return std::define_static_array(out);
    }

    template <class U, std::meta::info R>
    static consteval bool listed_once()
    {
        return std::apply(
            [](auto const &...m) {
                auto const hit = [](auto const &e) {
                    if constexpr (std::is_same_v<decltype(e.pointer), decltype(&[:R:])>)
                        return e.pointer == &[:R:];
                    else
                        return false;
                };
                return (int{hit(m)} + ... + 0) == 1;
            },
            describe_members<U>::members);
    }
#endif

    template <class U>
    static consteval bool reflected_members_listed()
    {
#ifdef __cpp_impl_reflection
        static constexpr auto r = public_members<U>();
        constexpr std::size_t n = std::tuple_size_v<std::remove_cvref_t<decltype(describe_members<U>::members)>>;
        if constexpr (r.size() != n)
            return false;
        else
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (listed_once<U, r[I]>() && ...);
            }(std::make_index_sequence<r.size()>{});
#else
        return true;
#endif
    }

#ifdef __cpp_impl_reflection
    static consteval std::string_view key_of(std::meta::info const r)
    {
        for (std::meta::info const a : std::meta::annotations_of(r))
            if (std::meta::remove_const(std::meta::type_of(a)) == ^^cbor::key)
                return std::meta::extract<cbor::key>(a).text;
        auto const name = std::meta::u8identifier_of(r);
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
        std::vector<std::vector<unsigned char>> encoded;
        for (std::meta::info const m : members) {
            std::string_view const key = key_of(m);
            encoded.push_back(key_encoded(major_type::text_string, key.size(), key));
        }
        return keys_unique(std::move(encoded));
    }

    static consteval bool annotated(std::meta::info const entity, std::meta::info const type)
    {
        return std::ranges::any_of(std::meta::annotations_of(entity), [type](std::meta::info const a) {
            return std::meta::remove_const(std::meta::type_of(a)) == type;
        });
    }

    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
        if constexpr (std::uses_allocator_v<E, Alloc> || !std::is_class_v<E> || !std::is_aggregate_v<E>) {
            return false;
        } else {
            if (!std::meta::bases_of(^^E, std::meta::access_context::unprivileged()).empty())
                return false;
            bool reached = false;
            template for (constexpr std::meta::info m :
                          std::define_static_array(std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::unprivileged()))) {
                using M = typename[:std::meta::type_of(m):];
                if constexpr (std::is_array_v<M> || std::is_reference_v<M>)
                    return false;
                else
                    reached = reached || std::uses_allocator_v<M, Alloc> || uses_allocator_aggregate<M, Alloc>();
            }
            return reached;
        }
    }

    template <class E, class Alloc>
    static constexpr E element_make(Alloc const &alloc, E &&from)
    {
        if constexpr (uses_allocator_aggregate<E, Alloc>()) {
            static constexpr auto members =
                std::define_static_array(std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::unprivileged()));
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return E{element_make<typename[:std::meta::type_of(members[I]):]>(alloc, std::move(from.[:members[I]:]))...};
            }(std::make_index_sequence<members.size()>());
        } else {
            return std::make_obj_using_allocator<E>(alloc, std::move(from));
        }
    }

    template <class E, class Container>
    static constexpr E element_make(Container const &container)
    {
        if constexpr (requires { container.get_allocator(); }) {
            if constexpr (uses_allocator_aggregate<E, decltype(container.get_allocator())>())
                return element_make<E>(container.get_allocator(), E{});
            else
                return std::make_obj_using_allocator<E>(container.get_allocator());
        } else {
            return E{};
        }
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

    template <class T, class Root>
    static consteval std::size_t record_size();

    template <class T, class Root>
    static consteval std::size_t extent_of()
    {
        if constexpr (is_record<std::remove_cv_t<T>>)
            return record_size<T, Root>();
        else
            return fixed_size<T, Root>();
    }

    template <class T, std::meta::info Member, class Root>
    static consteval std::size_t member_offset();

    template <class U>
    static constexpr bool is_inline_value =
        std::is_arithmetic_v<U> || std::is_enum_v<U> || is_wide_integer<U> || is_fixed_string<U>;

    template <class U>
    static consteval bool holds_references()
    {
        if constexpr (is_inline_value<U>)
            return false;
        else if constexpr (requires { fixed_length<U>::value; })
            return holds_references<typename fixed_length<U>::element>();
        else
            return true;
    }

    template <class U>
    static consteval std::size_t block_items()
    {
        if constexpr (is_inline_value<U> || is_record<U> || is_optional_record<U>)
            return 0;
        else if constexpr (requires { fixed_length<U>::value; })
            return fixed_length<U>::value * block_items<typename fixed_length<U>::element>();
        else
            return 1;
    }

    template <class U>
    static consteval std::size_t block_size()
    {
        std::size_t n = 0;
        template for (constexpr std::meta::info m : data_members<U>())
            n += block_items<std::remove_cv_t<typename[:std::meta::type_of(m):]>>();
        return n;
    }

    template <class U, std::meta::info Member>
    static consteval std::size_t block_index()
    {
        std::size_t n = 0;
        template for (constexpr std::meta::info m : data_members<U>()) {
            if constexpr (m == Member)
                return n;
            n += block_items<std::remove_cv_t<typename[:std::meta::type_of(m):]>>();
        }
        return n;
    }

    struct record_block {
        std::size_t base;
        std::size_t end;
    };

    template <class U>
    static constexpr record_block record_block_at(std::size_t const base)
    {
        return {base, base + block_size<U>()};
    }

    enum class record_part { whole, block, after_block, values, references };

    static constexpr std::size_t typed_array_head = heads::initial_byte_size + sizeof(std::uint8_t) + item_head;


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

    template <class Root, class U>
    static consteval std::span<char const> zero_initialized_record()
    {
        std::vector<char> encoded;
        heads::head_append(encoded, major_type::tag, *tag_number_of(^^U));
        straight_reference_encode<Root, U>(encoded);
        fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
        auto const count = heads::big_endian(static_cast<std::uint32_t>(data_members<U>().size()));
        std::ranges::copy(count, encoded.end() - sizeof(std::uint32_t));
        template for (constexpr auto m : data_members<U>())
            zero_initialized_encode<Root, typename[:std::meta::type_of(m):]>(encoded);
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
            constexpr std::string_view key = key_of(members[i]);
            heads::head_append(encoded, major_type::text_string, key.size());
            encoded.insert(encoded.end(), key.begin(), key.end());
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
        } else if constexpr (is_optional<U>) {
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

    static consteval std::span<char const> packing_prefix_of()
    {
        std::vector<char> encoded;
        heads::head_append(encoded, major_type::tag,
                           std::to_underlying(validity::tag_number::split_basic_packed_cbor));
        heads::head_append(encoded, major_type::array, 3);
        fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
        return std::define_static_array(encoded);
    }

    template <class Root>
    static consteval std::span<char const> arguments_of()
    {
        std::vector<char> encoded;
        heads::head_append(encoded, major_type::array, packing_table_of<Root>().size());
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

        template <class U>
        void record_add(U const &value)
        {
            template for (constexpr auto m : data_members<U>())
                add<typename[:std::meta::type_of(m):]>(value.[:m:]);
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
            } else if constexpr (is_record<U>) {
                items += 1;
                bytes_add(record_size<U, Root>());
                record_add(value);
            } else if constexpr (is_optional_record<U>) {
                items += 1;
                if (value.has_value()) {
                    bytes_add(record_size<typename U::value_type, Root>());
                    record_add(*value);
                } else {
                    bytes_add(heads::initial_byte_size);
                }
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

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static void
    zero_initialized_record_copy(std::span<char, record_size<U, Root>()> const record)
    {
        constexpr std::size_t n = record_size<U, Root>();
        static_assert(zero_initialized_record<Root, U>().size() >= n,
                      "The zero-initialized record must hold the whole record.");
        std::span<char const, n> const from{zero_initialized_record<Root, U>().data(), n};
        std::copy(from.begin(), from.end(), record.begin());
    }

    struct encode_cursor {
        std::size_t position;
        std::size_t index;
    };

    template <class Root>
    static consteval std::size_t directory_at()
    {
        return packing_prefix_of().size() + item_head;
    }

    template <class Root, class E, bool Exact, record_part Part, class R>
    CBOR_ALWAYS_INLINE static encode_cursor elements_encode(std::span<char> const out, std::size_t const data, R const &range,
                                       encode_cursor position)
    {
        constexpr std::size_t size = fixed_size<E, Root>();
        std::size_t at = data;
        for (auto const &e : range) {
            auto const field = out.subspan(at).template first<size>();
            if constexpr (Part == record_part::values)
                zero_initialized_copy<Root, E>(field);
            position = value_encode<Root, E, Exact, Part>(out, field, e, position);
            at += size;
        }
        return position;
    }

    template <class Root>
    CBOR_ALWAYS_INLINE static void reference_write(std::span<char> const out, std::span<char, dynamic_type_sizes> const field,
                                                   std::size_t const j, std::size_t const item)
    {
        std::size_t const m = j + shared_start_written - shared_first;
        heads::u32_write(out, directory_at<Root>() + sizeof(std::uint32_t) * (j + 1), item);
        field[1] =
            heads::initial_byte(static_cast<major_type>(m & 1),
                                std::to_underlying(rfc8949::additional_information::four_byte_argument));
        auto const n = heads::big_endian(static_cast<std::uint32_t>(m >> 1));
        std::ranges::copy(n, field.template last<sizeof(std::uint32_t)>().begin());
    }

    template <class Root>
    CBOR_ALWAYS_INLINE static std::size_t item_written(std::span<char const> const out,
                                                       std::span<char const, dynamic_type_sizes> const field)
    {
        std::size_t const m =
            2 * std::size_t{heads::unsigned_read<std::uint32_t>(field.template last<sizeof(std::uint32_t)>())} +
            (static_cast<unsigned char>(field[1]) >> rfc8949::additional_information_bits);
        std::size_t const j = m + shared_first - shared_start_written;
        return heads::unsigned_read<std::uint32_t>(
            out.subspan(directory_at<Root>() + sizeof(std::uint32_t) * (j + 1)).template first<sizeof(std::uint32_t)>());
    }

    template <class Root, bool Exact, class U>
    CBOR_ALWAYS_INLINE static encode_cursor item_encode(std::span<char> const out, std::span<char, dynamic_type_sizes> const field,
                                                        U const &value, encode_cursor c)
    {
        std::size_t const j = c.index++;
        std::size_t const item = c.position;
        reference_write<Root>(out, field, j, item);
        std::size_t const data = item + item_head;
        if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            heads::item_head_write(out, item, major_type::array, value.has_value() ? 1 : 0);
            c.position = data;
            if (value.has_value()) {
                c.position += fixed_size<E, Root>();
                auto const element = out.subspan(data).template first<fixed_size<E, Root>()>();
                zero_initialized_copy<Root, E>(element);
                c = value_encode<Root, E, Exact, record_part::values>(out, element, *value, c);
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
                c = value_encode<Root, K, Exact, record_part::values>(out, key, k, c);
                at += fixed_size<K, Root>();
                auto const mapped = out.subspan(at).template first<fixed_size<M, Root>()>();
                zero_initialized_copy<Root, M>(mapped);
                c = value_encode<Root, M, Exact, record_part::values>(out, mapped, v, c);
                at += fixed_size<M, Root>();
            }
        } else if constexpr (is_typed_array<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            std::size_t const length = std::ranges::size(value);
            out[item] = heads::initial_byte(
                major_type::tag, std::to_underlying(rfc8949::additional_information::one_byte_argument));
            out[item + heads::initial_byte_size] = static_cast<char>(typed_array_tag<E>());
            heads::item_head_write(out, item + heads::initial_byte_size + sizeof(std::uint8_t), major_type::byte_string,
                                   length * sizeof(E));
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
            c = elements_encode<Root, E, Exact, record_part::values>(out, data, value, c);
        }
        return c;
    }

    template <class Root, bool Exact, class U>
    CBOR_ALWAYS_INLINE static encode_cursor item_references_encode(std::span<char> const out, std::size_t const item,
                                                                   U const &value, encode_cursor c)
    {
        std::size_t const data = item + item_head;
        if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            if constexpr (holds_references<E>())
                if (value.has_value())
                    c = value_encode<Root, E, Exact, record_part::references>(
                        out, out.subspan(data).template first<fixed_size<E, Root>()>(), *value, c);
        } else if constexpr (is_map<U>) {
            using K = typename U::key_type;
            using M = typename U::mapped_type;
            if constexpr (holds_references<K>() || holds_references<M>()) {
                std::size_t at = data;
                for (auto const &[k, v] : value) {
                    c = value_encode<Root, K, Exact, record_part::references>(
                        out, out.subspan(at).template first<fixed_size<K, Root>()>(), k, c);
                    at += fixed_size<K, Root>();
                    c = value_encode<Root, M, Exact, record_part::references>(
                        out, out.subspan(at).template first<fixed_size<M, Root>()>(), v, c);
                    at += fixed_size<M, Root>();
                }
            }
        } else if constexpr (is_list<U> && !is_typed_array<U>) {
            using E = std::ranges::range_value_t<U>;
            if constexpr (holds_references<std::remove_cv_t<E>>())
                c = elements_encode<Root, E, Exact, record_part::references>(out, data, value, c);
        }
        return c;
    }

    template <class Root, bool Exact, record_part Part, class V>
    CBOR_ALWAYS_INLINE static encode_cursor reference_encode(std::span<char> const out, std::span<char, dynamic_type_sizes> const field,
                                        V const &value, encode_cursor c)
    {
        using U = std::remove_cv_t<V>;
        if constexpr (is_record<U> || is_optional_record<U>) {
            if constexpr (Part == record_part::block) {
                return c;
            } else {
                std::size_t const j = c.index++;
                std::size_t const item = c.position;
                reference_write<Root>(out, field, j, item);
                if constexpr (is_record<U>) {
                    auto const record = out.subspan(item).template first<record_size<U, Root>()>();
                    zero_initialized_record_copy<Root, U>(record);
                    c.position = item + record_size<U, Root>();
                    c = record_encode<Root, U, Exact>(out, record, value, c);
                } else {
                    using E = typename U::value_type;
                    if (value.has_value()) {
                        auto const record = out.subspan(item).template first<record_size<E, Root>()>();
                        zero_initialized_record_copy<Root, E>(record);
                        c.position = item + record_size<E, Root>();
                        c = record_encode<Root, E, Exact>(out, record, *value, c);
                    } else {
                        out[item] = heads::initial_byte(major_type::simple_float,
                                                        std::to_underlying(simple_value::undefined));
                        c.position = item + heads::initial_byte_size;
                    }
                }
                return c;
            }
        } else if constexpr (Part == record_part::block) {
            return item_encode<Root, Exact>(out, field, value, c);
        } else if constexpr (Part == record_part::after_block) {
            return item_references_encode<Root, Exact>(out, item_written<Root>(out, field), value, c);
        } else {
            std::size_t const item = c.position;
            c = item_encode<Root, Exact>(out, field, value, c);
            return item_references_encode<Root, Exact>(out, item, value, c);
        }
    }

    template <class Root, class T, bool Exact, record_part Part>
    CBOR_ALWAYS_INLINE static encode_cursor value_encode(std::span<char> const out, std::span<char, fixed_size<T, Root>()> const field,
                                    T const &value, encode_cursor position)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (!holds_references<U>() &&
                      (Part == record_part::after_block || Part == record_part::references)) {
        } else if constexpr (holds_references<U>() && !requires { fixed_length<U>::value; } &&
                             Part == record_part::values) {
        } else if constexpr (std::same_as<U, bool>) {
            field.front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + value);
        } else if constexpr (std::is_enum_v<U>) {
            position = value_encode<Root, std::underlying_type_t<U>, Exact, Part>(out, field, std::to_underlying(value), position);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 magnitude = static_cast<uint128>(value);
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = static_cast<uint128>(value >> std::numeric_limits<int128>::digits);
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
            M const sign = static_cast<M>(value >> std::numeric_limits<U>::digits);
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
                    position = value_encode<Root, E, Exact, Part>(out, field.subspan(at).template first<fixed_size<E, Root>()>(), e, position);
                    at += fixed_size<E, Root>();
                }
            }
        } else {
            position = reference_encode<Root, Exact, Part>(out, field, value, position);
        }
        return position;
    }

    template <class Root, class U, bool Exact>
    CBOR_ALWAYS_INLINE static encode_cursor
    record_encode(std::span<char> const out, std::span<char, record_size<U, Root>()> const record,
                  U const &value, encode_cursor position)
    {
        template for (constexpr auto m : data_members<U>())
        {
            using M = typename[:std::meta::type_of(m):];
            position = value_encode<Root, M, Exact, record_part::block>(
                out, record.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(),
                value.[:m:], position);
        }
        template for (constexpr auto m : data_members<U>())
        {
            using M = typename[:std::meta::type_of(m):];
            position = value_encode<Root, M, Exact, record_part::after_block>(
                out, record.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(),
                value.[:m:], position);
        }
        return position;
    }

    template <class U>
    static consteval std::meta::info member_named(std::string_view const name)
    {
        constexpr auto members = data_members<U>();
        auto const m = std::ranges::find_if(members, [name](std::meta::info const r) {
            return key_of(r) == name;
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
            M const sign = static_cast<M>(-static_cast<M>((head >> rfc8949::additional_information_bits) & 1));
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
            return (head | 1) == static_cast<unsigned char>(heads::initial_byte(
                                     major_type::simple_float, std::to_underlying(simple_value::true_value)));
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
                    return (head & static_cast<unsigned char>(~(1 << rfc8949::additional_information_bits))) == expected;
            } else if constexpr (std::same_as<U, uint128>) {
                return head == expected && static_cast<unsigned char>(field[1]) == static_cast<unsigned char>(zero_initialized<void, U>()[1]);
            } else {
                return head == expected;
            }
        }
    }

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static bool record_head_valid(std::span<char const, record_size<U, Root>()> const record)
    {
        constexpr std::size_t n =
            heads::head_size(*tag_number_of(^^U)) + straight_reference_size<Root, U>() + item_head;
        static_assert(zero_initialized_record<Root, U>().size() >= n,
                      "The zero-initialized record must hold the whole record head.");
        std::span<char const, n> const expected{zero_initialized_record<Root, U>().data(), n};
        return std::ranges::equal(record.template first<n>(), expected);
    }

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    static constexpr char reference_tag_byte =
        heads::initial_byte(major_type::tag, std::to_underlying(validity::tag_number::reference));

    CBOR_ALWAYS_INLINE static std::expected<std::uint64_t, error>
    shared_index_read(std::span<char const, dynamic_type_sizes> const field, std::size_t const shared_start)
    {
        auto const info = static_cast<unsigned char>(field[1]);
        if (field[0] != reference_tag_byte ||
            (info & static_cast<unsigned char>(~(1 << rfc8949::additional_information_bits))) != std::to_underlying(rfc8949::additional_information::four_byte_argument))
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::uint64_t const index = shared_first +
                                    2 * std::uint64_t{heads::unsigned_read<std::uint32_t>(
                                            field.subspan<2, sizeof(std::uint32_t)>())} +
                                    (info >> rfc8949::additional_information_bits);
        if (index < shared_start) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        return index - shared_start;
    }

    template <major_type Major, class E>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    item_reference_read(std::string_view const encoded, std::size_t const item, std::size_t const end,
                        std::size_t const element)
    {
        if constexpr (is_typed_array_element<E>) {
            static_assert(validity::typed_array_element_size(typed_array_tag<E>()) == sizeof(E),
                          "The elements of the typed array tag of E are sizeof(E) bytes.");
            if (!validity::pending_items_fit(item + typed_array_head, end)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            static constexpr std::array head{
                heads::initial_byte(major_type::tag,
                                    std::to_underlying(rfc8949::additional_information::one_byte_argument)),
                static_cast<char>(typed_array_tag<E>()),
                heads::initial_byte(major_type::byte_string,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument))};
            if (!std::ranges::equal(std::span<char const>(encoded).subspan(item).template first<head.size()>(), head)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::size_t const size =
                heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(item + head.size()).template first<sizeof(std::uint32_t)>());
            if (!validity::pending_items_fit(size, end - item - typed_array_head)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (!validity::typed_array_check(typed_array_tag<E>(), size)) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            if (error const r =
                    validity::check_argument<true>(major_type::byte_string, size).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            return reference{item + typed_array_head, size / sizeof(E)};
        } else {
            if (!validity::pending_items_fit(item + item_head, end)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (encoded[item] !=
                heads::initial_byte(Major,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument)))
                [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::size_t const length =
                heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(item + 1).template first<sizeof(std::uint32_t)>());
            auto const size = validity::checked_mul(length, element);
            if (!size || !validity::pending_items_fit(*size, end - item - item_head)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (error const r = validity::check_argument<true>(Major, length).error_or(error{}); r != error{})
                [[unlikely]]
                return std::unexpected(r);
            return reference{item + item_head, length};
        }
    }

    template <class Root>
    CBOR_ALWAYS_INLINE static std::size_t directory_entry(std::string_view const encoded, std::size_t const j)
    {
        return heads::unsigned_read<std::uint32_t>(
            std::span<char const>(encoded)
                .subspan(directory_at<Root>() + sizeof(std::uint32_t) * j)
                .template first<sizeof(std::uint32_t)>());
    }

    template <class Root, major_type Major, class E = void>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    reference_read(std::string_view const encoded, std::span<char const, dynamic_type_sizes> const field,
                   cbor::directory const dir, std::size_t const element)
    {
        auto const j = shared_index_read(field, dir.shared_start);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        return item_read<Root, Major, E>(encoded, dir, *j, element);
    }

    static constexpr std::size_t outside_block = std::numeric_limits<std::size_t>::max();

    template <class Root, std::size_t BlockIndex, major_type Major, class E = void>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    block_reference_read(std::string_view const encoded, std::span<char const, dynamic_type_sizes> const field,
                        cbor::directory const dir, record_block const block, std::size_t const element)
    {
        if constexpr (BlockIndex == outside_block) {
            return reference_read<Root, Major, E>(encoded, field, dir, element);
        } else {
            std::size_t const j = block.base + BlockIndex;
            auto const given = shared_index_read(field, dir.shared_start);
            if (!given) [[unlikely]]
                return std::unexpected(given.error());
            if (!validity::check_index(*given, dir.count - root_and_end_entries)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if (error const e = validity::check_reference_index(*given, j).error_or(error{}); e != error{})
                [[unlikely]]
                return std::unexpected(e);
            return item_read<Root, Major, E>(encoded, dir, j, element);
        }
    }

    template <class Root>
    CBOR_ALWAYS_INLINE static std::expected<std::pair<std::size_t, std::size_t>, error>
    item_range(std::string_view const encoded, cbor::directory const dir, std::uint64_t const j)
    {
        auto const i = validity::check_index(j, dir.count - root_and_end_entries);
        if (!i) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        std::size_t const n = static_cast<std::size_t>(std::to_underlying(*i));
        std::size_t const item = directory_entry<Root>(encoded, n + 1);
        std::size_t const end = directory_entry<Root>(encoded, n + 2);
        if (error const r =
                validity::check_item_range(item, end, dir.items_at, dir.items_end).error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        return std::pair{item, end};
    }

    template <class Root, major_type Major, class E = void>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    item_read(std::string_view const encoded, cbor::directory const dir, std::uint64_t const j,
              std::size_t const element)
    {
        auto const range = item_range<Root>(encoded, dir, j);
        if (!range) [[unlikely]]
            return std::unexpected(range.error());
        return item_reference_read<Major, E>(encoded, range->first, range->second, element);
    }

    static constexpr char undefined_byte =
        heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined));

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error>
    record_start(std::string_view const encoded, cbor::directory const dir, std::uint64_t const j)
    {
        auto const i = validity::check_index(j, dir.count - root_and_end_entries);
        if (!i) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        std::size_t const item =
            directory_entry<Root>(encoded, static_cast<std::size_t>(std::to_underlying(*i)) + 1);
        if (error const r = validity::check_item_range(item, item + record_size<U, Root>(), dir.items_at,
                                                       dir.items_end)
                                .error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (directory_entry<Root>(encoded, static_cast<std::size_t>(std::to_underlying(*i)) + 2) !=
            item + record_size<U, Root>()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (!record_head_valid<Root, U>(
                std::span<char const>(encoded).subspan(item).template first<record_size<U, Root>()>()))
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return item;
    }

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static std::expected<void, error>
    block_references_check(std::span<char const, fixed_size<U, Root>()> const field, std::size_t const first,
                           cbor::directory const dir)
    {
        if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            std::expected<void, error> done;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(0uz, n))) {
                if (done)
                    done = block_references_check<Root, E>(
                        field.template subspan<heads::head_size(n) + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(),
                        first + i * block_items<E>(), dir);
            }
            return done;
        } else {
            auto const j = shared_index_read(field, dir.shared_start);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (!validity::check_index(*j, dir.count - root_and_end_entries)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            return validity::check_reference_index(*j, first);
        }
    }

    template <class T>
    static std::expected<cbor::directory, error> directory_read(std::string_view const encoded)
    {
        static constexpr auto prefix = packing_prefix_of();
        constexpr std::size_t directory_items = 1;
        constexpr std::size_t count_at = prefix.size() - sizeof(std::uint32_t);
        constexpr std::size_t header = directory_at<T>();
        constexpr auto least_size =
            validity::checked_add(header, sizeof(std::uint32_t) * root_and_end_entries)
                .and_then(
                    [](std::size_t const head) { return validity::checked_add(head, record_size<T, T>()); });
        static_assert(least_size.has_value(), "The least size of an encoded item must fit in std::size_t.");
        constexpr std::size_t least = *least_size;
        if (!validity::pending_items_fit(least, encoded.size())) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (!std::ranges::equal(std::span(encoded).template first<count_at>(),
                                std::span(prefix).template first<count_at>()) ||
            encoded[prefix.size()] !=
                heads::initial_byte(major_type::byte_string,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument)))
            [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length =
            heads::unsigned_read<std::uint32_t>(std::span<char const>(encoded)
                                                    .subspan(prefix.size() + heads::initial_byte_size)
                                                    .template first<sizeof(std::uint32_t)>());
        std::size_t const count = length / sizeof(std::uint32_t);
        if (length % sizeof(std::uint32_t) != 0 || count < root_and_end_entries) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (!validity::pending_items_fit(length, encoded.size() - header - record_size<T, T>())) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::size_t const items = count - root_and_end_entries;
        std::size_t const table = heads::unsigned_read<std::uint32_t>(
            std::span<char const>(encoded).subspan(count_at).template first<sizeof(std::uint32_t)>());
        if (table < directory_items || table - directory_items < items) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const shared_start = table - items;
        std::size_t const fillers = shared_start - directory_items;
        std::size_t const after = header + length;
        if (!validity::pending_items_fit(fillers, encoded.size() - after - record_size<T, T>())) [[unlikely]]
            return std::unexpected(error::too_little_data);
        static constexpr char undefined = static_cast<char>(
            heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined)));
        constexpr std::size_t written = fillers_written;
        static constexpr auto written_fillers = [] {
            std::array<char, written> a{};
            a.fill(undefined);
            return a;
        }();
        if (fillers == written) [[likely]] {
            if (!std::ranges::equal(std::span(encoded).subspan(after).template first<written>(), written_fillers))
                [[unlikely]]
                return std::unexpected(error::incorrect_type);
        } else if (!std::ranges::all_of(std::span(encoded).subspan(after, fillers),
                                        [](char const c) { return c == undefined; })) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        cbor::directory dir{count, after + fillers, directory_entry<T>(encoded, count - 1),
                            directory_entry<T>(encoded, 0), shared_start};
        if (error const r = validity::check_item_range(dir.items_at, dir.items_end, dir.items_at, dir.root)
                                .error_or(error{});
            r != error{}) [[unlikely]]
            return std::unexpected(r);
        if (!validity::pending_items_fit(dir.root, encoded.size() - record_size<T, T>())) [[unlikely]]
            return std::unexpected(error::too_little_data);
        static constexpr auto arguments = arguments_of<T>();
        std::string_view const given{std::span(encoded).subspan(dir.items_end, dir.root - dir.items_end)};
        if (!std::ranges::equal(given, arguments)) [[unlikely]]
            if (error const r = arguments_check<T>(given).error_or(error{}); r != error{}) [[unlikely]]
                return std::unexpected(r);
        return dir;
    }

    template <class T>
    static std::expected<void, error> arguments_check(std::string_view const given)
    {
        static constexpr auto arguments = arguments_of<T>();
        constexpr std::size_t types = packing_table_of<T>().size();
        heads::decoder<false> message{given};
        heads::decoder<false> own{std::string_view(arguments.data(), arguments.size())};
        auto const entries = message.head_decode();
        if (!entries) [[unlikely]]
            return std::unexpected(entries.error());
        if (entries->major != major_type::array || entries->argument < types) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (auto const r = own.head_decode(); !r) [[unlikely]]
            return std::unexpected(r.error());
        for (std::uint64_t i = 0; i < entries->argument; ++i) {
            auto const function = message.head_decode();
            if (!function) [[unlikely]]
                return std::unexpected(function.error());
            if (function->major != major_type::tag ||
                function->argument != std::to_underlying(validity::tag_number::record_function)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const keys = message.head_decode();
            if (!keys) [[unlikely]]
                return std::unexpected(keys.error());
            if (keys->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            bool const known = i < types;
            if (known) {
                if (auto const r = own.head_decode(); !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const own_keys = own.head_decode();
                if (!own_keys) [[unlikely]]
                    return std::unexpected(own_keys.error());
                if (own_keys->argument != keys->argument) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            }
            for (std::uint64_t k = 0; k < keys->argument; ++k) {
                auto const key = message.head_decode();
                if (!key) [[unlikely]]
                    return std::unexpected(key.error());
                if (key->major != major_type::text_string) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const text = message.byte_string_decode(key->argument);
                if (!text) [[unlikely]]
                    return std::unexpected(text.error());
                if (!known)
                    continue;
                auto const own_key = own.head_decode();
                if (!own_key) [[unlikely]]
                    return std::unexpected(own_key.error());
                auto const own_text = own.byte_string_decode(own_key->argument);
                if (!own_text) [[unlikely]]
                    return std::unexpected(own_text.error());
                if (*text != *own_text) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            }
        }
        if (!message.encoded.empty()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
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
                return std::type_identity<typename cbor::packed<Root>::template accessor<U>>{};
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

    template <class Root, class M, fixed_string Path, std::size_t At, std::size_t BlockIndex = outside_block>
    CBOR_ALWAYS_INLINE static auto
    path_enter(std::string_view const encoded, std::span<char const, fixed_size<M, Root>()> const field,
               cbor::directory const dir, record_block const block,
               std::array<std::size_t, index_slots<Path>()> const &indexes)
        -> std::expected<typename decltype(path_result<Root, M, Path, At>())::type, error>
    {
        using U = std::remove_cv_t<M>;
        if constexpr (is_record<U>) {
            auto const j = shared_index_read(field, dir.shared_start);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (error const e = validity::check_reference_after(*j, block.end).error_or(error{}); e != error{})
                [[unlikely]]
                return std::unexpected(e);
            auto const at = record_start<Root, U>(encoded, dir, *j);
            if (!at) [[unlikely]]
                return std::unexpected(at.error());
            return path_walk<Root, M, Path, At>(
                encoded, std::span<char const>(encoded).subspan(*at).template first<record_size<U, Root>()>(), dir,
                record_block_at<U>(static_cast<std::size_t>(*j) + 1), indexes);
        } else {
            return path_walk<Root, M, Path, At, BlockIndex>(encoded, field, dir, block, indexes);
        }
    }

    template <class Root, class T, fixed_string Path, std::size_t At, std::size_t BlockIndex = outside_block>
    CBOR_ALWAYS_INLINE static auto
    path_walk(std::string_view const encoded, std::span<char const, extent_of<T, Root>()> const field,
              cbor::directory const floor, record_block const block,
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
            } else if constexpr (is_optional_record<U>) {
                using E = typename U::value_type;
                using X = typename decltype(path_result<Root, E, Path, At>())::type;
                auto const j = shared_index_read(field, floor.shared_start);
                if (!j) [[unlikely]]
                    return std::unexpected(j.error());
                if (error const e = validity::check_reference_after(*j, block.end).error_or(error{}); e != error{})
                    [[unlikely]]
                    return std::unexpected(e);
                auto const range = item_range<Root>(encoded, floor, *j);
                if (!range) [[unlikely]]
                    return std::unexpected(range.error());
                if (range->second - range->first == heads::initial_byte_size && encoded[range->first] == undefined_byte)
                    return std::optional<X>{};
                auto const at = record_start<Root, E>(encoded, floor, *j);
                if (!at) [[unlikely]]
                    return std::unexpected(at.error());
                auto const x = path_walk<Root, E, Path, At>(
                    encoded, std::span<char const>(encoded).subspan(*at).template first<record_size<E, Root>()>(), floor,
                    record_block_at<E>(static_cast<std::size_t>(*j) + 1), indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r =
                    block_reference_read<Root, BlockIndex, major_type::array>(encoded, field, floor, block, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->length > 1) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                using X = typename decltype(path_result<Root, E, Path, At>())::type;
                if (r->length == 0)
                    return std::optional<X>{};
                auto const x = path_walk<Root, E, Path, At>(
                    encoded,
                    std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>(),
                    floor, block, indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = block_reference_read < Root, BlockIndex,
                           is_text_range<U> ? major_type::text_string
                                            : major_type::byte_string > (encoded, field, floor, block, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return std::string_view(std::span(encoded).subspan(r->data, r->length));
            } else if constexpr (is_map<U>) {
                auto const r = block_reference_read<Root, BlockIndex, major_type::map>(
                    encoded, field, floor, block,
                    fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::packed<Root>::template accessor<U>(encoded, field, floor, block, *r);
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = block_reference_read<Root, BlockIndex, major_type::array, E>(encoded, field, floor, block, sizeof(E));
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::packed<Root>::template accessor<U>(encoded, field, floor, block, *r);
            } else if constexpr (is_list<U>) {
                auto const r = block_reference_read<Root, BlockIndex, major_type::array>(
                    encoded, field, floor, block, fixed_size<std::ranges::range_value_t<U>, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::packed<Root>::template accessor<U>(encoded, field, floor, block, *r);
            } else if constexpr ((std::is_class_v<U> || std::is_array_v<U>) && !std::same_as<U, int128> &&
                                 !std::same_as<U, uint128>) {
                return typename cbor::packed<Root>::template accessor<U>(encoded, field, floor, block);
            } else {
                if (!fixed_head_valid<U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return fixed_value_read<U>(field);
            }
        } else if constexpr (path[At] == '.') {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            using M = typename[:std::meta::type_of(m):];
            auto const member = field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>();
            if constexpr (block_items<std::remove_cv_t<M>>() == 1 && !requires { fixed_length<std::remove_cv_t<M>>::value; }) {
                return path_enter<Root, M, Path, end, block_index<U, m>()>(encoded, member, floor, block, indexes);
            } else if constexpr (block_items<std::remove_cv_t<M>>() != 0) {
                if (error const e = block_references_check<Root, std::remove_cv_t<M>>(
                                        member, block.base + block_index<U, m>(), floor)
                                        .error_or(error{});
                    e != error{}) [[unlikely]]
                    return std::unexpected(e);
            }
            return path_enter<Root, M, Path, end>(encoded, member, floor, block, indexes);
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
                    return path_enter<Root, E, Path, close + 1>(
                        encoded, field.subspan(head + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(),
                        floor, block, indexes);
                } else {
                    constexpr std::size_t i = index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1)));
                    return path_enter<Root, E, Path, close + 1>(
                        encoded, field.template subspan<head + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(),
                        floor, block, indexes);
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
                    auto const r =
                        block_reference_read<Root, BlockIndex, major_type::array, F>(encoded, field, floor, block, sizeof(F));
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    if (error const c = validity::check_index(i, r->length).error_or(error{}); c != error{})
                        [[unlikely]]
                        return std::unexpected(c);
                    return typed_array_element_read<F>(std::span<char const>(encoded).subspan(r->data + i * sizeof(F)).template first<sizeof(F)>());
                }
                auto const r =
                    block_reference_read<Root, BlockIndex, major_type::array>(encoded, field, floor, block, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (error const c = validity::check_index(i, r->length).error_or(error{}); c != error{})
                    [[unlikely]]
                    return std::unexpected(c);
                return path_enter<Root, E, Path, close + 1>(encoded,
                                                            std::span<char const>(encoded)
                                                                .subspan(r->data + i * fixed_size<E, Root>())
                                                                .template first<fixed_size<E, Root>()>(),
                                                            floor, block, indexes);
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
            auto const j = shared_index_read(field, dir.shared_start);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (!validity::check_index(*j, dir.count - root_and_end_entries)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if (error const e = validity::check_reference_index(*j, index).error_or(error{}); e != error{}) [[unlikely]]
                return std::unexpected(e);
            if (directory_entry<Root>(encoded, index + 1) != at) [[unlikely]]
                return std::unexpected(error::syntax_error);
            auto const r = item_reference_read<Major, E>(encoded, at, end, element);
            if (!r) [[unlikely]]
                return r;
            index += 1;
            at = r->data + r->length * element;
            return r;
        }

        template <class Root, class U>
        CBOR_ALWAYS_INLINE std::expected<std::size_t, error> record_take(std::span<char const, dynamic_type_sizes> const field)
        {
            auto const j = shared_index_read(field, dir.shared_start);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (!validity::check_index(*j, dir.count - root_and_end_entries)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if (error const e = validity::check_reference_index(*j, index).error_or(error{}); e != error{}) [[unlikely]]
                return std::unexpected(e);
            if (directory_entry<Root>(encoded, index + 1) != at) [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (!validity::pending_items_fit(heads::initial_byte_size, end - at)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::size_t const record = at;
            if constexpr (is_optional_record<U>) {
                if (encoded[at] == undefined_byte) {
                    index += 1;
                    at += heads::initial_byte_size;
                    return record;
                }
            }
            using R = typename decltype([] {
                if constexpr (is_optional_record<U>)
                    return std::type_identity<typename U::value_type>{};
                else
                    return std::type_identity<U>{};
            }())::type;
            if (!validity::pending_items_fit(record_size<R, Root>(), end - at)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            index += 1;
            at += record_size<R, Root>();
            return record;
        }

        template <class Root, class U, bool Take>
        CBOR_ALWAYS_INLINE std::expected<reference, error> item_reference(std::span<char const, dynamic_type_sizes> const field)
        {
            constexpr auto major = [] {
                if constexpr (is_optional<U>)
                    return major_type::array;
                else if constexpr (is_text_range<U>)
                    return major_type::text_string;
                else if constexpr (is_byte_range<U>)
                    return major_type::byte_string;
                else if constexpr (is_map<U>)
                    return major_type::map;
                else
                    return major_type::array;
            }();
            using E = std::conditional_t<is_typed_array<U>, std::remove_cv_t<std::ranges::range_value_t<U>>, void>;
            constexpr std::size_t element = [] {
                if constexpr (is_optional<U>)
                    return fixed_size<typename U::value_type, Root>();
                else if constexpr (is_text_range<U> || is_byte_range<U>)
                    return std::size_t{1};
                else if constexpr (is_map<U>)
                    return fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>();
                else if constexpr (is_typed_array<U>)
                    return sizeof(std::remove_cv_t<std::ranges::range_value_t<U>>);
                else
                    return fixed_size<std::ranges::range_value_t<U>, Root>();
            }();
            if constexpr (Take)
                return reference_take<Root, major, E>(field, element);
            else
                return reference_read<Root, major, E>(encoded, field, dir, element);
        }

        template <class Root, class U>
        std::expected<void, error> record_read(U &out, std::span<char const, record_size<U, Root>()> const record,
                                               std::size_t const depth, std::size_t const depth_max)
        {
            if (!record_head_valid<Root, U>(record)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> done;
            template for (constexpr std::meta::info m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                if (done)
                    done = member_read<Root, M, record_part::block>(
                        out.[:m:], record.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(), depth,
                        depth_max);
            }
            template for (constexpr std::meta::info m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                if (done)
                    done = member_read<Root, M, record_part::after_block>(
                        out.[:m:], record.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(), depth,
                        depth_max);
            }
            return done;
        }

        template <class Root, class T, record_part Part>
        std::expected<void, error> member_read(T &out, std::span<char const, fixed_size<T, Root>()> const field,
                                               std::size_t const depth, std::size_t const depth_max)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (!holds_references<U>() && Part == record_part::after_block) {
                return {};
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
                        done = member_read<Root, E, Part>(
                            std::span(out).template subspan<i, 1>().front(),
                            field.template subspan<heads::head_size(n) + i * fixed_size<E, Root>(),
                                                   fixed_size<E, Root>()>(),
                            depth, depth_max);
                }
                return done;
            } else if constexpr (is_inline_value<U>) {
                if (!fixed_head_valid<U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                out = fixed_value_read<U>(field);
                return {};
            } else if constexpr (is_record<U> || is_optional_record<U>) {
                if constexpr (Part == record_part::block) {
                    return {};
                } else {
                    auto const record = record_take<Root, U>(field);
                    if (!record) [[unlikely]]
                        return std::unexpected(record.error());
                    if constexpr (is_record<U>) {
                        return record_read<Root>(
                            out, std::span<char const>(encoded).subspan(*record).template first<record_size<U, Root>()>(),
                            depth, depth_max);
                    } else {
                        using E = typename U::value_type;
                        if (encoded[*record] == undefined_byte) {
                            out.reset();
                            return {};
                        }
                        if (auto const nesting = validity::check_nesting_depth(depth + 1, depth_max); !nesting)
                            [[unlikely]]
                            return std::unexpected(nesting.error());
                        E element{};
                        if (auto const e = record_read<Root>(
                                element,
                                std::span<char const>(encoded).subspan(*record).template first<record_size<E, Root>()>(),
                                depth + 1, depth_max);
                            !e) [[unlikely]]
                            return e;
                        out = std::move(element);
                        return {};
                    }
                }
            } else if constexpr (Part == record_part::block) {
                auto const r = item_reference<Root, U, true>(field);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return {};
            } else {
                auto const r = item_reference<Root, U, Part != record_part::after_block>(field);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return content_read<Root>(out, *r, depth, depth_max);
            }
        }

        template <class Root, class U>
        std::expected<void, error> content_read(U &out, reference const r, std::size_t const depth,
                                                std::size_t const depth_max)
        {
            if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                if (r.length > 1) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                if (r.length == 0) {
                    out.reset();
                    return {};
                }
                if (auto const nesting = validity::check_nesting_depth(depth + 1, depth_max); !nesting) [[unlikely]]
                    return std::unexpected(nesting.error());
                E element{};
                if (auto const e = member_read<Root, E, record_part::whole>(
                        element, std::span<char const>(encoded).subspan(r.data).template first<fixed_size<E, Root>()>(),
                        depth + 1, depth_max);
                    !e) [[unlikely]]
                    return e;
                out = std::move(element);
                return {};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                std::string_view const part{std::span(encoded).subspan(r.data, r.length)};
                if constexpr (std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>) {
                    auto const raw = std::as_bytes(std::span(part));
                    if constexpr (std::ranges::view<U>)
                        out = U(raw.begin(), raw.end());
                    else
                        out.assign(raw.begin(), raw.end());
                } else if constexpr (std::ranges::view<U>) {
                    out = U(part.begin(), part.end());
                } else {
                    out.assign(part.begin(), part.end());
                }
                return {};
            } else if constexpr (is_map<U>) {
                using K = typename U::key_type;
                using V = typename U::mapped_type;
                constexpr std::size_t pair = fixed_size<K, Root>() + fixed_size<V, Root>();
                if (auto const c = validity::check_nesting_depth(r.length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                out.clear();
                for (std::size_t i = 0; i < r.length; ++i) {
                    auto const entry = std::span<char const>(encoded).subspan(r.data + i * pair).template first<pair>();
                    K key = element_make<K>(out);
                    V value = element_make<V>(out);
                    if (auto const e = member_read<Root, K, record_part::whole>(
                            key, entry.template first<fixed_size<K, Root>()>(), depth + 1, depth_max);
                        !e) [[unlikely]]
                        return e;
                    if (auto const e = member_read<Root, V, record_part::whole>(
                            value, entry.template last<fixed_size<V, Root>()>(), depth + 1, depth_max);
                        !e) [[unlikely]]
                        return e;
                    out.try_emplace(std::move(key), std::move(value));
                }
                return {};
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                if (auto const c = validity::check_nesting_depth(r.length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                auto const from = std::span<char const>(encoded).subspan(r.data, r.length * sizeof(E));
                if constexpr (std::endian::native == std::endian::little && std::ranges::contiguous_range<U> &&
                              requires { out.resize(std::size_t{}); }) {
                    out.resize(r.length);
                    std::ranges::copy(std::as_bytes(from), std::as_writable_bytes(std::span(out)).begin());
                } else {
                    out.clear();
                    out.reserve(r.length);
                    for (std::size_t i = 0; i < r.length; ++i)
                        out.push_back(typed_array_element_read<E>(from.subspan(i * sizeof(E)).template first<sizeof(E)>()));
                }
                return {};
            } else {
                using E = std::ranges::range_value_t<U>;
                if (auto const c = validity::check_nesting_depth(r.length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                out.clear();
                out.reserve(r.length);
                for (std::size_t i = 0; i < r.length; ++i) {
                    E element = element_make<E>(out);
                    auto const entry =
                        std::span<char const>(encoded).subspan(r.data + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>();
                    if (auto const e = member_read<Root, E, record_part::whole>(element, entry, depth + 1, depth_max); !e)
                        [[unlikely]]
                        return e;
                    out.push_back(std::move(element));
                }
                return {};
            }
        }
    };

    template <class T>
    static std::expected<void, error> root_read(T &out, std::string_view const encoded,
                                                std::size_t const depth_max)
    {
        auto const dir = directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        auto const field =
            std::span<char const>(encoded).subspan(dir->root).template first<record_size<T, T>()>();
        decode_cursor c{encoded, *dir, 0, dir->items_at, dir->items_end};
        if (auto const r = c.template record_read<T>(out, field, 0, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (c.index != dir->count - root_and_end_entries || c.at != dir->items_end) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    template <class>
    friend class cbor::packed;

    template <class T>
    static std::expected<std::size_t, std::errc> encoded_size(second_item<T> const &second)
    {
        constexpr auto fixed_part = validity::checked_add(directory_at<T>(), fillers_written)
                                        .and_then([](std::size_t const head) {
                                            return validity::checked_add(head, arguments_of<T>().size());
                                        })
                                        .and_then([](std::size_t const head) {
                                            return validity::checked_add(head, record_size<T, T>());
                                        });
        static_assert(fixed_part.has_value(), "The fixed part of an encoded item must fit in std::size_t.");
        constexpr std::size_t fixed = *fixed_part;
        if (second.overflow) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        auto const entries = validity::checked_add(second.items, root_and_end_entries);
        if (!entries) [[unlikely]]
            return entries;
        auto const directory = validity::checked_mul(*entries, sizeof(std::uint32_t));
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
        static constexpr auto prefix = packing_prefix_of();
        static constexpr auto arguments = arguments_of<T>();
        std::size_t const size = encoded.size() - (Exact ? 0 : heads::head_padding);
        std::ranges::copy(prefix, encoded.template first<prefix.size()>().begin());
        heads::u32_write(encoded, prefix.size() - sizeof(std::uint32_t), shared_start_written + second.items);
        std::size_t const entries = second.items + root_and_end_entries;
        heads::item_head_write(encoded, prefix.size(), major_type::byte_string,
                               sizeof(std::uint32_t) * entries);
        std::size_t const data = directory_at<T>() + sizeof(std::uint32_t) * entries;
        std::ranges::fill(
            encoded.subspan(data, fillers_written),
            heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined)));
        std::size_t const at = size - record_size<T, T>();
        auto const root = encoded.subspan(at).template first<record_size<T, T>()>();
        zero_initialized_record_copy<T, T>(root);
        encode_cursor const end =
            record_encode<T, T, Exact>(encoded, root, value, encode_cursor{data + fillers_written, 0});
        std::ranges::copy(arguments,
                          encoded.subspan(end.position).template first<arguments.size()>().begin());
        heads::u32_write(encoded, directory_at<T>(), at);
        heads::u32_write(encoded, directory_at<T>() + sizeof(std::uint32_t) * (entries - 1), end.position);
        return size;
    }

    template <class U>
    static consteval auto members_of()
    {
        auto const members = data_members<U>();
        if (!keys_unique(members))
            std::unreachable();
        return members;
    }

    template <class>
    friend class cbor::typed_array_view;
#endif

    friend class generic;

    friend class pfr;
};

#ifdef __cpp_impl_reflection

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
    friend class packed;

public:
    typed_array_view(typed_array_view const &) = default;

    typed_array_view(typed_array_view &&other) noexcept
        : owner(std::move(other.owner)), bytes(std::exchange(other.bytes, std::span<char const>()))
    {
    }

    typed_array_view &operator=(typed_array_view const &) = default;

    typed_array_view &operator=(typed_array_view &&other) noexcept
    {
        owner = std::move(other.owner);
        bytes = std::exchange(other.bytes, std::span<char const>());
        return *this;
    }

    ~typed_array_view() = default;

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
            return reflection::typed_array_element_read<E>(std::span<char const, sizeof(E)>(at, sizeof(E)));
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

    CBOR_ALWAYS_INLINE iterator cbegin() const
    {
        return begin();
    }

    CBOR_ALWAYS_INLINE iterator cend() const
    {
        return end();
    }

    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> rbegin() const
    {
        return std::reverse_iterator<iterator>(end());
    }

    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> rend() const
    {
        return std::reverse_iterator<iterator>(begin());
    }

    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> crbegin() const
    {
        return rbegin();
    }

    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> crend() const
    {
        return rend();
    }

    CBOR_ALWAYS_INLINE E front() const
    {
        if (!validity::check_index(std::size_t{0}, size())) [[unlikely]]
            validity::throw_logic_error("cbor::typed_array_view::front: the view is empty");
        return reflection::typed_array_element_read<E>(bytes.template first<sizeof(E)>());
    }

    CBOR_ALWAYS_INLINE E back() const
    {
        if (!validity::check_index(std::size_t{0}, size())) [[unlikely]]
            validity::throw_logic_error("cbor::typed_array_view::back: the view is empty");
        return reflection::typed_array_element_read<E>(bytes.template last<sizeof(E)>());
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
        return typed_array_view(std::move(owner), std::exchange(bytes, std::span<char const>()).first(n * sizeof(E)));
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
        return typed_array_view(std::move(owner), std::exchange(bytes, std::span<char const>()).last(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<E, error> at(std::size_t const i) const
    {
        if (error const c = validity::check_index(i, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return reflection::typed_array_element_read<E>(bytes.subspan(i * sizeof(E)).template first<sizeof(E)>());
    }

    CBOR_ALWAYS_INLINE E operator[](std::size_t const i) const
    {
        if (!validity::check_index(i, size())) [[unlikely]]
            validity::throw_logic_error("cbor::typed_array_view::operator[]: the index is outside of the view");
        return reflection::typed_array_element_read<E>(bytes.subspan(i * sizeof(E)).template first<sizeof(E)>());
    }
};

template <class Root>
consteval bool tags_registered()
{
    std::vector<std::uint64_t> numbers;
    for (std::meta::info const type : reflection::packing_table_of<Root>()) {
        auto const number = reflection::tag_number_of(type);
        if (!number)
            return false;
        numbers.push_back(*number);
    }
    std::ranges::sort(numbers);
    return validity::keys_unique(numbers);
}

template <class T, class Root>
consteval std::size_t reflection::fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = heads::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (reflection::has_fixed_underlying_type<U>)
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
    } else if constexpr (requires { reflection::fixed_length<U>::value; }) {
        using E = typename reflection::fixed_length<U>::element;
        constexpr std::size_t n = reflection::fixed_length<U>::value;
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
    } else if constexpr (reflection::is_record<U>) {
        return reflection::dynamic_type_sizes;
    } else if constexpr (
        requires(U const &v) {
            v.has_value();
            *v;
            typename U::value_type;
        } && !requires { typename U::error_type; })
        return reflection::dynamic_type_sizes;
    else if constexpr (std::ranges::sized_range<U>)
        return reflection::dynamic_type_sizes;
    else
        return no_fixed_size<T>();
}

template <class T, class Root>
consteval std::size_t reflection::record_size()
{
    using U = std::remove_cv_t<T>;
    if constexpr (!std::meta::bases_of(^^U, std::meta::access_context::unprivileged()).empty() ||
                  !reflection::keys_unique(reflection::data_members<U>())) {
        return no_fixed_size<U>();
    } else {
        std::size_t size = heads::head_size(*reflection::tag_number_of(^^U)) +
                           reflection::straight_reference_size<Root, U>() + reflection::item_head;
        template for (constexpr auto m : reflection::data_members<U>())
        {
            if constexpr (!std::meta::has_identifier(m) || std::meta::is_bit_field(m) ||
                          !std::meta::is_public(m))
                return no_fixed_size<U>();
            constexpr std::size_t member = fixed_size<typename[:std::meta::type_of(m):], Root>();
            auto const sum = validity::checked_add(size, member);
            if (!sum)
                return no_fixed_size<U>();
            size = *sum;
        }
        return size;
    }
}

template <class T, std::meta::info Member, class Root>
consteval std::size_t reflection::member_offset()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::meta::parent_of(Member) != std::meta::dealias(^^U)) {
        return no_fixed_size<T>();
    } else {
        std::size_t offset = heads::head_size(*reflection::tag_number_of(^^U)) +
                             reflection::straight_reference_size<Root, U>() + reflection::item_head;
        template for (constexpr auto m : reflection::data_members<U>()) {
            if constexpr (m == Member)
                return offset;
            offset += fixed_size<typename[:std::meta::type_of(m):], Root>();
        }
        return no_fixed_size<T>();
    }
}

template <class T>
class packed
{
    static_assert(!reflection::table_tag_taken_by_standard_type<T>(),
                  "cbor::packed takes a tag that no standard type has.");
    static_assert(!reflection::table_tag_invalid<T>(), "cbor::packed takes a tag that the IANA registry calls always invalid.");

    template <class X>
    static constexpr bool value_holds_no_view = std::is_trivially_copyable_v<X> && !std::same_as<X, std::string_view> &&
                                                !std::same_as<X, std::optional<std::string_view>>;

public:
    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t fixed_size()
    {
        return reflection::extent_of<U, T>();
    }

    template <std::meta::info Member, class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t member_offset()
    {
        return reflection::member_offset<U, Member, T>();
    }

    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    class accessor
    {
        static constexpr bool listed = reflection::is_list<U> || reflection::is_map<U>;

        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::span<char const, reflection::extent_of<U, T>()> field;
        cbor::directory dir;
        reflection::record_block block;
        [[no_unique_address]] std::conditional_t<listed, reflection::reference, std::monostate> items;

        accessor(std::string_view const b, std::span<char const, reflection::extent_of<U, T>()> const f,
                 cbor::directory const d, reflection::record_block const k)
            requires(!listed)
            : encoded(b), field(f), dir(d), block(k)
        {
        }

        accessor(std::string_view const b, std::span<char const, reflection::extent_of<U, T>()> const f,
                 cbor::directory const d, reflection::record_block const k, reflection::reference const r)
            requires(listed)
            : encoded(b), field(f), dir(d), block(k), items(r)
        {
        }

        accessor(std::shared_ptr<void const> o, std::string_view const b,
                 std::span<char const, reflection::extent_of<U, T>()> const f, cbor::directory const d,
                 reflection::record_block const k)
            requires(!listed)
            : owner(std::move(o)), encoded(b), field(f), dir(d), block(k)
        {
        }

        friend class packed;

        friend class reflection;

        template <class>
        friend class accessor;

    public:
        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(((Path.view().starts_with('@') && reflection::path_valid<U, Path, 1>()) ||
                      (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                     sizeof...(Index) == reflection::index_slots<Path>() && std::is_lvalue_reference_v<Self>)
        CBOR_ALWAYS_INLINE auto at_path(this Self &&self, Index const... indexes)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::packed::accessor::at_path: the accessor was moved from");
            auto result = [&] {
                std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
                constexpr std::string_view path = Path.view();
                if constexpr (path.starts_with('$') && std::same_as<U, T>) {
                    return reflection::path_walk<T, T, Path, 1>(self.encoded, self.field, self.dir,
                                                                reflection::record_block_at<T>(0), i);
                } else if constexpr (path.starts_with('$')) {
                    constexpr std::size_t root = reflection::record_size<T, T>();
                    return reflection::path_walk<T, T, Path, 1>(
                        self.encoded,
                        std::span<char const>(self.encoded).subspan(self.dir.root).template first<root>(),
                        self.dir, reflection::record_block_at<T>(0), i);
                } else if constexpr (reflection::is_typed_array<U> && path.size() > 1) {
                    using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                    constexpr std::size_t close = reflection::index_end(path, 1);
                    std::size_t at;
                    if constexpr (close == 2)
                        at = std::get<0>(i);
                    else
                        at = reflection::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                    if (error const c = validity::check_index(at, self.items.length).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::expected<E, error>(std::unexpect, c);
                    return std::expected<E, error>(reflection::typed_array_element_read<E>(
                        std::span<char const>(self.encoded).subspan(self.items.data + at * sizeof(E)).template first<sizeof(E)>()));
                } else if constexpr (reflection::is_list<U> && path.size() > 1) {
                    using E = std::ranges::range_value_t<U>;
                    constexpr std::size_t close = reflection::index_end(path, 1);
                    using X = typename decltype(reflection::path_result<T, E, Path, close + 1>())::type;
                    std::size_t at;
                    if constexpr (close == 2)
                        at = std::get<0>(i);
                    else
                        at = reflection::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                    if (error const c = validity::check_index(at, self.items.length).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::expected<X, error>(std::unexpect, c);
                    return std::expected<X, error>(reflection::path_enter<T, E, Path, close + 1>(
                        self.encoded,
                        std::span<char const>(self.encoded)
                            .subspan(self.items.data + at * reflection::fixed_size<E, T>())
                            .template first<reflection::fixed_size<E, T>()>(),
                        self.dir, self.block, i));
                } else {
                    return reflection::path_walk<T, U, Path, 1>(self.encoded, self.field, self.dir, self.block, i);
                }
            
            }();
            if constexpr (requires { result->owner; })
                if (result)
                    result->owner = self.owner;
            return result;
        }

        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(std::same_as<U, T> && Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>() &&
                     sizeof...(Index) == reflection::index_slots<Path>())
        auto view(this Self &&self, Index const... indexes)
        {
            using X = typename decltype(reflection::path_result<T, T, Path, 1>())::type;
            using E = typename decltype([]<class V>(std::type_identity<accessor<V>>) {
                static_assert(reflection::is_typed_array<V>, "cbor::packed::accessor::view: the path does not end at a typed array");
                return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<V>>>{};
            }(std::type_identity<X>{}))::type;
            auto const list = self.template at_path<Path>(indexes...);
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

    template <owning_range_of_chars Encoded>
    static std::expected<accessor<>, error> path(std::shared_ptr<Encoded const> encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_null(encoded, "cbor::packed::path: the encoded data item is null");
        std::string_view const bytes(std::ranges::data(*encoded), std::ranges::size(*encoded));
        return path_with_owner(std::shared_ptr<void const>(std::move(encoded)), bytes);
    }

    template <borrowed_range_of_chars Encoded>
    static std::expected<accessor<>, error> path(std::shared_ptr<void const> owner, Encoded const &encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        return path_with_owner(std::move(owner), std::string_view(std::ranges::data(encoded), std::ranges::size(encoded)));
    }

    template <fixed_string Path, std::convertible_to<std::size_t>... Index>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>() && Path.view().starts_with('$') &&
                 reflection::path_valid<T, Path, 1>() && sizeof...(Index) == reflection::index_slots<Path>() &&
                 value_holds_no_view<typename decltype(reflection::path_result<T, T, Path, 1>())::type>)
    static auto at_path(std::string_view const encoded, Index const... indexes)
    {
        using X = typename decltype(reflection::path_result<T, T, Path, 1>())::type;
        auto const dir = reflection::directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::expected<X, error>(std::unexpect, dir.error());
        auto const root = std::span<char const>(encoded).subspan(dir->root).template first<fixed_size()>();
        if (!reflection::record_head_valid<T, T>(root)) [[unlikely]]
            return std::expected<X, error>(std::unexpect, error::incorrect_type);
        std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
        return std::expected<X, error>(
            reflection::path_walk<T, T, Path, 1>(encoded, root, *dir, reflection::record_block_at<T>(0), i));
    }

    template <owning_range_of_chars Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static std::expected<accessor<>, error> path(Encoded &&encoded)
    {
        return path(std::make_shared<Encoded const>(std::move(encoded)));
    }

    template <owning_range_of_chars Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static std::expected<ref<T>, error> decode(Encoded &&encoded)
    {
#if defined(__cpp_exceptions)
        try {
#endif
            return decode(std::make_shared<Encoded const>(std::move(encoded)));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    template <owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        return decode(std::move(encoded), T{});
    }

    template <owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded, T target)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_null(encoded, "cbor::packed::decode: the encoded data item is null");
        std::string_view const bytes(std::ranges::data(*encoded), std::ranges::size(*encoded));
        return decode_with_owner(std::shared_ptr<void const>(std::move(encoded)), bytes, std::move(target));
    }

    template <borrowed_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        return decode(std::move(owner), encoded, T{});
    }

    template <borrowed_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded, T target)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        return decode_with_owner(std::move(owner), std::string_view(std::ranges::data(encoded), std::ranges::size(encoded)),
                            std::move(target));
    }

    CBOR_ALWAYS_INLINE static std::expected<std::string, error> encode(T const &value)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        reflection::second_item<T> second;
        second.record_add(value);
        auto const size = reflection::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(validity::writer_error(size.error()));
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::string out;
        out.resize_and_overwrite(padded, [&](char *const p, std::size_t const n) {
            return reflection::encoded_write<false>(std::span<char>(p, n), value, second);
        });
        return out;
    }

    template <class Target>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        reflection::second_item<T> second;
        second.record_add(value);
        auto const size = reflection::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(validity::writer_error(size.error()));
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        CBOR_ASSUME(padded >= fixed_size());
        if constexpr (std::same_as<U, std::string>) {
            std::size_t const at = target.size();
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(validity::writer_error(total.error()));
            target.resize_and_overwrite(*total, [&](char *const p, std::size_t const n) {
                return at + reflection::encoded_write<false>(std::span<char>(p, n).subspan(at), value, second);
            });
            return *size;
        } else if constexpr (encoding::byte_container<U> && requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(validity::writer_error(total.error()));
            target.reserve(*total);
            target.insert(target.end(), padded, char{});
            reflection::encoded_write<false>(std::span<char>(target).subspan(at), value, second);
            target.resize(at + *size);
            return *size;
        } else if constexpr (!encoding::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (out.size() >= padded) {
                reflection::encoded_write<false>(out.first(padded), value, second);
                return *size;
            }
        }
        decltype(auto) message = encoding::message_of(target, *size);
        if constexpr (requires { std::span<char>(message); }) {
            std::span<char> const out(message);
            if (out.size() < *size) [[unlikely]]
                return std::unexpected(error::no_buffer_space);
            reflection::encoded_write<true>(out.first(*size), value, second);
            if (auto const r = message.done(*size); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            return *size;
        } else {
            std::string encoded(padded, '\0');
            encoded.resize(reflection::encoded_write<false>(std::span<char>(encoded), value, second));
            if (auto const r = message.append(encoded); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            return encoded.size();
        }
    }

private:
    static std::expected<accessor<>, error> path_with_owner(std::shared_ptr<void const> owner, std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_empty(
            owner, "cbor::packed::path: the owner of the encoded data item is empty");
        auto const dir = reflection::directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        auto const root = std::span<char const>(encoded).subspan(dir->root).template first<fixed_size()>();
        if (!reflection::record_head_valid<T, T>(root)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return accessor<>(std::move(owner), encoded, root, *dir, reflection::record_block_at<T>(0));
    }

    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<void const> owner,
                                                     std::string_view const encoded, T target)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::packed::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
        try {
#endif
            if (auto const r = reflection::root_read<T>(target, encoded, limits::nesting_depth); !r)
                [[unlikely]]
                return std::unexpected(r.error());
            return ref<T>(std::move(owner), std::move(target));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }
};
#endif

}

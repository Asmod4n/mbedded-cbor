#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <version>
#if __has_include(<spanstream>)
#include <spanstream>
#endif
#if __has_include(<flat_set>)
#include <flat_set>
#endif
#ifdef __cpp_impl_reflection
#include <meta>
#elif __has_include(<boost/pfr.hpp>)
#include <boost/pfr.hpp>
#endif

#include "item_size.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "lazy.hpp"
#include "owning_ref.hpp"
#include "packed.hpp"
#include "shared.hpp"

namespace cbor
{

class pfr
{
    template <class U>
    static consteval bool struct_readable()
    {
        if constexpr (has_describe_members<U>) {
            return true;
#ifdef __cpp_impl_reflection
        } else if constexpr (std::is_aggregate_v<U>) {
            static_assert(std::meta::bases_of(^^U, std::meta::access_context::unprivileged()).empty(),
                          "cbor::structs reads a class with a base class only through CBOR_DESCRIBE_STRUCT.");
            static_assert(reflection::annotations_absent<U>(),
                          "cbor::structs reads an aggregate with no CBOR_DESCRIBE_STRUCT by its member "
                          "names; it takes no annotation.");
            return true;
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        } else if constexpr (std::is_aggregate_v<U>) {
            return true;
#else
        } else if constexpr (std::is_aggregate_v<U>) {
            static_assert(false, "cbor::structs reads an aggregate with no CBOR_DESCRIBE_STRUCT through "
                                 "reflection or Boost.PFR; this build has neither.");
#endif
        } else {
            static_assert(
                false,
                "cbor::structs reads a class that is not an aggregate only through CBOR_DESCRIBE_STRUCT.");
        }
    }

    template <class U>
    static consteval std::size_t members_count()
    {
        if constexpr (has_describe_members<U>)
            return std::tuple_size_v<std::remove_const_t<decltype(describe_members<U>::members)>>;
#ifdef __cpp_impl_reflection
        else
            return reflection::members_of<U>().size();
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        else
            return boost::pfr::tuple_size_v<U>;
#endif
    }

    template <class U>
    static constexpr std::size_t tuple_size_v = members_count<U>();

    template <std::size_t I, class U>
    static constexpr auto &get(U &value)
    {
        using V = std::remove_const_t<U>;
        if constexpr (has_describe_members<V>) {
            return value.*std::get<I>(describe_members<V>::members).pointer;
        } else {
#ifdef __cpp_impl_reflection
            static constexpr auto members = reflection::members_of<V>();
            return value.[:members[I]:];
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
            return boost::pfr::get<I>(value);
#endif
        }
    }

    template <std::size_t I, class U>
    static consteval map_key key_of()
    {
        if constexpr (has_describe_members<U>)
            return std::get<I>(describe_members<U>::members).key;
#ifdef __cpp_impl_reflection
        else
            return map_key(reflection::key_of(reflection::members_of<U>()[I]));
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        else
            return map_key(boost::pfr::get_name<I, U>());
#endif
    }

    template <class U>
    static consteval std::optional<std::uint64_t> tag_of()
    {
        if constexpr (has_describe_members<U>)
            if (describe_members<U>::tag.has_value())
                return describe_members<U>::tag->number;
        return std::nullopt;
    }

    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
#ifdef __cpp_impl_reflection
        return reflection::uses_allocator_aggregate<E, Alloc>();
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        if constexpr (std::uses_allocator_v<E, Alloc> || !std::is_class_v<E> || !std::is_aggregate_v<E> ||
                      has_describe_members<E> || std::ranges::range<E>) {
            return false;
        } else {
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return ((!std::is_array_v<boost::pfr::tuple_element_t<I, E>> &&
                         !std::is_reference_v<boost::pfr::tuple_element_t<I, E>>) &&
                        ...) &&
                       (false || ... ||
                        (std::uses_allocator_v<boost::pfr::tuple_element_t<I, E>, Alloc> ||
                         uses_allocator_aggregate<boost::pfr::tuple_element_t<I, E>, Alloc>()));
            }(std::make_index_sequence<boost::pfr::tuple_size_v<E>>());
        }
#else
        return false;
#endif
    }

    template <class E, class Alloc>
    static constexpr E element_make(Alloc const &alloc, E &&from)
    {
#ifdef __cpp_impl_reflection
        return reflection::element_make<E>(alloc, std::move(from));
#else
        if constexpr (uses_allocator_aggregate<E, Alloc>()) {
#if defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return E{element_make<boost::pfr::tuple_element_t<I, E>>(
                    alloc, std::move(boost::pfr::get<I>(from)))...};
            }(std::make_index_sequence<boost::pfr::tuple_size_v<E>>());
#endif
        } else {
            return std::make_obj_using_allocator<E>(alloc, std::move(from));
        }
#endif
    }

    template <class E>
    static consteval void check_default_constructible()
    {
        static_assert(
            std::is_default_constructible_v<E>,
            "cbor::structs decodes into a default-constructed object; this type has no default constructor.");
    }

    template <class E, class Container>
    static constexpr E element_make(Container const &container)
    {
        check_default_constructible<E>();
        if constexpr (requires { container.get_allocator(); }) {
            if constexpr (uses_allocator_aggregate<E, decltype(container.get_allocator())>())
                return element_make<E>(container.get_allocator(), E{});
            else
                return std::make_obj_using_allocator<E>(container.get_allocator());
        } else {
            return E{};
        }
    }

    friend class generic;

    template <class>
    friend class structs;
};

class generic
{
    template <class>
    friend class structs;

    template <class U>
    static constexpr bool is_std_tuple = false;

    template <class... E>
    static constexpr bool is_std_tuple<std::tuple<E...>> = true;

    template <class U>
    static constexpr bool is_std_array = false;

    template <class E, std::size_t N>
    static constexpr bool is_std_array<std::array<E, N>> = true;

    template <class U>
    static constexpr bool is_multimap = reflection::is_map<U> && requires(U &c, typename U::value_type const &v) {
        { c.insert(v) } -> std::same_as<typename U::iterator>;
    };

    template <class U>
    static auto key_groups(U const &value)
    {
        return value | std::views::chunk_by([&value](auto const &a, auto const &b) {
                   if constexpr (requires { value.key_eq(); })
                       return value.key_eq()(a.first, b.first);
                   else
                       return !value.key_comp()(a.first, b.first);
               });
    }

    template <class U>
    static constexpr bool is_tagged_item = false;

    template <std::uint64_t N, class E>
    static constexpr bool is_tagged_item<tagged_item<N, E>> = true;

    template <class U>
    static constexpr bool is_std_variant = false;

    template <class... E>
    static constexpr bool is_std_variant<std::variant<E...>> = true;

    template <class U>
    static constexpr bool is_std_vector = false;

    template <class E, class A>
    static constexpr bool is_std_vector<std::vector<E, A>> = true;

    template <class U>
    static constexpr bool is_sys_time = false;

    template <class D>
        requires std::ratio_less_equal_v<typename D::period, std::ratio<1>>
    static constexpr bool is_sys_time<std::chrono::sys_time<D>> = true;

    template <class U>
    static constexpr bool is_set = false;

    template <class K, class C, class A>
    static constexpr bool is_set<std::set<K, C, A>> = true;

#ifdef __cpp_lib_flat_set
    template <class K, class C, class KC>
    static constexpr bool is_set<std::flat_set<K, C, KC>> = true;
#endif

    template <class E>
    static constexpr bool is_typed_array_element =
        std::same_as<E, std::byte> || std::same_as<E, float> || std::same_as<E, double> ||
        reflection::is_typed_array_integer<E>;

    template <class U>
    static constexpr bool is_typed_array = requires {
        typename U::value_type;
        requires is_std_vector<U> || is_std_array<U>;
        requires is_typed_array_element<typename U::value_type>;
    };

    template <class U>
    static constexpr std::uint64_t tag_of_standard_type()
    {
        if constexpr (is_sys_time<U>)
            return std::to_underlying(rfc8949::tag_number::epoch_based_date_time);
        else if constexpr (std::same_as<U, std::chrono::year_month_day>)
            return std::to_underlying(rfc8943::tag_number::days_since_epoch_date);
        else if constexpr (is_set<U>)
            return std::to_underlying(cbor_sets::tag_number::set);
        else
            return reflection::typed_array_tag<typename U::value_type>();
    }

    template <class U>
    static constexpr bool is_tagged_standard_type =
        is_sys_time<U> || std::same_as<U, std::chrono::year_month_day> || is_set<U> || is_typed_array<U>;

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    static constexpr std::array<char const *, 6> date_time_formats{"%FT%TZ", "%FT%Tz", "%FT%T%Ez",
                                                                   "%Ft%TZ", "%Ft%Tz", "%Ft%T%Ez"};

    static constexpr std::array<char const *, 1> full_date_formats{"%F"};
#endif

    template <class U>
    static constexpr bool standard_tag_accepted(std::uint64_t const tag)
    {
        if constexpr (is_sys_time<U>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag == std::to_underlying(rfc8949::tag_number::standard_date_time_string))
                return true;
#endif
            return std::same_as<typename U::duration, std::chrono::seconds> &&
                   tag == tag_of_standard_type<U>();
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag == std::to_underlying(rfc8943::tag_number::full_date_string))
                return true;
#endif
            return tag == tag_of_standard_type<U>();
        } else {
            return tag == tag_of_standard_type<U>();
        }
    }

    template <class U>
    static constexpr bool is_basic_string = requires {
        requires std::same_as<U, std::basic_string<char, std::char_traits<char>, typename U::allocator_type>>;
    };

    template <class U, class V, bool Checked>
    static std::expected<void, error> integer_read(heads::decoder<Checked> &d, V &out)
    {
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::unsigned_integer && h->major != major_type::negative_integer) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        bool const negative = h->major == major_type::negative_integer;
        if (error const c = validity::check_number_range<U>(negative, h->argument).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        U const magnitude = static_cast<U>(h->argument);
        out = static_cast<V>(negative ? static_cast<U>(~magnitude) : magnitude);
        return {};
    }

    template <class U>
    static bool head_accepted(heads::head const &h)
    {
        if constexpr (std::same_as<U, bool>)
            return heads::is_boolean(h);
        else if constexpr (std::same_as<U, std::nullptr_t>)
            return heads::is_null(h);
        else if constexpr (std::same_as<U, simple_value>)
            return h.major == major_type::simple_float &&
                   h.info <= std::to_underlying(rfc8949::simple_float_information::simple_value_follows);
        else if constexpr (std::is_floating_point_v<U>)
            return h.major == major_type::simple_float &&
                   h.info >= std::to_underlying(rfc8949::simple_float_information::half_precision_float) &&
                   h.info <= std::to_underlying(rfc8949::simple_float_information::double_precision_float);
        else if constexpr (reflection::is_wide_integer<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer ||
                   (h.major == major_type::tag &&
                    (h.argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
                     h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum)));
        else if constexpr (std::is_enum_v<U>)
            return head_accepted<std::underlying_type_t<U>>(h);
        else if constexpr (std::is_unsigned_v<U>)
            return h.major == major_type::unsigned_integer && h.argument <= std::numeric_limits<U>::max();
        else if constexpr (std::is_integral_v<U>)
            return (h.major == major_type::unsigned_integer || h.major == major_type::negative_integer) &&
                   h.argument <= static_cast<std::uint64_t>(std::numeric_limits<U>::max());
        else if constexpr (is_tagged_standard_type<U>)
            return h.major == major_type::tag && standard_tag_accepted<U>(h.argument);
        else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>)
            return h.major == major_type::text_string;
        else if constexpr (std::same_as<U, std::span<std::byte const>>)
            return h.major == major_type::byte_string;
        else if constexpr (reflection::is_optional<U>)
            return heads::is_null(h) || head_accepted<typename U::value_type>(h);
        else if constexpr (is_tagged_item<U>)
            return h.major == major_type::tag && h.argument == U::number;
        else if constexpr (is_std_variant<U>)
            return false;
        else if constexpr (is_std_tuple<U> || is_std_array<U>)
            return h.major == major_type::array && h.argument == std::tuple_size_v<U>;
        else if constexpr (reflection::is_map<U>)
            return h.major == major_type::map;
        else if constexpr (requires(U const &c) { typename U::value_type; c.size(); })
            return h.major == major_type::array;
        else if constexpr (pfr::tag_of<U>().has_value())
            return h.major == major_type::tag && h.argument == *pfr::tag_of<U>();
        else
            return h.major == major_type::map;
    }

#ifdef __SIZEOF_INT128__
    template <class U, bool Checked>
    static std::expected<void, error> wide_integer_read(heads::decoder<Checked> &d, U &out)
    {
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool negative = h->major == major_type::negative_integer;
        uint128 magnitude = h->argument;
        if (h->major == major_type::tag) {
            if (h->argument != std::to_underlying(rfc8949::tag_number::unsigned_bignum) &&
                h->argument != std::to_underlying(rfc8949::tag_number::negative_bignum)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            negative = h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
            auto const b = d.head_decode();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (error const c = validity::check_tag_content(h->argument, b->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.byte_string_decode(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::string_view const digits = heads::magnitude_without_leading_zeros(*bytes);
            if (error const c =
                    validity::check_magnitude_size(digits.size(), sizeof(uint128)).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            magnitude = 0;
            for (char const c : digits)
                magnitude =
                    magnitude << std::numeric_limits<std::uint8_t>::digits | static_cast<std::uint8_t>(c);
        } else if (h->major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if (error const c = validity::check_number_range<U>(negative, magnitude).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        U const value = static_cast<U>(magnitude);
        out = negative ? static_cast<U>(~value) : value;
        return {};
    }
#endif

    template <class U, std::size_t I = 0, bool Checked>
    static std::expected<void, error> variant_read(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                   heads::head const &h, std::size_t const depth,
                                                   std::size_t const depth_max)
    {
        if constexpr (I == std::variant_size_v<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            using A = std::variant_alternative_t<I, U>;
            if (head_accepted<A>(h))
                return generic_value_read(d, out.template emplace<I>(), depth, depth_max);
            return variant_read<U, I + 1>(d, out, h, depth, depth_max);
        }
    }

    template <class U, bool Checked>
    static std::expected<void, error> generic_read(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                   std::size_t const depth, std::size_t const depth_max)
    {
        using slot = typename value_sharing::sharing_decoder<Checked>::slot;
        std::size_t const first_mark = d.slots.size();
        for (;;) {
            heads::decoder<Checked> look = d;
            auto const h = look.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                d.encoded = look.encoded;
                d.slots.push_back(slot::open);
                continue;
            }
            if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref))
                break;
            auto const n = look.head_decode();
            if (!n) [[unlikely]]
                return std::unexpected(n.error());
            if (error const e = validity::check_tag_content(h->argument, n->major).error_or(error{});
                e != error{}) [[unlikely]]
                return std::unexpected(e);
            auto const index = value_sharing::check_sharedref_index(n->argument, d.slots.size());
            if (!index) [[unlikely]]
                return std::unexpected(index.error());
            if (d.slots[std::to_underlying(*index)] == slot::in_skipped_member) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            return std::unexpected(error::incorrect_type);
        }
        if (auto const r = generic_value_read(d, out, depth + (d.slots.size() - first_mark), depth_max); !r)
            [[unlikely]]
            return r;
        std::ranges::fill(std::span(d.slots).subspan(first_mark, d.slots.size() - first_mark), slot::value_member);
        return {};
    }

    template <class U, bool Checked>
    static std::expected<void, error> generic_value_read(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                         std::size_t const depth, std::size_t const depth_max)
    {
        if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        if constexpr (std::same_as<U, bool>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!heads::is_boolean(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = h->info == std::to_underlying(simple_value::true_value);
            return {};
        } else if constexpr (std::same_as<U, simple_value>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!head_accepted<U>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (error const r = validity::check_simple_value(h->info, h->argument).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            out = static_cast<simple_value>(h->argument);
            return {};
        } else if constexpr (reflection::is_wide_integer<U>) {
            return wide_integer_read(d, out);
        } else if constexpr (is_std_variant<U>) {
            heads::decoder<Checked> probe = d;
            auto const h = probe.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            return variant_read(d, out, *h, depth, depth_max);
        } else if constexpr (std::is_enum_v<U>) {
            static_assert(reflection::has_fixed_underlying_type<U>,
                          "cbor::structs reads an enum only with a fixed underlying type, because every value of that type is a value of the enum.");
            return integer_read<std::underlying_type_t<U>>(d, out);
        } else if constexpr (std::is_integral_v<U>) {
            return integer_read<U>(d, out);
        } else if constexpr (std::is_floating_point_v<U>) {
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::structs reads and writes float and double only.");
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!head_accepted<U>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if constexpr (std::same_as<U, double>) {
                out = heads::float_decode(h->info, h->argument);
            } else if (h->info ==
                       std::to_underlying(rfc8949::simple_float_information::half_precision_float)) {
                out = heads::float_decode_binary16(static_cast<std::uint16_t>(h->argument));
            } else if (h->info ==
                       std::to_underlying(rfc8949::simple_float_information::single_precision_float)) {
                out = std::bit_cast<float>(static_cast<std::uint32_t>(h->argument));
            } else {
                double const value = std::bit_cast<double>(h->argument);
                constexpr int narrow =
                    heads::double_precision.significand_bits - heads::single_precision.significand_bits;
                if (heads::is_nan(heads::double_precision, h->argument)
                        ? (h->argument & ((std::uint64_t{1} << narrow) - 1u)) != 0
                        : heads::preferred_float_info(value) ==
                              rfc8949::simple_float_information::double_precision_float) [[unlikely]]
                    return std::unexpected(error::number_out_of_range);
                out = std::bit_cast<float>(static_cast<std::uint32_t>(
                    heads::float_encode(rfc8949::simple_float_information::single_precision_float, value)));
            }
            return {};
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!heads::is_null(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return {};
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.byte_string_decode(h->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            if constexpr (is_basic_string<U>)
                out.assign(*text);
            else
                out = *text;
            return {};
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.byte_string_decode(h->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            out = std::as_bytes(std::span(*bytes));
            return {};
        } else if constexpr (is_tagged_standard_type<U>) {
            auto const t = d.head_decode();
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (!head_accepted<U>(*t)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return tag_content_read(d, out, *t, depth, depth_max);
        } else if constexpr (reflection::is_optional<U>) {
            if (!d.encoded.empty() &&
                d.encoded.front() == heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null))) {
                d.encoded.remove_prefix(1);
                out.reset();
                return {};
            }
            return generic_read(d, out.emplace(), depth, depth_max);
        } else if constexpr (is_tagged_item<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag || h->argument != U::number) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return generic_read(d, out.content, depth + 1, depth_max);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array || h->argument != std::tuple_size_v<U>) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> r;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((r = r.and_then([&] { return generic_read(d, std::get<I>(out), depth + 1, depth_max); })),
                 ...);
            }(std::make_index_sequence<std::tuple_size_v<U>>());
            return r;
        } else if constexpr (is_multimap<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = pfr::element_make<typename U::key_type>(out);
                if (auto const r = generic_read(d, key, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                if (auto const r = validity::check_key_unique(out.contains(key)); !r) [[unlikely]]
                    return r;
                auto const values = d.head_decode();
                if (!values) [[unlikely]]
                    return std::unexpected(values.error());
                if (values->major != major_type::array || values->argument == 0) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                for (std::uint64_t k = 0; k < values->argument; ++k) {
                    auto value = pfr::element_make<typename U::mapped_type>(out);
                    if (auto const r = generic_read(d, value, depth + 2, depth_max); !r) [[unlikely]]
                        return r;
                    out.emplace(key, std::move(value));
                }
            }
            return {};
        } else if constexpr (reflection::is_map<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = pfr::element_make<typename U::key_type>(out);
                if (auto const r = generic_read(d, key, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                auto value = pfr::element_make<typename U::mapped_type>(out);
                if (auto const r = generic_read(d, value, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                if constexpr (requires { out.try_emplace(std::move(key), std::move(value)); }) {
                    if (auto const r = validity::check_key_unique(
                            !out.try_emplace(std::move(key), std::move(value)).second);
                        !r) [[unlikely]]
                        return r;
                } else
                    out.emplace(std::move(key), std::move(value));
            }
            return {};
        } else if constexpr (requires {
                                 typename U::value_type;
                                 out.size();
                             }) {
            using E = typename U::value_type;
            pfr::check_default_constructible<E>();
            static_assert(
                requires(E &&e) { out.insert(out.end(), std::move(e)); },
                "cbor::structs decodes a container with insert(end(), value); this container has none.");
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                if constexpr (requires {
                                  { out.emplace_back() } -> std::same_as<E &>;
                                  requires !pfr::uses_allocator_aggregate<E, decltype(out.get_allocator())>();
                              }) {
                    if (auto const r = generic_read(d, out.emplace_back(), depth + 1, depth_max); !r)
                        [[unlikely]]
                        return r;
                } else {
                    E element = pfr::element_make<E>(out);
                    if (auto const r = generic_read(d, element, depth + 1, depth_max); !r) [[unlikely]]
                        return r;
                    out.insert(out.end(), std::move(element));
                }
            }
            return {};
        } else {
            return struct_read(d, out, depth, depth_max);
        }
    }

    template <class U, bool Checked>
    static std::expected<void, error> epoch_read(value_sharing::sharing_decoder<Checked> &d, U &out)
    {
        using rep = std::chrono::seconds::rep;
        heads::decoder<Checked> look = d;
        auto const h = look.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        rep seconds = 0;
        if (h->major == major_type::simple_float) {
            if (!head_accepted<double>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            d.encoded = look.encoded;
            auto const whole = validity::check_whole_number<rep>(heads::float_decode(h->info, h->argument));
            if (!whole) [[unlikely]]
                return std::unexpected(whole.error());
            seconds = *whole;
        } else if (auto const r = integer_read<rep>(d, seconds); !r) [[unlikely]] {
            return r;
        }
        out = U{std::chrono::seconds{seconds}};
        return {};
    }

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    template <class U, bool Checked, std::size_t N>
    static std::expected<void, error> text_parse(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                 std::array<char const *, N> const &formats)
    {
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const text = d.byte_string_decode(h->argument);
        if (!text) [[unlikely]]
            return std::unexpected(text.error());
        for (char const *const format : formats) {
            U value{};
            std::ispanstream in(*text);
            in >> std::chrono::parse(format, value);
            if (!in.fail() && in.peek() == std::ispanstream::traits_type::eof()) {
                out = value;
                return {};
            }
        }
        return std::unexpected(error::incorrect_type);
    }
#endif

    template <class U, bool Checked>
    static std::expected<void, error> tag_content_read(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                       heads::head const &tag, std::size_t const depth,
                                                       std::size_t const depth_max)
    {
        if constexpr (is_sys_time<U>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8949::tag_number::standard_date_time_string))
                return text_parse(d, out, date_time_formats);
#endif
            if constexpr (std::same_as<typename U::duration, std::chrono::seconds>)
                return epoch_read(d, out);
            else
                return std::unexpected(error::incorrect_type);
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8943::tag_number::full_date_string))
                return text_parse(d, out, full_date_formats);
#endif
            std::chrono::days::rep days = 0;
            if (auto const r = integer_read<std::chrono::days::rep>(d, days); !r) [[unlikely]]
                return r;
            std::chrono::year_month_day const date{std::chrono::sys_days{std::chrono::days{days}}};
            if (!date.ok()) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            out = date;
            return {};
        } else if constexpr (is_set<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = pfr::element_make<typename U::value_type>(out);
                if (auto const r = generic_read(d, element, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                if (auto const r = validity::check_key_unique(!out.insert(std::move(element)).second); !r)
                    [[unlikely]]
                    return r;
            }
            return {};
        } else {
            using E = typename U::value_type;
            auto const b = d.head_decode();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (error const c = validity::check_tag_content(tag.argument, b->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.byte_string_decode(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            if (auto const r = validity::typed_array_check(tag.argument, bytes->size()); !r) [[unlikely]]
                return r;
            std::size_t const count = bytes->size() / sizeof(E);
            if constexpr (is_std_array<U>) {
                if (count != out.size()) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            } else {
                out.resize(count);
            }
            std::span<char const> const elements(*bytes);
            for (std::size_t i = 0; i < count; ++i)
                out[i] = reflection::typed_array_element_read<E>(
                    elements.subspan(i * sizeof(E)).template first<sizeof(E)>());
            return {};
        }
    }

    template <class U, std::size_t I>
    static bool key_matches(heads::head const &k, std::string_view const text)
    {
        constexpr map_key key = pfr::key_of<I, U>();
        return k.major == key.major && k.argument == key.argument &&
               (key.major != major_type::text_string || text == key.text);
    }

    template <class U, bool Checked>
    static std::expected<void, error> struct_read(value_sharing::sharing_decoder<Checked> &d, U &out,
                                                  std::size_t const depth, std::size_t const depth_max)
    {
        static_assert(pfr::struct_readable<U>());
        constexpr std::size_t count = pfr::tuple_size_v<U>;
        if constexpr (constexpr std::optional<std::uint64_t> tag = pfr::tag_of<U>(); tag.has_value()) {
            auto const t = d.head_decode();
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (t->major != major_type::tag || t->argument != *tag) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        }
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        auto const member_read =
            [&]<std::size_t I>(std::integral_constant<std::size_t, I>) -> std::expected<void, error> {
            if (auto const unique = validity::check_key_unique(found.test(I)); !unique) [[unlikely]]
                return unique;
            found.set(I);
            return generic_read(d, pfr::get<I>(out), depth + 1, depth_max);
        };
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            auto k = d.head_decode();
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::string_view text;
            if (heads::is_bignum(k->major, k->argument)) [[unlikely]] {
                auto const read = d.bignum_decode(k->argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                if (*read)
                    k = heads::head{k->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                        ? major_type::negative_integer
                                        : major_type::unsigned_integer,
                                    heads::preferred_argument_info(**read), **read};
            } else if (k->major == major_type::text_string) {
                auto const t = d.byte_string_decode(k->argument);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                text = *t;
            } else if (k->major != major_type::unsigned_integer && k->major != major_type::negative_integer) {
                if (k->major == major_type::byte_string) {
                    if (auto const b = d.byte_string_decode(k->argument); !b) [[unlikely]]
                        return std::unexpected(b.error());
                } else if (k->major == major_type::array || k->major == major_type::map ||
                           k->major == major_type::tag) [[unlikely]] {
                    return std::unexpected(error::unsupported_value);
                } else if (error const c =
                               validity::check_simple_value(k->info, k->argument).error_or(error{});
                           c != error{}) [[unlikely]] {
                    return std::unexpected(c);
                }
            }
            std::expected<void, error> r;
            bool const matched = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (
                    false || ... ||
                    (key_matches<U, I>(*k, text) &&
                     (static_cast<void>(r = member_read(std::integral_constant<std::size_t, I>{})), true)));
            }(std::make_index_sequence<count>());
            if (!matched) {
                if (auto const s = well_formedness::item_skip(d, d); !s) [[unlikely]]
                    return s;
            } else if (!r) [[unlikely]] {
                return r;
            }
        }
        bool const missing = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (false || ... ||
                    (!reflection::is_optional<std::remove_cvref_t<decltype(pfr::get<I>(out))>> &&
                     !found.test(I)));
        }(std::make_index_sequence<count>());
        if (missing) [[unlikely]]
            return std::unexpected(error::key_not_found);
        return {};
    }

    template <class U>
    static consteval std::array<std::size_t, pfr::tuple_size_v<U>> key_order()
    {
        constexpr std::size_t count = pfr::tuple_size_v<U>;
        std::array<map_key, count> const keys = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<map_key, count>{pfr::key_of<I, U>()...};
        }(std::make_index_sequence<count>());
        std::array<std::size_t, count> order;
        std::ranges::iota(order, std::size_t{0});
        std::ranges::sort(order, [&](std::size_t const a, std::size_t const b) {
            return reflection::key_encoding_less(keys[a], keys[b]);
        });
        if (!validity::keys_unique(order,
                                   [&](std::size_t const i) { return reflection::key_encoding_of(keys[i]); }))
            std::unreachable();
        return order;
    }

    template <class U>
    static bool member_present(U const &value)
    {
        if constexpr (reflection::is_optional<U>)
            return value.has_value();
        else
            return true;
    }

    static std::size_t float_size(double const value)
    {
        switch (heads::preferred_float_info(value)) {
        case rfc8949::simple_float_information::half_precision_float:
            return heads::initial_byte_size + sizeof(std::uint16_t);
        case rfc8949::simple_float_information::single_precision_float:
            return heads::initial_byte_size + sizeof(std::uint32_t);
        default:
            return heads::initial_byte_size + sizeof(std::uint64_t);
        }
    }

    template <class U>
    static std::expected<std::size_t, std::errc> generic_size(U const &value)
    {
        if constexpr (std::same_as<U, bool> || std::same_as<U, std::nullptr_t>) {
            return heads::initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return heads::head_size(std::to_underlying(value));
#ifdef __SIZEOF_INT128__
        } else if constexpr (reflection::is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? ~static_cast<uint128>(value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return heads::head_size(static_cast<std::uint64_t>(magnitude));
            std::size_t const digits =
                sizeof(uint128) - static_cast<std::size_t>(std::countl_zero(magnitude)) /
                                      std::numeric_limits<std::uint8_t>::digits;
            return heads::initial_byte_size + heads::head_size(digits) + digits;
#endif
        } else if constexpr (is_std_variant<U>) {
            return std::visit([](auto const &e) { return generic_size(e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return generic_size(std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>)
                return heads::head_size(value < 0 ? static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value))
                                           : static_cast<std::uint64_t>(value));
            else
                return heads::head_size(value);
        } else if constexpr (std::is_floating_point_v<U>) {
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::structs reads and writes float and double only.");
            return float_size(static_cast<double>(value));
        } else if constexpr (is_tagged_standard_type<U>) {
            auto const content = tag_content_size(value);
            if (!content) [[unlikely]]
                return content;
            return validity::checked_add(heads::head_size(tag_of_standard_type<U>()), *content);
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view> ||
                             std::same_as<U, std::span<std::byte const>>) {
            return validity::checked_add(heads::head_size(value.size()), value.size());
        } else if constexpr (reflection::is_optional<U>) {
            if (!value)
                return heads::initial_byte_size;
            return generic_size(*value);
        } else if constexpr (is_tagged_item<U>) {
            auto const content = generic_size(value.content);
            if (!content) [[unlikely]]
                return content;
            return validity::checked_add(heads::head_size(U::number), *content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            std::expected<std::size_t, std::errc> size = heads::head_size(std::tuple_size_v<U>);
            std::apply(
                [&](auto const &...e) {
                    ((size = size.and_then([&](std::size_t const sum) {
                          return generic_size(e).and_then(
                              [sum](std::size_t const n) { return validity::checked_add(sum, n); });
                      })),
                     ...);
                },
                value);
            return size;
        } else if constexpr (is_multimap<U>) {
            auto const groups = key_groups(value);
            std::expected<std::size_t, std::errc> size = heads::head_size(std::ranges::distance(groups));
            for (auto const group : groups) {
                size = size.and_then([&](std::size_t const sum) {
                    return generic_size(group.front().first).and_then([&](std::size_t const n) {
                        return validity::checked_add(sum, n + heads::head_size(std::ranges::distance(group)));
                    });
                });
                for (auto const &[k, v] : group)
                    size = size.and_then([&v](std::size_t const sum) {
                        return generic_size(v).and_then([sum](std::size_t const n) { return validity::checked_add(sum, n); });
                    });
                if (!size) [[unlikely]]
                    return size;
            }
            return size;
        } else if constexpr (reflection::is_map<U>) {
            std::size_t size = heads::head_size(value.size());
            for (auto const &[k, v] : value) {
                auto const key = generic_size(k).and_then([size](std::size_t const n) { return validity::checked_add(size, n); });
                if (!key) [[unlikely]]
                    return key;
                auto const mapped = generic_size(v).and_then([&key](std::size_t const n) { return validity::checked_add(*key, n); });
                if (!mapped) [[unlikely]]
                    return mapped;
                size = *mapped;
            }
            return size;
        } else if constexpr (requires {
                                 value.size();
                                 typename U::value_type;
                             }) {
            return elements_size(value);
        } else {
            static_assert(pfr::struct_readable<U>());
            std::size_t present = 0;
            auto const member_size =
                [&]<std::size_t I>(std::integral_constant<std::size_t, I>,
                                   std::size_t const sum) -> std::expected<std::size_t, std::errc> {
                auto const &m = pfr::get<I>(value);
                if (!member_present(m))
                    return sum;
                ++present;
                constexpr map_key key = pfr::key_of<I, U>();
                auto const keyed =
                    validity::checked_add(sum, heads::head_size(key.argument) + key.text.size());
                if (!keyed) [[unlikely]]
                    return keyed;
                std::expected<std::size_t, std::errc> member;
                if constexpr (reflection::is_optional<std::remove_cvref_t<decltype(m)>>)
                    member = generic_size(*m);
                else
                    member = generic_size(m);
                return member.and_then(
                    [&keyed](std::size_t const n) { return validity::checked_add(*keyed, n); });
            };
            std::expected<std::size_t, std::errc> size = std::size_t{0};
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((size = size.and_then([&](std::size_t const sum) {
                     return member_size(std::integral_constant<std::size_t, I>{}, sum);
                 })),
                 ...);
            }(std::make_index_sequence<pfr::tuple_size_v<U>>());
            if (!size) [[unlikely]]
                return size;
            std::size_t head = heads::head_size(present);
            if constexpr (constexpr std::optional<std::uint64_t> tag = pfr::tag_of<U>(); tag.has_value())
                head += heads::head_size(*tag);
            return validity::checked_add(*size, head);
        }
    }

    template <class U>
    static std::expected<std::size_t, std::errc> elements_size(U const &value)
    {
        std::size_t size = heads::head_size(value.size());
        for (typename U::value_type const &e : value) {
            auto const sum = generic_size(e).and_then(
                [size](std::size_t const n) { return validity::checked_add(size, n); });
            if (!sum) [[unlikely]]
                return sum;
            size = *sum;
        }
        return size;
    }

    template <class U>
    static std::expected<std::size_t, std::errc> tag_content_size(U const &value)
    {
        if constexpr (is_sys_time<U>) {
            static_assert(std::same_as<typename U::duration, std::chrono::seconds>,
                          "cbor::structs writes tag 1 from std::chrono::sys_seconds only.");
            return generic_size(value.time_since_epoch().count());
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
            if (!value.ok()) [[unlikely]]
                validity::throw_logic_error("cbor::structs::encode: the year_month_day is no valid date");
            return generic_size(std::chrono::sys_days{value}.time_since_epoch().count());
        } else if constexpr (is_set<U>) {
            return elements_size(value);
        } else {
            auto const bytes = validity::checked_mul(value.size(), sizeof(typename U::value_type));
            if (!bytes) [[unlikely]]
                return bytes;
            return validity::checked_add(heads::head_size(*bytes), *bytes);
        }
    }

    static std::size_t bytes_write(std::span<char> const out, std::size_t const at, std::span<char const> const bytes)
    {
        std::ranges::copy(bytes, out.subspan(at, bytes.size()).begin());
        return at + bytes.size();
    }

    template <class U>
    static std::size_t generic_write(std::span<char> const out, std::size_t at, U const &value)
    {
        if constexpr (std::same_as<U, bool>) {
            out.subspan(at).front() =
                heads::initial_byte(major_type::simple_float, value ? std::to_underlying(simple_value::true_value)
                                                                    : std::to_underlying(simple_value::false_value));
            return at + heads::initial_byte_size;
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            out.subspan(at).front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null));
            return at + heads::initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return at + heads::head_write(out, at, major_type::simple_float, std::to_underlying(value));
#ifdef __SIZEOF_INT128__
        } else if constexpr (reflection::is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? ~static_cast<uint128>(value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return at + heads::head_write(out, at,
                                       negative ? major_type::negative_integer : major_type::unsigned_integer,
                                       static_cast<std::uint64_t>(magnitude));
            std::size_t const digits =
                sizeof(uint128) - static_cast<std::size_t>(std::countl_zero(magnitude)) /
                                      std::numeric_limits<std::uint8_t>::digits;
            at += heads::head_write(out, at, major_type::tag,
                                    std::to_underlying(negative ? rfc8949::tag_number::negative_bignum
                                                                : rfc8949::tag_number::unsigned_bignum));
            at += heads::head_write(out, at, major_type::byte_string, digits);
            auto const bytes = heads::big_endian(magnitude);
            return bytes_write(out, at, std::span<char const>(bytes).last(digits));
#endif
        } else if constexpr (is_std_variant<U>) {
            return std::visit([&](auto const &e) { return generic_write(out, at, e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return generic_write(out, at, std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>) {
                if (value < 0)
                    return at + heads::head_write(out, at, major_type::negative_integer,
                                           static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value)));
            }
            return at + heads::head_write(out, at, major_type::unsigned_integer, static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            double const d = static_cast<double>(value);
            rfc8949::simple_float_information const info = heads::preferred_float_info(d);
            return at + heads::head_write(out, at, major_type::simple_float, std::to_underlying(info), heads::float_encode(info, d));
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            at += heads::head_write(out, at, major_type::text_string, value.size());
            return bytes_write(out, at, value);
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            at += heads::head_write(out, at, major_type::byte_string, value.size());
            std::ranges::copy(value, std::as_writable_bytes(out.subspan(at, value.size())).begin());
            return at + value.size();
        } else if constexpr (is_tagged_standard_type<U>) {
            at += heads::head_write(out, at, major_type::tag, tag_of_standard_type<U>());
            return tag_content_write(out, at, value);
        } else if constexpr (reflection::is_optional<U>) {
            if (!value) {
                out.subspan(at).front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null));
                return at + heads::initial_byte_size;
            }
            return generic_write(out, at, *value);
        } else if constexpr (is_tagged_item<U>) {
            at += heads::head_write(out, at, major_type::tag, U::number);
            return generic_write(out, at, value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            at += heads::head_write(out, at, major_type::array, std::tuple_size_v<U>);
            std::apply([&](auto const &...e) { ((at = generic_write(out, at, e)), ...); }, value);
            return at;
        } else if constexpr (is_multimap<U>) {
            auto const groups = key_groups(value);
            at += heads::head_write(out, at, major_type::map, std::ranges::distance(groups));
            for (auto const group : groups) {
                at = generic_write(out, at, group.front().first);
                at += heads::head_write(out, at, major_type::array, std::ranges::distance(group));
                for (auto const &[k, v] : group)
                    at = generic_write(out, at, v);
            }
            return at;
        } else if constexpr (reflection::is_map<U>) {
            at += heads::head_write(out, at, major_type::map, value.size());
            for (auto const &[k, v] : value) {
                at = generic_write(out, at, k);
                at = generic_write(out, at, v);
            }
            return at;
        } else if constexpr (requires {
                                 value.size();
                                 typename U::value_type;
                             }) {
            return elements_write(out, at, value);
        } else {
            static_assert(pfr::struct_readable<U>());
            if constexpr (constexpr std::optional<std::uint64_t> tag = pfr::tag_of<U>(); tag.has_value())
                at += heads::head_write(out, at, major_type::tag, *tag);
            std::size_t const present = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (std::size_t{0} + ... +
                        (member_present(pfr::get<I>(value)) ? std::size_t{1} : std::size_t{0}));
            }(std::make_index_sequence<pfr::tuple_size_v<U>>());
            at += heads::head_write(out, at, major_type::map, present);
            static constexpr auto order = key_order<U>();
            [&]<std::size_t... J>(std::index_sequence<J...>) {
                ((at = member_write<order[J]>(out, at, value)), ...);
            }(std::make_index_sequence<order.size()>());
            return at;
        }
    }

    template <class U>
    static std::size_t elements_write(std::span<char> const out, std::size_t at, U const &value)
    {
        at += heads::head_write(out, at, major_type::array, value.size());
        for (typename U::value_type const &e : value)
            at = generic_write(out, at, e);
        return at;
    }

    template <class U>
    static std::size_t tag_content_write(std::span<char> const out, std::size_t at, U const &value)
    {
        if constexpr (is_sys_time<U>) {
            return generic_write(out, at, value.time_since_epoch().count());
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
            return generic_write(out, at, std::chrono::sys_days{value}.time_since_epoch().count());
        } else if constexpr (is_set<U>) {
            return elements_write(out, at, value);
        } else {
            using E = typename U::value_type;
            at += heads::head_write(out, at, major_type::byte_string, value.size() * sizeof(E));
            for (E const e : value) {
                reflection::typed_array_element_write<E>(out.subspan(at).template first<sizeof(E)>(), e);
                at += sizeof(E);
            }
            return at;
        }
    }

    template <std::size_t I, class U>
    static std::size_t member_write(std::span<char> const out, std::size_t at, U const &value)
    {
        auto const &m = pfr::get<I>(value);
        if (!member_present(m))
            return at;
        constexpr map_key key = pfr::key_of<I, U>();
        at += heads::head_write(out, at, key.major, key.argument);
        at = bytes_write(out, at, key.text);
        if constexpr (reflection::is_optional<std::remove_cvref_t<decltype(m)>>)
            return generic_write(out, at, *m);
        else
            return generic_write(out, at, m);
    }
};

template <class T>
class structs
{
public:
    static std::expected<owning_ref<T>, error> decode(std::string_view const encoded)
    {
#if defined(__cpp_exceptions)
        try {
#endif
            return decode(std::make_shared<std::string const>(encoded));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    template <std::same_as<std::string> Encoded>
    static std::expected<owning_ref<T>, error> decode(Encoded &&encoded)
    {
#if defined(__cpp_exceptions)
        try {
#endif
            return decode(std::make_shared<std::string const>(std::move(encoded)));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<std::string const> encoded)
    {
        pfr::check_default_constructible<T>();
        return decode(std::move(encoded), T{});
    }

    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<std::string const> encoded, T &&target)
    {
        validity::throw_logic_error_if_null(encoded, "cbor::structs::decode: the encoded data item is null");
        std::string_view const bytes = *encoded;
        return decode(std::shared_ptr<void const>(std::move(encoded)), bytes, std::move(target));
    }

    CBOR_ALWAYS_INLINE static std::expected<std::string, error> encode(T const &value)
    {
        auto const counted = generic::generic_size(value);
        if (!counted) [[unlikely]]
            return std::unexpected(validity::writer_error(counted.error()));
        auto const sum = validity::checked_add(*counted, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::string out;
        out.resize_and_overwrite(padded, [&](char *const p, std::size_t const n) {
            return generic::generic_write(std::span<char>(p, n), 0, value);
        });
        return out;
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        auto const counted = generic::generic_size(value);
        if (!counted) [[unlikely]]
            return std::unexpected(validity::writer_error(counted.error()));
        auto const sum = validity::checked_add(*counted, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::size_t const size = *counted;
        if constexpr (std::same_as<U, std::string>) {
            std::size_t const at = target.size();
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(validity::writer_error(total.error()));
            target.resize_and_overwrite(*total, [&](char *const p, std::size_t const n) {
                return generic::generic_write(std::span<char>(p, n), at, value);
            });
            return size;
        } else if constexpr (encoding::byte_container<U> &&
                             requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(validity::writer_error(total.error()));
            target.resize(*total);
            generic::generic_write(std::span<char>(target), at, value);
            target.resize(at + size);
            return size;
        } else if constexpr (!encoding::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (out.size() >= padded) {
                generic::generic_write(out.first(padded), 0, value);
                return size;
            }
        }
        std::string encoded(padded, '\0');
        encoded.resize(generic::generic_write(std::span<char>(encoded), 0, value));
        decltype(auto) message = encoding::message_of(target, encoded.size());
        if (auto const r = message.append(encoded); !r) [[unlikely]]
            return std::unexpected(validity::writer_error(r.error()));
        if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
            return std::unexpected(validity::writer_error(r.error()));
        return encoded.size();
    }

private:
    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<void const> owner,
                                                      std::string_view const encoded, T &&target)
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::structs::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
        try {
#endif
            auto value = validity::limits_apply(
                limits.load(),
                [&]<bool Checked>(std::size_t const depth_max, validity::limit_checks<Checked> const checks) {
                    return read(encoded, depth_max, std::move(target), checks);
                });
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            return owning_ref<T>(std::move(owner), std::move(*value));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    template <bool Checked>
    static std::expected<T, error> read(std::string_view const encoded, std::size_t const depth_max, T out,
                                        validity::limit_checks<Checked> const checks)
    {
        if (auto const r = validity::check_input_bytes(encoded.size(), checks); !r) [[unlikely]]
            return std::unexpected(r.error());
        value_sharing::sharing_decoder<Checked> d{{encoded, checks}, {}};
        if (auto const r = generic::generic_read(d, out, 0, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (!d.encoded.empty()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return out;
    }
};

}

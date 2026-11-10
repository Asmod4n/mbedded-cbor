/**
 * @file
 * Writes a C++ value as CBOR and reads it back into a value of the
 * same type. Writes a struct as a map with one key for each member
 * that holds a value. Finds the members by reflection, by Boost.PFR or
 * in describe_members. Writes std::chrono times and dates, sets and
 * arrays of numbers under their standard tags. Reads values marked as
 * shareable.
 * https://www.rfc-editor.org/rfc/rfc8949
 * https://www.rfc-editor.org/rfc/rfc8943#section-2.1
 * https://www.rfc-editor.org/rfc/rfc8746#section-2
 * https://github.com/input-output-hk/cbor-sets-spec/blob/master/CBOR_SETS.md
 * http://cbor.schmorp.de/value-sharing
 */
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
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
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
#if !defined(BOOST_PFR_CORE_NAME_ENABLED) || !BOOST_PFR_CORE_NAME_ENABLED
#error "cbor::structs needs Boost.PFR 1.84 or later with boost::pfr::get_name"
#endif
#endif

#include "item_size.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "ref.hpp"
#include "schema.hpp"
#include "shared.hpp"

namespace cbor
{

class pfr
{
    template <class U>
    static consteval bool struct_readable()
    {
        if constexpr (has_describe_members<U>) {
            return reflection::description_valid<U>();
#ifdef __cpp_impl_reflection
        } else if constexpr (std::is_aggregate_v<U>) {
            static_assert(std::meta::bases_of(^^U, std::meta::access_context::unprivileged()).empty(),
                          "cbor::structs reads a class with a base class only through cbor::describe_members.");
            static_assert(reflection::annotations_absent<U>(),
                          "cbor::structs reads an aggregate with no cbor::describe_members by its member "
                          "names; it takes no annotation.");
            return true;
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        } else if constexpr (std::is_aggregate_v<U>) {
            return true;
#else
        } else if constexpr (std::is_aggregate_v<U>) {
            static_assert(false, "cbor::structs reads an aggregate with no cbor::describe_members through "
                                 "reflection or Boost.PFR; this build has neither.");
#endif
        } else {
            static_assert(
                false,
                "cbor::structs reads a class that is not an aggregate only through cbor::describe_members.");
        }
    }

    template <class U>
    static consteval std::size_t members_count()
    {
        if constexpr (has_describe_members<U>)
            return describe::members_count<U>;
#ifdef __cpp_impl_reflection
        else
            return reflection::data_members<U>().size();
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
            return describe::get<I>(value);
        } else {
#ifdef __cpp_impl_reflection
            static constexpr auto members = reflection::data_members<V>();
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
            return describe::key_of<U, I>;
#ifdef __cpp_impl_reflection
        else
            return map_key(reflection::key_of(reflection::data_members<U>()[I]));
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        else
            return map_key(boost::pfr::get_name<I, U>());
#endif
    }

    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
#ifdef __cpp_impl_reflection
        return reflection::uses_allocator_aggregate<E, Alloc>();
#else
        if constexpr (has_describe_members<E>)
            return false;
        else
            return reflection::uses_allocator_aggregate<E, Alloc>();
#endif
    }

    template <class E, class Alloc>
    static constexpr E make_element(Alloc const &alloc, E &&from)
    {
        if constexpr (uses_allocator_aggregate<E, Alloc>())
            return reflection::make_element<E>(alloc, std::move(from));
        else
            return std::make_obj_using_allocator<E>(alloc, std::move(from));
    }

    template <class E>
    static consteval void check_default_constructible()
    {
        static_assert(
            std::is_default_constructible_v<E>,
            "cbor::structs decodes into a default-constructed object; this type has no default constructor.");
    }

    template <class E, class Container>
    static constexpr E make_element(Container const &container)
    {
        check_default_constructible<E>();
        if constexpr (requires { container.get_allocator(); }) {
            if constexpr (uses_allocator_aggregate<E, decltype(container.get_allocator())>())
                return make_element<E>(container.get_allocator(), E{});
            else
                return std::make_obj_using_allocator<E>(container.get_allocator());
        } else {
            return E{};
        }
    }

    friend class generic;

    template <class>
    friend class structs;

    template <class T>
    friend void detail::copy_views_into_zone(T &value, detail::string_saver &zone);
};

class generic
{
    template <class>
    friend class structs;

    template <class T>
    friend void detail::copy_views_into_zone(T &value, detail::string_saver &zone);

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

    static constexpr auto element_count = [](std::uint64_t const count, auto const &) { return count + 1; };

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
    static constexpr auto date_time_formats =
        std::to_array<char const *>({"%FT%TZ", "%FT%Tz", "%FT%T%Ez", "%Ft%TZ", "%Ft%Tz", "%Ft%T%Ez"});

    static constexpr auto full_date_formats = std::to_array<char const *>({"%F"});
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

    template <class U, class V>
    static std::expected<void, error> read_integer(heads::decoder<true> &d, V &out)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::unsigned_integer && h->major != major_type::negative_integer) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        bool const negative = h->major == major_type::negative_integer;
        if (error const c = detail::validity::check_number_range<U>(negative, h->argument).error_or(error{});
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
            return heads::is_simple_value(h);
        else if constexpr (std::is_floating_point_v<U>)
            return heads::is_floating_point(h.major, h.info);
        else if constexpr (reflection::is_wide_integer<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer ||
                   heads::is_bignum(h.major, h.argument);
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
        else if constexpr (detail::is_std_variant<U>)
            return false;
        else if constexpr (is_std_tuple<U> || is_std_array<U>)
            return h.major == major_type::array && h.argument == std::tuple_size_v<U>;
        else if constexpr (reflection::is_map<U>)
            return h.major == major_type::map;
        else if constexpr (requires(U const &c) { typename U::value_type; c.size(); })
            return h.major == major_type::array;
        else if constexpr (reflection::tag_number_of<U>().has_value())
            return h.major == major_type::tag && h.argument == *reflection::tag_number_of<U>();
        else
            return h.major == major_type::map;
    }

#ifdef CBOR_HAS_INT128
    template <class U>
    static std::expected<void, error> read_wide_integer(heads::decoder<true> &d, U &out)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool negative = h->major == major_type::negative_integer;
        uint128 magnitude = h->argument;
        if (h->major == major_type::tag) {
            if (h->argument != std::to_underlying(rfc8949::tag_number::unsigned_bignum) &&
                h->argument != std::to_underlying(rfc8949::tag_number::negative_bignum)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            negative = h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
            auto const b = d.decode_head();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (error const c = detail::validity::check_tag_content(h->argument, b->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.decode_string(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::span<char const> const digits = heads::magnitude_without_leading_zeros(std::span(*bytes));
            if (error const c =
                    detail::validity::check_magnitude_size(digits.size(), sizeof(uint128)).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            magnitude = 0;
            for (char const c : digits)
                magnitude =
                    magnitude << std::numeric_limits<std::uint8_t>::digits | static_cast<std::uint8_t>(c);
        } else if (h->major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if (error const c = detail::validity::check_number_range<U>(negative, magnitude).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        U const value = static_cast<U>(magnitude);
        out = negative ? static_cast<U>(~value) : value;
        return {};
    }
#endif

    template <class U, std::size_t I = 0>
    static std::expected<void, error> read_variant(value_sharing::sharing_decoder &d, U &out,
                                                   heads::head const &h)
    {
        if constexpr (I == std::variant_size_v<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            using A = std::variant_alternative_t<I, U>;
            if (head_accepted<A>(h))
                return read_generic_value(d, out.template emplace<I>());
            return read_variant<U, I + 1>(d, out, h);
        }
    }

    template <class U>
    static std::expected<void, error> read_generic(value_sharing::sharing_decoder &d, U &out)
    {
        using marked_value = value_sharing::sharing_decoder::marked_value;
        std::size_t const first_mark = d.marked_values.size();
        for (;;) {
            heads::decoder<true> look = d;
            auto const h = look.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                d.encoded = look.encoded;
                d.marked_values.push_back(marked_value::open);
                continue;
            }
            if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref))
                break;
            auto const n = look.decode_head();
            if (!n) [[unlikely]]
                return std::unexpected(n.error());
            if (error const e = detail::validity::check_tag_content(h->argument, n->major).error_or(error{});
                e != error{}) [[unlikely]]
                return std::unexpected(e);
            auto const index = value_sharing::check_sharedref_index(n->argument, d.marked_values.size());
            if (!index) [[unlikely]]
                return std::unexpected(index.error());
            if (d.marked_values[std::to_underlying(*index)] == marked_value::in_skipped_member) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            return std::unexpected(error::incorrect_type);
        }
        if (auto const r = read_generic_value(d, out); !r)
            [[unlikely]]
            return r;
        std::ranges::fill(std::span(d.marked_values).subspan(first_mark, d.marked_values.size() - first_mark), marked_value::value_member);
        return {};
    }

    template <class U>
    static std::expected<void, error> read_generic_value(value_sharing::sharing_decoder &d, U &out)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        if constexpr (std::same_as<U, bool>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!heads::is_boolean(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = h->info == std::to_underlying(simple_value::true_value);
            return {};
        } else if constexpr (std::same_as<U, simple_value>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!head_accepted<U>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = static_cast<simple_value>(h->argument);
            return {};
        } else if constexpr (reflection::is_wide_integer<U>) {
            return read_wide_integer(d, out);
        } else if constexpr (detail::is_std_variant<U>) {
            heads::decoder<true> probe = d;
            auto const h = probe.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            return read_variant(d, out, *h);
        } else if constexpr (std::is_enum_v<U>) {
            static_assert(reflection::has_fixed_underlying_type<U>,
                          "cbor::structs reads an enum only with a fixed underlying type, because every value of that type is a value of the enum.");
            return read_integer<std::underlying_type_t<U>>(d, out);
        } else if constexpr (std::is_integral_v<U>) {
            return read_integer<U>(d, out);
        } else if constexpr (std::is_floating_point_v<U>) {
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::structs reads and writes float and double only.");
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!head_accepted<U>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if constexpr (std::same_as<U, double>) {
                out = heads::decode_float(h->info, h->argument);
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
                    heads::encode_float(rfc8949::simple_float_information::single_precision_float, value)));
            }
            return {};
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!heads::is_null(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return {};
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.decode_string(h->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            if constexpr (is_basic_string<U>)
                out.assign(*text);
            else
                out = *text;
            return {};
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.decode_string(h->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            out = std::as_bytes(std::span(*bytes));
            return {};
        } else if constexpr (is_tagged_standard_type<U>) {
            auto const t = d.decode_head();
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (!head_accepted<U>(*t)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return read_tag_content(d, out, *t);
        } else if constexpr (reflection::is_optional<U>) {
            if (detail::validity::bytes_fit(heads::initial_byte_size, d.encoded.size()) &&
                d.encoded.front() ==
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null))) {
                d.encoded.remove_prefix(1);
                out.reset();
                return {};
            }
            return read_generic(d, out.emplace());
        } else if constexpr (is_tagged_item<U>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag || h->argument != U::number) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return read_generic(d, out.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array || h->argument != std::tuple_size_v<U>) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> r;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((r = r.and_then([&] { return read_generic(d, std::get<I>(out)); })),
                 ...);
            }(std::make_index_sequence<std::tuple_size_v<U>>());
            return r;
        } else if constexpr (is_multimap<U>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = pfr::make_element<typename U::key_type>(out);
                if (auto const r = read_generic(d, key); !r) [[unlikely]]
                    return r;
                if (auto const r = detail::validity::check_key_unique(out.contains(key)); !r) [[unlikely]]
                    return r;
                auto const values = d.decode_head();
                if (!values) [[unlikely]]
                    return std::unexpected(values.error());
                if (values->major != major_type::array || values->argument == 0) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if (auto const r = detail::validity::check_pending_items(values->major, values->info,
                                                                         values->argument, d.encoded.size());
                    !r) [[unlikely]]
                    return r;
                for (std::uint64_t k = 0; k < values->argument; ++k) {
                    auto value = pfr::make_element<typename U::mapped_type>(out);
                    if (auto const r = read_generic(d, value); !r) [[unlikely]]
                        return r;
                    out.emplace(key, std::move(value));
                }
            }
            return {};
        } else if constexpr (reflection::is_map<U>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = pfr::make_element<typename U::key_type>(out);
                if (auto const r = read_generic(d, key); !r) [[unlikely]]
                    return r;
                auto value = pfr::make_element<typename U::mapped_type>(out);
                if (auto const r = read_generic(d, value); !r) [[unlikely]]
                    return r;
                if constexpr (requires { out.try_emplace(std::move(key), std::move(value)); }) {
                    if (auto const r = detail::validity::check_key_unique(
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
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                if constexpr (requires {
                                  { out.emplace_back() } -> std::same_as<E &>;
                                  requires !pfr::uses_allocator_aggregate<E, decltype(out.get_allocator())>();
                              }) {
                    if (auto const r = read_generic(d, out.emplace_back()); !r)
                        [[unlikely]]
                        return r;
                } else {
                    E element = pfr::make_element<E>(out);
                    if (auto const r = read_generic(d, element); !r) [[unlikely]]
                        return r;
                    out.insert(out.end(), std::move(element));
                }
            }
            return {};
        } else {
            return read_struct(d, out);
        }
    }

    template <class U>
    static std::expected<void, error> read_epoch(value_sharing::sharing_decoder &d, U &out)
    {
        using rep = std::chrono::seconds::rep;
        heads::decoder<true> look = d;
        auto const h = look.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        rep seconds = 0;
        if (h->major == major_type::simple_float) {
            if (!head_accepted<double>(*h)) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            d.encoded = look.encoded;
            auto const whole =
                detail::validity::check_whole_number<rep>(heads::decode_float(h->info, h->argument));
            if (!whole) [[unlikely]]
                return std::unexpected(whole.error());
            seconds = *whole;
        } else if (h->major != major_type::unsigned_integer && h->major != major_type::negative_integer) [[unlikely]] {
            return std::unexpected(error::inadmissible_type_for_tag_content);
        } else if (auto const r = read_integer<rep>(d, seconds); !r) [[unlikely]] {
            return r;
        }
        out = U{std::chrono::seconds{seconds}};
        return {};
    }

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    template <class U, std::size_t N>
    static std::expected<void, error> parse_text(value_sharing::sharing_decoder &d, U &out,
                                                 std::array<char const *, N> const &formats)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const text = d.decode_string(h->argument);
        if (!text) [[unlikely]]
            return std::unexpected(text.error());
        for (char const *const format : formats) {
            U value{};
            std::ispanstream in(*text);
            in >> std::chrono::parse(format, value);
            bool valid = !in.fail() && in.peek() == std::ispanstream::traits_type::eof();
            if constexpr (std::same_as<U, std::chrono::year_month_day>)
                valid = valid && value.ok();
            if (valid) {
                out = value;
                return {};
            }
        }
        return std::unexpected(error::incorrect_type);
    }
#endif

    template <class U>
    static std::expected<void, error> read_tag_content(value_sharing::sharing_decoder &d, U &out,
                                                       heads::head const &tag)
    {
        if constexpr (is_sys_time<U>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8949::tag_number::standard_date_time_string))
                return parse_text(d, out, date_time_formats);
#endif
            if constexpr (std::same_as<typename U::duration, std::chrono::seconds>)
                return read_epoch(d, out);
            else
                return std::unexpected(error::incorrect_type);
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8943::tag_number::full_date_string))
                return parse_text(d, out, full_date_formats);
#endif
            std::chrono::days::rep days = 0;
            if (auto const r = read_integer<std::chrono::days::rep>(d, days); !r) [[unlikely]]
                return r;
            if (auto const r = detail::validity::check_day_count(days); !r) [[unlikely]]
                return r;
            out = std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{days}}};
            return {};
        } else if constexpr (is_set<U>) {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = pfr::make_element<typename U::value_type>(out);
                if (auto const r = read_generic(d, element); !r) [[unlikely]]
                    return r;
                if (auto const r = detail::validity::check_key_unique(!out.insert(std::move(element)).second);
                    !r) [[unlikely]]
                    return r;
            }
            return {};
        } else {
            using E = typename U::value_type;
            auto const b = d.decode_head();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (error const c = detail::validity::check_tag_content(tag.argument, b->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.decode_string(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            if (auto const r = detail::validity::check_typed_array(tag.argument, bytes->size()); !r)
                [[unlikely]]
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
                out[i] = reflection::read_typed_array_element<E>(
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

    template <class U>
    static std::expected<void, error> read_struct(value_sharing::sharing_decoder &d, U &out)
    {
        static_assert(pfr::struct_readable<U>());
        constexpr std::size_t count = pfr::tuple_size_v<U>;
        if constexpr (constexpr std::optional<std::uint64_t> tag = reflection::tag_number_of<U>(); tag.has_value()) {
            auto const t = d.decode_head();
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (t->major != major_type::tag || t->argument != *tag) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        }
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        auto const read_member =
            [&]<std::size_t I>(std::integral_constant<std::size_t, I>) -> std::expected<void, error> {
            if (auto const unique = detail::validity::check_key_unique(found.test(I)); !unique) [[unlikely]]
                return unique;
            found.set(I);
            return read_generic(d, pfr::get<I>(out));
        };
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            auto k = d.decode_head();
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::string_view text;
            if (heads::is_bignum(k->major, k->argument)) [[unlikely]] {
                auto const read = d.decode_bignum(k->argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                if (*read)
                    k = heads::head{k->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                        ? major_type::negative_integer
                                        : major_type::unsigned_integer,
                                    heads::preferred_argument_info(**read), **read};
            } else if (k->major == major_type::text_string) {
                auto const t = d.decode_string(k->argument);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                text = *t;
            } else if (k->major != major_type::unsigned_integer && k->major != major_type::negative_integer) {
                if (k->major == major_type::byte_string) {
                    if (auto const b = d.decode_string(k->argument); !b) [[unlikely]]
                        return std::unexpected(b.error());
                } else if (k->major == major_type::array || k->major == major_type::map ||
                           k->major == major_type::tag) [[unlikely]] {
                    if (k->major == major_type::tag &&
                        k->argument == std::to_underlying(rfc8949::tag_number::shareable))
                        d.mark();
                    std::uint64_t const items =
                        k->major == major_type::tag     ? 1
                        : k->major == major_type::array ? k->argument
                                                        : detail::validity::checked_mul(
                                                              k->argument, rfc8949::data_items_per_pair)
                                                              .value_or(std::numeric_limits<std::uint64_t>::max());
                    if (auto const p = detail::validity::check_pending_items(items, d.encoded.size()); !p)
                        [[unlikely]]
                        return std::unexpected(p.error());
                    for (std::uint64_t i = 0; i < items; ++i)
                        if (auto const e = well_formedness::skip_item(d, d); !e) [[unlikely]]
                            return e;
                }
            }
            std::expected<void, error> r;
            bool const matched = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (
                    false || ... ||
                    (key_matches<U, I>(*k, text) &&
                     (static_cast<void>(r = read_member(std::integral_constant<std::size_t, I>{})), true)));
            }(std::make_index_sequence<count>());
            if (!matched) {
                if (auto const s = well_formedness::skip_item(d, d); !s) [[unlikely]]
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
        std::array<std::size_t, count> const order = reflection::keys_sorted(keys);
        if (std::ranges::adjacent_find(order, {}, [&](std::size_t const i) {
                return reflection::key_encoding_of(keys[i]);
            }) != std::ranges::end(order))
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

    template <std::floating_point F>
    static std::size_t float_size(F const value)
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
    static std::expected<std::size_t, error> generic_size(U const &value)
    {
        if constexpr (!std::is_arithmetic_v<U> && !std::is_enum_v<U>)
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
        if constexpr (std::same_as<U, bool> || std::same_as<U, std::nullptr_t>) {
            return heads::initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return heads::head_size(std::to_underlying(value));
#ifdef CBOR_HAS_INT128
        } else if constexpr (reflection::is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? ~static_cast<uint128>(value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return heads::head_size(static_cast<std::uint64_t>(magnitude));
            std::size_t const digits =
                sizeof(uint128) - static_cast<std::size_t>(std::countl_zero(static_cast<std::uint64_t>(magnitude >> std::numeric_limits<std::uint64_t>::digits))) /
                                      std::numeric_limits<std::uint8_t>::digits;
            return heads::initial_byte_size + heads::head_size(digits) + digits;
#endif
        } else if constexpr (detail::is_std_variant<U>) {
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
            return float_size(value);
        } else if constexpr (is_tagged_standard_type<U>) {
            auto const content = tag_content_size(value);
            if (!content) [[unlikely]]
                return content;
            return detail::validity::checked_add(heads::head_size(tag_of_standard_type<U>()), *content)
                .transform_error(detail::validity::writer_error);
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view> ||
                             std::same_as<U, std::span<std::byte const>>) {
            return detail::validity::checked_add(heads::head_size(value.size()), value.size())
                .transform_error(detail::validity::writer_error);
        } else if constexpr (reflection::is_optional<U>) {
            if (!value)
                return heads::initial_byte_size;
            return generic_size(*value);
        } else if constexpr (is_tagged_item<U>) {
            auto const content = generic_size(value.content);
            if (!content) [[unlikely]]
                return content;
            return detail::validity::checked_add(heads::head_size(U::number), *content)
                .transform_error(detail::validity::writer_error);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            std::expected<std::size_t, error> size = heads::head_size(std::tuple_size_v<U>);
            std::apply(
                [&](auto const &...e) {
                    ((size = size.and_then([&](std::size_t const sum) {
                         return generic_size(e).and_then([sum](std::size_t const n) {
                             return detail::validity::checked_add(sum, n).transform_error(
                                 detail::validity::writer_error);
                         });
                     })),
                     ...);
                },
                value);
            return size;
        } else if constexpr (is_multimap<U>) {
            auto groups = key_groups(value);
            std::expected<std::size_t, error> size =
                heads::head_size(std::ranges::fold_left(groups, std::uint64_t{0}, element_count));
            for (auto const group : groups) {
                size = size.and_then([&](std::size_t const sum) {
                    return generic_size(group.front().first).and_then([&](std::size_t const n) {
                        return detail::validity::checked_add(sum, n)
                            .transform_error(detail::validity::writer_error)
                            .and_then([&group](std::size_t const total) {
                                return detail::validity::checked_add(
                                           total, heads::head_size(std::ranges::fold_left(
                                                      group, std::uint64_t{0}, element_count)))
                                    .transform_error(detail::validity::writer_error);
                            });
                    });
                });
                for (auto const &[k, v] : group)
                    size = size.and_then([&v](std::size_t const sum) {
                        return generic_size(v).and_then([sum](std::size_t const n) {
                            return detail::validity::checked_add(sum, n).transform_error(
                                detail::validity::writer_error);
                        });
                    });
                if (!size) [[unlikely]]
                    return size;
            }
            return size;
        } else if constexpr (reflection::is_map<U>) {
            std::size_t size = heads::head_size(value.size());
            for (auto const &[k, v] : value) {
                auto const key = generic_size(k).and_then([size](std::size_t const n) {
                    return detail::validity::checked_add(size, n).transform_error(
                        detail::validity::writer_error);
                });
                if (!key) [[unlikely]]
                    return key;
                auto const mapped = generic_size(v).and_then([&key](std::size_t const n) {
                    return detail::validity::checked_add(*key, n).transform_error(
                        detail::validity::writer_error);
                });
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
                                   std::size_t const sum) -> std::expected<std::size_t, error> {
                auto const &m = pfr::get<I>(value);
                if (!member_present(m))
                    return sum;
                ++present;
                constexpr map_key key = pfr::key_of<I, U>();
                auto const keyed =
                    detail::validity::checked_add(sum, heads::head_size(key.argument) + key.text.size())
                        .transform_error(detail::validity::writer_error);
                if (!keyed) [[unlikely]]
                    return keyed;
                std::expected<std::size_t, error> encoded_size;
                if constexpr (reflection::is_optional<std::remove_cvref_t<decltype(m)>>)
                    encoded_size = generic_size(*m);
                else
                    encoded_size = generic_size(m);
                return encoded_size.and_then([&keyed](std::size_t const n) {
                    return detail::validity::checked_add(*keyed, n).transform_error(
                        detail::validity::writer_error);
                });
            };
            std::expected<std::size_t, error> size = std::size_t{0};
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((size = size.and_then([&](std::size_t const sum) {
                     return member_size(std::integral_constant<std::size_t, I>{}, sum);
                 })),
                 ...);
            }(std::make_index_sequence<pfr::tuple_size_v<U>>());
            if (!size) [[unlikely]]
                return size;
            std::size_t head = heads::head_size(present);
            if constexpr (constexpr std::optional<std::uint64_t> tag = reflection::tag_number_of<U>(); tag.has_value())
                head += heads::head_size(*tag);
            return detail::validity::checked_add(*size, head).transform_error(detail::validity::writer_error);
        }
    }

    template <class U>
    static std::expected<std::size_t, error> elements_size(U const &value)
    {
        std::size_t size = heads::head_size(value.size());
        for (typename U::value_type const &e : value) {
            auto const sum = generic_size(e).and_then([size](std::size_t const n) {
                return detail::validity::checked_add(size, n).transform_error(detail::validity::writer_error);
            });
            if (!sum) [[unlikely]]
                return sum;
            size = *sum;
        }
        return size;
    }

    template <class U>
    static std::expected<std::size_t, error> tag_content_size(U const &value)
    {
        if constexpr (is_sys_time<U>) {
            static_assert(std::same_as<typename U::duration, std::chrono::seconds>,
                          "cbor::structs writes tag 1 from std::chrono::sys_seconds only.");
            return generic_size(value.time_since_epoch().count());
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
            if (!value.ok()) [[unlikely]]
                detail::validity::throw_logic_error(
                    "cbor::structs::encode: the year_month_day is no valid date");
            return generic_size(std::chrono::sys_days{value}.time_since_epoch().count());
        } else if constexpr (is_set<U>) {
            return elements_size(value);
        } else {
            auto const bytes = detail::validity::checked_mul(value.size(), sizeof(typename U::value_type))
                                   .transform_error(detail::validity::writer_error);
            if (!bytes) [[unlikely]]
                return bytes;
            return detail::validity::checked_add(heads::head_size(*bytes), *bytes)
                .transform_error(detail::validity::writer_error);
        }
    }

    static std::size_t write_bytes(std::span<char> const out, std::size_t const at, std::span<char const> const bytes)
    {
        std::ranges::copy(bytes, out.subspan(at, bytes.size()).begin());
        return at + bytes.size();
    }

    template <class U>
    static std::expected<std::size_t, error> write_generic(std::span<char> const out, std::size_t at, U const &value)
    {
        if constexpr (!std::is_arithmetic_v<U> && !std::is_enum_v<U> && !is_basic_string<U> &&
                      !std::same_as<U, std::string_view> && !std::same_as<U, std::span<std::byte const>>)
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
        if constexpr (std::same_as<U, bool>) {
            out.subspan(at).front() =
                heads::initial_byte(major_type::simple_float, value ? std::to_underlying(simple_value::true_value)
                                                                    : std::to_underlying(simple_value::false_value));
            return at + heads::initial_byte_size;
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            out.subspan(at).front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null));
            return at + heads::initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return at + heads::write_head(out, at, major_type::simple_float, std::to_underlying(value));
#ifdef CBOR_HAS_INT128
        } else if constexpr (reflection::is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? ~static_cast<uint128>(value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return at + heads::write_head(out, at,
                                       negative ? major_type::negative_integer : major_type::unsigned_integer,
                                       static_cast<std::uint64_t>(magnitude));
            std::size_t const digits =
                sizeof(uint128) - static_cast<std::size_t>(std::countl_zero(static_cast<std::uint64_t>(magnitude >> std::numeric_limits<std::uint64_t>::digits))) /
                                      std::numeric_limits<std::uint8_t>::digits;
            at += heads::write_head(out, at, major_type::tag,
                                    std::to_underlying(negative ? rfc8949::tag_number::negative_bignum
                                                                : rfc8949::tag_number::unsigned_bignum));
            at += heads::write_head(out, at, major_type::byte_string, digits);
            auto const bytes = heads::big_endian(magnitude);
            return write_bytes(out, at, std::span<char const>(bytes).last(digits));
#endif
        } else if constexpr (detail::is_std_variant<U>) {
            return std::visit([&](auto const &e) { return write_generic(out, at, e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return write_generic(out, at, std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>) {
                if (value < 0)
                    return at + heads::write_head(out, at, major_type::negative_integer,
                                           static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value)));
            }
            return at + heads::write_head(out, at, major_type::unsigned_integer, static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            rfc8949::simple_float_information const info = heads::preferred_float_info(value);
            return at + heads::write_head(out, at, major_type::simple_float, std::to_underlying(info), heads::encode_float(info, value));
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            at += heads::write_head(out, at, major_type::text_string, value.size());
            return write_bytes(out, at, value);
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            at += heads::write_head(out, at, major_type::byte_string, value.size());
            std::ranges::copy(value, std::as_writable_bytes(out.subspan(at, value.size())).begin());
            return at + value.size();
        } else if constexpr (is_tagged_standard_type<U>) {
            at += heads::write_head(out, at, major_type::tag, tag_of_standard_type<U>());
            return write_tag_content(out, at, value);
        } else if constexpr (reflection::is_optional<U>) {
            if (!value) {
                out.subspan(at).front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null));
                return at + heads::initial_byte_size;
            }
            return write_generic(out, at, *value);
        } else if constexpr (is_tagged_item<U>) {
            at += heads::write_head(out, at, major_type::tag, U::number);
            return write_generic(out, at, value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            at += heads::write_head(out, at, major_type::array, std::tuple_size_v<U>);
            std::expected<std::size_t, error> written = at;
            std::apply([&](auto const &...e) {
                ((written = written.and_then([&](std::size_t const w) { return write_generic(out, w, e); })), ...);
            }, value);
            return written;
        } else if constexpr (is_multimap<U>) {
            auto groups = key_groups(value);
            at += heads::write_head(out, at, major_type::map,
                                    std::ranges::fold_left(groups, std::uint64_t{0}, element_count));
            for (auto const group : groups) {
                auto const key = write_generic(out, at, group.front().first);
                if (!key) [[unlikely]]
                    return key;
                at = *key;
                at += heads::write_head(out, at, major_type::array,
                                        std::ranges::fold_left(group, std::uint64_t{0}, element_count));
                for (auto const &[k, v] : group) {
                    auto const mapped = write_generic(out, at, v);
                    if (!mapped) [[unlikely]]
                        return mapped;
                    at = *mapped;
                }
            }
            return at;
        } else if constexpr (reflection::is_map<U>) {
            at += heads::write_head(out, at, major_type::map, value.size());
            for (auto const &[k, v] : value) {
                auto const pair = write_generic(out, at, k).and_then(
                    [&](std::size_t const w) { return write_generic(out, w, v); });
                if (!pair) [[unlikely]]
                    return pair;
                at = *pair;
            }
            return at;
        } else if constexpr (requires {
                                 value.size();
                                 typename U::value_type;
                             }) {
            return write_elements(out, at, value);
        } else {
            static_assert(pfr::struct_readable<U>());
            if constexpr (constexpr std::optional<std::uint64_t> tag = reflection::tag_number_of<U>(); tag.has_value())
                at += heads::write_head(out, at, major_type::tag, *tag);
            std::size_t const present = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (std::size_t{0} + ... +
                        (member_present(pfr::get<I>(value)) ? std::size_t{1} : std::size_t{0}));
            }(std::make_index_sequence<pfr::tuple_size_v<U>>());
            at += heads::write_head(out, at, major_type::map, present);
            static constexpr auto order = key_order<U>();
            std::expected<std::size_t, error> written = at;
            [&]<std::size_t... J>(std::index_sequence<J...>) {
                ((written = written.and_then([&](std::size_t const w) { return write_member<order[J]>(out, w, value); })), ...);
            }(std::make_index_sequence<order.size()>());
            return written;
        }
    }

    template <class U>
    static std::expected<std::size_t, error> write_elements(std::span<char> const out, std::size_t at, U const &value)
    {
        at += heads::write_head(out, at, major_type::array, value.size());
        for (typename U::value_type const &e : value) {
            auto const element = write_generic(out, at, e);
            if (!element) [[unlikely]]
                return element;
            at = *element;
        }
        return at;
    }

    template <class U>
    static std::expected<std::size_t, error> write_tag_content(std::span<char> const out, std::size_t at,
                                                               U const &value)
    {
        if constexpr (is_sys_time<U>) {
            return write_generic(out, at, value.time_since_epoch().count());
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
            return write_generic(out, at, std::chrono::sys_days{value}.time_since_epoch().count());
        } else if constexpr (is_set<U>) {
            return write_elements(out, at, value);
        } else {
            using E = typename U::value_type;
            at += heads::write_head(out, at, major_type::byte_string, value.size() * sizeof(E));
            for (E const e : value) {
                reflection::write_typed_array_element<E>(out.subspan(at).template first<sizeof(E)>(), e);
                at += sizeof(E);
            }
            return at;
        }
    }

    template <std::size_t I, class U>
    static std::expected<std::size_t, error> write_member(std::span<char> const out, std::size_t at, U const &value)
    {
        auto const &m = pfr::get<I>(value);
        if (!member_present(m))
            return at;
        constexpr map_key key = pfr::key_of<I, U>();
        at += heads::write_head(out, at, key.major, key.argument);
        at = write_bytes(out, at, key.text);
        if constexpr (reflection::is_optional<std::remove_cvref_t<decltype(m)>>)
            return write_generic(out, at, *m);
        else
            return write_generic(out, at, m);
    }
};

/**
 * @brief Writes a C++ value as CBOR and reads it back.
 *
 * `T`, and each member of a struct in `T`, is one of the types in this table.
 * `encode` writes the item of the right column.
 * `decode` takes only that item, under its own tag.
 *
 * | C++ type | CBOR item |
 * |---|---|
 * | `bool`, `std::nullptr_t`, `cbor::simple_value` | `true` or `false`, `null`, a simple value |
 * | an integer of 8 to 64 bits; `__int128` | an integer; a bignum (tag 2, 3) |
 * | an enum with a fixed underlying type | the integer |
 * | `float`, `double` | the shortest float that holds the value |
 * | `std::basic_string<char>`, `std::string_view` | a text string |
 * | `std::span<std::byte const>` | a byte string |
 * | `std::vector`, `std::array` of `std::byte`, an integer, `float`, `double` | an RFC 8746 typed array |
 * | `std::set`, `std::flat_set` | tag 258 on an array |
 * | `std::chrono::sys_seconds` | tag 1 on an integer; `decode` also takes a float whose value is a whole number |
 * | `std::chrono::year_month_day` | tag 100 on an integer of days |
 * | `std::optional<E>` | the item of E, or `null`; no key when empty |
 * | `std::variant<E...>` | the item of the alternative |
 * | `std::tuple<E...>`, `std::array<E, N>` | an array of that count |
 * | a map type; a multimap type | a map; a map to arrays |
 * | another container | an array |
 * | `cbor::tagged_item<N, E>` | tag N on the item of E |
 * | a struct | a map |
 *
 * `decode` gives `incorrect_type` for another item: a `std::vector<int>` takes a typed array, not an array.
 * An enum with no fixed underlying type, a `long double` and a struct under `tagged_item` do not compile.
 *
 * @code{.callseq}
 * cbor::structs<T>::encode(value)              → std::expected<std::string, cbor::error>
 * cbor::structs<T>::decode(std::move(encoded)) → std::expected<cbor::ref<T>, cbor::error>
 * @endcode
 * @code{.example}
 * message: {"id": 4}
 * call: auto const r = cbor::structs<reading>::encode(reading{4, std::nullopt})
 * result: 5 bytes: a1 62 69 64 04
 * call: cbor::lazy::adopt(*r)
 * result: lazy of {"id": 4}
 * call: auto const s = cbor::structs<std::set<std::uint8_t>>::encode(std::set<std::uint8_t>{3, 1})
 * result: 6 bytes: d9 01 02 82 01 03
 * call: cbor::lazy::adopt(*s)
 * result: lazy of 258([1, 3])
 * call: cbor::structs<reading>::decode(std::move(bytes))
 * result: ref to reading{4, std::nullopt}
 * @endcode
 *
 * @snippet doc_reference_structs.cpp structs
 * @see cbor::structs::encode()
 * @see cbor::structs::decode()
 * @see cbor::describe_members
 * @see cbor::ref
 *
 * @ingroup structs
 */
template <class T>
class structs
{
public:
    /**
     * @brief Reads one item into a `T`.
     *
     * The result is a cbor::ref that holds the owner of the bytes, so a `std::string_view` in `T` stays valid.
     * No form copies the bytes.
     * The first form moves an rvalue into a new owner; an lvalue does not compile.
     * `owner` keeps the bytes of a borrowed `encoded` alive.
     * A form with `target` reads into the containers of `target`, with their allocator.
     * The other forms start from `T{}`.
     * A byte after the item is not read.
     *
     * @code{.callseq}
     * cbor::structs<T>::decode(std::move(encoded))                → std::expected<cbor::ref<T>, cbor::error>
     * cbor::structs<T>::decode(owner)                             → std::expected<cbor::ref<T>, cbor::error>
     * cbor::structs<T>::decode(owner, std::move(target))          → std::expected<cbor::ref<T>, cbor::error>
     * cbor::structs<T>::decode(owner, encoded)                    → std::expected<cbor::ref<T>, cbor::error>
     * cbor::structs<T>::decode(owner, encoded, std::move(target)) → std::expected<cbor::ref<T>, cbor::error>
     * @endcode
     * @code{.example}
     * message: {"name": "ann", "age": 30}
     * call: auto const p = cbor::structs<person>::decode(std::move(bytes))
     * result: ref to person{"ann", 30}
     * call: (*p)->age
     * result: 30
     * call: (*p)->name
     * result: "ann"
     * @endcode
     *
     * @retval incorrect_type an item of another type, or a missing or other tag
     * @retval number_out_of_range a number that the type cannot hold
     * @retval key_not_found no key for a member that is not `std::optional`
     * @retval duplicate_key a key that comes twice
     * @retval too_little_data the input ends inside the item
     * @retval syntax_error a head is not well-formed
     * @retval indefinite_length a head uses indefinite length
     * @retval nesting_depth_exceeded the item is deeper than the stack of the thread holds
     * @retval inadmissible_type_for_tag_content a tag has content that its definition does not allow
     * @retval sharedref_index_not_marked, sharedref_not_complete a tag 29 that names no
     * shared value that the decode holds
     * @retval string_length_exceeded a string is longer than `limits::string_length`
     * @retval container_elements_exceeded the count of an array or a map is greater than `limits::container_elements`
     * @retval not_enough_memory a refused allocation
     * @snippet doc_reference_structs.cpp decode
     * @see cbor::structs::encode()
     * @see cbor::ref
     */
    template <owning_range_of_chars Encoded>
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

    /**
     * @brief Reads one item into a `T` from the bytes that @p encoded owns.
     * @throws std::logic_error @p encoded is null
     */
    template <owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded)
    {
        pfr::check_default_constructible<T>();
        return decode(std::move(encoded), T{});
    }

    /**
     * @brief Reads one item into @p target from the bytes that @p encoded owns.
     * @param target the value whose containers and allocator the read uses
     * @throws std::logic_error @p encoded is null
     */
    template <owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded, T &&target)
    {
        detail::validity::throw_logic_error_if_null(encoded,
                                                    "cbor::structs::decode: the encoded data item is null");
        std::string_view const bytes{std::span<char const>(*encoded)};
        return decode(std::shared_ptr<void const>(std::move(encoded)), bytes, std::move(target));
    }

    /**
     * @brief Reads one item into a `T` from bytes that @p owner keeps alive.
     * @throws std::logic_error @p owner is empty
     */
    template <borrowed_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        pfr::check_default_constructible<T>();
        return decode(std::move(owner), encoded, T{});
    }

    /**
     * @brief Reads one item into @p target from bytes that @p owner keeps alive.
     * @param target the value whose containers and allocator the read uses
     * @throws std::logic_error @p owner is empty
     */
    template <borrowed_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded,
                                               T &&target)
    {
        return decode_with_owner(std::move(owner), std::string_view(std::span<char const>(encoded)),
                            std::move(target));
    }

    /**
     * @brief Writes @p value as one CBOR item in preferred serialization, into a new `std::string`.
     *
     * A struct is a map, and its keys come in the order of RFC 8949 4.2.1.
     * The second form appends to `target` and returns the count of bytes that it appended.
     * `target` is a `std::string`, a container of `char`, a `std::span<char>` or a writer.
     * The function writes no shared value (tag 28, tag 29).
     * A type that `structs` does not take does not compile.
     *
     * @code{.callseq}
     * cbor::structs<T>::encode(value)         → std::expected<std::string, cbor::error>
     * cbor::structs<T>::encode(value, target) → std::expected<std::size_t, cbor::error>
     * @endcode
     * @code{.example}
     * message: {"name": "ann", "age": 30}
     * call: auto const encoded = cbor::structs<person>::encode(person{"ann", 30})
     * result: 16 bytes: a2 63 61 67 65 18 1e 64 6e 61 6d 65 63 61 6e 6e
     * call: cbor::lazy::adopt(*encoded)
     * result: lazy of {"age": 30, "name": "ann"}
     * @endcode
     *
     * @retval value_too_large the size of the bytes does not fit in `std::size_t`
     * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
     * @throws std::bad_alloc a refused allocation of the result
     * @snippet doc_reference_structs.cpp encode
     * @see cbor::structs::decode()
     */
    CBOR_ALWAYS_INLINE static std::expected<std::string, error> encode(T const &value)
    {
        auto const counted = generic::generic_size(value);
        if (!counted) [[unlikely]]
            return std::unexpected(counted.error());
        auto const sum = detail::validity::checked_add(*counted, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::string out(padded, '\0');
        auto const written = generic::write_generic(std::span<char>(out), 0, value);
        if (!written) [[unlikely]]
            return std::unexpected(written.error());
        out.resize(*written);
        return out;
    }

    /**
     * @brief Appends @p value as one CBOR item in preferred serialization to @p target.
     * @param target a `std::string`, a container of `char`, a `std::span<char>` or a writer
     * @return the count of bytes that it appended
     * @retval no_buffer_space a `std::span<char>` that is too small for the bytes, or the writer gives this error
     * @retval value_too_large the size of the bytes does not fit in `std::size_t`, or the writer gives this error
     * @retval not_enough_memory, io_error the writer gives this error
     * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
     */
    template <class Target>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        auto const counted = generic::generic_size(value);
        if (!counted) [[unlikely]]
            return std::unexpected(counted.error());
        auto const sum = detail::validity::checked_add(*counted, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::size_t const size = *counted;
        if constexpr (encoding::byte_container<U> &&
                             requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            auto const total = detail::validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(detail::validity::writer_error(total.error()));
            target.resize(*total);
            auto const written = generic::write_generic(std::span<char>(target), at, value);
            target.resize(written ? at + size : at);
            if (!written) [[unlikely]]
                return std::unexpected(written.error());
            return size;
        } else if constexpr (!encoding::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (detail::validity::bytes_fit(padded, out.size())) {
                auto const written = generic::write_generic(out.first(padded), 0, value);
                if (!written) [[unlikely]]
                    return std::unexpected(written.error());
                return size;
            }
        }
        std::string encoded(padded, '\0');
        auto const written = generic::write_generic(std::span<char>(encoded), 0, value);
        if (!written) [[unlikely]]
            return std::unexpected(written.error());
        encoded.resize(*written);
        decltype(auto) message = encoding::message_of(target, encoded.size());
        if (auto const r = message.append(encoded); !r) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(r.error()));
        if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(r.error()));
        return encoded.size();
    }

private:
    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<void const> owner,
                                                     std::string_view const encoded, T &&target)
    {
        detail::validity::throw_logic_error_if_empty(
            owner, "cbor::structs::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
        try {
#endif
            auto value = read(encoded, std::move(target));
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            return ref<T>(std::move(owner), std::move(*value));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    static std::expected<T, error> read(std::string_view const encoded, T out)
    {
        value_sharing::sharing_decoder d{{encoded}, {}};
        if (auto const r = generic::read_generic(d, out); !r) [[unlikely]]
            return std::unexpected(r.error());
        return out;
    }
};


namespace detail
{

template <class T>
void copy_views_into_zone(T &value, string_saver &zone)
{
    if constexpr (std::is_same_v<T, std::string_view>) {
        value = zone.save(value, detail::validity::string_saver_chunk_size);
    } else if constexpr (std::is_same_v<T, std::span<std::byte const>>) {
        value = zone.save(value, detail::validity::string_saver_chunk_size);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        detail::copy_views_into_zone(value.bytes, zone);
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || generic::is_sys_time<T> ||
                         std::is_same_v<T, std::nullptr_t> ||
                         requires { typename T::traits_type; typename T::allocator_type; }) {
    } else if constexpr (requires { typename T::value_type; value.has_value(); *value; }) {
        if (value)
            detail::copy_views_into_zone(*value, zone);
    } else if constexpr (detail::is_std_variant<T>) {
        std::visit([&zone](auto &alternative) { detail::copy_views_into_zone(alternative, zone); }, value);
    } else if constexpr (generic::is_std_tuple<T>) {
        std::apply([&zone](auto &...element) { (detail::copy_views_into_zone(element, zone), ...); }, value);
    } else if constexpr (generic::is_tagged_item<T>) {
        detail::copy_views_into_zone(value.content, zone);
    } else if constexpr (generic::is_std_vector<T> || generic::is_std_array<T>) {
        for (auto &element : value)
            detail::copy_views_into_zone(element, zone);
    } else if constexpr (requires { typename T::key_type; typename T::mapped_type; }) {
        T copied(value.get_allocator());
        for (auto const &[key, mapped] : value) {
            typename T::key_type k = key;
            typename T::mapped_type m = mapped;
            detail::copy_views_into_zone(k, zone);
            detail::copy_views_into_zone(m, zone);
            copied.emplace(std::move(k), std::move(m));
        }
        value = std::move(copied);
    } else if constexpr (requires { typename T::key_type; }) {
        T copied(value.get_allocator());
        for (auto const &element : value) {
            typename T::key_type k = element;
            detail::copy_views_into_zone(k, zone);
            copied.insert(std::move(k));
        }
        value = std::move(copied);
    } else {
        static_assert(std::is_class_v<T> && std::is_aggregate_v<T>,
                      "cbor::ref::clone takes a type whose views it knows how to copy.");
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (detail::copy_views_into_zone(pfr::get<I>(value), zone), ...);
        }(std::make_index_sequence<pfr::members_count<T>()>{});
    }
}

}

}

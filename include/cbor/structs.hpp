/**
 * @file
 * Writes a C++ value as CBOR with cbor::encode and reads it back into a
 * value of the same type with cbor::structs. Writes a struct as a map
 * with one key for each member that holds a value, from its
 * describe_members. Reads the members by reflection, by Boost.PFR or
 * from describe_members. Writes std::chrono times and dates, sets and
 * arrays of numbers under their standard tags.
 * https://www.rfc-editor.org/rfc/rfc8949
 * https://www.rfc-editor.org/rfc/rfc8943#section-2.1
 * https://www.rfc-editor.org/rfc/rfc8746#section-2
 * https://github.com/input-output-hk/cbor-sets-spec/blob/master/CBOR_SETS.md
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
#include <deque>
#include <expected>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
#include "rfc9581.hpp"
#include "diagnostic_notation.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "ref.hpp"
#include "result.hpp"
#include "schema.hpp"
#include "shared.hpp"

namespace cbor
{

namespace detail
{

class pfr
{
    template <class U>
    static consteval bool struct_readable()
    {
        if constexpr (has_describe_members<U>) {
            return detail::reflection::description_valid<U>();
#ifdef __cpp_impl_reflection
        } else if constexpr (std::is_aggregate_v<U>) {
            static_assert(std::meta::bases_of(^^U, std::meta::access_context::unprivileged()).empty(),
                          "cbor::structs reads a class with a base class only through cbor::describe_members.");
            static_assert(detail::reflection::annotations_absent<U>(),
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

    template <class U, bool Reflected>
    static consteval bool struct_writable()
    {
        static_assert(Reflected || has_describe_members<U>,
                      "cbor::encode writes a class only through a cbor::describe_members specialization, also where "
                      "reflection or Boost.PFR could read its members: the description states the keys and the tag "
                      "that the bytes carry.");
        if constexpr (has_describe_members<U>)
            return detail::reflection::description_valid<U>();
        else
            return true;
    }

    template <class U>
    static consteval std::size_t members_count()
    {
        if constexpr (has_describe_members<U>)
            return describe::members_count<U>;
#ifdef __cpp_impl_reflection
        else
            return detail::reflection::data_members<U>().size();
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
            static constexpr auto members = detail::reflection::data_members<V>();
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
            return map_key(detail::reflection::key_of(detail::reflection::data_members<U>()[I]));
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
        else
            return map_key(boost::pfr::get_name<I, U>());
#endif
    }

    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
        return detail::reflection::uses_allocator_aggregate<E, Alloc>();
    }

    template <class E, class Alloc>
    static constexpr E make_element(Alloc const &alloc, E &&from)
    {
        if constexpr (uses_allocator_aggregate<E, Alloc>())
            return detail::reflection::make_element<E>(alloc, std::move(from));
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

    template <bool>
    friend class detail::basic_generic;

    template <class>
    friend class cbor::structs;

    template <class T, class Kept>
    friend void detail::copy_views_into_zone(T &value, detail::string_saver &zone, Kept &kept);
};

}

namespace detail
{

template <bool Reflected>
class basic_generic
{
    template <class>
    friend class cbor::structs;

    template <class T, class Kept>
    friend void detail::copy_views_into_zone(T &value, detail::string_saver &zone, Kept &kept);

    template <class U>
    static constexpr bool is_std_tuple = false;

    template <class... E>
    static constexpr bool is_std_tuple<std::tuple<E...>> = true;

    template <class U>
    static constexpr bool is_std_array = false;

    template <class E, std::size_t N>
    static constexpr bool is_std_array<std::array<E, N>> = true;

    template <class U>
    static constexpr bool is_multimap = detail::reflection::is_map<U> && requires(U &c, typename U::value_type const &v) {
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
    static constexpr bool is_std_pair = false;

    template <class F, class S>
    static constexpr bool is_std_pair<std::pair<F, S>> = true;

    template <class U>
    static constexpr bool is_std_vector = false;

    template <class E, class A>
    static constexpr bool is_std_vector<std::vector<E, A>> = true;

    template <class U>
    static constexpr bool is_smart_pointer = false;

    template <class E, class D>
    static constexpr bool is_smart_pointer<std::unique_ptr<E, D>> = !std::is_array_v<E>;

    template <class E>
    static constexpr bool is_smart_pointer<std::shared_ptr<E>> = !std::is_array_v<E>;

    template <class U>
    static constexpr bool is_shared_ptr = false;

    template <class E>
    static constexpr bool is_shared_ptr<std::shared_ptr<E>> = true;

    template <class U>
    static constexpr bool is_std_deque = false;

    template <class E, class A>
    static constexpr bool is_std_deque<std::deque<E, A>> = true;

    template <class U>
    static constexpr bool is_std_list = false;

    template <class E, class A>
    static constexpr bool is_std_list<std::list<E, A>> = true;

    template <class U>
    static constexpr bool is_object_pointer =
        (std::is_pointer_v<U> && std::is_object_v<std::remove_pointer_t<U>>) || is_smart_pointer<U>;

    template <class U>
    using pointee_t = std::remove_cv_t<typename std::pointer_traits<U>::element_type>;

    template <auto matches, class U, class... Seen>
    static consteval bool any_reached()
    {
        using V = std::remove_cv_t<U>;
        if constexpr ((std::same_as<V, Seen> || ...)) {
            return false;
        } else if constexpr (matches.template operator()<V>()) {
            return true;
        } else if constexpr (is_object_pointer<V>) {
            return any_reached<matches, pointee_t<V>, Seen...>();
        } else if constexpr (has_describe_value<V>) {
            using W = std::remove_cvref_t<decltype(describe_members<V>::value(std::declval<V const &>()))>;
            return any_reached<matches, W, V, Seen...>();
        } else if constexpr (detail::reflection::is_optional<V>) {
            return any_reached<matches, typename V::value_type, V, Seen...>();
        } else if constexpr (detail::is_std_variant<V>) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (any_reached<matches, std::variant_alternative_t<I, V>, V, Seen...>() || ...);
            }(std::make_index_sequence<std::variant_size_v<V>>());
        } else if constexpr (is_tagged_item<V>) {
            return any_reached<matches, decltype(std::declval<V &>().content), V, Seen...>();
        } else if constexpr (is_std_tuple<V> || is_std_pair<V>) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (any_reached<matches, std::tuple_element_t<I, V>, V, Seen...>() || ...);
            }(std::make_index_sequence<std::tuple_size_v<V>>());
        } else if constexpr (is_basic_string<V> || is_string_view<V>) {
            return false;
        } else if constexpr (std::ranges::range<V>) {
            return any_reached<matches, std::ranges::range_value_t<V>, V, Seen...>();
        } else if constexpr (std::is_class_v<V> &&
                             (has_describe_members<V> || (Reflected && std::is_aggregate_v<V>))) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (any_reached<matches,
                                    std::remove_cvref_t<decltype(detail::pfr::get<I>(std::declval<V &>()))>, V,
                                    Seen...>() ||
                        ...);
            }(std::make_index_sequence<detail::pfr::tuple_size_v<V>>());
        } else {
            return false;
        }
    }

    template <class U>
    static constexpr bool is_string_view = false;

    template <class C, class Traits>
    static constexpr bool is_string_view<std::basic_string_view<C, Traits>> = true;

    template <class R>
    static constexpr bool holds_pointer = any_reached<[]<class V>() { return is_object_pointer<V>; }, R>();

    struct slot {
        void *object;
        void const *type;
        bool complete;
    };

    struct kept_targets {
        kept_objects &kept;
    };

    struct shared_values {
        kept_objects &kept;
        std::vector<slot> slots;
        bool in_place;
    };

    template <class Objects>
    struct object_decoder : detail::heads::decoder<true> {
        Objects &objects;
    };

    template <class D>
    static constexpr bool reads_shared_values = requires(D &d) { d.objects.slots; };

    template <class R, class U>
    static constexpr bool tracked = any_reached<[]<class V>() {
        if constexpr (is_object_pointer<V>)
            return std::same_as<pointee_t<V>, std::remove_cv_t<U>>;
        else
            return false;
    }, R>();

    template <class U>
    static constexpr char class_id = 0;

    using identity = std::pair<void const *, void const *>;

    template <class U>
    static identity identity_of(U const &object)
    {
        return {std::addressof(object), &class_id<U>};
    }

    struct address_hash {
        std::size_t operator()(identity const &object) const noexcept
        {
            return std::hash<void const *>{}(object.first);
        }
    };

    using identities = std::unordered_set<identity, address_hash>;

    template <class R>
    struct back_edge_search {
        identities on_path;
        identities targets;

        template <class U>
        static constexpr bool tracks = tracked<R, U>;
    };

    template <class R>
    struct cycle_marks {
        identities targets;
        std::unordered_map<identity, std::uint64_t, address_hash> open;
        std::uint64_t marked;

        template <class U>
        static constexpr bool tracks = tracked<R, U>;
    };

    template <class U>
    static constexpr bool is_sys_time = false;

    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_sys_time<std::chrono::sys_time<D>> = true;

    template <class U>
    static constexpr bool is_duration = false;

    template <class R, class P>
        requires std::signed_integral<R> || std::floating_point<R>
    static constexpr bool is_duration<std::chrono::duration<R, P>> = true;

    template <class U>
    static constexpr bool is_whole_seconds = false;

    template <class R>
        requires std::signed_integral<R> && (sizeof(R) <= sizeof(std::int64_t))
    static constexpr bool is_whole_seconds<std::chrono::sys_time<std::chrono::duration<R>>> = true;

    template <class U>
    static constexpr bool is_tai_time = false;

    template <class U>
    static constexpr bool is_gps_time = false;

    template <class U>
    static constexpr bool is_utc_time = false;

    template <class U>
    static constexpr bool is_file_time = false;

    template <class U>
    static constexpr bool is_zoned_time = false;

    template <class U>
    static constexpr bool is_local_time = false;

#if __cpp_lib_chrono >= 201907L
    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_tai_time<std::chrono::tai_time<D>> = true;

    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_gps_time<std::chrono::gps_time<D>> = true;

    template <class D>
    static constexpr bool is_local_time<std::chrono::local_time<D>> = true;
#endif

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_utc_time<std::chrono::utc_time<D>> = true;

    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_file_time<std::chrono::file_time<D>> = true;

    template <class D>
        requires std::signed_integral<typename D::rep> || std::floating_point<typename D::rep>
    static constexpr bool is_zoned_time<std::chrono::zoned_time<D, std::chrono::time_zone const *>> = true;
#endif

    template <class U>
    static constexpr bool is_clock_time =
        is_tai_time<U> || is_gps_time<U> || is_utc_time<U> || is_file_time<U> || is_zoned_time<U>;

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
        detail::reflection::is_typed_array_integer<E>;

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
        else if constexpr (is_clock_time<U>)
            return std::to_underlying(rfc9581::tag_number::extended_time);
        else if constexpr (is_duration<U>)
            return std::to_underlying(rfc9581::tag_number::duration);
        else if constexpr (std::same_as<U, std::chrono::year_month_day>)
            return std::to_underlying(rfc8943::tag_number::days_since_epoch_date);
        else if constexpr (is_set<U>)
            return std::to_underlying(cbor_sets::tag_number::set);
        else
            return detail::reflection::typed_array_tag<typename U::value_type>();
    }

    template <class U>
    static constexpr bool is_tagged_standard_type =
        is_sys_time<U> || is_clock_time<U> || is_duration<U> || std::same_as<U, std::chrono::year_month_day> ||
        is_set<U> || is_typed_array<U>;

    template <class U>
    static constexpr bool is_extended_time = (is_sys_time<U> && !is_whole_seconds<U>) || is_duration<U>;

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
            return tag == tag_of_standard_type<U>() || tag == std::to_underlying(rfc9581::tag_number::extended_time) ||
                   tag == std::to_underlying(rfc8943::tag_number::days_since_epoch_date);
        } else if constexpr (is_tai_time<U> || is_gps_time<U>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            return standard_tag_accepted<std::chrono::sys_time<typename U::duration>>(tag);
#else
            return tag == tag_of_standard_type<U>();
#endif
        } else if constexpr (is_utc_time<U> || is_file_time<U>) {
            return standard_tag_accepted<std::chrono::sys_time<typename U::duration>>(tag);
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
    static std::expected<void, error> read_integer(detail::heads::head const &h, V &out)
    {
        if (h.major != major_type::unsigned_integer && h.major != major_type::negative_integer) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        bool const negative = h.major == major_type::negative_integer;
        if (error const c = detail::validity::check_number_range<U>(negative, h.argument).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        U const magnitude = static_cast<U>(h.argument);
        out = static_cast<V>(negative ? static_cast<U>(~magnitude) : magnitude);
        return {};
    }

    template <class U>
    static bool head_accepted(detail::heads::head const &h)
    {
        static_assert(!is_local_time<U>, "cbor::structs: a std::chrono::local_time has no form in CBOR.");
        if constexpr (std::same_as<U, bool>)
            return detail::heads::is_boolean(h);
        else if constexpr (std::same_as<U, std::nullptr_t>)
            return detail::heads::is_null(h);
        else if constexpr (is_object_pointer<U>)
            return detail::heads::is_null(h) || head_accepted<pointee_t<U>>(h);
        else if constexpr (std::same_as<U, simple_value>)
            return detail::heads::is_simple_value(h);
        else if constexpr (std::is_floating_point_v<U>)
            return detail::heads::is_floating_point(h.major, h.info);
        else if constexpr (detail::reflection::is_wide_integer<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer ||
                   detail::heads::is_bignum(h.major, h.argument);
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
        else if constexpr (detail::reflection::is_optional<U>)
            return detail::heads::is_null(h) || head_accepted<typename U::value_type>(h);
        else if constexpr (is_tagged_item<U>)
            return h.major == major_type::tag && h.argument == U::number;
        else if constexpr (detail::is_std_variant<U>)
            return false;
        else if constexpr (is_std_tuple<U> || is_std_array<U>)
            return h.major == major_type::array && h.argument == std::tuple_size_v<U>;
        else if constexpr (detail::reflection::is_map<U>)
            return h.major == major_type::map;
        else if constexpr (requires(U const &c) { typename U::value_type; c.size(); })
            return h.major == major_type::array;
        else if constexpr (detail::reflection::tag_number_of<U>().has_value())
            return h.major == major_type::tag && h.argument == *detail::reflection::tag_number_of<U>();
        else
            return h.major == major_type::map;
    }

#ifdef CBOR_HAS_INT128
    template <class U>
    static std::expected<void, error> read_wide_integer(detail::heads::decoder<true> &d, detail::heads::head const &h,
                                                        U &out)
    {
        bool negative = h.major == major_type::negative_integer;
        uint128 magnitude = h.argument;
        if (h.major == major_type::tag) {
            if (h.argument != std::to_underlying(rfc8949::tag_number::unsigned_bignum) &&
                h.argument != std::to_underlying(rfc8949::tag_number::negative_bignum)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
            auto const b = d.decode_head();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (error const c = detail::validity::check_tag_content(h.argument, b->major).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            auto const bytes = d.decode_string(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::span<char const> const digits = detail::heads::magnitude_without_leading_zeros(std::span(*bytes));
            if (error const c =
                    detail::validity::check_magnitude_size(digits.size(), sizeof(uint128)).error_or(error{});
                c != error{}) [[unlikely]]
                return std::unexpected(c);
            magnitude = detail::heads::magnitude_value<uint128>(digits);
        } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
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

    CBOR_ALWAYS_INLINE static std::expected<detail::heads::head, error>
    decode_head_through_self_described_cbor(detail::heads::decoder<true> &d)
    {
        auto h = d.decode_head();
        while (h && h->major == major_type::tag && detail::validity::is_self_described_cbor_tag(h->argument))
            [[unlikely]]
            h = d.decode_head();
        return h;
    }

    template <class U, std::size_t I = 0, class D>
    static std::expected<void, error> read_variant(D &d, detail::heads::head const &h, U &out)
    {
        if constexpr (I == std::variant_size_v<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            using A = std::variant_alternative_t<I, U>;
            if (head_accepted<A>(h))
                return read_generic_value(d, h, out.template emplace<I>());
            return read_variant<U, I + 1>(d, h, out);
        }
    }

    template <class D, class U>
    static std::expected<void, error> read_generic(D &d, U &out)
    {
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major == major_type::tag) [[unlikely]] {
            if (detail::validity::is_self_described_cbor_tag(h->argument))
                return read_generic(d, out);
            if constexpr (reads_shared_values<D>) {
                if (h->argument == std::to_underlying(rfc8949::tag_number::shareable))
                    return read_shareable(d, out);
                if (h->argument == std::to_underlying(rfc8949::tag_number::sharedref))
                    return read_sharedref(d, out);
            }
            if (detail::validity::tag_refused_by_value_types(h->argument))
                return std::unexpected(error::invalid_tag);
        }
        return read_generic_value(d, *h, out);
    }

    template <class D, class F>
    static std::expected<void, error> read_in_place(D &d, bool const in_place, F const &read)
    {
        if constexpr (reads_shared_values<D>) {
            bool const outer = std::exchange(d.objects.in_place, in_place);
            auto const r = read();
            d.objects.in_place = outer;
            return r;
        } else {
            return read();
        }
    }

    template <class D, class U, class F>
    static std::expected<void, error> read_target(D &d, U &out, F const &read)
    {
        using V = pointee_t<U>;
        detail::pfr::check_default_constructible<V>();
        if constexpr (std::is_pointer_v<U>) {
            auto const target = std::make_shared<V>();
            d.objects.kept.push_back(target);
            out = target.get();
            return read_in_place(d, true, [&] { return read(*target); });
        } else if constexpr (is_shared_ptr<U>) {
            auto target = std::make_shared<V>();
            auto const r = read_in_place(d, true, [&] { return read(*target); });
            out = std::move(target);
            return r;
        } else {
            static_assert(std::same_as<U, std::unique_ptr<typename U::element_type>>,
                          "cbor::structs reads a std::unique_ptr only with std::default_delete.");
            auto target = std::make_unique<V>();
            auto const r = read_in_place(d, true, [&] { return read(*target); });
            out = std::move(target);
            return r;
        }
    }

    template <class D, class U>
    static std::expected<void, error> read_shareable(D &d, U &out)
    {
        if constexpr (is_object_pointer<U>) {
            return read_target(d, out, [&](pointee_t<U> &target) { return read_shareable(d, target); });
        } else {
            std::size_t const index = d.objects.slots.size();
            d.objects.slots.push_back(
                {d.objects.in_place ? static_cast<void *>(std::addressof(out)) : nullptr, &class_id<U>, false});
            auto const r = read_generic(d, out);
            d.objects.slots[index].complete = true;
            return r;
        }
    }

    template <class D, class U>
    static std::expected<void, error> read_sharedref(D &d, U &out)
    {
        if constexpr (!is_object_pointer<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            auto const h = d.decode_head();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (auto const c =
                    detail::validity::check_tag_content(std::to_underlying(rfc8949::tag_number::sharedref), h->major);
                !c) [[unlikely]]
                return std::unexpected(c.error());
            auto const index = detail::value_sharing::check_sharedref_index(h->argument, d.objects.slots.size());
            if (!index) [[unlikely]]
                return std::unexpected(index.error());
            slot const &named = d.objects.slots[std::to_underlying(*index)];
            if ((is_smart_pointer<U> && !is_shared_ptr<U>) || named.object == nullptr ||
                named.type != &class_id<pointee_t<U>>) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (named.complete) [[unlikely]]
                return std::unexpected(error::sharedref_not_cyclic);
            if constexpr (is_smart_pointer<U>) {
                return std::unexpected(error::cyclic_data_structure);
            } else {
                out = static_cast<pointee_t<U> *>(named.object);
                return {};
            }
        }
    }

    template <class D>
    static std::expected<void, error> skip_value(D &d)
    {
        std::string_view const before = d.encoded;
        if (auto const s = detail::well_formedness::skip_item(d); !s) [[unlikely]]
            return s;
        if constexpr (reads_shared_values<D>) {
            detail::heads::decoder<true> skipped{before.substr(0, before.size() - d.encoded.size())};
            while (!skipped.encoded.empty()) {
                auto const h = skipped.decode_head();
                if (!h) [[unlikely]]
                    return std::unexpected(h.error());
                if (h->major == major_type::byte_string || h->major == major_type::text_string) {
                    if (auto const t = skipped.decode_string(h->argument); !t) [[unlikely]]
                        return std::unexpected(t.error());
                } else if (h->major == major_type::tag &&
                           h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                    d.objects.slots.push_back({nullptr, nullptr, true});
                }
            }
        }
        return {};
    }

    template <class D, class U>
    static std::expected<void, error> read_generic_value(D &d, detail::heads::head const &h, U &out)
    {
        static_assert(!has_describe_value<U>, "cbor::structs reads no type whose description holds a value.");
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        if constexpr (std::same_as<U, bool>) {
            if (!detail::heads::is_boolean(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = h.info == std::to_underlying(simple_value::true_value);
            return {};
        } else if constexpr (std::same_as<U, simple_value>) {
            if (!head_accepted<U>(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = static_cast<simple_value>(h.argument);
            return {};
        } else if constexpr (detail::reflection::is_wide_integer<U>) {
            return read_wide_integer(d, h, out);
        } else if constexpr (detail::is_std_variant<U>) {
            return read_variant(d, h, out);
        } else if constexpr (std::is_enum_v<U>) {
            static_assert(detail::reflection::has_fixed_underlying_type<U>,
                          "cbor::structs reads an enum only with a fixed underlying type, because every value of that type is a value of the enum.");
            return read_integer<std::underlying_type_t<U>>(h, out);
        } else if constexpr (std::is_integral_v<U>) {
            return read_integer<U>(h, out);
        } else if constexpr (std::is_floating_point_v<U>) {
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::structs reads and writes float and double only.");
            if (!head_accepted<U>(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if constexpr (std::same_as<U, double>) {
                out = detail::ieee754::decode_float(h.info, h.argument);
            } else if (h.info ==
                       std::to_underlying(rfc8949::simple_information::half_precision_float)) {
                out = detail::ieee754::float_decode_binary16(static_cast<std::uint16_t>(h.argument));
            } else if (h.info ==
                       std::to_underlying(rfc8949::simple_information::single_precision_float)) {
                out = std::bit_cast<float>(static_cast<std::uint32_t>(h.argument));
            } else {
                double const value = std::bit_cast<double>(h.argument);
                constexpr int narrow =
                    detail::ieee754::double_precision.significand_bits - detail::ieee754::single_precision.significand_bits;
                if (detail::ieee754::is_nan(detail::ieee754::double_precision, h.argument)
                        ? (h.argument & ((std::uint64_t{1} << narrow) - 1u)) != 0
                        : detail::ieee754::preferred_float_info(value) ==
                              rfc8949::simple_information::double_precision_float) [[unlikely]]
                    return std::unexpected(error::number_out_of_range);
                out = std::bit_cast<float>(static_cast<std::uint32_t>(
                    detail::ieee754::encode_float(rfc8949::simple_information::single_precision_float, value)));
            }
            return {};
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            if (!detail::heads::is_null(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return {};
        } else if constexpr (is_object_pointer<U>) {
            if (detail::heads::is_null(h)) {
                out = nullptr;
                return {};
            }
            return read_target(d, out, [&](pointee_t<U> &target) { return read_generic_value(d, h, target); });
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            if (h.major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.decode_string(h.argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            if constexpr (is_basic_string<U>)
                out.assign(*text);
            else
                out = *text;
            return {};
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            if (h.major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.decode_string(h.argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            out = std::as_bytes(std::span(*bytes));
            return {};
        } else if constexpr (is_tagged_standard_type<U>) {
            if (!head_accepted<U>(h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return read_tag_content(d, out, h);
        } else if constexpr (detail::reflection::is_optional<U>) {
            if (detail::heads::is_null(h)) {
                out.reset();
                return {};
            }
            return read_generic_value(d, h, out.emplace());
        } else if constexpr (is_tagged_item<U>) {
            if (h.major != major_type::tag || h.argument != U::number) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return read_generic(d, out.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            if (h.major != major_type::array || h.argument != std::tuple_size_v<U>) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> r;
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                ((r = r.and_then([&] { return read_generic(d, std::get<I>(out)); })),
                 ...);
            }(std::make_index_sequence<std::tuple_size_v<U>>());
            return r;
        } else if constexpr (is_multimap<U>) {
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h.major, h.info, h.argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h.argument; ++i) {
                auto key = detail::pfr::make_element<typename U::key_type>(out);
                if (auto const r = read_in_place(d, false, [&] { return read_generic(d, key); }); !r) [[unlikely]]
                    return r;
                auto at = [&] {
                    if constexpr (requires { out.lower_bound(key); })
                        return out.lower_bound(key);
                    else
                        return out.end();
                }();
                if constexpr (requires { out.lower_bound(key); })
                    if (auto const r = detail::validity::check_key_unique(at != out.end() &&
                                                                          !out.key_comp()(key, at->first));
                        !r) [[unlikely]]
                        return r;
                auto const values = decode_head_through_self_described_cbor(d);
                if (!values) [[unlikely]]
                    return std::unexpected(values.error());
                if (values->major != major_type::array || values->argument == 0) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if (auto const r = detail::validity::check_pending_items(values->major, values->info,
                                                                         values->argument, d.encoded.size());
                    !r) [[unlikely]]
                    return r;
                for (std::uint64_t k = 0; k < values->argument; ++k) {
                    auto value = detail::pfr::make_element<typename U::mapped_type>(out);
                    if (auto const r = read_in_place(d, false, [&] { return read_generic(d, value); }); !r)
                        [[unlikely]]
                        return r;
                    if constexpr (requires { out.lower_bound(key); })
                        out.emplace_hint(at, key, std::move(value));
                    else
                        at = out.emplace_hint(at, key, std::move(value));
                }
            }
            return {};
        } else if constexpr (detail::reflection::is_map<U>) {
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h.major, h.info, h.argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            for (std::uint64_t i = 0; i < h.argument; ++i) {
                auto key = detail::pfr::make_element<typename U::key_type>(out);
                if (auto const r = read_in_place(d, false, [&] { return read_generic(d, key); }); !r) [[unlikely]]
                    return r;
                auto value = detail::pfr::make_element<typename U::mapped_type>(out);
                if (auto const r = read_in_place(d, false, [&] { return read_generic(d, value); }); !r) [[unlikely]]
                    return r;
                if constexpr (requires { out.try_emplace(std::move(key), std::move(value)); }) {
                    if (auto const r = detail::validity::check_key_unique(
                            !out.try_emplace(std::move(key), std::move(value)).second);
                        !r) [[unlikely]]
                        return r;
                } else if (auto const r = detail::validity::check_key_unique(
                               !out.emplace(std::move(key), std::move(value)).second);
                           !r) [[unlikely]] {
                    return r;
                }
            }
            return {};
        } else if constexpr (requires {
                                 typename U::value_type;
                                 out.size();
                             }) {
            using E = typename U::value_type;
            detail::pfr::check_default_constructible<E>();
            static_assert(
                requires(E &&e) { out.insert(out.end(), std::move(e)); },
                "cbor::structs decodes a container with insert(end(), value); this container has none.");
            if (h.major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (auto const r =
                    detail::validity::check_pending_items(h.major, h.info, h.argument, d.encoded.size());
                !r) [[unlikely]]
                return r;
            out.clear();
            if constexpr (requires { out.reserve(std::size_t{}); })
                out.reserve(static_cast<std::size_t>(
                    std::min<std::uint64_t>(h.argument, d.encoded.size() / min_encoded_size<E>())));
            for (std::uint64_t i = 0; i < h.argument; ++i) {
                if (!detail::validity::bytes_fit(min_encoded_size<E>(), d.encoded.size())) [[unlikely]]
                    return std::unexpected(error::too_little_data);
                constexpr bool keeps_addresses = is_std_vector<U> || is_std_deque<U> || is_std_list<U>;
                if constexpr (reads_shared_values<D> && is_std_vector<U>)
                    detail::validity::throw_logic_error_if(
                        out.size() == out.capacity(),
                        "cbor::structs::decode_cycles: a std::vector would move the elements that it read");
                if constexpr (requires {
                                  { out.emplace_back() } -> std::same_as<E &>;
                                  requires !detail::pfr::uses_allocator_aggregate<E, decltype(out.get_allocator())>();
                              }) {
                    if (auto const r =
                            read_in_place(d, keeps_addresses, [&] { return read_generic(d, out.emplace_back()); });
                        !r) [[unlikely]]
                        return r;
                } else if constexpr (reads_shared_values<D> && keeps_addresses) {
                    E &element = *out.insert(out.end(), detail::pfr::make_element<E>(out));
                    if (auto const r = read_in_place(d, true, [&] { return read_generic(d, element); }); !r)
                        [[unlikely]]
                        return r;
                } else {
                    E element = detail::pfr::make_element<E>(out);
                    if (auto const r = read_in_place(d, false, [&] { return read_generic(d, element); }); !r)
                        [[unlikely]]
                        return r;
                    out.insert(out.end(), std::move(element));
                }
            }
            return {};
        } else {
            return read_struct(d, h, out);
        }
    }

    template <class D, bool zoned = false>
    static std::expected<std::tuple<D, rfc9581::timescale, std::optional<std::string_view>>, error>
    read_extended_time(detail::heads::decoder<true> &d)
    {
        auto const h = decode_head_through_self_described_cbor(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (auto const r = detail::validity::check_extended_time_map(h->major, h->info, h->argument, d.encoded.size());
            !r) [[unlikely]]
            return std::unexpected(r.error());
        std::optional<std::variant<std::int64_t, double>> base;
        std::optional<std::pair<std::uint64_t, rfc9581::map_key>> fraction;
        std::optional<rfc9581::timescale> timescale;
        std::optional<std::string_view> zone;
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            auto const k = decode_head_through_self_described_cbor(d);
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::optional<rfc9581::map_key> key;
            if (k->major == major_type::text_string) {
                if (auto const t = d.decode_string(k->argument); !t) [[unlikely]]
                    return std::unexpected(t.error());
            } else {
                auto const known = detail::validity::check_extended_time_key(k->major, k->argument);
                if (!known) [[unlikely]]
                    return std::unexpected(known.error());
                key = *known;
            }
            if constexpr (!zoned) {
                if (key == rfc9581::map_key::time_zone_hint_critical) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                if (key == rfc9581::map_key::time_zone_hint)
                    key.reset();
            }
            if (!key) {
                if (auto const s = detail::well_formedness::skip_item(d); !s) [[unlikely]]
                    return std::unexpected(s.error());
                continue;
            }
            auto const v = decode_head_through_self_described_cbor(d);
            if (!v) [[unlikely]]
                return std::unexpected(v.error());
            if (*key == rfc9581::map_key::base_time) {
                if (auto const r = detail::validity::check_extended_time_entry_once(base.has_value()); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (v->major == major_type::simple) {
                    if (!head_accepted<double>(*v)) [[unlikely]]
                        return std::unexpected(error::inadmissible_type_for_tag_content);
                    base = detail::ieee754::decode_float(v->info, v->argument);
                } else if (v->major != major_type::unsigned_integer && v->major != major_type::negative_integer)
                    [[unlikely]] {
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                } else {
                    std::int64_t seconds = 0;
                    if (auto const r = read_integer<std::int64_t>(*v, seconds); !r) [[unlikely]]
                        return std::unexpected(r.error());
                    base = seconds;
                }
            } else if (std::ranges::contains(detail::validity::fraction_keys, *key)) {
                if (auto const r = detail::validity::check_extended_time_entry_once(fraction.has_value()); !r)
                    [[unlikely]]
                    return std::unexpected(r.error());
                if (v->major != major_type::unsigned_integer) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                fraction = std::pair{v->argument, *key};
            } else if (*key == rfc9581::map_key::time_zone_hint || *key == rfc9581::map_key::time_zone_hint_critical) {
                if (auto const r = detail::validity::check_extended_time_entry_once(zone.has_value()); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (v->major != major_type::text_string) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const name = d.decode_string(v->argument);
                if (!name) [[unlikely]]
                    return std::unexpected(name.error());
                zone = *name;
            } else {
                if (auto const r = detail::validity::check_extended_time_entry_once(timescale.has_value()); !r)
                    [[unlikely]]
                    return std::unexpected(r.error());
                auto const scale = detail::validity::check_timescale(v->major, v->argument);
                if (!scale) [[unlikely]]
                    return std::unexpected(scale.error());
                timescale = *scale;
            }
        }
        if (!base || (fraction && !std::holds_alternative<std::int64_t>(*base))) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        auto const ticks =
            fraction ? detail::validity::check_extended_time_ticks<D>(std::get<std::int64_t>(*base), fraction->first,
                                                                      fraction->second)
                     : std::visit([](auto const seconds) { return detail::validity::check_epoch_ticks<D>(seconds); },
                                  *base);
        if (!ticks) [[unlikely]]
            return std::unexpected(ticks.error());
        return std::tuple{*ticks, timescale.value_or(rfc9581::timescale::utc), zone};
    }

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    template <class D>
    static std::expected<D, error> convert_tai_to_utc(D const tai)
    {
        if (auto const r = detail::validity::check_clock_range(tai, detail::validity::clock_offset_bound); !r)
            [[unlikely]]
            return std::unexpected(r.error());
        return detail::validity::check_exact_ticks<D>(
            std::chrono::clock_cast<std::chrono::system_clock>(std::chrono::tai_time<D>{tai}).time_since_epoch());
    }

    template <class D>
    static std::expected<D, error> convert_utc_to_tai(D const utc)
    {
        if (auto const r = detail::validity::check_clock_range(utc, detail::validity::clock_offset_bound); !r)
            [[unlikely]]
            return std::unexpected(r.error());
        return detail::validity::check_exact_ticks<D>(
            std::chrono::clock_cast<std::chrono::tai_clock>(std::chrono::sys_time<D>{utc}).time_since_epoch());
    }

    static std::chrono::seconds file_clock_bound()
    {
        return std::chrono::abs(
                   std::chrono::clock_cast<std::chrono::system_clock>(std::chrono::file_time<std::chrono::seconds>{})
                       .time_since_epoch()) +
               detail::validity::clock_offset_bound;
    }
#endif

    template <class D>
    static std::expected<D, error> convert_to_utc(D const ticks, rfc9581::timescale const scale)
    {
        if (scale == rfc9581::timescale::utc) [[likely]]
            return ticks;
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
        return detail::validity::check_shifted_ticks(ticks, detail::validity::ptp_epoch_since_tai_clock_epoch)
            .and_then([](D const tai) { return convert_tai_to_utc(tai); });
#else
        return std::unexpected(error::inadmissible_type_for_tag_content);
#endif
    }

    template <class D>
    static std::expected<D, error> read_ptp_ticks(detail::heads::decoder<true> &d, detail::heads::head const &tag)
    {
        if (tag.argument == std::to_underlying(rfc9581::tag_number::extended_time)) {
            auto const t = read_extended_time<D>(d);
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (std::get<1>(*t) == rfc9581::timescale::tai) [[likely]]
                return std::get<0>(*t);
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            return convert_utc_to_tai(std::get<0>(*t)).and_then([](D const tai) {
                return detail::validity::check_shifted_ticks(tai, -detail::validity::ptp_epoch_since_tai_clock_epoch);
            });
#else
            return std::unexpected(error::inadmissible_type_for_tag_content);
#endif
        }
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
        std::chrono::sys_time<D> utc{};
        if (auto const r = read_tag_content(d, utc, tag); !r) [[unlikely]]
            return std::unexpected(r.error());
        return convert_utc_to_tai(utc.time_since_epoch()).and_then([](D const tai) {
            return detail::validity::check_shifted_ticks(tai, -detail::validity::ptp_epoch_since_tai_clock_epoch);
        });
#else
        return std::unexpected(error::incorrect_type);
#endif
    }

    template <class U>
    static std::expected<void, error> read_epoch_days(detail::heads::decoder<true> &d, U &out)
    {
        auto const h = decode_head_through_self_described_cbor(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::int64_t days = 0;
        if (auto const r = read_integer<std::int64_t>(*h, days); !r) [[unlikely]]
            return r;
        auto const ticks = detail::validity::check_seconds_of_days(days).and_then(
            [](std::int64_t const seconds) { return detail::validity::check_epoch_ticks<typename U::duration>(seconds); });
        if (!ticks) [[unlikely]]
            return std::unexpected(ticks.error());
        out = U{*ticks};
        return {};
    }

    template <class U>
    static std::expected<void, error> read_epoch(detail::heads::decoder<true> &d, U &out)
    {
        using D = typename U::duration;
        auto const h = decode_head_through_self_described_cbor(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::expected<D, error> ticks;
        if (h->major == major_type::simple) {
            if (!head_accepted<double>(*h)) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            ticks = detail::validity::check_epoch_ticks<D>(detail::ieee754::decode_float(h->info, h->argument));
        } else if (h->major != major_type::unsigned_integer && h->major != major_type::negative_integer) [[unlikely]] {
            return std::unexpected(error::inadmissible_type_for_tag_content);
        } else {
            std::int64_t seconds = 0;
            if (auto const r = read_integer<std::int64_t>(*h, seconds); !r) [[unlikely]]
                return r;
            ticks = detail::validity::check_epoch_ticks<D>(seconds);
        }
        if (!ticks) [[unlikely]]
            return std::unexpected(ticks.error());
        out = U{*ticks};
        return {};
    }

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    template <class U, std::size_t N>
    static std::expected<void, error> parse_text(detail::heads::decoder<true> &d, U &out,
                                                 std::array<char const *, N> const &formats)
    {
        auto const h = decode_head_through_self_described_cbor(d);
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

    template <class Decoder, class U>
    static std::expected<void, error> read_tag_content(Decoder &d, U &out, detail::heads::head const &tag)
    {
        if constexpr (is_sys_time<U>) {
            if (tag.argument == std::to_underlying(rfc8949::tag_number::epoch_based_date_time)) [[likely]]
                return read_epoch(d, out);
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8949::tag_number::standard_date_time_string))
                return parse_text(d, out, date_time_formats);
#endif
            if (tag.argument == std::to_underlying(rfc9581::tag_number::extended_time)) {
                auto const t = read_extended_time<typename U::duration>(d);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                auto const ticks = convert_to_utc(std::get<0>(*t), std::get<1>(*t));
                if (!ticks) [[unlikely]]
                    return std::unexpected(ticks.error());
                out = U{*ticks};
                return {};
            }
            return read_epoch_days(d, out);
        } else if constexpr (is_tai_time<U> || is_gps_time<U> || is_utc_time<U>) {
            using D = typename U::duration;
            auto const ptp = read_ptp_ticks<D>(d, tag);
            if (!ptp) [[unlikely]]
                return std::unexpected(ptp.error());
            if constexpr (is_gps_time<U>) {
                auto const gps = detail::validity::check_shifted_ticks(*ptp, -detail::validity::tai_minus_gps);
                if (!gps) [[unlikely]]
                    return std::unexpected(gps.error());
                out = U{*gps};
                return {};
            } else {
                auto const tai = detail::validity::check_shifted_ticks(*ptp, detail::validity::ptp_epoch_since_tai_clock_epoch);
                if (!tai) [[unlikely]]
                    return std::unexpected(tai.error());
                if constexpr (is_tai_time<U>) {
                    out = U{*tai};
                    return {};
                } else {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
                    if (auto const r = detail::validity::check_clock_range(*tai, detail::validity::clock_offset_bound);
                        !r) [[unlikely]]
                        return r;
                    auto const utc = detail::validity::check_exact_ticks<D>(
                        std::chrono::clock_cast<std::chrono::utc_clock>(std::chrono::tai_time<D>{*tai}).time_since_epoch());
                    if (!utc) [[unlikely]]
                        return std::unexpected(utc.error());
                    out = U{*utc};
#endif
                    return {};
                }
            }
        } else if constexpr (is_file_time<U> || is_zoned_time<U>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            using D = typename U::duration;
            if constexpr (is_file_time<U>) {
                std::chrono::sys_time<D> utc{};
                if (auto const r = read_tag_content(d, utc, tag); !r) [[unlikely]]
                    return r;
                if (auto const r = detail::validity::check_clock_range(utc.time_since_epoch(), file_clock_bound()); !r)
                    [[unlikely]]
                    return r;
                auto const file = detail::validity::check_exact_ticks<D>(
                    std::chrono::clock_cast<std::chrono::file_clock>(utc).time_since_epoch());
                if (!file) [[unlikely]]
                    return std::unexpected(file.error());
                out = U{*file};
            } else {
                auto const t = read_extended_time<D, true>(d);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                auto const utc = convert_to_utc(std::get<0>(*t), std::get<1>(*t));
                if (!utc) [[unlikely]]
                    return std::unexpected(utc.error());
                auto const zone =
                    detail::validity::check_time_zone(std::get<2>(*t).value_or(detail::validity::utc_zone_name));
                if (!zone) [[unlikely]]
                    return std::unexpected(zone.error());
                out = U{*zone, std::chrono::sys_time<D>{*utc}};
            }
#endif
            return {};
        } else if constexpr (is_duration<U>) {
            auto const t = read_extended_time<U>(d);
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            if (std::get<1>(*t) != rfc9581::timescale::utc) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            out = std::get<0>(*t);
            return {};
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
            if (tag.argument == std::to_underlying(rfc8943::tag_number::full_date_string))
                return parse_text(d, out, full_date_formats);
#endif
            std::chrono::days::rep days = 0;
            auto const h = decode_head_through_self_described_cbor(d);
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (auto const r = read_integer<std::chrono::days::rep>(*h, days); !r) [[unlikely]]
                return r;
            if (auto const r = detail::validity::check_day_count(days); !r) [[unlikely]]
                return r;
            out = std::chrono::year_month_day{std::chrono::sys_days{std::chrono::days{days}}};
            return {};
        } else if constexpr (is_set<U>) {
            auto const h = decode_head_through_self_described_cbor(d);
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
                auto element = detail::pfr::make_element<typename U::value_type>(out);
                if (auto const r = read_in_place(d, false, [&] { return read_generic(d, element); }); !r)
                    [[unlikely]]
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
                out[i] = detail::reflection::read_typed_array_element<E>(
                    elements.subspan(i * sizeof(E)).template first<sizeof(E)>());
            return {};
        }
    }

    template <class U, std::size_t I>
    static consteval std::size_t min_key_size()
    {
        constexpr map_key key = detail::pfr::key_of<I, U>();
        if constexpr (key.major == major_type::text_string)
            return detail::heads::head_size(key.text.size()) + key.text.size();
        else
            return detail::heads::head_size(key.argument);
    }

    template <class U>
    static consteval std::size_t min_encoded_size()
    {
        constexpr std::size_t one_head = detail::heads::initial_byte_size;
        if constexpr (detail::is_std_variant<U>) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return std::min({min_encoded_size<std::variant_alternative_t<I, U>>()...});
            }(std::make_index_sequence<std::variant_size_v<U>>());
        } else if constexpr (is_tagged_standard_type<U>) {
            std::size_t tag_head = detail::heads::head_size(tag_of_standard_type<U>());
            if constexpr (is_sys_time<U>)
                tag_head = std::min(tag_head, detail::heads::head_size(std::to_underlying(rfc8949::tag_number::standard_date_time_string)));
            else if constexpr (std::same_as<U, std::chrono::year_month_day>)
                tag_head = std::min(tag_head, detail::heads::head_size(std::to_underlying(rfc8943::tag_number::full_date_string)));
            if constexpr (is_typed_array<U> && is_std_array<U>) {
                constexpr std::size_t content = std::tuple_size_v<U> * sizeof(typename U::value_type);
                return tag_head + detail::heads::head_size(content) + content;
            } else {
                return tag_head + one_head;
            }
        } else if constexpr (is_tagged_item<U>) {
            return detail::heads::head_size(U::number) + min_encoded_size<std::remove_cvref_t<decltype(std::declval<U &>().content)>>();
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (detail::heads::head_size(std::tuple_size_v<U>) + ... +
                        min_encoded_size<std::tuple_element_t<I, U>>());
            }(std::make_index_sequence<std::tuple_size_v<U>>());
        } else if constexpr (std::is_class_v<U> && !is_smart_pointer<U> && !detail::reflection::is_optional<U> &&
                             !is_basic_string<U> &&
                             !std::same_as<U, std::string_view> && !std::same_as<U, std::span<std::byte const>> &&
                             !detail::reflection::is_wide_integer<U> && !detail::reflection::is_map<U> &&
                             !requires(U const &c) { typename U::value_type; c.size(); }) {
            std::size_t tag_head = 0;
            if constexpr (constexpr std::optional<std::uint64_t> tag = detail::reflection::tag_number_of<U>(); tag.has_value())
                tag_head = detail::heads::head_size(*tag);
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return ((tag_head + one_head) + ... +
                        (detail::reflection::is_optional<std::remove_cvref_t<decltype(detail::pfr::get<I>(std::declval<U &>()))>>
                             ? std::size_t{0}
                             : min_key_size<U, I>() +
                                   min_encoded_size<std::remove_cvref_t<decltype(detail::pfr::get<I>(std::declval<U &>()))>>()));
            }(std::make_index_sequence<detail::pfr::tuple_size_v<U>>());
        } else {
            return one_head;
        }
    }

    template <class U, std::size_t I>
    static bool key_matches(detail::heads::head const &k, std::string_view const text)
    {
        constexpr map_key key = detail::pfr::key_of<I, U>();
        return k.major == key.major && k.argument == key.argument &&
               (key.major != major_type::text_string || text == key.text);
    }

    template <class D, class U>
    static std::expected<void, error> read_struct(D &d, detail::heads::head const &first, U &out)
    {
        static_assert(detail::pfr::struct_readable<U>());
        constexpr std::size_t count = detail::pfr::tuple_size_v<U>;
        std::expected<detail::heads::head, error> h = first;
        if constexpr (constexpr std::optional<std::uint64_t> tag = detail::reflection::tag_number_of<U>(); tag.has_value()) {
            if (first.major != major_type::tag || first.argument != *tag) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            h = decode_head_through_self_described_cbor(d);
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
        }
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        auto const read_member =
            [&]<std::size_t I>(std::integral_constant<std::size_t, I>) -> std::expected<void, error> {
            if (auto const unique = detail::validity::check_key_unique(found.test(I)); !unique) [[unlikely]]
                return unique;
            found.set(I);
            return read_generic(d, detail::pfr::get<I>(out));
        };
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            auto k = decode_head_through_self_described_cbor(d);
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            detail::heads::head key = *k;
            std::string_view text;
            if (detail::heads::is_bignum(key.major, key.argument)) [[unlikely]] {
                auto const read = d.decode_bignum(key.argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                if (*read)
                    key = **read;
            } else if (key.major == major_type::text_string) {
                auto const t = d.decode_string(key.argument);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                text = *t;
            } else if (key.major != major_type::unsigned_integer && key.major != major_type::negative_integer) {
                if (key.major == major_type::byte_string) {
                    if (auto const b = d.decode_string(key.argument); !b) [[unlikely]]
                        return std::unexpected(b.error());
                } else if (key.major == major_type::array || key.major == major_type::map ||
                           key.major == major_type::tag) [[unlikely]] {
                    std::uint64_t const items = detail::validity::count_enclosed_data_items(key.major, key.argument);
                    if (auto const p = detail::validity::check_bytes_left(items, d.encoded.size()); !p)
                        [[unlikely]]
                        return std::unexpected(p.error());
                    if constexpr (reads_shared_values<D>)
                        if (key.major == major_type::tag &&
                            key.argument == std::to_underlying(rfc8949::tag_number::shareable))
                            d.objects.slots.push_back({nullptr, nullptr, true});
                    for (std::uint64_t i = 0; i < items; ++i)
                        if (auto const e = skip_value(d); !e) [[unlikely]]
                            return e;
                }
            }
            std::expected<void, error> r;
            bool const matched = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (
                    false || ... ||
                    (key_matches<U, I>(key, text) &&
                     (static_cast<void>(r = read_member(std::integral_constant<std::size_t, I>{})), true)));
            }(std::make_index_sequence<count>());
            if (!matched) {
                if (auto const s = skip_value(d); !s) [[unlikely]]
                    return s;
            } else if (!r) [[unlikely]] {
                return r;
            }
        }
        bool const missing = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return (false || ... ||
                    (!detail::reflection::is_optional<std::remove_cvref_t<decltype(detail::pfr::get<I>(out))>> &&
                     !found.test(I)));
        }(std::make_index_sequence<count>());
        if (missing) [[unlikely]]
            return std::unexpected(error::key_not_found);
        return {};
    }

    template <class U>
    static consteval std::array<std::size_t, detail::pfr::tuple_size_v<U>> key_order()
    {
        constexpr std::size_t count = detail::pfr::tuple_size_v<U>;
        std::array<map_key, count> const keys = [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<map_key, count>{detail::pfr::key_of<I, U>()...};
        }(std::make_index_sequence<count>());
        std::array<std::size_t, count> const order = detail::reflection::keys_sorted(keys);
        if (std::ranges::adjacent_find(order, {}, [&](std::size_t const i) {
                return detail::reflection::key_encoding_of(keys[i]);
            }) != std::ranges::end(order))
            std::unreachable();
        return order;
    }

    template <class U>
    static bool member_present(U const &value)
    {
        if constexpr (detail::reflection::is_optional<U>)
            return value.has_value();
        else
            return true;
    }

    template <class U>
    static auto duration_of(U const &value)
    {
        if constexpr (is_duration<U>)
            return value;
        else
            return value.time_since_epoch();
    }

    template <class U>
    using extended_time_parts =
        std::tuple<std::chrono::sys_time<typename U::duration>, rfc9581::timescale, std::optional<std::string_view>>;

    template <class U>
    static std::expected<extended_time_parts<U>, error> extended_time_of(U const &value)
    {
        using D = typename U::duration;
        if constexpr (is_zoned_time<U>) {
            return extended_time_parts<U>{value.get_sys_time(), rfc9581::timescale::utc,
                                          value.get_time_zone()->name()};
        } else {
            std::expected<D, error> ptp;
            if constexpr (is_tai_time<U>) {
                ptp = detail::validity::check_shifted_ticks(value.time_since_epoch(),
                                                            -detail::validity::ptp_epoch_since_tai_clock_epoch);
            } else if constexpr (is_gps_time<U>) {
                ptp = detail::validity::check_shifted_ticks(value.time_since_epoch(), detail::validity::tai_minus_gps);
            } else {
#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
                std::chrono::seconds const bound =
                    is_file_time<U> ? file_clock_bound() : detail::validity::clock_offset_bound;
                if (auto const r = detail::validity::check_clock_range(value.time_since_epoch(), bound); !r)
                    [[unlikely]]
                    return std::unexpected(r.error());
                if constexpr (is_file_time<U>) {
                    auto const utc = detail::validity::check_exact_ticks<D>(
                        std::chrono::clock_cast<std::chrono::system_clock>(value).time_since_epoch());
                    if (!utc) [[unlikely]]
                        return std::unexpected(utc.error());
                    return extended_time_parts<U>{std::chrono::sys_time<D>{*utc}, rfc9581::timescale::utc,
                                                  std::nullopt};
                } else {
                    ptp = detail::validity::check_exact_ticks<D>(
                              std::chrono::clock_cast<std::chrono::tai_clock>(value).time_since_epoch())
                              .and_then([](D const tai) {
                                  return detail::validity::check_shifted_ticks(
                                      tai, -detail::validity::ptp_epoch_since_tai_clock_epoch);
                              });
                }
#endif
            }
            if (!ptp) [[unlikely]]
                return std::unexpected(ptp.error());
            return extended_time_parts<U>{std::chrono::sys_time<D>{*ptp}, rfc9581::timescale::tai, std::nullopt};
        }
    }

    template <class U, class S>
    static constexpr std::uint64_t extended_time_tag(S const &seconds, rfc9581::timescale const scale,
                                                     std::optional<std::string_view> const zone)
    {
        if constexpr (is_duration<U>)
            return std::to_underlying(rfc9581::tag_number::duration);
        else if (seconds.index() == 2 || scale != rfc9581::timescale::utc || zone)
            return std::to_underlying(rfc9581::tag_number::extended_time);
        else
            return std::to_underlying(rfc8949::tag_number::epoch_based_date_time);
    }

    template <class S>
    static constexpr std::size_t extended_time_entries(S const &seconds, rfc9581::timescale const scale,
                                                       std::optional<std::string_view> const zone)
    {
        return std::size_t{1} + std::size_t{seconds.index() == 2} + std::size_t{scale != rfc9581::timescale::utc} +
               std::size_t{zone.has_value()};
    }

    template <class Out, class U>
    static std::expected<std::size_t, error>
    write_extended_time(Out const out, std::size_t at, U const &value,
                        rfc9581::timescale const scale = rfc9581::timescale::utc,
                        std::optional<std::string_view> const zone = std::nullopt)
    {
        auto const seconds = detail::validity::check_epoch_seconds(duration_of(value));
        if (!seconds) [[unlikely]]
            return std::unexpected(seconds.error());
        std::uint64_t const tag = extended_time_tag<U>(*seconds, scale, zone);
        at += put_head(out, at, major_type::tag, tag);
        if (tag != std::to_underlying(rfc8949::tag_number::epoch_based_date_time)) {
            at += put_head(out, at, major_type::map, extended_time_entries(*seconds, scale, zone));
            auto const key = write_value(out, at, std::to_underlying(rfc9581::map_key::base_time));
            if (!key) [[unlikely]]
                return key;
            at = *key;
        }
        auto const time = std::visit(
            [&](auto const &s) -> std::expected<std::size_t, error> {
                if constexpr (is_std_pair<std::remove_cvref_t<decltype(s)>> &&
                              detail::validity::fraction_key_of_period<typename U::period>().has_value()) {
                    constexpr auto key = detail::validity::fraction_key_of_period<typename U::period>();
                    return write_value(out, at, s.first)
                        .and_then([&](std::size_t const w) { return write_value(out, w, std::to_underlying(*key)); })
                        .and_then([&](std::size_t const w) { return write_value(out, w, s.second.count()); });
                } else if constexpr (is_std_pair<std::remove_cvref_t<decltype(s)>>) {
                    return std::unexpected(error::number_out_of_range);
                } else {
                    return write_value(out, at, s);
                }
            },
            *seconds);
        if (!time || (scale == rfc9581::timescale::utc && !zone)) [[likely]]
            return time;
        at = *time;
        if (scale != rfc9581::timescale::utc) {
            auto const w = write_value(out, at, std::to_underlying(rfc9581::map_key::timescale_critical))
                               .and_then([&](std::size_t const n) {
                                   return write_value(out, n, std::to_underlying(scale));
                               });
            if (!w) [[unlikely]]
                return w;
            at = *w;
        }
        if (zone)
            return write_value(out, at, std::to_underlying(rfc9581::map_key::time_zone_hint))
                .and_then([&](std::size_t const n) { return write_value(out, n, *zone); });
        return at;
    }

    CBOR_ALWAYS_INLINE static std::size_t put_head(std::span<char> const out, std::size_t const at, major_type const major,
                                                   std::uint64_t const argument)
    {
        return detail::heads::write_head(out, at, major, argument);
    }

    CBOR_ALWAYS_INLINE static std::size_t put_head(std::span<char> const out, std::size_t const at, major_type const major,
                                                   std::uint8_t const info, std::uint64_t const argument)
    {
        return detail::heads::write_head(out, at, major, info, argument);
    }

    CBOR_ALWAYS_INLINE static std::size_t put_simple(std::span<char> const out, std::size_t const at,
                                                     simple_value const value)
    {
        out.subspan(at).front() = detail::heads::initial_byte(major_type::simple, std::to_underlying(value));
        return at + detail::heads::initial_byte_size;
    }

    CBOR_ALWAYS_INLINE static std::size_t put_bytes(std::span<char> const out, std::size_t const at,
                                                    std::span<char const> const bytes)
    {
        std::ranges::copy(bytes, out.subspan(at, bytes.size()).begin());
        return at + bytes.size();
    }

    CBOR_ALWAYS_INLINE static std::size_t put_bytes(std::span<char> const out, std::size_t const at,
                                                    std::span<std::byte const> const bytes)
    {
        std::ranges::copy(bytes, std::as_writable_bytes(out.subspan(at, bytes.size())).begin());
        return at + bytes.size();
    }

    template <class E>
    CBOR_ALWAYS_INLINE static std::size_t put_typed_array_element(std::span<char> const out, std::size_t const at,
                                                                  E const e)
    {
        detail::reflection::write_typed_array_element<E>(out.subspan(at).template first<sizeof(E)>(), e);
        return at + sizeof(E);
    }

    struct edn_out {
        detail::edn_text &text;
    };

    static std::size_t put_head(edn_out const out, std::size_t, major_type const major, std::uint64_t const argument)
    {
        out.text.write_head(major, argument);
        return 0;
    }

    static std::size_t put_head(edn_out const out, std::size_t, major_type const major, std::uint8_t const info,
                                std::uint64_t const argument)
    {
        out.text.write_head(major, info, argument);
        return 0;
    }

    static std::size_t put_simple(edn_out const out, std::size_t const at, simple_value const value)
    {
        out.text.write_head(major_type::simple, std::to_underlying(value));
        return at;
    }

    static std::size_t put_bytes(edn_out const out, std::size_t const at, std::span<char const> const bytes)
    {
        out.text.write_payload(std::string_view(bytes));
        return at;
    }

    static std::size_t put_bytes(edn_out const out, std::size_t const at, std::span<std::byte const> const bytes)
    {
        out.text.write_payload(bytes);
        return at;
    }

    template <class E>
    static std::size_t put_typed_array_element(edn_out const out, std::size_t const at, E const e)
    {
        std::array<char, sizeof(E)> element;
        detail::reflection::write_typed_array_element<E>(std::span<char, sizeof(E)>(element), e);
        out.text.write_payload(std::string_view(element));
        return at;
    }

    template <class Target>
    struct bytes_sink {
        Target &bytes;
        error &failure;
    };

    template <class Target>
    [[gnu::cold, gnu::noinline]] static bool grow(bytes_sink<Target> const out, std::size_t const at,
                                                  std::size_t const need)
    {
        if constexpr (requires { out.bytes.resize(std::size_t{}); }) {
            auto const size = detail::validity::grown_size(out.bytes.size(), at, need, out.bytes.max_size());
            if (!size) [[unlikely]] {
                out.failure = size.error();
                return false;
            }
            out.bytes.resize(*size);
            return true;
        } else {
            out.failure = error::no_buffer_space;
            return false;
        }
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static bool room(bytes_sink<Target> const out, std::size_t const at,
                                        std::size_t const need)
    {
        if (out.failure != error{}) [[unlikely]]
            return false;
        if (detail::validity::bytes_fit(need, std::ranges::size(out.bytes) - at)) [[likely]]
            return true;
        return grow(out, at, need);
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::span<char> written_part(bytes_sink<Target> const out,
                                                           std::size_t const end)
    {
        if constexpr (requires { out.bytes.resize(std::size_t{}); })
            return std::span<char>(out.bytes);
        else
            return std::span<char>(out.bytes).first(end);
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::size_t put_head(bytes_sink<Target> const out, std::size_t const at,
                                                   major_type const major, std::uint64_t const argument)
    {
        std::size_t const need = detail::heads::head_size(argument);
        if (!room(out, at, need)) [[unlikely]]
            return 0;
        return put_head(written_part(out, at + need), at, major, argument);
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::size_t put_head(bytes_sink<Target> const out, std::size_t const at,
                                                   major_type const major, std::uint8_t const info,
                                                   std::uint64_t const argument)
    {
        std::size_t const need = detail::heads::initial_byte_size + detail::heads::argument_size(info);
        if (!room(out, at, need)) [[unlikely]]
            return 0;
        return put_head(written_part(out, at + need), at, major, info, argument);
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::size_t put_simple(bytes_sink<Target> const out, std::size_t const at,
                                                     simple_value const value)
    {
        if (!room(out, at, detail::heads::initial_byte_size)) [[unlikely]]
            return at;
        return put_simple(std::span<char>(out.bytes), at, value);
    }

    template <class Target, class Bytes>
    CBOR_ALWAYS_INLINE static std::size_t put_bytes(bytes_sink<Target> const out, std::size_t const at,
                                                    Bytes const bytes)
    {
        if (!room(out, at, bytes.size())) [[unlikely]]
            return at;
        return put_bytes(std::span<char>(out.bytes), at, bytes);
    }

    template <class Target, class E>
    CBOR_ALWAYS_INLINE static std::size_t put_typed_array_element(bytes_sink<Target> const out,
                                                                  std::size_t const at, E const e)
    {
        if (!room(out, at, sizeof(E))) [[unlikely]]
            return at;
        return put_typed_array_element(std::span<char>(out.bytes), at, e);
    }

    template <class Objects>
    struct null_sink {
        Objects &objects;
    };

    template <class Objects>
    static std::size_t put_head(null_sink<Objects>, std::size_t, major_type, std::uint64_t)
    {
        return 0;
    }

    template <class Objects>
    static std::size_t put_head(null_sink<Objects>, std::size_t, major_type, std::uint8_t, std::uint64_t)
    {
        return 0;
    }

    template <class Objects>
    static std::size_t put_simple(null_sink<Objects>, std::size_t const at, simple_value)
    {
        return at;
    }

    template <class Objects, class Bytes>
    static std::size_t put_bytes(null_sink<Objects>, std::size_t const at, Bytes)
    {
        return at;
    }

    template <class Objects, class E>
    static std::size_t put_typed_array_element(null_sink<Objects>, std::size_t const at, E)
    {
        return at;
    }

    template <class Inner, class Objects>
    struct marking_sink {
        Inner inner;
        Objects &objects;
    };

    template <class Inner, class Objects>
    CBOR_ALWAYS_INLINE static std::size_t put_head(marking_sink<Inner, Objects> const out, std::size_t const at,
                                                   major_type const major, std::uint64_t const argument)
    {
        return put_head(out.inner, at, major, argument);
    }

    template <class Inner, class Objects>
    CBOR_ALWAYS_INLINE static std::size_t put_head(marking_sink<Inner, Objects> const out, std::size_t const at,
                                                   major_type const major, std::uint8_t const info,
                                                   std::uint64_t const argument)
    {
        return put_head(out.inner, at, major, info, argument);
    }

    template <class Inner, class Objects>
    CBOR_ALWAYS_INLINE static std::size_t put_simple(marking_sink<Inner, Objects> const out, std::size_t const at,
                                                     simple_value const value)
    {
        return put_simple(out.inner, at, value);
    }

    template <class Inner, class Objects, class Bytes>
    CBOR_ALWAYS_INLINE static std::size_t put_bytes(marking_sink<Inner, Objects> const out, std::size_t const at,
                                                    Bytes const bytes)
    {
        return put_bytes(out.inner, at, bytes);
    }

    template <class Inner, class Objects, class E>
    CBOR_ALWAYS_INLINE static std::size_t put_typed_array_element(marking_sink<Inner, Objects> const out,
                                                                  std::size_t const at, E const e)
    {
        return put_typed_array_element(out.inner, at, e);
    }

    template <class Out, class U>
    static constexpr bool tracked_in =
        requires { requires std::remove_reference_t<decltype(std::declval<Out>().objects)>::template tracks<U>; };

    template <class Out, class U>
    static std::expected<std::size_t, error> write_generic(Out const out, std::size_t const at, U const &value)
    {
        if constexpr (tracked_in<Out, U>)
            return write_tracked(out, at, value);
        else
            return write_value(out, at, value);
    }

    template <class Objects, class U>
    static std::expected<std::size_t, error> write_tracked(null_sink<Objects> const out, std::size_t const at,
                                                           U const &value)
    {
        identity const id = identity_of(value);
        out.objects.on_path.insert(id);
        auto const written = write_value(out, at, value);
        out.objects.on_path.erase(id);
        return written;
    }

    template <class Inner, class Objects, class U>
    static std::expected<std::size_t, error> write_tracked(marking_sink<Inner, Objects> const out, std::size_t at,
                                                           U const &value)
    {
        identity const id = identity_of(value);
        if (!out.objects.targets.contains(id))
            return write_value(out, at, value);
        at += put_head(out, at, major_type::tag, std::to_underlying(rfc8949::tag_number::shareable));
        out.objects.open.emplace(id, out.objects.marked++);
        auto const written = write_value(out, at, value);
        out.objects.open.erase(id);
        return written;
    }

    template <class Objects, class P>
    static std::expected<std::size_t, error> write_reference(null_sink<Objects> const out, std::size_t const at,
                                                             P const &pointer)
    {
        identity const id = identity_of(*pointer);
        if constexpr (is_smart_pointer<P>) {
            if (out.objects.on_path.contains(id)) [[unlikely]]
                return std::unexpected(error::cyclic_data_structure);
        } else if (out.objects.on_path.contains(id)) {
            out.objects.targets.insert(id);
            return at;
        }
        return write_generic(out, at, *pointer);
    }

    template <class Inner, class Objects, class P>
    static std::expected<std::size_t, error> write_reference(marking_sink<Inner, Objects> const out, std::size_t at,
                                                             P const &pointer)
    {
        auto const open = out.objects.open.find(identity_of(*pointer));
        if (open == out.objects.open.end())
            return write_generic(out, at, *pointer);
        at += put_head(out, at, major_type::tag, std::to_underlying(rfc8949::tag_number::sharedref));
        return at + put_head(out, at, major_type::unsigned_integer, open->second);
    }

    template <class Out, class U>
    static std::expected<std::size_t, error> write_value(Out const out, std::size_t at, U const &value)
    {
        if constexpr (!std::is_arithmetic_v<U> && !std::is_enum_v<U> && !is_basic_string<U> &&
                      !std::same_as<U, std::string_view> && !std::same_as<U, std::span<std::byte const>>)
            if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
                return std::unexpected(r.error());
        if constexpr (has_describe_value<U>) {
            static_assert(!has_describe_members<U>,
                          "cbor::describe_members holds either members or a value, not both.");
            static_assert(detail::reflection::tag_valid<U>());
            if constexpr (constexpr std::optional<std::uint64_t> tag = detail::reflection::tag_number_of<U>();
                          tag.has_value())
                at += put_head(out, at, major_type::tag, *tag);
            return write_value(out, at, describe_members<U>::value(value));
        } else if constexpr (std::same_as<U, bool>) {
            return put_simple(out, at, value ? simple_value::true_value : simple_value::false_value);
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            return put_simple(out, at, simple_value::null);
        } else if constexpr (is_object_pointer<U>) {
            if (!value)
                return put_simple(out, at, simple_value::null);
            if constexpr (tracked_in<Out, pointee_t<U>>)
                return write_reference(out, at, value);
            else
                return write_generic(out, at, *value);
        } else if constexpr (std::same_as<U, simple_value>) {
            return at + put_head(out, at, major_type::simple, std::to_underlying(value));
#ifdef CBOR_HAS_INT128
        } else if constexpr (detail::reflection::is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? ~static_cast<uint128>(value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return at + put_head(out, at,
                                       negative ? major_type::negative_integer : major_type::unsigned_integer,
                                       static_cast<std::uint64_t>(magnitude));
            std::size_t const digits =
                sizeof(uint128) - static_cast<std::size_t>(std::countl_zero(static_cast<std::uint64_t>(magnitude >> std::numeric_limits<std::uint64_t>::digits))) /
                                      std::numeric_limits<std::uint8_t>::digits;
            at += put_head(out, at, major_type::tag,
                                    std::to_underlying(negative ? rfc8949::tag_number::negative_bignum
                                                                : rfc8949::tag_number::unsigned_bignum));
            at += put_head(out, at, major_type::byte_string, digits);
            auto const bytes = detail::heads::big_endian(magnitude);
            return put_bytes(out, at, std::span<char const>(bytes).last(digits));
#endif
        } else if constexpr (detail::is_std_variant<U>) {
            return std::visit([&](auto const &e) { return write_generic(out, at, e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return write_value(out, at, std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>) {
                if (value < 0)
                    return at + put_head(out, at, major_type::negative_integer,
                                           static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value)));
            }
            return at + put_head(out, at, major_type::unsigned_integer, static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            rfc8949::simple_information const info = detail::ieee754::preferred_float_info(value);
            return at + put_head(out, at, major_type::simple, std::to_underlying(info), detail::ieee754::encode_float(info, value));
        } else if constexpr (is_basic_string<U> || std::same_as<U, std::string_view>) {
            at += put_head(out, at, major_type::text_string, value.size());
            return put_bytes(out, at, std::span<char const>(value));
        } else if constexpr (std::same_as<U, std::span<std::byte const>>) {
            at += put_head(out, at, major_type::byte_string, value.size());
            return put_bytes(out, at, value);
        } else if constexpr (is_clock_time<U>) {
            auto const t = extended_time_of(value);
            if (!t) [[unlikely]]
                return std::unexpected(t.error());
            return write_extended_time(out, at, std::get<0>(*t), std::get<1>(*t), std::get<2>(*t));
        } else if constexpr (is_extended_time<U>) {
            return write_extended_time(out, at, value);
        } else if constexpr (is_tagged_standard_type<U>) {
            at += put_head(out, at, major_type::tag, tag_of_standard_type<U>());
            return write_tag_content(out, at, value);
        } else if constexpr (detail::reflection::is_optional<U>) {
            if (!value) {
                return put_simple(out, at, simple_value::null);
            }
            return write_generic(out, at, *value);
        } else if constexpr (is_tagged_item<U>) {
            at += put_head(out, at, major_type::tag, U::number);
            return write_generic(out, at, value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            at += put_head(out, at, major_type::array, std::tuple_size_v<U>);
            std::expected<std::size_t, error> written = at;
            std::apply([&](auto const &...e) {
                ((written = written.and_then([&](std::size_t const w) { return write_generic(out, w, e); })), ...);
            }, value);
            return written;
        } else if constexpr (is_multimap<U>) {
            auto groups = key_groups(value);
            at += put_head(out, at, major_type::map,
                                    std::ranges::fold_left(groups, std::uint64_t{0}, element_count));
            for (auto const group : groups) {
                auto const key = write_generic(out, at, group.front().first);
                if (!key) [[unlikely]]
                    return key;
                at = *key;
                at += put_head(out, at, major_type::array,
                                        std::ranges::fold_left(group, std::uint64_t{0}, element_count));
                for (auto const &[k, v] : group) {
                    auto const mapped = write_generic(out, at, v);
                    if (!mapped) [[unlikely]]
                        return mapped;
                    at = *mapped;
                }
            }
            return at;
        } else if constexpr (detail::reflection::is_map<U>) {
            at += put_head(out, at, major_type::map, value.size());
            for (auto const &[k, v] : value) {
                auto const pair = write_generic(out, at, k).and_then(
                    [&](std::size_t const w) { return write_generic(out, w, v); });
                if (!pair) [[unlikely]]
                    return pair;
                at = *pair;
            }
            return at;
        } else if constexpr (requires {
                                 requires std::ranges::sized_range<U>;
                                 requires is_std_pair<std::ranges::range_value_t<U>>;
                             }) {
            at += put_head(out, at, major_type::map, std::ranges::size(value));
            for (auto const &[k, v] : value) {
                auto const pair = write_generic(out, at, k).and_then([&](std::size_t const w) {
                    return write_generic(out, w, v);
                });
                if (!pair) [[unlikely]]
                    return pair;
                at = *pair;
            }
            return at;
        } else if constexpr (std::ranges::sized_range<U>) {
            return write_elements(out, at, value);
        } else if constexpr (std::ranges::range<U>) {
            static_assert(
                std::ranges::sized_range<U>,
                "cbor::encode writes a range only with its size: the library writes no indefinite length.");
            return at;
        } else {
            static_assert(detail::pfr::struct_writable<U, Reflected>());
            if constexpr (constexpr std::optional<std::uint64_t> tag = detail::reflection::tag_number_of<U>(); tag.has_value())
                at += put_head(out, at, major_type::tag, *tag);
            std::size_t const present = [&]<std::size_t... I>(std::index_sequence<I...>) {
                return (std::size_t{0} + ... +
                        (member_present(detail::pfr::get<I>(value)) ? std::size_t{1} : std::size_t{0}));
            }(std::make_index_sequence<detail::pfr::tuple_size_v<U>>());
            at += put_head(out, at, major_type::map, present);
            static constexpr auto order = key_order<U>();
            std::expected<std::size_t, error> written = at;
            [&]<std::size_t... J>(std::index_sequence<J...>) {
                ((written = written.and_then([&](std::size_t const w) { return write_member<order[J]>(out, w, value); })), ...);
            }(std::make_index_sequence<order.size()>());
            return written;
        }
    }

    template <class Out, class U>
    static std::expected<std::size_t, error> write_elements(Out const out, std::size_t at, U const &value)
    {
        at += put_head(out, at, major_type::array, std::ranges::size(value));
        for (auto const &e : value) {
            auto const element = write_generic(out, at, static_cast<std::ranges::range_value_t<U> const &>(e));
            if (!element) [[unlikely]]
                return element;
            at = *element;
        }
        return at;
    }

    template <class Out, class U>
    static std::expected<std::size_t, error> write_tag_content(Out const out, std::size_t at, U const &value)
    {
        if constexpr (is_whole_seconds<U>) {
            return write_value(out, at, value.time_since_epoch().count());
        } else if constexpr (std::same_as<U, std::chrono::year_month_day>) {
            if (!value.ok()) [[unlikely]]
                detail::validity::throw_logic_error("cbor::encode: the year_month_day is no valid date");
            return write_value(out, at, std::chrono::sys_days{value}.time_since_epoch().count());
        } else if constexpr (is_set<U>) {
            return write_elements(out, at, value);
        } else {
            using E = typename U::value_type;
            at += put_head(out, at, major_type::byte_string, value.size() * sizeof(E));
            for (E const e : value)
                at = put_typed_array_element(out, at, e);
            return at;
        }
    }

    template <std::size_t I, class Out, class U>
    static std::expected<std::size_t, error> write_member(Out const out, std::size_t at, U const &value)
    {
        auto const &m = detail::pfr::get<I>(value);
        if (!member_present(m))
            return at;
        constexpr map_key key = detail::pfr::key_of<I, U>();
        at += put_head(out, at, key.major, key.argument);
        at = put_bytes(out, at, std::span<char const>(key.text));
        if constexpr (detail::reflection::is_optional<std::remove_cvref_t<decltype(m)>>)
            return write_generic(out, at, *m);
        else
            return write_generic(out, at, m);
    }

    template <class T>
    static std::expected<cycle_marks<T>, error> marks_of(T const &value)
    {
        back_edge_search<T> search;
        auto const searched = write_generic(null_sink<back_edge_search<T>>{search}, 0, value);
        if (!searched) [[unlikely]]
            return std::unexpected(searched.error());
        return cycle_marks<T>{std::move(search.targets), {}, 0};
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> append_message(Target &&out,
                                                                               std::string const &encoded)
    {
        decltype(auto) message = detail::message_of(out, encoded.size());
        if (auto const r = message.append(encoded); !r) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(r.error()));
        if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(r.error()));
        return encoded.size();
    }

public:
    template <class R>
    static constexpr bool holds_raw_pointer = any_reached<[]<class V>() {
        if constexpr (std::is_pointer_v<V>)
            return is_object_pointer<V>;
        else
            return false;
    }, R>();

    template <class T>
    static std::expected<std::string, error> edn_of(T const &value)
    {
        detail::edn_text text;
        if (auto const r = write_generic(edn_out{text}, 0, value); !r) [[unlikely]]
            return std::unexpected(r.error());
        return text.release();
    }

    template <class T>
    CBOR_ALWAYS_INLINE static std::expected<std::string, error> encode_to_string(T const &value)
    {
        std::string out;
        error failure{};
        auto const written = write_generic(bytes_sink<std::string>{out, failure}, 0, value);
        if (!written) [[unlikely]]
            return std::unexpected(written.error());
        if (failure != error{}) [[unlikely]]
            return std::unexpected(failure);
        out.resize(*written);
        return out;
    }

    template <class Target, class T>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> encode_into(Target &&out, T const &value)
    {
        using U = std::remove_cvref_t<Target>;
        if constexpr (!detail::byte_container<U> && requires { std::span<char>(out); }) {
            std::span<char> bytes(out);
            error failure{};
            auto const written = write_generic(bytes_sink<std::span<char>>{bytes, failure}, 0, value);
            if (!written) [[unlikely]]
                return std::unexpected(written.error());
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return *written;
        } else {
            auto const encoded = encode_to_string(value);
            if (!encoded) [[unlikely]]
                return std::unexpected(encoded.error());
            return append_message(std::forward<Target>(out), *encoded);
        }
    }

    template <class T>
    static std::expected<std::string, error> encode_cycles_to_string(T const &value)
    {
        if constexpr (!holds_pointer<T>) {
            return encode_to_string(value);
        } else {
            auto marks = marks_of(value);
            if (!marks) [[unlikely]]
                return std::unexpected(marks.error());
            std::string out;
            error failure{};
            auto const written =
                write_generic(marking_sink<bytes_sink<std::string>, cycle_marks<T>>{{out, failure}, *marks}, 0, value);
            if (!written) [[unlikely]]
                return std::unexpected(written.error());
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            out.resize(*written);
            return out;
        }
    }

    template <class Target, class T>
    static std::expected<std::size_t, error> encode_cycles_into(Target &&out, T const &value)
    {
        using U = std::remove_cvref_t<Target>;
        if constexpr (!holds_pointer<T>) {
            return encode_into(std::forward<Target>(out), value);
        } else if constexpr (!detail::byte_container<U> && requires { std::span<char>(out); }) {
            auto marks = marks_of(value);
            if (!marks) [[unlikely]]
                return std::unexpected(marks.error());
            std::span<char> bytes(out);
            error failure{};
            auto const written = write_generic(
                marking_sink<bytes_sink<std::span<char>>, cycle_marks<T>>{{bytes, failure}, *marks}, 0, value);
            if (!written) [[unlikely]]
                return std::unexpected(written.error());
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return *written;
        } else {
            auto const encoded = encode_cycles_to_string(value);
            if (!encoded) [[unlikely]]
                return std::unexpected(encoded.error());
            return append_message(std::forward<Target>(out), *encoded);
        }
    }
};

using generic = basic_generic<false>;

template <class T>
    requires(!is_view<T> && basic_generic<true>::holds_raw_pointer<T>)
struct ref_targets<T> {
    T const *in_block = nullptr;
    using kept = kept_objects;
};

}

/**
 * @brief Writes `value` as one CBOR item in preferred serialization, into a new `std::string`.
 *
 * `T` is one of the types of the table of cbor::structs. A struct is written only through its
 * cbor::describe_members specialization; an aggregate without one does not compile, also where reflection or
 * Boost.PFR is present. A struct is a map, and its keys come in the order of RFC 8949 4.2.1.
 * A cbor::item and a cbor::owning<cbor::item> take the overload that writes a tree.
 * A `U*`, `std::unique_ptr<U>` or `std::shared_ptr<U>` writes null when it is empty, and else the value of its
 * target. The call writes an object once for each path from `value` to it, so the size of the output and the run
 * time grow with the number of paths. A cycle through pointers recurses until the stack of the thread is full, and
 * the call then gives nesting_depth_exceeded. cbor::encode_cycles() writes a cycle with tags 28 and 29.
 *
 * @code{.example}
 * message: {"name": "ann", "age": 30}
 * call: auto const encoded = cbor::encode<person>(person{"ann", 30})
 * result: 16 bytes: a2 63 61 67 65 18 1e 64 6e 61 6d 65 63 61 6e 6e
 * call: cbor::lazy::adopt(std::string(*encoded))
 * result: lazy of {"age": 30, "name": "ann"}
 * @endcode
 *
 * @retval value_too_large the size of the bytes does not fit in `std::size_t`
 * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds, or a pointer in it leads
 * back into the value (a cycle)
 * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
 * @throws std::bad_alloc a refused allocation of the result
 * @snippet doc_reference_structs.cpp encode
 * @see cbor::structs::decode()
 * @see cbor::describe_members
 *
 * @ingroup encode
 */
template <class T>
    requires(!std::same_as<T, item> && !std::same_as<T, owning<item>>)
CBOR_ALWAYS_INLINE inline result<std::string> encode(T const &value)
{
    return result<std::string>(detail::generic::encode_to_string(value));
}

/**
 * @brief Appends `value` as one CBOR item in preferred serialization to `out`, and gives the count of bytes
 * that it appended.
 *
 * The item is that of cbor::encode(T const &): a pointer writes the value of its target once for each path to it, so
 * the size of the output and the run time grow with the number of paths, and a cycle through pointers gives
 * nesting_depth_exceeded.
 * `out` is a container of bytes, for example `std::string`, a `std::span` of bytes, or a writer.
 *
 * @retval no_buffer_space a `std::span` that is too small for the bytes, or the writer gives this error; the
 * span then holds the bytes written before the first one that did not fit
 * @retval value_too_large the size of the bytes does not fit in `std::size_t`, or the writer gives this error
 * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds, or a pointer in it leads
 * back into the value (a cycle)
 * @retval not_enough_memory, io_error the writer gives this error
 * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
 * @see cbor::encode(T const &)
 *
 * @ingroup encode
 */
template <class T, class Target>
    requires(detail::is_target_v<std::remove_reference_t<Target>> && !std::same_as<T, item> &&
             !std::same_as<T, owning<item>>)
CBOR_ALWAYS_INLINE inline result<std::size_t> encode(Target &&out, T const &value)
{
    return result<std::size_t>(detail::generic::encode_into(std::forward<Target>(out), value));
}

/**
 * @brief Gives the text of `value` in the extended diagnostic notation (EDN): the text that
 * cbor::diagnostic_notation() gives for the bytes of cbor::encode(T const &).
 *
 * The call writes the text from the value, with the type dispatch of cbor::encode, and makes no bytes. It takes the
 * types that cbor::encode takes; a cbor::item and a cbor::owning<cbor::item> take the overload that walks a tree.
 * A pointer writes the text of its target once for each path to it, so the size of the text and the run time grow
 * with the number of paths, and a cycle through pointers gives nesting_depth_exceeded.
 *
 * @code{.example}
 * call: cbor::to_edn<reading>(reading{4, std::nullopt})
 * result: "{\"id\": 4}"
 * @endcode
 *
 * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds, or a pointer in it leads
 * back into the value (a cycle)
 * @retval number_out_of_range a time that its tag cannot hold, as cbor::encode gives it
 * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
 * @see cbor::encode(T const &)
 * @see cbor::to_edn(item)
 *
 * @ingroup tools
 */
template <class T>
    requires(!std::same_as<T, item> && !std::same_as<T, owning<item>>)
inline result<std::string> to_edn(T const &value)
{
    return result<std::string>(detail::generic::edn_of(value));
}

/**
 * @brief Writes `value` as cbor::encode(T const &) does, and writes each cycle through a pointer with tags 28 and 29,
 * into a new `std::string`.
 *
 * A pointer closes a cycle when its target is an object whose write has not ended. That object is written under
 * tag 28, and the pointer writes tag 29 with the index of that tag 28 (http://cbor.schmorp.de/value-sharing). The
 * index counts the tags 28 of the call in the order in which they are written. Each other pointer writes the value
 * of its target, as cbor::encode does: an object that two pointers reach outside a cycle is written twice, and an
 * object on a cycle gets a tag 28 on each path to it. Such a tag 28 can stand with no tag 29 that names it.
 * cbor::decode_cycles() reads the bytes. The call walks the value twice, and the first walk writes no bytes. A `T`
 * that holds no pointer gives the bytes of cbor::encode.
 *
 * @retval value_too_large the size of the bytes does not fit in `std::size_t`
 * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds
 * @retval cyclic_data_structure a `std::unique_ptr` or a `std::shared_ptr` closes a cycle
 * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
 * @throws std::bad_alloc a refused allocation of the result or of the table of the objects
 * @see cbor::encode(T const &)
 * @see cbor::decode_cycles()
 *
 * @ingroup encode
 */
template <class T>
    requires(!std::same_as<T, item> && !std::same_as<T, owning<item>>)
inline result<std::string> encode_cycles(T const &value)
{
    return result<std::string>(detail::generic::encode_cycles_to_string(value));
}

/**
 * @brief Appends `value` as cbor::encode_cycles(T const &) writes it to `out`, and gives the count of bytes that it
 * appended.
 *
 * `out` is a container of bytes, for example `std::string`, a `std::span` of bytes, or a writer.
 *
 * @retval no_buffer_space a `std::span` that is too small for the bytes, or the writer gives this error; the
 * span then holds the bytes written before the first one that did not fit
 * @retval value_too_large the size of the bytes does not fit in `std::size_t`, or the writer gives this error
 * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds
 * @retval cyclic_data_structure a `std::unique_ptr` or a `std::shared_ptr` closes a cycle
 * @retval not_enough_memory, io_error the writer gives this error
 * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
 * @throws std::bad_alloc a refused allocation of the table of the objects
 * @see cbor::encode_cycles(T const &)
 *
 * @ingroup encode
 */
template <class T, class Target>
    requires(detail::is_target_v<std::remove_reference_t<Target>> && !std::same_as<T, item> &&
             !std::same_as<T, owning<item>>)
inline result<std::size_t> encode_cycles(Target &&out, T const &value)
{
    return result<std::size_t>(detail::generic::encode_cycles_into(std::forward<Target>(out), value));
}

/**
 * @brief Reads a C++ value back from CBOR that cbor::encode wrote.
 *
 * `T`, and each member of a struct in `T`, is one of the types in this table.
 * cbor::encode writes the item of the right column; it writes a struct only through its cbor::describe_members.
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
 * | `std::chrono::sys_time<D>`, any period, an integral or floating rep | whole seconds: tag 1 on the integer; an integral rep of a millisecond to an attosecond: tag 1001 (RFC 9581) with the seconds and the fraction; a floating rep: tag 1 on the `double` when it is exact. `decode` also takes tag 0 where the C++ library has `std::chrono::parse`, tag 1001 and tag 100. A value that D cannot hold exactly gives `number_out_of_range` |
 * | `std::chrono::duration<R, P>` | tag 1002 (RFC 9581) with the seconds and the fraction |
 * | `std::chrono::tai_time<D>`, `std::chrono::gps_time<D>` | tag 1001 (RFC 9581) with the timescale TAI; GPS converts by the constant of RFC 9581 3.4 |
 * | `std::chrono::utc_time<D>` | tag 1001 with the timescale TAI, through the leap second table of the C++ library; exact in a leap second |
 * | `std::chrono::file_time<D>` | as the `sys_time` that `std::chrono::clock_cast` gives |
 * | `std::chrono::zoned_time<D>` | tag 1001 with its time in UTC and the time zone name under key -10 |
 * | `std::chrono::year_month_day` | tag 100 on an integer of days; `decode` also takes tag 1004 where the C++ library has `std::chrono::parse` |
 * | `std::optional<E>` | the item of E, or `null`; no key when empty |
 * | `std::variant<E...>` | the item of the alternative |
 * | `std::tuple<E...>`, `std::array<E, N>` | an array of that count |
 * | a map type; a multimap type | a map; a map to arrays |
 * | another container | an array |
 * | `cbor::tagged_item<N, E>` | tag N on the item of E |
 * | `U*`, `std::unique_ptr<U>`, `std::shared_ptr<U>` | `null` when empty, else the item of U |
 * | a struct | a map; a struct in a struct is a map in a map |
 * | a struct that holds a `std::vector` of itself | a map whose arrays hold maps of the same form, at any depth |
 *
 * `decode` gives `incorrect_type` for another item: a `std::vector<int>` takes a typed array, not an array.
 * An enum with no fixed underlying type does not compile in `decode`.
 * A `long double` and a struct under `tagged_item` do not compile.
 *
 * @code{.example}
 * message: {"id": 4}
 * call: auto const r = cbor::encode<reading>(reading{4, std::nullopt})
 * result: 5 bytes: a1 62 69 64 04
 * call: cbor::lazy::adopt(std::string(*r))
 * result: lazy of {"id": 4}
 * call: auto const s = cbor::encode<std::set<std::uint8_t>>(std::set<std::uint8_t>{3, 1})
 * result: 6 bytes: d9 01 02 82 01 03
 * call: cbor::lazy::adopt(std::string(*s))
 * result: lazy of 258([1, 3])
 * call: auto const l = cbor::encode<line>(line{point{1, 2}, point{3, 4}})
 * result: 37 bytes: a2 62 74 6f d9 07 0a a2 61 78 f9 42 00 61 79 f9 44 00 64 66 72 6f 6d d9 07 0a a2 61 78 f9 3c 00 61 79 f9 40 00
 * call: cbor::lazy::adopt(std::string(*l))
 * result: lazy of {"to": 1802({"x": 3.0, "y": 4.0}), "from": 1802({"x": 1.0, "y": 2.0})}
 * call: auto const n = cbor::encode<node>(node{"root", std::vector<node>{node{"a", std::vector<node>{}}}})
 * result: 39 bytes: a2 64 6e 61 6d 65 64 72 6f 6f 74 68 63 68 69 6c 64 72 65 6e 81 a2 64 6e 61 6d 65 61 61 68 63 68 69 6c 64 72 65 6e 80
 * call: cbor::lazy::adopt(std::string(*n))
 * result: lazy of {"name": "root", "children": [{"name": "a", "children": []}]}
 * call: cbor::structs<reading>::decode(std::move(bytes))
 * result: ref to reading{4, std::nullopt}
 * @endcode
 *
 * @snippet doc_reference_structs.cpp structs
 * @see cbor::encode(T const &)
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
     * A tag 55799 (RFC 8949, section 3.4.6) has no semantics. `decode` reads through any number of them to
     * their content. It does not read through one where it is the content of a tag 2, a tag 3 or a typed
     * array. There the content is not a byte string. A key under tag 55799 matches as the key without the
     * tag.
     * A repeated key gives `duplicate_key`, except in an unordered multimap, which appends the values of each repeat.
     * A container reserves storage for at most as many elements as the bytes left after its head can hold, by the
     * least encoding of one element. While an item is read, each open container holds no more than that; the open
     * containers are at most as many as the nesting of the input, which the stack limit bounds. After a decode that
     * succeeds, each container holds storage for no more elements than it read.
     * A `U*`, `std::unique_ptr<U>` or `std::shared_ptr<U>` is empty for null, and else points to a new `U` that holds
     * the item: from `std::make_unique`, from `std::make_shared`, or for a `U*` an object that the owner of the
     * cbor::ref keeps. A `U*` is valid while a copy of the cbor::ref lives. Each pointer gets its own object.
     * `decode` reads no tag 28 and no tag 29; cbor::structs::decode_cycles() reads them.
     *
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
     * @retval invalid_tag a value under tag 28 or tag 29
     * @retval string_length_exceeded a string is longer than `limits::string_length`
     * @retval container_elements_exceeded the count of an array or a map is greater than `limits::container_elements`
     * @throws std::bad_alloc a refused allocation
     * @snippet doc_reference_structs.cpp decode
     * @see cbor::encode(T const &)
     * @see cbor::ref
     */
    template <owning_range_of_bytes Encoded>
    static result<ref<T>> decode(Encoded &&encoded)
    {
        return decode(std::make_shared<Encoded const>(std::move(encoded)));
    }

    /**
     * @brief Reads one item into a `T` from the bytes that @p encoded owns.
     * @throws std::logic_error @p encoded is null
     */
    template <owning_range_of_bytes Encoded>
    static result<ref<T>> decode(std::shared_ptr<Encoded const> encoded)
    {
        detail::pfr::check_default_constructible<T>();
        return decode(std::move(encoded), T{});
    }

    /**
     * @brief Reads one item into @p target from the bytes that @p encoded owns.
     * @param target the value whose containers and allocator the read uses
     * @throws std::logic_error @p encoded is null
     */
    template <owning_range_of_bytes Encoded>
    static result<ref<T>> decode(std::shared_ptr<Encoded const> encoded, T &&target)
    {
        detail::validity::throw_logic_error_if_null(encoded,
                                                    "cbor::structs::decode: the encoded data item is null");
        std::string_view const bytes = detail::chars_of(*encoded);
        return decode(std::shared_ptr<void const>(std::move(encoded)), bytes, std::move(target));
    }

    /**
     * @brief Reads one item into a `T` from bytes that @p owner keeps alive.
     * @throws std::logic_error @p owner is empty
     */
    template <borrowed_range_of_bytes Encoded>
    static result<ref<T>> decode(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        detail::pfr::check_default_constructible<T>();
        return decode(std::move(owner), encoded, T{});
    }

    /**
     * @brief Reads one item into @p target from bytes that @p owner keeps alive.
     * @param target the value whose containers and allocator the read uses
     * @throws std::logic_error @p owner is empty
     */
    template <borrowed_range_of_bytes Encoded>
    static result<ref<T>> decode(std::shared_ptr<void const> owner, Encoded const &encoded, T &&target)
    {
        return result<ref<T>>(decode_with_owner(std::move(owner), detail::chars_of(encoded), std::move(target)));
    }

    /**
     * @brief Reads one item into a `T` as decode() does, and resolves its tags 28 and 29
     * (http://cbor.schmorp.de/value-sharing).
     *
     * A tag 28 marks the value under it. It stands on any value; the index of a tag 29 counts the tags 28 in the
     * order of the bytes, also those in a skipped key or value. A tag 29 at a `U*` gives the address of the `U` that
     * its tag 28 marks, while the read of that `U` is open: the `U*` closes a cycle. cbor::encode_cycles() writes such
     * bytes. A `U*` reaches an object of the value only where the read does not move that object: in a member, in a
     * `std::array`, `std::tuple`, `std::optional` or `std::variant`, in an element of a `std::vector`, a `std::deque`
     * or a `std::list`, and in the target of a pointer. When `T` holds a `U*`, the cbor::ref keeps its value in its
     * owner, so the address of the value stays when the cbor::ref is copied or moved.
     * The forms take the bytes as decode() takes them.
     *
     * @code{.example}
     * message: 28({"self": 29(0)})
     * call: auto const r = cbor::structs<loop>::decode_cycles(std::move(bytes))
     * result: ref to a loop whose self is its own address
     * call: (*r)->self == &**r
     * result: true
     * @endcode
     *
     * @retval incorrect_type, number_out_of_range, key_not_found, duplicate_key, too_little_data, syntax_error,
     * indefinite_length, nesting_depth_exceeded, inadmissible_type_for_tag_content, string_length_exceeded,
     * container_elements_exceeded as decode()
     * @retval incorrect_type a tag 29 at a value that is no pointer, at a `std::unique_ptr`, or that names a tag 28 of
     * another type or of a value whose address the read does not keep
     * @retval sharedref_index_not_marked a tag 29 whose index names no tag 28 before it
     * @retval sharedref_not_cyclic a tag 29 that names a tag 28 whose value is read to its end, so no cycle
     * @retval cyclic_data_structure a tag 29 at a `std::shared_ptr` that names a value whose read is open
     * @throws std::bad_alloc a refused allocation
     * @see cbor::encode_cycles()
     * @see decode()
     */
    template <owning_range_of_bytes Encoded>
    static result<ref<T>> decode_cycles(Encoded &&encoded)
    {
        return decode_cycles(std::make_shared<Encoded const>(std::move(encoded)));
    }

    /**
     * @brief Reads one item into a `T` from the bytes that @p encoded owns, and resolves its tags 28 and 29.
     * @throws std::logic_error @p encoded is null
     */
    template <owning_range_of_bytes Encoded>
    static result<ref<T>> decode_cycles(std::shared_ptr<Encoded const> encoded)
    {
        detail::validity::throw_logic_error_if_null(encoded,
                                                    "cbor::structs::decode_cycles: the encoded data item is null");
        std::string_view const bytes = detail::chars_of(*encoded);
        return decode_cycles(std::shared_ptr<void const>(std::move(encoded)), bytes);
    }

    /**
     * @brief Reads one item into a `T` from bytes that @p owner keeps alive, and resolves its tags 28 and 29.
     * @throws std::logic_error @p owner is empty
     */
    template <borrowed_range_of_bytes Encoded>
    static result<ref<T>> decode_cycles(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        return result<ref<T>>(decode_cycles_with_owner(std::move(owner), detail::chars_of(encoded)));
    }

    /**
     * @brief Writes @p value as one CBOR item in preferred serialization, into a new `std::string`.
     *
     * A struct is a map, and its keys come in the order of RFC 8949 4.2.1.
     * The second form appends to `out` and returns the count of bytes that it appended.
     * `out` is a `std::string`, a container of `char`, a `std::span<char>` or a writer.
     * A type that `structs` does not take does not compile.
     * A pointer writes null when it is empty, and else the value of its target once for each path to it, so the
     * size of the output and the run time grow with the number of paths. A cycle through pointers recurses until the
     * stack of the thread is full, and the call then gives nesting_depth_exceeded.
     *
     * @code{.example}
     * message: {"name": "ann", "age": 30}
     * call: auto const encoded = cbor::structs<person>::encode(person{"ann", 30})
     * result: 16 bytes: a2 63 61 67 65 18 1e 64 6e 61 6d 65 63 61 6e 6e
     * call: cbor::lazy::adopt(std::string(*encoded))
     * result: lazy of {"age": 30, "name": "ann"}
     * @endcode
     *
     * @retval value_too_large the size of the bytes does not fit in `std::size_t`
     * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds, or a pointer in it
     * leads back into the value (a cycle)
     * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
     * @throws std::bad_alloc a refused allocation of the result
     * @snippet doc_reference_structs.cpp encode
     * @see cbor::structs::decode()
     */
    CBOR_ALWAYS_INLINE static result<std::string> encode(T const &value)
    {
        return result<std::string>(detail::basic_generic<true>::encode_to_string(value));
    }

    /**
     * @brief Appends @p value as one CBOR item in preferred serialization to @p out.
     * @param out a `std::string`, a container of `char`, a `std::span<char>` or a writer. A `T` that is itself such
     * a type has only the form that returns a new `std::string`.
     * @return the count of bytes that it appended
     * @retval no_buffer_space a `std::span<char>` that is too small for the bytes, or the writer gives this error
     * @retval value_too_large the size of the bytes does not fit in `std::size_t`, or the writer gives this error
     * @retval nesting_depth_exceeded the value is deeper than the stack of the thread holds, or a pointer in it
     * leads back into the value (a cycle)
     * @retval not_enough_memory, io_error the writer gives this error
     * @throws std::logic_error a `std::chrono::year_month_day` that is no valid date
     */
    template <class Target>
        requires(detail::is_target_v<std::remove_reference_t<Target>> &&
                 !detail::is_target_v<T>)
    CBOR_ALWAYS_INLINE static result<std::size_t> encode(Target &&out, T const &value)
    {
        return result<std::size_t>(detail::basic_generic<true>::encode_into(std::forward<Target>(out), value));
    }

private:
    struct owner_of_targets {
        std::shared_ptr<void const> bytes;
        detail::kept_objects targets;
    };

    struct block_of_value {
        std::shared_ptr<void const> bytes;
        detail::kept_objects targets;
        T value;
    };

    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<void const> owner,
                                                     std::string_view const encoded, T &&target)
    {
        detail::validity::throw_logic_error_if_empty(
            owner, "cbor::structs::decode: the owner of the encoded data item is empty");
        if constexpr (detail::basic_generic<true>::holds_raw_pointer<T>) {
            auto const held = std::make_shared<owner_of_targets>(std::move(owner), detail::kept_objects{});
            auto value = read(encoded, held->targets, std::move(target));
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            return ref<T>(held, std::move(*value));
        } else {
            auto value = read(encoded, std::move(target));
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            return ref<T>(std::move(owner), std::move(*value));
        }
    }

    static std::expected<T, error> read(std::string_view const encoded, T out)
    {
        detail::heads::decoder<true> d{encoded};
        if (auto const r = detail::generic::read_generic(d, out); !r) [[unlikely]]
            return std::unexpected(r.error());
        return out;
    }

    static std::expected<T, error> read(std::string_view const encoded, detail::kept_objects &targets, T out)
    {
        detail::generic::kept_targets objects{targets};
        detail::generic::object_decoder<detail::generic::kept_targets> d{{encoded}, objects};
        if (auto const r = detail::generic::read_generic(d, out); !r) [[unlikely]]
            return std::unexpected(r.error());
        return out;
    }

    static std::expected<void, error> read_cycles(std::string_view const encoded, detail::kept_objects &targets,
                                                  T &out)
    {
        detail::generic::shared_values objects{targets, {}, true};
        detail::generic::object_decoder<detail::generic::shared_values> d{{encoded}, objects};
        return detail::generic::read_generic(d, out);
    }

    static std::expected<ref<T>, error> decode_cycles_with_owner(std::shared_ptr<void const> owner,
                                                                 std::string_view const encoded)
    {
        detail::validity::throw_logic_error_if_empty(
            owner, "cbor::structs::decode_cycles: the owner of the encoded data item is empty");
        detail::pfr::check_default_constructible<T>();
        if constexpr (detail::basic_generic<true>::holds_raw_pointer<T>) {
            auto const held = std::make_shared<block_of_value>(std::move(owner), detail::kept_objects{}, T{});
            if (auto const r = read_cycles(encoded, held->targets, held->value); !r) [[unlikely]]
                return std::unexpected(r.error());
            ref<T> decoded(held, T{});
            decoded.targets.in_block = &held->value;
            return decoded;
        } else {
            detail::kept_objects targets;
            T value{};
            if (auto const r = read_cycles(encoded, targets, value); !r) [[unlikely]]
                return std::unexpected(r.error());
            return ref<T>(std::move(owner), std::move(value));
        }
    }
};

namespace detail
{

template <class T>
ref<T> copy_with_cycles(T const &value)
{
    auto encoded = basic_generic<true>::encode_cycles_to_string(value);
    if (!encoded) [[unlikely]]
        throw std::system_error(make_error_code(encoded.error()));
    auto decoded = structs<T>::decode_cycles(std::move(*encoded));
    if (!decoded) [[unlikely]]
        throw std::system_error(make_error_code(decoded.error()));
    return *std::move(decoded);
}

}


namespace detail
{

template <class T, class Kept>
void copy_views_into_zone(T &value, string_saver &zone, Kept &kept)
{
    if constexpr (std::is_same_v<T, std::string_view>) {
        value = zone.save(value, detail::validity::string_saver_chunk_size);
    } else if constexpr (std::is_same_v<T, std::span<std::byte const>>) {
        value = zone.save(value, detail::validity::string_saver_chunk_size);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        detail::copy_views_into_zone(value.bytes, zone, kept);
    } else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || detail::generic::is_sys_time<T> ||
                         detail::generic::is_clock_time<T> || detail::generic::is_duration<T> ||
                         std::is_same_v<T, std::nullptr_t> ||
                         requires { typename T::traits_type; typename T::allocator_type; }) {
    } else if constexpr (requires { typename T::value_type; value.has_value(); *value; }) {
        if (value)
            detail::copy_views_into_zone(*value, zone, kept);
    } else if constexpr (detail::is_std_variant<T>) {
        std::visit([&zone, &kept](auto &alternative) { detail::copy_views_into_zone(alternative, zone, kept); }, value);
    } else if constexpr (detail::generic::is_std_tuple<T>) {
        std::apply([&zone, &kept](auto &...element) { (detail::copy_views_into_zone(element, zone, kept), ...); }, value);
    } else if constexpr (detail::generic::is_tagged_item<T>) {
        detail::copy_views_into_zone(value.content, zone, kept);
    } else if constexpr (detail::generic::is_std_vector<T> || detail::generic::is_std_array<T>) {
        for (auto &element : value)
            detail::copy_views_into_zone(element, zone, kept);
    } else if constexpr (requires { typename T::key_type; typename T::mapped_type; }) {
        T copied(value.get_allocator());
        for (auto const &[key, mapped] : value) {
            typename T::key_type k = key;
            typename T::mapped_type m = mapped;
            detail::copy_views_into_zone(k, zone, kept);
            detail::copy_views_into_zone(m, zone, kept);
            copied.emplace(std::move(k), std::move(m));
        }
        value = std::move(copied);
    } else if constexpr (requires { typename T::key_type; }) {
        T copied(value.get_allocator());
        for (auto const &element : value) {
            typename T::key_type k = element;
            detail::copy_views_into_zone(k, zone, kept);
            copied.insert(std::move(k));
        }
        value = std::move(copied);
    } else if constexpr (std::is_pointer_v<T> || detail::generic::is_shared_ptr<T>) {
        if (value) {
            auto copy = std::make_shared<detail::generic::pointee_t<T>>(*value);
            detail::copy_views_into_zone(*copy, zone, kept);
            if constexpr (std::is_pointer_v<T>) {
                value = copy.get();
                kept.push_back(std::move(copy));
            } else {
                value = std::move(copy);
            }
        }
    } else {
        static_assert(std::is_class_v<T> && std::is_aggregate_v<T>,
                      "cbor::ref::clone takes a type whose views it knows how to copy.");
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (detail::copy_views_into_zone(detail::pfr::get<I>(value), zone, kept), ...);
        }(std::make_index_sequence<detail::pfr::members_count<T>()>{});
    }
}

}

}

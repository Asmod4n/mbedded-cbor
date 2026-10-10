#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <new>
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

#include "item_size.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "lazy.hpp"
#include "owning_ref.hpp"
#include "schema.hpp"
#include "shared.hpp"

namespace cbor
{

#ifdef __cpp_impl_reflection
class generic
{
    template <class>
    friend class databind;

    template <class U>
    static constexpr bool is_std_tuple = false;

    template <class... E>
    static constexpr bool is_std_tuple<std::tuple<E...>> = true;

    template <class U>
    static constexpr bool is_std_array = false;

    template <class E, std::size_t N>
    static constexpr bool is_std_array<std::array<E, N>> = true;

    template <class U>
    static constexpr bool is_tagged = false;

    template <std::uint64_t N, class E>
    static constexpr bool is_tagged<tagged<N, E>> = true;

    template <class U>
    static constexpr bool is_std_variant = false;

    template <class... E>
    static constexpr bool is_std_variant<std::variant<E...>> = true;

    template <class U>
    static constexpr bool is_byte = std::same_as<U, std::byte> || std::same_as<U, unsigned char>;

    template <class U>
    static constexpr bool is_byte_container = requires {
        typename U::value_type;
        requires is_byte<typename U::value_type>;
        requires std::same_as<U, std::vector<typename U::value_type>> || is_std_array<U>;
    };

    template <class U, class V>
    static std::expected<void, error> integer_read(heads::decoder &d, V &out)
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
        else if constexpr (packed::is_wide_integer<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer ||
                   (h.major == major_type::tag &&
                    (h.argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
                     h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum)));
        else if constexpr (std::is_unsigned_v<U>)
            return h.major == major_type::unsigned_integer;
        else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer;
        else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>)
            return h.major == major_type::text_string;
        else if constexpr (std::same_as<U, std::span<std::byte const>> || is_byte_container<U>)
            return h.major == major_type::byte_string;
        else if constexpr (packed::is_optional<U>)
            return heads::is_null(h) || head_accepted<typename U::value_type>(h);
        else if constexpr (is_tagged<U>)
            return h.major == major_type::tag && h.argument == U::number;
        else if constexpr (is_std_variant<U>)
            return false;
        else if constexpr (is_std_tuple<U> || is_std_array<U>)
            return h.major == major_type::array && h.argument == std::tuple_size_v<U>;
        else if constexpr (packed::is_map<U>)
            return h.major == major_type::map;
        else if constexpr (requires(U const &c) { typename U::value_type; c.size(); })
            return h.major == major_type::array;
        else
            return h.major == major_type::map;
    }

#ifdef __SIZEOF_INT128__
    template <class U>
    static std::expected<void, error> wide_integer_read(heads::decoder &d, U &out)
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
            if (error const c = validity::check_tag_content(h->argument, b->major, b->info).error_or(error{});
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

    template <class U, std::size_t I = 0>
    static std::expected<void, error> variant_read(value_sharing::sharing_decoder &d, U &out, heads::head const &h, std::size_t const depth, std::size_t const depth_max)
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

    template <class U>
    static std::expected<void, error> generic_read(value_sharing::sharing_decoder &d, U &out, std::size_t const depth, std::size_t const depth_max)
    {
        std::size_t const item_at = d.message.encoded.size() - d.encoded.size();
        for (;;) {
            heads::decoder look = d;
            auto const h = look.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                d.encoded = look.encoded;
                d.message.mark(d);
                continue;
            }
            if (h->argument != std::to_underlying(rfc8949::tag_number::sharedref))
                break;
            d.encoded = look.encoded;
            auto const target = value_sharing::top_level_item::sharedref_decode(d, d.message.encoded.size() - d.encoded.size(), item_at, d.message.sharedrefs);
            if (!target) [[unlikely]]
                return std::unexpected(target.error());
            std::string_view const rest = d.encoded;
            d.encoded = std::string_view(std::span(d.message.encoded).subspan(target->offset));
            auto const r = generic_value_read(d, out, depth + 1, depth_max);
            d.encoded = rest;
            return r;
        }
        return generic_value_read(d, out, depth, depth_max);
    }

    template <class U>
    static std::expected<void, error> generic_value_read(value_sharing::sharing_decoder &d, U &out, std::size_t const depth, std::size_t const depth_max)
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
        } else if constexpr (packed::is_wide_integer<U>) {
            return wide_integer_read(d, out);
        } else if constexpr (is_std_variant<U>) {
            heads::decoder probe = d;
            auto const h = probe.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            return variant_read(d, out, *h, depth, depth_max);
        } else if constexpr (std::is_enum_v<U>) {
            return integer_read<std::underlying_type_t<U>>(d, out);
        } else if constexpr (std::is_integral_v<U>) {
            return integer_read<U>(d, out);
        } else if constexpr (std::is_floating_point_v<U>) {
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::databind reads and writes float and double only.");
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
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.byte_string_decode(h->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            if constexpr (std::same_as<U, std::string>)
                if (auto const r = d.decoded_bytes_count(text->size(), sizeof(char)); !r) [[unlikely]]
                    return r;
            out = U(*text);
            return {};
        } else if constexpr (std::same_as<U, std::span<std::byte const>> || is_byte_container<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const bytes = d.byte_string_decode(h->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            auto const view = std::as_bytes(std::span(*bytes));
            if constexpr (std::same_as<U, std::span<std::byte const>>) {
                out = view;
            } else if constexpr (is_std_array<U>) {
                if (view.size() != out.size()) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                std::ranges::transform(view, out.begin(), [](std::byte const b) { return static_cast<typename U::value_type>(b); });
            } else {
                if (auto const r = d.decoded_bytes_count(view.size(), sizeof(typename U::value_type)); !r) [[unlikely]]
                    return r;
                out.resize(view.size());
                std::ranges::transform(view, out.begin(), [](std::byte const b) { return static_cast<typename U::value_type>(b); });
            }
            return {};
        } else if constexpr (packed::is_optional<U>) {
            if (!d.encoded.empty() &&
                d.encoded.front() == heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null))) {
                d.encoded.remove_prefix(1);
                out.reset();
                return {};
            }
            return generic_read(d, out.emplace(), depth, depth_max);
        } else if constexpr (is_tagged<U>) {
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
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, std::tuple_size_v<U>))) {
                if (r)
                    r = generic_read(d, std::get<i>(out), depth + 1, depth_max);
            }
            return r;
        } else if constexpr (packed::is_map<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                typename U::key_type key{};
                if (auto const r = generic_read(d, key, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                typename U::mapped_type value{};
                if (auto const r = generic_read(d, value, depth + 1, depth_max); !r) [[unlikely]]
                    return r;
                if (auto const r = d.decoded_bytes_count(1, sizeof(typename U::value_type)); !r) [[unlikely]]
                    return r;
                if constexpr (requires { out.try_emplace(std::move(key), std::move(value)); })
                    out.try_emplace(std::move(key), std::move(value));
                else
                    out.emplace(std::move(key), std::move(value));
            }
            return {};
        } else if constexpr (requires { typename U::value_type; out.size(); }) {
            using E = typename U::value_type;
            static_assert(
                requires(E &&e) { out.insert(out.end(), std::move(e)); },
                "cbor::databind decodes a container with insert(end(), value); this container has none.");
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                if (auto const r = d.decoded_bytes_count(1, sizeof(E)); !r) [[unlikely]]
                    return r;
                if constexpr (requires {
                                  { out.emplace_back() } -> std::same_as<E &>;
                              }) {
                    if (auto const r = generic_read(d, out.emplace_back(), depth + 1, depth_max); !r)
                        [[unlikely]]
                        return r;
                } else {
                    E element{};
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

    template <class U, std::size_t I>
    static bool key_matches(heads::head const &k, std::string_view const text)
    {
        if constexpr (packed::has_integer_keys<U>) {
            constexpr std::int64_t key = std::get<I>(U::keys);
            if constexpr (key >= 0)
                return k.major == major_type::unsigned_integer && k.argument == static_cast<std::uint64_t>(key);
            else
                return k.major == major_type::negative_integer && k.argument == static_cast<std::uint64_t>(-1 - key);
        } else {
            constexpr std::string_view name = packed::key_of(packed::members_of<U>()[I]);
            return k.major == major_type::text_string && text == name;
        }
    }

    template <class U>
    static std::expected<void, error> struct_read(value_sharing::sharing_decoder &d, U &out, std::size_t const depth, std::size_t const depth_max)
    {
        static constexpr auto members = packed::members_of<U>();
        constexpr std::size_t count = members.size();
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            std::size_t const key_at = d.message.encoded.size() - d.encoded.size();
            auto k = d.head_decode();
            while (k && k->major == major_type::tag &&
                   k->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                d.message.mark(d);
                k = d.head_decode();
            }
            heads::decoder referenced{};
            bool const indirect = k && k->major == major_type::tag &&
                                  k->argument == std::to_underlying(rfc8949::tag_number::sharedref);
            if (indirect) {
                auto const target = value_sharing::top_level_item::sharedref_decode(d, d.message.encoded.size() - d.encoded.size(), key_at, d.message.sharedrefs);
                if (!target) [[unlikely]]
                    return std::unexpected(target.error());
                referenced = heads::decoder{std::string_view(std::span(d.message.encoded).subspan(target->offset))};
                k = referenced.head_decode();
            }
            heads::decoder &from = indirect ? referenced : d;
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::string_view text;
            if (k->major == major_type::text_string) {
                auto const t = from.byte_string_decode(k->argument);
                if (!t) [[unlikely]]
                    return std::unexpected(t.error());
                text = *t;
            } else if (k->major != major_type::unsigned_integer && k->major != major_type::negative_integer) {
                if (k->major == major_type::byte_string) {
                    if (auto const b = from.byte_string_decode(k->argument); !b) [[unlikely]]
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
            bool matched = false;
            std::expected<void, error> r;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, count))) {
                if (!matched && key_matches<U, i>(*k, text)) {
                    matched = true;
                    if (found.test(i)) {
                        r = well_formedness::item_skip(d, d.message);
                    } else {
                        found.set(i);
                        r = generic_read(d, out.[:members[i]:], depth + 1, depth_max);
                    }
                }
            }
            if (!matched) {
                if (auto const s = well_formedness::item_skip(d, d.message); !s) [[unlikely]]
                    return s;
            } else if (!r) [[unlikely]] {
                return r;
            }
        }
        template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, count))) {
            using M = std::remove_cvref_t<decltype(out.[:members[i]:])>;
            if constexpr (!packed::is_optional<M>)
                if (!found.test(i)) [[unlikely]]
                    return std::unexpected(error::key_not_found);
        }
        return {};
    }

    template <class U>
    static bool member_present(U const &value)
    {
        if constexpr (packed::is_optional<U>)
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
        } else if constexpr (packed::is_wide_integer<U>) {
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
            static_assert(std::same_as<U, float> || std::same_as<U, double>, "cbor::databind reads and writes float and double only.");
            return float_size(static_cast<double>(value));
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view> ||
                             std::same_as<U, std::span<std::byte const>> || is_byte_container<U>) {
            return validity::checked_add(heads::head_size(value.size()), value.size());
        } else if constexpr (packed::is_optional<U>) {
            if (!value)
                return heads::initial_byte_size;
            return generic_size(*value);
        } else if constexpr (is_tagged<U>) {
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
        } else if constexpr (packed::is_map<U>) {
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
        } else if constexpr (requires { value.size(); typename U::value_type; }) {
            std::size_t size = heads::head_size(value.size());
            for (auto const &e : value) {
                auto const sum = generic_size(e).and_then([size](std::size_t const n) { return validity::checked_add(size, n); });
                if (!sum) [[unlikely]]
                    return sum;
                size = *sum;
            }
            return size;
        } else {
            static constexpr auto members = packed::members_of<U>();
            std::size_t size = 0;
            std::size_t present = 0;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                auto const &m = value.[:members[i]:];
                if (member_present(m)) {
                    ++present;
                    if constexpr (packed::has_integer_keys<U>) {
                        constexpr std::int64_t key = std::get<i>(U::keys);
                        auto const sum = validity::checked_add(size, *generic_size(key));
                        if (!sum) [[unlikely]]
                            return sum;
                        size = *sum;
                    } else {
                        constexpr std::string_view name = packed::key_of(members[i]);
                        auto const sum = validity::checked_add(size, heads::head_size(name.size()) + name.size());
                        if (!sum) [[unlikely]]
                            return sum;
                        size = *sum;
                    }
                    std::expected<std::size_t, std::errc> member;
                    if constexpr (packed::is_optional<std::remove_cvref_t<decltype(m)>>)
                        member = generic_size(*m);
                    else
                        member = generic_size(m);
                    auto const sum = member.and_then([size](std::size_t const n) { return validity::checked_add(size, n); });
                    if (!sum) [[unlikely]]
                        return sum;
                    size = *sum;
                }
            }
            return validity::checked_add(size, heads::head_size(present));
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
        } else if constexpr (packed::is_wide_integer<U>) {
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
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>) {
            at += heads::head_write(out, at, major_type::text_string, value.size());
            return bytes_write(out, at, value);
        } else if constexpr (std::same_as<U, std::span<std::byte const>> || is_byte_container<U>) {
            at += heads::head_write(out, at, major_type::byte_string, value.size());
            if constexpr (std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>)
                std::ranges::copy(value, std::as_writable_bytes(out.subspan(at, value.size())).begin());
            else
                std::ranges::transform(value, out.subspan(at, value.size()).begin(),
                                       [](auto const b) { return static_cast<char>(b); });
            return at + value.size();
        } else if constexpr (packed::is_optional<U>) {
            if (!value) {
                out.subspan(at).front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::null));
                return at + heads::initial_byte_size;
            }
            return generic_write(out, at, *value);
        } else if constexpr (is_tagged<U>) {
            at += heads::head_write(out, at, major_type::tag, U::number);
            return generic_write(out, at, value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            at += heads::head_write(out, at, major_type::array, std::tuple_size_v<U>);
            std::apply([&](auto const &...e) { ((at = generic_write(out, at, e)), ...); }, value);
            return at;
        } else if constexpr (packed::is_map<U>) {
            at += heads::head_write(out, at, major_type::map, value.size());
            for (auto const &[k, v] : value) {
                at = generic_write(out, at, k);
                at = generic_write(out, at, v);
            }
            return at;
        } else if constexpr (requires { value.size(); typename U::value_type; }) {
            at += heads::head_write(out, at, major_type::array, value.size());
            for (auto const &e : value)
                at = generic_write(out, at, e);
            return at;
        } else {
            static constexpr auto members = packed::members_of<U>();
            std::size_t present = 0;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                if (member_present(value.[:members[i]:]))
                    ++present;
            }
            at += heads::head_write(out, at, major_type::map, present);
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                auto const &m = value.[:members[i]:];
                if (member_present(m)) {
                    if constexpr (packed::has_integer_keys<U>) {
                        constexpr std::int64_t key = std::get<i>(U::keys);
                        at = generic_write(out, at, key);
                    } else {
                        constexpr std::string_view name = packed::key_of(members[i]);
                        at += heads::head_write(out, at, major_type::text_string, name.size());
                        at = bytes_write(out, at, name);
                    }
                    if constexpr (packed::is_optional<std::remove_cvref_t<decltype(m)>>)
                        at = generic_write(out, at, *m);
                    else
                        at = generic_write(out, at, m);
                }
            }
            return at;
        }
    }
};

template <class T>
class databind
{
public:
    static std::expected<owning_ref<T>, error> decode(std::string_view encoded) = delete;

    template <std::same_as<std::string> Encoded>
    static std::expected<owning_ref<T>, error> decode(Encoded &&encoded)
    {
#if defined(__cpp_exceptions)
        try {
#endif
            auto owner = std::make_shared<std::string const>(std::move(encoded));
            std::string_view const view = *owner;
            return decode(std::move(owner), view);
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<void const> owner, std::string_view const encoded)
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::databind::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
        try {
#endif
            auto value = read(encoded, limits.nesting_depth);
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            return owning_ref<T>(std::move(owner), std::move(*value));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    template <class Encoded>
        requires std::same_as<std::remove_const_t<Encoded>, std::string>
    static std::expected<owning_ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded &&encoded) = delete;

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
    static std::expected<T, error> read(std::string_view const encoded, std::size_t const depth_max)
    {
        if (auto const r = validity::check_input_bytes(encoded.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        value_sharing::sharing_decoder d{{encoded}, {{}, encoded, {}, 0}};
        T out{};
        if (auto const r = generic::generic_read(d, out, 0, depth_max); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (!d.encoded.empty()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return out;
    }
};
#endif

}

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#if defined(__cpp_exceptions)
#include <stdexcept>
#endif
#include <string_view>
#include <system_error>
#include <utility>
#if __has_include(<stdckdint.h>)
#include <stdckdint.h>
#endif

#include "rfc8746.hpp"
#include "rfc9535.hpp"
#include "error.hpp"
#include "rfc8949.hpp"

#ifdef _MSC_VER
#define CBOR_ALWAYS_INLINE [[msvc::forceinline]]
#define CBOR_NEVER_INLINE [[msvc::noinline]]
#define CBOR_ASSUME(condition) __assume(condition)
#else
#define CBOR_ALWAYS_INLINE [[gnu::always_inline]]
#define CBOR_NEVER_INLINE [[gnu::noinline]]
#define CBOR_ASSUME(condition) [[assume(condition)]]
#endif

#ifndef CBOR_NESTING_DEPTH_DEFAULT
#define CBOR_NESTING_DEPTH_DEFAULT 128
#endif

#ifndef CBOR_STRING_LENGTH_DEFAULT
#define CBOR_STRING_LENGTH_DEFAULT SIZE_MAX
#endif

#ifndef CBOR_CONTAINER_ELEMENTS_DEFAULT
#define CBOR_CONTAINER_ELEMENTS_DEFAULT SIZE_MAX
#endif

namespace cbor
{

struct lazy;
struct item;

template <class>
class owning;

template <bool>
struct tree_decoder;

struct limits {
    static constexpr std::size_t nesting_depth = CBOR_NESTING_DEPTH_DEFAULT;
    static constexpr std::size_t string_length = CBOR_STRING_LENGTH_DEFAULT;
    static constexpr std::size_t container_elements = CBOR_CONTAINER_ELEMENTS_DEFAULT;
};

class validity
{
    enum class tag_number : std::uint64_t {
        reference = 6,
        basic_packed_cbor = 113,
        record_function = 114,
        straight_argument_first = 128,
        straight_argument_last = 135
    };

public:
    enum class offset : std::size_t {};

    enum class index : std::uint64_t {};

    static constexpr offset npos{std::numeric_limits<std::size_t>::max()};

    static constexpr std::size_t nesting_depth_limit = 512;

    static constexpr std::size_t size_limit = std::numeric_limits<std::size_t>::max();

    static constexpr std::size_t initial_size_limit = std::size_t{1} << 20;
    static constexpr std::size_t bump_chunk_size = std::size_t{1} << 13;

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_limit(std::uint64_t const value,
                                                                               std::size_t const limit,
                                                                               error const exceeded)
    {
        if (value > limit) [[unlikely]]
            return std::unexpected(exceeded);
        return {};
    }

    static constexpr std::expected<void, error> check_nesting_depth(std::size_t const depth,
                                                                    std::size_t const depth_max)
    {
        return check_limit(depth, depth_max, error::nesting_depth_exceeded);
    }

    template <bool Checked>
    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_argument(major_type const major, std::uint64_t const argument)
    {
        if constexpr (Checked &&
                      (limits::string_length != size_limit || limits::container_elements != size_limit)) {
            if (argument <= std::min(limits::string_length, limits::container_elements)) [[likely]]
                return {};
            if constexpr (limits::string_length != size_limit) {
                if (major == major_type::byte_string || major == major_type::text_string)
                    return check_limit(argument, limits::string_length, error::string_length_exceeded);
            }
            if constexpr (limits::container_elements != size_limit) {
                if (major == major_type::array || major == major_type::map)
                    return check_limit(argument, limits::container_elements,
                                       error::container_elements_exceeded);
            }
        }
        return {};
    }

    static constexpr bool pending_items_fit(std::uint64_t const pending, std::size_t const bytes_left)
    {
        return pending <= bytes_left;
    }

    static constexpr std::expected<void, error> check_pending_items(std::uint64_t const pending,
                                                                    std::size_t const bytes_left)
    {
        if (!pending_items_fit(pending, bytes_left)) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_pending_items(major_type const major, std::uint8_t const info, std::uint64_t const argument,
                        std::size_t const bytes_left)
    {
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length))
            return {};
        if (major == major_type::array)
            return check_pending_items(argument, bytes_left);
        if (major == major_type::map) {
            auto const items = checked_mul(argument, std::uint64_t{rfc8949::data_items_per_pair});
            if (!items) [[unlikely]]
                return std::unexpected(error::too_little_data);
            return check_pending_items(*items, bytes_left);
        }
        return {};
    }

    [[noreturn]] static void throw_logic_error(char const *const what)
    {
#if defined(__cpp_exceptions)
        throw std::logic_error(what);
#else
        (void)what;
        std::abort();
#endif
    }

    static void throw_logic_error_if(bool const wrong, char const *const what)
    {
        if (wrong) [[unlikely]]
            throw_logic_error(what);
    }

    template <class T>
    static void throw_logic_error_if_empty(std::shared_ptr<T> const &owner, char const *const what)
    {
        if (owner.use_count() == 0) [[unlikely]]
            throw_logic_error(what);
    }

    template <class T>
    static void throw_logic_error_if_null(std::shared_ptr<T> const &pointer, char const *const what)
    {
        if (!pointer) [[unlikely]]
            throw_logic_error(what);
    }

    template <std::unsigned_integral T>
        requires(sizeof(T) >= sizeof(unsigned int))
    static constexpr std::expected<T, std::errc> checked_mul(T const a, std::type_identity_t<T> const b)
    {
#ifdef __STDC_VERSION_STDCKDINT_H__
        if !consteval {
            T product;
            if (ckd_mul(&product, a, b)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return product;
        }
#elif defined(__has_builtin)
#if __has_builtin(__builtin_mul_overflow)
        if !consteval {
            T product;
            if (__builtin_mul_overflow(a, b, &product)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return product;
        }
#endif
#endif
        if (a != 0 && b > std::numeric_limits<T>::max() / a) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return a * b;
    }

    template <std::unsigned_integral T>
        requires(sizeof(T) >= sizeof(unsigned int))
    static constexpr std::expected<T, std::errc> checked_add(T const a, std::type_identity_t<T> const b)
    {
#ifdef __STDC_VERSION_STDCKDINT_H__
        if !consteval {
            T sum;
            if (ckd_add(&sum, a, b)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return sum;
        }
#elif defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
        if !consteval {
            T sum;
            if (__builtin_add_overflow(a, b, &sum)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return sum;
        }
#endif
#endif
        if (b > std::numeric_limits<T>::max() - a) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return a + b;
    }

    template <std::ranges::forward_range R, class Projection = std::identity>
    static constexpr bool keys_unique(R const &sorted, Projection const projection = {})
    {
        return std::ranges::adjacent_find(sorted, {}, projection) == std::ranges::end(sorted);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_key_unique(bool const equal_key_seen)
    {
        if (equal_key_seen) [[unlikely]]
            return std::unexpected(error::duplicate_key);
        return {};
    }

    template <bool Checked, class First, class Second>
    static std::expected<bool, error> keys_equivalent(First &first, offset first_at, Second &second,
                                                      offset second_at, std::size_t depth,
                                                      std::size_t depth_max);

    static constexpr error writer_error(std::errc const e) noexcept
    {
        if (e == std::errc::no_buffer_space)
            return error::no_buffer_space;
        if (e == std::errc::value_too_large)
            return error::value_too_large;
        if (e == std::errc::not_enough_memory)
            return error::not_enough_memory;
        return error::io_error;
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_additional_information(major_type const major, std::uint8_t const info)
    {
        if (info <= std::to_underlying(rfc8949::additional_information::eight_byte_argument)) [[likely]]
            return {};
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length) &&
            major != major_type::unsigned_integer && major != major_type::negative_integer &&
            major != major_type::tag)
            return {};
        return std::unexpected(error::syntax_error);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_definite_length(major_type const major, std::uint8_t const info)
    {
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length) &&
            major >= major_type::byte_string && major <= major_type::map) [[unlikely]]
            return std::unexpected(error::indefinite_length);
        if (info > std::to_underlying(rfc8949::additional_information::eight_byte_argument)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_chunk(major_type const string, major_type const major, std::uint8_t const info)
    {
        if (major != string || info == std::to_underlying(rfc8949::additional_information::indefinite_length))
            [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_one_data_item(std::string_view const rest)
    {
        if (!rest.empty()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_simple_value(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(rfc8949::simple_float_information::simple_value_follows) &&
            argument < rfc8949::simple_value_one_byte_min) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_tag_content(std::uint64_t const tag, major_type const content)
    {
        major_type admitted;
        switch (tag) {
        case std::to_underlying(rfc8949::tag_number::unsigned_bignum):
        case std::to_underlying(rfc8949::tag_number::negative_bignum):
        case std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item):
            admitted = major_type::byte_string;
            break;
        case std::to_underlying(rfc8949::tag_number::sharedref):
            admitted = major_type::unsigned_integer;
            break;
        default:
            if (tag < std::to_underlying(rfc8746::tag_number::typed_array_first) ||
                tag > std::to_underlying(rfc8746::tag_number::typed_array_last))
                return {};
            admitted = major_type::byte_string;
            break;
        }
        if (content != admitted) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::size_t typed_array_element_size(std::uint64_t const tag)
    {
        std::uint64_t const f = tag >> std::to_underlying(rfc8746::bit_field::f) & 1;
        std::uint64_t const ll = tag >> std::to_underlying(rfc8746::bit_field::ll) &
                                 ((1 << (std::to_underlying(rfc8746::bit_field::e) - std::to_underlying(rfc8746::bit_field::ll))) - 1);
        return std::size_t{1} << (f + ll);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> typed_array_check(std::uint64_t const tag,
                                                                                     std::size_t const size)
    {
        if (tag < std::to_underlying(rfc8746::tag_number::typed_array_first) ||
            tag > std::to_underlying(rfc8746::tag_number::typed_array_last) ||
            tag == std::to_underlying(rfc8746::tag_number::typed_array_reserved)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (size % typed_array_element_size(tag) != 0) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_typed_array_content(std::uint64_t const tag, major_type const content, std::size_t const size)
    {
        if (auto const r = check_tag_content(tag, content); !r) [[unlikely]]
            return r;
        return typed_array_check(tag, size);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_entries_all_read(std::uint64_t const claimed, std::uint64_t const readable, error const failure)
    {
        if (failure != error{}) [[unlikely]]
            return std::unexpected(failure);
        if (claimed > readable) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr offset element_offset(offset const start, std::size_t const element_size,
                                                              index const i)
    {
        return offset{std::to_underlying(start) + static_cast<std::size_t>(std::to_underlying(i)) * element_size};
    }

    CBOR_ALWAYS_INLINE static constexpr offset next(offset const at, std::size_t const size)
    {
        return offset{std::to_underlying(at) + size};
    }

    CBOR_ALWAYS_INLINE static constexpr offset distance(std::string_view const whole, std::string_view const rest)
    {
        return offset{whole.size() - rest.size()};
    }

    template <class Bytes>
        requires std::ranges::contiguous_range<Bytes const> && std::ranges::sized_range<Bytes const>
    CBOR_ALWAYS_INLINE static constexpr auto subspan(Bytes const &encoded, offset const at)
    {
        using view = std::conditional_t<std::is_convertible_v<Bytes const &, std::string_view>, std::string_view,
                                        std::span<std::ranges::range_value_t<Bytes> const>>;
        view const whole(encoded);
        if (std::to_underlying(at) > whole.size()) [[unlikely]]
            throw_logic_error("cbor::validity: the offset is after the end of its input");
        return view(std::views::drop(whole, static_cast<std::ranges::range_difference_t<view>>(std::to_underlying(at))));
    }

    CBOR_ALWAYS_INLINE static constexpr std::span<char const> element_span(std::string_view const elements,
                                                                           index const i, std::size_t const size)
    {
        return std::span(elements).subspan(static_cast<std::size_t>(std::to_underlying(i)) * size, size);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<std::string_view, error>
    substr(std::string_view const encoded, offset const at, std::size_t const size)
    {
        if (std::to_underlying(at) > encoded.size() || encoded.size() - std::to_underlying(at) < size) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return encoded.substr(std::to_underlying(at), size);
    }

    template <std::integral I>
    CBOR_ALWAYS_INLINE static constexpr std::expected<index, error>
    check_index(I const i, std::uint64_t const length)
    {
        if constexpr (std::is_signed_v<I>) {
            if (i < 0) {
                std::uint64_t const back = std::uint64_t{0} - static_cast<std::uint64_t>(i);
                if (back > length) [[unlikely]]
                    return std::unexpected(error::index_out_of_bounds);
                return index{length - back};
            }
        }
        if (static_cast<std::uint64_t>(i) >= length) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return index{static_cast<std::uint64_t>(i)};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_count(std::size_t const count,
                                                                               std::size_t const size)
    {
        if (count > size) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return {};
    }

    template <class T, class M>
        requires(std::numeric_limits<T>::is_integer && std::numeric_limits<M>::is_integer &&
                 !std::numeric_limits<M>::is_signed && sizeof(T) <= sizeof(M))
    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_number_range(bool const negative,
                                                                                      M const magnitude)
    {
        if ((!std::numeric_limits<T>::is_signed && negative) ||
            magnitude > static_cast<M>(std::numeric_limits<T>::max())) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return {};
    }

    template <class T, class M>
        requires(std::is_same_v<T, double> && std::numeric_limits<M>::is_integer && !std::numeric_limits<M>::is_signed)
    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_number_range(bool const negative,
                                                                                      M const magnitude)
    {
        constexpr std::uint64_t exact_max = std::uint64_t{1} << std::numeric_limits<double>::digits;
        if (magnitude > (negative ? exact_max - 1 : exact_max)) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return {};
    }

    template <std::signed_integral T>
    static constexpr std::expected<T, error> check_whole_number(double const value)
    {
        constexpr double low = static_cast<double>(std::numeric_limits<T>::min());
        if (!(value >= low && value < -low && value == std::trunc(value))) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return static_cast<T>(value);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_magnitude_size(std::size_t const size, std::size_t const width)
    {
        if (size > width) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return {};
    }

    friend class heads;

    friend struct lazy;

    template <bool>
    friend struct tree_decoder;

    friend std::expected<owning<item>, error> decode(std::string_view encoded);

    friend class well_formedness;

    friend class extended_diagnostic_notation;

    friend class jsonpath;

    friend class reflection;

    friend class generic;

    template <class>
    friend class structs;

#ifdef __cpp_impl_reflection
    template <class>
    friend class schema;
#endif
};

static_assert(validity::check_nesting_depth(limits::nesting_depth, validity::nesting_depth_limit).has_value(),
              "CBOR_NESTING_DEPTH_DEFAULT is at most validity::nesting_depth_limit.");
static_assert(validity::check_limit(limits::string_length, validity::size_limit,
                                    error::string_length_exceeded)
                  .has_value(),
              "CBOR_STRING_LENGTH_DEFAULT fits in std::size_t.");
static_assert(validity::check_limit(limits::container_elements, validity::size_limit,
                                    error::container_elements_exceeded)
                  .has_value(),
              "CBOR_CONTAINER_ELEMENTS_DEFAULT fits in std::size_t.");
}

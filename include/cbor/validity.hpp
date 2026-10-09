#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <functional>
#include <limits>
#include <memory>
#include <ranges>
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

#include "binding.hpp"
#include "error.hpp"
#include "rfc8746.hpp"
#include "rfc8949.hpp"

#ifdef _MSC_VER
#define CBOR_ALWAYS_INLINE [[msvc::forceinline]]
#define CBOR_ASSUME(condition) __assume(condition)
#else
#define CBOR_ALWAYS_INLINE [[gnu::always_inline]]
#define CBOR_ASSUME(condition) [[assume(condition)]]
#endif

namespace cbor
{

enum class pass;

struct lazy;

class validity
{
    enum class tag_number : std::uint64_t {
        reference = 6,
        basic_packed_cbor = 113,
        record_function = 114,
        straight_argument_first = 128,
        straight_argument_last = 135,
        inverted_argument_first = 136,
        inverted_argument_last = 143,
        split_basic_packed_cbor = 1113
    };

public:
    static constexpr std::size_t nesting_depth_limit = 1024;

    static constexpr std::size_t nesting_depth_default = 128;

    static constexpr std::expected<void, error> check_nesting_depth(std::size_t const depth,
                                                                    std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
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

    template <std::size_t DepthMax, class First, class Second>
    static std::expected<bool, error> keys_equivalent(First &first, std::size_t first_at, Second &second,
                                                      std::size_t second_at, std::size_t depth);

    template <std::size_t DepthMax, class Message>
    static std::expected<void, error> check_sorted_keys_unique(Message &message, std::size_t first_key,
                                                               std::uint64_t count, std::size_t depth);

    template <class Marks, class Projection>
    static std::expected<void, error> check_tag_content(std::uint64_t tag, std::string_view encoded,
                                                        std::size_t content_at, Marks const &marks,
                                                        Projection offset_of);

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
    check_simple_value(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(rfc8949::simple_float_information::simple_value_follows) &&
            argument < rfc8949::simple_value_one_byte_min) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_tag_content(std::uint64_t const tag, major_type const content, std::uint8_t const info)
    {
        major_type admitted;
        switch (tag) {
        case std::to_underlying(rfc8949::tag_number::standard_date_time_string):
            admitted = major_type::text_string;
            break;
        case std::to_underlying(rfc8949::tag_number::epoch_based_date_time):
            if (content != major_type::unsigned_integer && content != major_type::negative_integer &&
                (content != major_type::simple_float ||
                 info < std::to_underlying(rfc8949::simple_float_information::half_precision_float) ||
                 info > std::to_underlying(rfc8949::simple_float_information::double_precision_float)))
                [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            return {};
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
        std::uint64_t const f = tag >> 4 & 1;
        std::uint64_t const ll = tag & 3;
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

    CBOR_ALWAYS_INLINE static constexpr std::expected<std::size_t, error>
    check_sharedref_index(std::uint64_t const argument, std::size_t const marked)
    {
        if (!std::in_range<std::size_t>(argument)) [[unlikely]]
            return std::unexpected(error::sharedref_index_out_of_range);
        if (argument >= marked) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        return static_cast<std::size_t>(argument);
    }

    template <std::integral I>
    CBOR_ALWAYS_INLINE static constexpr std::expected<std::uint64_t, error>
    check_index(I const index, std::uint64_t const length)
    {
        if constexpr (std::is_signed_v<I>) {
            if (index < 0) {
                std::uint64_t const back = std::uint64_t{0} - static_cast<std::uint64_t>(index);
                if (back > length) [[unlikely]]
                    return std::unexpected(error::index_out_of_bounds);
                return length - back;
            }
        }
        if (static_cast<std::uint64_t>(index) >= length) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return static_cast<std::uint64_t>(index);
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

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_magnitude_size(std::size_t const size, std::size_t const width)
    {
        if (size > width) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return {};
    }

    friend class heads;

    friend class decoding;

    friend struct lazy;

    friend class well_formedness;

    friend class value_sharing;

    friend class diagnostic_notation;

    friend class jsonpath;

    template <std::size_t, class, class, pass>
    friend class walker;

#ifdef __cpp_impl_reflection
    friend class packed;

    friend class generic;

    template <class>
    friend class schema;

    template <class>
    friend class databind;
#endif
};

}

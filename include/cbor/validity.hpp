// Gives cbor::limits. Gives the checks for safety: limits, the stack
// that is left for a recursion, overflow of sums
// and products, number ranges, heads, tag
// content and typed arrays. Throws std::logic_error on a wrong use, or
// ends the process when the build has no exceptions. Gives the
// provisional tag number of the fixed array of cbor::schema, and the
// sizes of the blocks of encode_iov and of the string saver.
// https://www.rfc-editor.org/rfc/rfc8949
// https://www.rfc-editor.org/rfc/rfc8746#section-2
#pragma once

#ifndef CBOR_STRING_SAVER_CHUNK_SIZE
#define CBOR_STRING_SAVER_CHUNK_SIZE (std::size_t{1} << 13)
#endif

#ifndef CBOR_ENCODER_BLOCK_SIZE
#define CBOR_ENCODER_BLOCK_SIZE 16384
#endif

#ifndef CBOR_ENCODER_COPYBREAK
#define CBOR_ENCODER_COPYBREAK 4096
#endif

#ifndef CBOR_ENCODER_INLINE_BUFFER_SIZE
#define CBOR_ENCODER_INLINE_BUFFER_SIZE 1024
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
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
#include <numeric>
#include <optional>
#include <utility>
#include <variant>
#include <version>
#if __has_include(<stdckdint.h>)
#include <stdckdint.h>
#endif

#include "rfc8746.hpp"
#include "error.hpp"
#include "rfc8949.hpp"
#include "rfc9581.hpp"

#define CBOR_ALWAYS_INLINE [[gnu::always_inline]]
#define CBOR_NEVER_INLINE [[gnu::noinline]]
#if defined(_MSC_VER)
#define CBOR_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define CBOR_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#if defined(__linux__)
#include <pthread.h>
#include <sys/auxv.h>
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <pthread.h>
#elif defined(_WIN64)
extern "C" __declspec(dllimport) void __stdcall GetCurrentThreadStackLimits(unsigned long long *, unsigned long long *);
extern "C" __declspec(dllimport) int __stdcall SetThreadStackGuarantee(unsigned long *);
#elif defined(__wasm__) && !defined(__wasm_atomics__)
extern "C" unsigned char __stack_low;
#elif defined(__PICOLIBC__)
extern "C" [[gnu::weak]] char __stack[];
extern "C" [[gnu::weak]] char __stack_size[];
extern "C" [[gnu::weak]] char __stack_bottom[];
#else
#error "mbedded-cbor reads the stack bound of a thread on Linux, macOS, Windows, WebAssembly without threads and picolibc only."
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define CBOR_HAS_SANITIZER 1
#endif
#endif
#ifndef CBOR_HAS_SANITIZER
#define CBOR_HAS_SANITIZER 0
#endif

#ifndef CBOR_STRING_LENGTH_DEFAULT
#define CBOR_STRING_LENGTH_DEFAULT SIZE_MAX
#endif

#ifndef CBOR_CONTAINER_ELEMENTS_DEFAULT
#define CBOR_CONTAINER_ELEMENTS_DEFAULT SIZE_MAX
#endif

namespace cbor
{

namespace detail
{

class reflection;

}

template <class T>
class schema;

struct lazy;
struct item;

template <class>
class owning;

namespace detail
{

class canonical_ids;

}

/**
 * @brief Gives the limits of the build.
 *
 * The macro `CBOR_STRING_LENGTH_DEFAULT` sets `string_length`, and the macro `CBOR_CONTAINER_ELEMENTS_DEFAULT` sets
 * `container_elements`.
 * A macro that the build does not set gets its default, `SIZE_MAX`.
 * No call reads a limit at run time.
 * A limit at `SIZE_MAX` is not set, and its check is not in the program.
 * A limit counts what a call reads or decodes.
 * An item that a call only skips does not count.
 * Every translation unit of one program uses the same values.
 *
 * Nesting has no fixed limit.
 * A reader that recurses stops where the stack of its thread ends: a call that comes within 16 KiB of the end
 * (32 KiB on Windows or with a sanitizer) returns `nesting_depth_exceeded`.
 * Each thread reads the bounds of its own stack once, at its first check: from the system on Linux (glibc and musl),
 * macOS and Windows, and from the linker on WebAssembly without threads and on picolibc.
 * Another target does not compile.
 * A frame outside these bounds, such as the stack of a fiber or of a signal handler, is not checked.
 * The first check of a thread does not run in a signal handler.
 * On Linux, a change of `RLIMIT_STACK` after the first check, and a mapping that a program places with `MAP_FIXED`
 * below the stack of the main thread, are outside the bound.
 * On picolibc, a stack smaller than the reserve refuses every nested item.
 * cbor::item_size(), cbor::sequence_reader and a path of child segments do not recurse and read any depth.
 * A deep value of a user type (`structs`, `%schema`) is destroyed and copied by its own recursion: a thread with a
 * smaller stack than the one that decoded it can overflow there.
 *
 * @code{.example}
 * message: {}
 * call: cbor::limits::string_length
 * result: 4294967295
 * call: cbor::limits::container_elements
 * result: 4294967295
 * @endcode
 *
 * @snippet doc_reference_errors.cpp limits
 * @see cbor::error
 *
 * @ingroup errors
 */
struct limits {
    /** @brief The bytes of one text or byte string; more give `string_length_exceeded`. */
    static constexpr std::size_t string_length = CBOR_STRING_LENGTH_DEFAULT;
    /** @brief The elements of an array, or the pairs of a map; more give `container_elements_exceeded`. */
    static constexpr std::size_t container_elements = CBOR_CONTAINER_ELEMENTS_DEFAULT;
};

namespace detail
{

class validity
{
    enum class tag_number : std::uint64_t { fixed_array = 32873 };

public:
    enum class offset : std::size_t {};

    enum class index : std::uint64_t {};

    static constexpr offset npos{std::numeric_limits<std::size_t>::max()};

#if defined(_WIN64) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || CBOR_HAS_SANITIZER
    static constexpr std::uintptr_t stack_reserve = 32768;
#else
    static constexpr std::uintptr_t stack_reserve = 16384;
#endif

#if defined(_WIN64)
    static constexpr std::uintptr_t windows_page_size = 4096;
#endif

#if defined(__linux__)
    static constexpr std::uintptr_t stack_guard_gap_pages = 256;
#endif

    static constexpr std::uintptr_t stack_limit_not_read = std::numeric_limits<std::uintptr_t>::max();
    static constexpr std::uintptr_t stack_limit_not_known = stack_limit_not_read - 1;

    static inline constinit thread_local std::uintptr_t stack_limit = stack_limit_not_read;

    [[gnu::cold]] static std::uintptr_t stack_low_of_this_thread(std::uintptr_t const frame)
    {
#if defined(__linux__)
        auto const page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
        auto const program = reinterpret_cast<char const *>(getauxval(AT_EXECFN));
        rlimit limit{};
        bool const limited = getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY;
        if (program != nullptr && limited) {
            std::uintptr_t const top =
                (reinterpret_cast<std::uintptr_t>(program) + std::string_view(program).size() + page) / page * page;
            if (limit.rlim_cur < top && frame < top && frame >= top - limit.rlim_cur)
                return top - limit.rlim_cur + page;
        }
        pthread_attr_t attributes;
        if (pthread_getattr_np(pthread_self(), &attributes) != 0) [[unlikely]]
            return stack_limit_not_known;
        void *address = nullptr;
        std::size_t size = 0;
        int const read = pthread_attr_getstack(&attributes, &address, &size);
        pthread_attr_destroy(&attributes);
        if (read != 0) [[unlikely]]
            return stack_limit_not_known;
        auto const low = reinterpret_cast<std::uintptr_t>(address);
        if (!limited && program != nullptr && reinterpret_cast<std::uintptr_t>(program) >= low &&
            reinterpret_cast<std::uintptr_t>(program) - low < size)
            return low + stack_guard_gap_pages * page;
        return low;
#elif defined(__APPLE__)
        (void)frame;
        auto const high = reinterpret_cast<std::uintptr_t>(pthread_get_stackaddr_np(pthread_self()));
        return high - pthread_get_stacksize_np(pthread_self());
#elif defined(_WIN64)
        (void)frame;
        unsigned long long low = 0;
        unsigned long long high = 0;
        GetCurrentThreadStackLimits(&low, &high);
        unsigned long guarantee = 0;
        if (SetThreadStackGuarantee(&guarantee) == 0) [[unlikely]]
            return stack_limit_not_known;
        return static_cast<std::uintptr_t>(low) + windows_page_size + guarantee;
#elif defined(__wasm__) && !defined(__wasm_atomics__)
        (void)frame;
        return reinterpret_cast<std::uintptr_t>(&__stack_low);
#elif defined(__PICOLIBC__)
        (void)frame;
        if (__stack == nullptr) [[unlikely]]
            return stack_limit_not_known;
        if (__stack_size != nullptr)
            return reinterpret_cast<std::uintptr_t>(__stack) - reinterpret_cast<std::uintptr_t>(__stack_size);
        if (__stack_bottom != nullptr)
            return reinterpret_cast<std::uintptr_t>(__stack_bottom);
        return stack_limit_not_known;
#endif
    }

    [[gnu::cold, gnu::noinline]] static std::expected<void, error> check_recursive_call(std::uintptr_t const frame)
    {
        if (stack_limit == stack_limit_not_read) {
            std::uintptr_t const low = stack_low_of_this_thread(frame);
            stack_limit = low == stack_limit_not_known ? stack_limit_not_known : low + stack_reserve;
        }
        if (stack_limit == stack_limit_not_known || (frame < stack_limit && frame >= stack_limit - stack_reserve))
            [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return {};
    }

    static constexpr std::size_t size_limit = std::numeric_limits<std::size_t>::max();

    static constexpr std::size_t initial_size_limit = std::size_t{1} << 20;
    static constexpr std::size_t string_saver_chunk_size = CBOR_STRING_SAVER_CHUNK_SIZE;
    static_assert(string_saver_chunk_size > 0, "A chunk of the zone must hold at least one byte.");

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_limit(std::uint64_t const value,
                                                                               std::size_t const limit,
                                                                               error const exceeded)
    {
        if (value > limit) [[unlikely]]
            return std::unexpected(exceeded);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_recursion_limit()
    {
        if !consteval {
            auto const frame = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
            if (frame < stack_limit) [[unlikely]]
                return check_recursive_call(frame);
        }
        return {};
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

    static constexpr bool bytes_fit(std::uint64_t const size, std::size_t const bytes_left)
    {
        return size <= bytes_left;
    }

    static constexpr std::expected<void, error> check_bytes_left(std::uint64_t const size,
                                                                 std::size_t const bytes_left)
    {
        if (!bytes_fit(size, bytes_left)) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return {};
    }

    static constexpr std::expected<void, std::errc> check_space_left(std::uint64_t const size,
                                                                     std::size_t const space_left)
    {
        if (!bytes_fit(size, space_left)) [[unlikely]]
            return std::unexpected(std::errc::no_buffer_space);
        return {};
    }

    static constexpr std::expected<void, error> check_text_left(std::string_view const text, std::size_t const at,
                                                                std::size_t const count)
    {
        if (!bytes_fit(at, text.size()) || !bytes_fit(count, text.size() - at)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return {};
    }

    static constexpr std::expected<void, error> check_subrange(std::size_t const first, std::size_t const last,
                                                               std::size_t const whole_first,
                                                               std::size_t const whole_last)
    {
        if (first < whole_first || last > whole_last || first > last) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    static constexpr std::uint64_t count_enclosed_data_items(major_type const major, std::uint64_t const argument)
    {
        switch (major) {
        case major_type::array:
            return argument;
        case major_type::map:
            return checked_mul(argument, std::uint64_t{rfc8949::data_items_per_pair})
                .value_or(std::numeric_limits<std::uint64_t>::max());
        case major_type::tag:
            return 1;
        default:
            return 0;
        }
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_pending_items(major_type const major, std::uint8_t const info, std::uint64_t const argument,
                        std::size_t const bytes_left)
    {
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length))
            return {};
        if (major != major_type::array && major != major_type::map)
            return {};
        return check_bytes_left(count_enclosed_data_items(major, argument), bytes_left);
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

    CBOR_ALWAYS_INLINE static void throw_logic_error_if(bool const wrong, char const *const what)
    {
        if (wrong) [[unlikely]]
            throw_logic_error(what);
    }

    template <class T>
    static void throw_logic_error_if_empty(std::shared_ptr<T> const &owner, char const *const what)
    {
        std::shared_ptr<void const> const none;
        if (!owner.owner_before(none) && !none.owner_before(owner)) [[unlikely]]
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

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_key_unique(bool const equal_key_seen)
    {
        if (equal_key_seen) [[unlikely]]
            return std::unexpected(error::duplicate_key);
        return {};
    }

    template <class First, class Second>
    static std::expected<bool, error> keys_equivalent(First const &first, offset first_at,
                                                      Second const &second, offset second_at);

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
        if (auto r = check_additional_information(major, info); !r) [[unlikely]]
            return r;
        if (info == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            return std::unexpected(major == major_type::simple ? error::syntax_error
                                                                     : error::indefinite_length);
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
        if (info == std::to_underlying(rfc8949::simple_information::simple_value_follows) &&
            argument < rfc8949::simple_value_one_byte_min) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr bool is_typed_array_tag(std::uint64_t const tag)
    {
        return tag >= std::to_underlying(rfc8746::tag_number::typed_array_first) &&
               tag <= std::to_underlying(rfc8746::tag_number::typed_array_last) &&
               tag != std::to_underlying(rfc8746::tag_number::typed_array_reserved);
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
            if (!is_typed_array_tag(tag))
                return {};
            admitted = major_type::byte_string;
            break;
        }
        if (content != admitted) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr bool is_self_described_cbor_tag(std::uint64_t const tag)
    {
        return tag == std::to_underlying(rfc8949::tag_number::self_described_cbor);
    }

    static constexpr bool tag_refused_by_value_types(std::uint64_t const tag)
    {
        return tag == std::to_underlying(rfc8949::tag_number::shareable) ||
               tag == std::to_underlying(rfc8949::tag_number::sharedref);
    }

    CBOR_ALWAYS_INLINE static constexpr std::size_t typed_array_element_size(std::uint64_t const tag)
    {
        std::uint64_t const f = tag >> std::to_underlying(rfc8746::bit_field::f) & 1;
        std::uint64_t const ll = tag >> std::to_underlying(rfc8746::bit_field::ll) &
                                 ((1 << (std::to_underlying(rfc8746::bit_field::e) - std::to_underlying(rfc8746::bit_field::ll))) - 1);
        return std::size_t{1} << (f + ll);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_whole_elements(std::uint64_t const size,
                                                                                        std::size_t const element)
    {
        if (size % element != 0) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_typed_array(std::uint64_t const tag,
                                                                                     std::uint64_t const size)
    {
        if (!is_typed_array_tag(tag)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return check_whole_elements(size, typed_array_element_size(tag));
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_typed_array_content(std::uint64_t const tag, major_type const content, std::uint64_t const size)
    {
        if (auto const r = check_tag_content(tag, content); !r) [[unlikely]]
            return r;
        return check_typed_array(tag, size);
    }

    CBOR_ALWAYS_INLINE static constexpr std::size_t checked_size(index const i)
    {
        if (!std::in_range<std::size_t>(std::to_underlying(i))) [[unlikely]]
            throw_logic_error("cbor::detail::validity: the index does not fit std::size_t");
        return static_cast<std::size_t>(std::to_underlying(i));
    }

    CBOR_ALWAYS_INLINE static constexpr offset element_offset(offset const start, std::size_t const element_size,
                                                              index const i)
    {
        return offset{std::to_underlying(start) + checked_size(i) * element_size};
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
            throw_logic_error("cbor::detail::validity: the offset is after the end of its input");
        return view(std::views::drop(whole, static_cast<std::ranges::range_difference_t<view>>(std::to_underlying(at))));
    }

    CBOR_ALWAYS_INLINE static constexpr std::span<char const> element_span(std::span<char const> const elements,
                                                                           index const i, std::size_t const size)
    {
        auto const end = checked_mul(checked_size(i) + 1, size);
        if (!end || !bytes_fit(*end, elements.size())) [[unlikely]]
            throw_logic_error("cbor::detail::validity: the element is after the end of its elements");
        return elements.subspan(*end - size, size);
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<std::string_view, error>
    substr(std::string_view const encoded, offset const at, std::size_t const size)
    {
        if (!bytes_fit(std::to_underlying(at), encoded.size()) ||
            !bytes_fit(size, encoded.size() - std::to_underlying(at))) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return std::string_view(std::span(encoded).subspan(std::to_underlying(at), size));
    }

    template <class T>
    CBOR_ALWAYS_INLINE static constexpr std::expected<std::span<T const>, error>
    substr(std::span<T const> const encoded, offset const at, std::size_t const size)
    {
        if (!bytes_fit(std::to_underlying(at), encoded.size()) ||
            !bytes_fit(size, encoded.size() - std::to_underlying(at))) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return encoded.subspan(std::to_underlying(at), size);
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

    template <class Integer>
        requires std::numeric_limits<Integer>::is_integer
    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_integer_argument_range(Integer const n)
    {
        if constexpr (sizeof(Integer) > sizeof(std::uint64_t)) {
            constexpr Integer largest_argument = std::numeric_limits<std::uint64_t>::max();
            if (n > largest_argument) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            if constexpr (std::numeric_limits<Integer>::is_signed)
                if (n < -largest_argument - 1) [[unlikely]]
                    return std::unexpected(error::number_out_of_range);
        }
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_count(std::size_t const count,
                                                                               std::size_t const size)
    {
        if (count > size) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return {};
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error> check_day_count(std::chrono::days::rep const days)
    {
        constexpr auto first_day =
            std::chrono::sys_days{std::chrono::year::min() / std::chrono::January / 1}.time_since_epoch().count();
        constexpr auto last_day =
            std::chrono::sys_days{std::chrono::year::max() / std::chrono::December / 31}.time_since_epoch().count();
        if (days < first_day || days > last_day) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
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
        if (negative && magnitude == std::numeric_limits<M>::max())
            return {};
        M const value = negative ? magnitude + 1 : magnitude;
        if (std::bit_width(value) - std::countr_zero(value) > std::numeric_limits<double>::digits) [[unlikely]]
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

    template <class P>
    static constexpr double seconds_of_ticks(double const ticks)
    {
        return ticks * P::num / P::den;
    }

    template <class P>
    static constexpr double ticks_of_seconds(double const seconds)
    {
        return seconds * P::den / P::num;
    }

    static constexpr std::uint64_t decimal_radix = 10;

    static constexpr auto fraction_keys =
        std::to_array({rfc9581::map_key::milliseconds, rfc9581::map_key::microseconds,
                       rfc9581::map_key::nanoseconds, rfc9581::map_key::picoseconds,
                       rfc9581::map_key::femtoseconds, rfc9581::map_key::attoseconds});

    static constexpr std::uint64_t fractions_per_second(rfc9581::map_key const key)
    {
        std::uint64_t per_second = 1;
        for (auto i = std::to_underlying(key); i < 0; ++i)
            per_second *= decimal_radix;
        return per_second;
    }

    template <class P>
    static constexpr std::optional<rfc9581::map_key> fraction_key_of_period()
    {
        if constexpr (P::num == 1) {
            for (rfc9581::map_key const key : fraction_keys)
                if (std::cmp_equal(fractions_per_second(key), P::den))
                    return key;
        }
        return std::nullopt;
    }

    template <class D>
    static constexpr std::expected<D, error> check_epoch_ticks(std::int64_t const seconds)
    {
        using rep = typename D::rep;
        using F = std::ratio_divide<std::ratio<1>, typename D::period>;
        if constexpr (std::floating_point<rep>) {
            return D{static_cast<rep>(static_cast<rep>(seconds) * F::num / F::den)};
        } else {
            if (seconds % F::den != 0 ||
                std::cmp_greater(seconds / F::den, std::numeric_limits<rep>::max() / F::num) ||
                std::cmp_less(seconds / F::den, std::numeric_limits<rep>::min() / F::num)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            return D{static_cast<rep>(static_cast<rep>(seconds / F::den) * static_cast<rep>(F::num))};
        }
    }

    template <class D>
    static std::expected<D, error> check_epoch_ticks(double const seconds)
    {
        using rep = typename D::rep;
        using P = typename D::period;
        if constexpr (std::floating_point<rep>) {
            if (!std::isfinite(seconds)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            return D{static_cast<rep>(ticks_of_seconds<P>(seconds))};
        } else {
            auto const ticks = check_whole_number<rep>(std::round(ticks_of_seconds<P>(seconds)));
            if (!ticks) [[unlikely]]
                return std::unexpected(ticks.error());
            if (seconds_of_ticks<P>(static_cast<double>(*ticks)) != seconds) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            return D{*ticks};
        }
    }

    template <class D>
    static std::expected<D, error> check_extended_time_ticks(std::int64_t const seconds, std::uint64_t const fraction,
                                                             rfc9581::map_key const key)
    {
        using rep = typename D::rep;
        using P = typename D::period;
        std::uint64_t const per_second = fractions_per_second(key);
        std::uint64_t const carry = fraction / per_second;
        std::uint64_t const rest = fraction % per_second;
        if (std::cmp_greater(carry, std::numeric_limits<std::int64_t>::max() - std::max<std::int64_t>(seconds, 0)))
            [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        std::int64_t const whole = seconds + static_cast<std::int64_t>(carry);
        if constexpr (std::floating_point<rep>) {
            return D{static_cast<rep>(ticks_of_seconds<P>(static_cast<double>(whole)) +
                                      ticks_of_seconds<P>(static_cast<double>(rest) / static_cast<double>(per_second)))};
        } else {
            auto const base = check_epoch_ticks<D>(whole);
            if (!base) [[unlikely]]
                return base;
            auto const den = static_cast<std::uint64_t>(P::den);
            std::uint64_t const common = std::gcd(per_second, den);
            auto const numerator = checked_mul(rest, den / common);
            auto const denominator = checked_mul(per_second / common, static_cast<std::uint64_t>(P::num));
            if (!numerator || !denominator || *numerator % *denominator != 0 ||
                !std::in_range<rep>(*numerator / *denominator)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            auto const part = static_cast<rep>(*numerator / *denominator);
            if (base->count() > std::numeric_limits<rep>::max() - part) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            return D{static_cast<rep>(base->count() + part)};
        }
    }

    static constexpr std::expected<std::int64_t, error> check_seconds_of_days(std::int64_t const days)
    {
        constexpr std::int64_t seconds_per_day = std::chrono::days::period::num;
        if (days > std::numeric_limits<std::int64_t>::max() / seconds_per_day ||
            days < std::numeric_limits<std::int64_t>::min() / seconds_per_day) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return days * seconds_per_day;
    }

    static constexpr std::expected<std::optional<rfc9581::map_key>, error>
    check_extended_time_key(major_type const major, std::uint64_t const argument)
    {
        if (major == major_type::unsigned_integer) {
            if (std::cmp_equal(argument, std::to_underlying(rfc9581::map_key::base_time)) ||
                std::cmp_equal(argument, std::to_underlying(rfc9581::map_key::timescale_critical)) ||
                std::cmp_equal(argument, std::to_underlying(rfc9581::map_key::time_zone_hint_critical)))
                return static_cast<rfc9581::map_key>(argument);
            return std::unexpected(error::inadmissible_type_for_tag_content);
        }
        if (major != major_type::negative_integer) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        if (!std::in_range<std::int64_t>(argument))
            return std::nullopt;
        auto const key = static_cast<rfc9581::map_key>(-1 - static_cast<std::int64_t>(argument));
        if (key == rfc9581::map_key::timescale_elective || key == rfc9581::map_key::timescale_elective_first ||
            key == rfc9581::map_key::time_zone_hint || std::ranges::contains(fraction_keys, key))
            return key;
        return std::nullopt;
    }

    static constexpr std::expected<void, error> check_extended_time_entry_once(bool const seen)
    {
        if (seen) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    static constexpr std::expected<rfc9581::timescale, error> check_timescale(major_type const major,
                                                                             std::uint64_t const argument)
    {
        if (major != major_type::unsigned_integer || (argument != std::to_underlying(rfc9581::timescale::utc) &&
                                                      argument != std::to_underlying(rfc9581::timescale::tai)))
            [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return static_cast<rfc9581::timescale>(argument);
    }

    static constexpr std::chrono::year tai_clock_epoch_year{1958};

    static constexpr std::chrono::seconds ptp_epoch_since_tai_clock_epoch =
        std::chrono::sys_days{} - std::chrono::sys_days{tai_clock_epoch_year / std::chrono::January / 1};

    static constexpr std::chrono::seconds tai_minus_gps{rfc9581::tai_minus_gps_seconds};

    static constexpr std::chrono::seconds clock_offset_bound = ptp_epoch_since_tai_clock_epoch + tai_minus_gps;

    template <class D>
    static constexpr std::expected<D, error> check_shifted_ticks(D const ticks, std::chrono::seconds const shift)
    {
        auto const step = check_epoch_ticks<D>(shift.count());
        if (!step) [[unlikely]]
            return step;
        if constexpr (std::integral<typename D::rep>) {
            if ((step->count() > 0 && ticks > D::max() - *step) || (step->count() < 0 && ticks < D::min() - *step))
                [[unlikely]]
                return std::unexpected(error::number_out_of_range);
        }
        return ticks + *step;
    }

    template <class D>
    static constexpr std::expected<void, error> check_clock_range(D const ticks, std::chrono::seconds const bound)
    {
        if constexpr (std::integral<typename D::rep>) {
            auto const seconds = std::chrono::floor<std::chrono::seconds>(ticks);
            auto const highest = std::chrono::floor<std::chrono::seconds>(D::max());
            auto const lowest = std::chrono::ceil<std::chrono::seconds>(D::min());
            if (seconds > highest - bound || seconds < lowest + bound) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
        }
        return {};
    }

    template <class D, class C>
    static constexpr std::expected<D, error> check_exact_ticks(C const ticks)
    {
        auto const result = std::chrono::duration_cast<D>(ticks);
        if constexpr (std::integral<typename D::rep>)
            if (std::chrono::duration_cast<C>(result) != ticks) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
        return result;
    }

#if __cpp_lib_chrono >= 201907L && defined(__cpp_lib_spanstream)
    static constexpr std::string_view utc_zone_name = "UTC";

    static std::expected<std::chrono::time_zone const *, error> check_time_zone(std::string_view const name)
    {
        std::chrono::tzdb const &db = std::chrono::get_tzdb();
        std::string_view target = name;
        auto const link = std::ranges::lower_bound(db.links, name, {}, &std::chrono::time_zone_link::name);
        if (link != db.links.end() && link->name() == name)
            target = link->target();
        auto const zone = std::ranges::lower_bound(db.zones, target, {}, &std::chrono::time_zone::name);
        if (zone == db.zones.end() || zone->name() != target) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return &*zone;
    }
#endif

    static constexpr std::expected<void, error>
    check_extended_time_map(major_type const major, std::uint8_t const info, std::uint64_t const argument,
                            std::size_t const bytes_left)
    {
        if (major != major_type::map ||
            info == std::to_underlying(rfc8949::additional_information::indefinite_length)) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return check_bytes_left(count_enclosed_data_items(major, argument), bytes_left);
    }

    template <class D>
    using epoch_seconds = std::conditional_t<fraction_key_of_period<typename D::period>().has_value() &&
                                                 std::integral<typename D::rep>,
                                             std::variant<std::int64_t, double, std::pair<std::int64_t, D>>,
                                             std::variant<std::int64_t, double>>;

    template <class D>
    static std::expected<epoch_seconds<D>, error> check_epoch_seconds(D const ticks)
    {
        using rep = typename D::rep;
        using P = typename D::period;
        if constexpr (std::integral<rep>) {
            if (ticks.count() % P::den == 0) {
                auto const whole = ticks.count() / P::den;
                if (std::cmp_greater(whole, std::numeric_limits<std::int64_t>::max() / P::num) ||
                    std::cmp_less(whole, std::numeric_limits<std::int64_t>::min() / P::num)) [[unlikely]]
                    return std::unexpected(error::number_out_of_range);
                return static_cast<std::int64_t>(whole) * static_cast<std::int64_t>(P::num);
            }
            if constexpr (fraction_key_of_period<P>().has_value()) {
                auto seconds = static_cast<std::int64_t>(ticks.count() / P::den);
                auto fraction = static_cast<std::int64_t>(ticks.count() % P::den);
                if (fraction < 0) {
                    fraction += P::den;
                    --seconds;
                }
                return std::pair{seconds, D{static_cast<rep>(fraction)}};
            }
            return std::unexpected(error::number_out_of_range);
        } else {
            double const seconds = seconds_of_ticks<P>(static_cast<double>(ticks.count()));
            if (auto const whole = check_whole_number<std::int64_t>(seconds); whole)
                return *whole;
            auto const back = check_epoch_ticks<D>(seconds);
            if (!back || *back != ticks) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            return seconds;
        }
    }

    CBOR_ALWAYS_INLINE static constexpr std::expected<void, error>
    check_magnitude_size(std::size_t const size, std::size_t const width)
    {
        if (size > width) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        return {};
    }

    friend class cbor::detail::reflection;

    template <class>
    friend class cbor::schema;
};

}

static_assert(detail::validity::check_limit(limits::string_length, detail::validity::size_limit,
                                            error::string_length_exceeded)
                  .has_value(),
              "CBOR_STRING_LENGTH_DEFAULT fits in std::size_t.");
static_assert(detail::validity::check_limit(limits::container_elements, detail::validity::size_limit,
                                            error::container_elements_exceeded)
                  .has_value(),
              "CBOR_CONTAINER_ELEMENTS_DEFAULT fits in std::size_t.");
}

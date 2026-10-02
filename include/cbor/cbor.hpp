#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#if __cpp_impl_reflection
#include <meta>
#include <stdckdint.h>
#include <stdfloat>
#endif
#include <string>
#include <string_view>
#if CBOR_SIMDUTF
#include <simdutf.h>
#endif
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cbor
{

enum class error {
    too_little_data = 1,
    syntax_error,
    indefinite_length,
    invalid_utf8_string,
    nesting_depth_exceeded,
    inadmissible_type_for_tag_content,
    sharedref_index_not_marked,
    sharedref_index_out_of_range,
    sharedref_not_complete,
    reserved_simple_value,
    unsupported_value,
    not_indexable,
    index_out_of_bounds,
    key_not_found,
    invalid_path,
    incorrect_type,
    number_out_of_range
};

enum class condition { not_well_formed = 1, not_valid, not_supported, not_found };

class category final : public std::error_category
{
public:
    constexpr category() = default;

    char const *name() const noexcept override
    {
        return "cbor";
    }

    std::string message(int const value) const override
    {
        switch (static_cast<error>(value)) {
        case error::too_little_data:
            return "too little data";
        case error::syntax_error:
            return "syntax error";
        case error::indefinite_length:
            return "indefinite length";
        case error::invalid_utf8_string:
            return "invalid UTF-8 string";
        case error::nesting_depth_exceeded:
            return "nesting depth exceeded";
        case error::inadmissible_type_for_tag_content:
            return "inadmissible type for tag content";
        case error::sharedref_index_not_marked:
            return "sharedref index not marked";
        case error::sharedref_index_out_of_range:
            return "sharedref index out of range";
        case error::sharedref_not_complete:
            return "sharedref not complete";
        case error::reserved_simple_value:
            return "reserved simple value";
        case error::unsupported_value:
            return "unsupported value";
        case error::not_indexable:
            return "not indexable";
        case error::index_out_of_bounds:
            return "index outside of array bounds";
        case error::key_not_found:
            return "key not found";
        case error::invalid_path:
            return "invalid path";
        case error::incorrect_type:
            return "incorrect type";
        case error::number_out_of_range:
            return "number out of range";
        }
        return "unknown cbor error";
    }

    std::error_condition default_error_condition(int const value) const noexcept override
    {
        switch (static_cast<error>(value)) {
        case error::too_little_data:
        case error::syntax_error:
            return {static_cast<int>(condition::not_well_formed), *this};
        case error::indefinite_length:
        case error::nesting_depth_exceeded:
        case error::unsupported_value:
            return {static_cast<int>(condition::not_supported), *this};
        case error::not_indexable:
        case error::index_out_of_bounds:
        case error::key_not_found:
        case error::incorrect_type:
        case error::number_out_of_range:
            return {static_cast<int>(condition::not_found), *this};
        case error::invalid_utf8_string:
        case error::inadmissible_type_for_tag_content:
        case error::sharedref_index_not_marked:
        case error::sharedref_index_out_of_range:
        case error::sharedref_not_complete:
        case error::reserved_simple_value:
        case error::invalid_path:
            return {static_cast<int>(condition::not_valid), *this};
        }
        return {value, *this};
    }
};

inline constinit category const cbor_category;

inline std::error_code make_error_code(error const e) noexcept
{
    return {static_cast<int>(e), cbor_category};
}

inline std::error_condition make_error_condition(condition const c) noexcept
{
    return {static_cast<int>(c), cbor_category};
}

} // namespace cbor

template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};

template <>
struct std::is_error_condition_enum<cbor::condition> : std::true_type {};

namespace cbor
{

enum class major_type : std::uint8_t {
    unsigned_integer,
    negative_integer,
    byte_string,
    text_string,
    array,
    map,
    tag,
    simple_float
};

enum class simple_value : std::uint8_t { false_value = 20, true_value, null, undefined };

struct typed_array {
    std::uint64_t tag;
    std::span<std::byte const> bytes;
};

template <class Tag>
struct customization_point {
    template <class... Args>
        requires requires(Tag const &tag, Args &&...args) { tag_invoke(tag, std::forward<Args>(args)...); }
    constexpr decltype(auto) operator()(Args &&...args) const
    {
        return tag_invoke(static_cast<Tag const &>(*this), std::forward<Args>(args)...);
    }
};

inline constexpr struct unsigned_integer_decode_t : customization_point<unsigned_integer_decode_t> {
} unsigned_integer_decode;
inline constexpr struct negative_integer_decode_t : customization_point<negative_integer_decode_t> {
} negative_integer_decode;
inline constexpr struct unsigned_bignum_decode_t : customization_point<unsigned_bignum_decode_t> {
} unsigned_bignum_decode;
inline constexpr struct negative_bignum_decode_t : customization_point<negative_bignum_decode_t> {
} negative_bignum_decode;
inline constexpr struct byte_string_decode_t : customization_point<byte_string_decode_t> {
} byte_string_decode;
inline constexpr struct text_string_decode_t : customization_point<text_string_decode_t> {
} text_string_decode;
inline constexpr struct array_decode_t : customization_point<array_decode_t> {
} array_decode;
inline constexpr struct array_append_t : customization_point<array_append_t> {
} array_append;
inline constexpr struct map_decode_t : customization_point<map_decode_t> {
} map_decode;
inline constexpr struct map_insert_t : customization_point<map_insert_t> {
} map_insert;
inline constexpr struct kind_of_t : customization_point<kind_of_t> {
} kind_of;
inline constexpr struct unsigned_of_t : customization_point<unsigned_of_t> {
} unsigned_of;
inline constexpr struct magnitude_of_t : customization_point<magnitude_of_t> {
} magnitude_of;
inline constexpr struct bytes_of_t : customization_point<bytes_of_t> {
} bytes_of;
inline constexpr struct text_of_t : customization_point<text_of_t> {
} text_of;
inline constexpr struct float_of_t : customization_point<float_of_t> {
} float_of;
inline constexpr struct simple_of_t : customization_point<simple_of_t> {
} simple_of;
inline constexpr struct array_size_t : customization_point<array_size_t> {
} array_size;
inline constexpr struct array_at_t : customization_point<array_at_t> {
} array_at;
inline constexpr struct map_size_t : customization_point<map_size_t> {
} map_size;
inline constexpr struct map_for_each_t : customization_point<map_for_each_t> {
} map_for_each;
inline constexpr struct typed_array_of_t : customization_point<typed_array_of_t> {
} typed_array_of;
inline constexpr struct embed_of_t : customization_point<embed_of_t> {
} embed_of;
inline constexpr struct registered_tag_t : customization_point<registered_tag_t> {
} registered_tag;
inline constexpr struct tag_begin_t : customization_point<tag_begin_t> {
} tag_begin;
inline constexpr struct registered_decode_t : customization_point<registered_decode_t> {
} registered_decode;
inline constexpr struct after_decode_t : customization_point<after_decode_t> {
} after_decode;
inline constexpr struct before_encode_t : customization_point<before_encode_t> {
} before_encode;
inline constexpr struct value_identity_t : customization_point<value_identity_t> {
} value_identity;
inline constexpr struct key_identity_t : customization_point<key_identity_t> {
} key_identity;
inline constexpr struct tag_decode_t : customization_point<tag_decode_t> {
} tag_decode;
inline constexpr struct float_decode_t : customization_point<float_decode_t> {
} float_decode;
inline constexpr struct simple_value_decode_t : customization_point<simple_value_decode_t> {
} simple_value_decode;

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> decode(Host &host, std::string_view bytes);

template <class Writer>
struct encoder;

enum class sharedrefs { off, on };

enum class kind {
    unsigned_integer,
    negative_integer,
    unsigned_bignum,
    negative_bignum,
    byte_string,
    text_string,
    floating_point,
    simple_value,
    array,
    map,
    typed_array,
    registered,
    unsupported
};

enum class pass;

struct lazy;

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &bytes);

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string bytes);

template <std::size_t DepthMax>
std::expected<lazy, error> lazy_at(lazy const &l, std::int64_t index);

template <std::size_t DepthMax>
std::expected<lazy, error> lazy_at(lazy const &l, std::string_view key);

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> lazy_decode(Host &host, lazy const &l);

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
std::expected<T, error> lazy_get(lazy const &l);

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

template <std::size_t DepthMax>
std::expected<lazy_elements<DepthMax>, error> lazy_elements_of(lazy const &array);

template <std::size_t DepthMax>
std::expected<lazy_entries<DepthMax>, error> lazy_entries_of(lazy const &map);

struct path_step {
    enum class kind { key, index, wildcard } kind;
    std::string_view key;
    std::int64_t index;
};

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> path_decode(Host &host, std::span<path_step const> steps, lazy const &l);

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view bytes);

template <std::size_t DepthMax, sharedrefs Sharing = sharedrefs::off, class Host, class Writer>
std::expected<void, std::error_code> encode(Host &host, Writer &writer, typename Host::value const &value);

#if __cpp_impl_reflection
template <class T>
consteval std::size_t fixed_size();

template <class T>
consteval std::size_t no_fixed_size();

template <class T, std::meta::info Member>
consteval std::size_t member_offset();

template <class Writer, class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
std::expected<void, std::errc> encode(Writer &writer, T const &value);

template <class T>
struct document {
    std::string_view bytes;
    std::size_t offset;
    std::size_t floor;
};

template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    consteval fixed_string(char const (&text)[N])
    {
        std::ranges::copy(text, value.begin());
    }

    consteval std::string_view view() const
    {
        return {value.data(), N - 1};
    }
};

template <class T, fixed_string Path>
auto at_path_compiled(document<T> const doc);
#endif

class internal
{
    enum class additional_information : std::uint8_t {
        one_byte_argument = 24,
        two_byte_argument,
        four_byte_argument,
        eight_byte_argument,
        indefinite_length = 31
    };

    static constexpr std::uint8_t simple_value_one_byte_min = 32;

    enum class tag_number : std::uint64_t {
        unsigned_bignum = 2,
        negative_bignum = 3,
        encoded_cbor_data_item = 24,
        shareable = 28,
        sharedref = 29,
        typed_array_first = 64,
        typed_array_reserved = 76,
        float128_big_endian = 83,
        typed_array_last = 87
    };

    template <class Host>
    using marks = std::vector<std::optional<typename Host::value>>;

#if __cpp_impl_reflection
    static constexpr std::size_t initial_byte_size = 1;

    static constexpr int extended_precision_digits = 64;

    static constexpr std::size_t head_size(std::uint64_t argument)
    {
        if (argument < std::to_underlying(additional_information::one_byte_argument))
            return initial_byte_size;
        if (std::in_range<std::uint8_t>(argument))
            return initial_byte_size + sizeof(std::uint8_t);
        if (std::in_range<std::uint16_t>(argument))
            return initial_byte_size + sizeof(std::uint16_t);
        if (std::in_range<std::uint32_t>(argument))
            return initial_byte_size + sizeof(std::uint32_t);
        return initial_byte_size + sizeof(std::uint64_t);
    }

    static constexpr std::size_t dynamic_type_sizes = initial_byte_size + 2 * (initial_byte_size + sizeof(std::uint32_t));

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
    static consteval std::span<std::meta::info const> data_members()
    {
        return std::define_static_array(
            std::meta::nonstatic_data_members_of(^^U, std::meta::access_context::unchecked()));
    }

    template <class U>
    static consteval std::size_t struct_fixed_size()
    {
        if constexpr (!std::meta::bases_of(^^U, std::meta::access_context::unchecked()).empty()) {
            return no_fixed_size<U>();
        } else {
            std::size_t size = head_size(data_members<U>().size());
            template for (constexpr auto m : data_members<U>()) {
                if constexpr (!std::meta::has_identifier(m) || std::meta::is_bit_field(m) || !std::meta::is_public(m))
                    return no_fixed_size<U>();
                constexpr std::size_t key = std::meta::u8identifier_of(m).size();
                size += head_size(key) + key + fixed_size<typename[:std::meta::type_of(m):]>();
            }
            return size;
        }
    }

    template <class T, std::meta::info Member>
    friend consteval std::size_t member_offset();

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
    static constexpr bool is_fixed_text = requires {
        typename fixed_length<U>::element;
        requires std::same_as<typename fixed_length<U>::element, char> ||
                     std::same_as<typename fixed_length<U>::element, char8_t>;
    };

    template <class U>
    static constexpr bool is_optional = requires(U const &v) {
        v.has_value();
        *v;
        typename U::value_type;
    } && !requires { typename U::error_type; };

    template <class U>
    static constexpr bool is_map = std::ranges::sized_range<U> && requires {
        typename U::key_type;
        typename U::mapped_type;
    };

    static constexpr void head_encode(std::vector<char> &bytes, major_type major, std::uint64_t argument)
    {
        std::size_t const size = head_size(argument);
        if (size == initial_byte_size) {
            bytes.push_back(static_cast<char>(std::to_underlying(major) << 5 | argument));
            return;
        }
        std::size_t const width = size - initial_byte_size;
        bytes.push_back(static_cast<char>(std::to_underlying(major) << 5 |
                                          (std::to_underlying(additional_information::one_byte_argument) +
                                           std::countr_zero(width))));
        for (std::size_t i = width; i-- > 0;)
            bytes.push_back(static_cast<char>(argument >> (8 * i)));
    }

    static constexpr void fixed_width_head_encode(std::vector<char> &bytes, major_type major, std::size_t width)
    {
        bytes.push_back(static_cast<char>(std::to_underlying(major) << 5 |
                                          (std::to_underlying(additional_information::one_byte_argument) +
                                           std::countr_zero(width))));
        bytes.resize(bytes.size() + width);
    }

    template <class T>
    static consteval void zero_initialized_encode(std::vector<char> &bytes)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            head_encode(bytes, major_type::simple_float, std::to_underlying(simple_value::false_value));
        } else if constexpr (std::is_enum_v<U>) {
            zero_initialized_encode<std::underlying_type_t<U>>(bytes);
        } else if constexpr (std::same_as<U, __int128> || std::same_as<U, unsigned __int128>) {
            head_encode(bytes, major_type::tag, std::to_underlying(tag_number::unsigned_bignum));
            head_encode(bytes, major_type::byte_string, sizeof(U));
            bytes.resize(bytes.size() + sizeof(U));
        } else if constexpr (std::is_integral_v<U>) {
            fixed_width_head_encode(bytes, major_type::unsigned_integer, sizeof(U));
        } else if constexpr (std::is_floating_point_v<U>) {
            constexpr int digits = std::numeric_limits<U>::digits;
            if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
                fixed_width_head_encode(bytes, major_type::simple_float, sizeof(std::float16_t));
            else if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits ||
                               digits == std::numeric_limits<std::float32_t>::digits)
                fixed_width_head_encode(bytes, major_type::simple_float, sizeof(std::float32_t));
            else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
                fixed_width_head_encode(bytes, major_type::simple_float, sizeof(std::float64_t));
            else {
                head_encode(bytes, major_type::tag, std::to_underlying(tag_number::float128_big_endian));
                head_encode(bytes, major_type::byte_string, sizeof(std::float128_t));
                bytes.resize(bytes.size() + sizeof(std::float128_t));
            }
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t>) {
                head_encode(bytes, major_type::text_string, n);
                bytes.resize(bytes.size() + n);
            } else if constexpr (std::same_as<E, unsigned char> || std::same_as<E, std::byte>) {
                head_encode(bytes, major_type::byte_string, n);
                bytes.resize(bytes.size() + n);
            } else {
                head_encode(bytes, major_type::array, n);
                for (std::size_t i = 0; i < n; ++i)
                    zero_initialized_encode<E>(bytes);
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            head_encode(bytes, major_type::map, data_members<U>().size());
            template for (constexpr auto m : data_members<U>()) {
                constexpr auto key = std::meta::u8identifier_of(m);
                head_encode(bytes, major_type::text_string, key.size());
                for (char8_t const c : key)
                    bytes.push_back(static_cast<char>(c));
                zero_initialized_encode<typename[:std::meta::type_of(m):]>(bytes);
            }
        } else {
            head_encode(bytes, major_type::array, 2);
            fixed_width_head_encode(bytes, major_type::unsigned_integer, sizeof(std::uint32_t));
            fixed_width_head_encode(bytes, major_type::unsigned_integer, sizeof(std::uint32_t));
        }
    }

    template <class T>
    static consteval std::span<char const> zero_initialized()
    {
        std::vector<char> bytes;
        zero_initialized_encode<T>(bytes);
        return std::define_static_array(bytes);
    }

    template <std::unsigned_integral V>
    static constexpr std::array<char, sizeof(V)> big_endian(V const value)
    {
        return std::bit_cast<std::array<char, sizeof(V)>>(std::byteswap(value));
    }

    static constexpr std::array<char, sizeof(unsigned __int128)> big_endian(unsigned __int128 const value)
    {
        auto const high = big_endian(static_cast<std::uint64_t>(value >> 64));
        auto const low = big_endian(static_cast<std::uint64_t>(value));
        std::array<char, sizeof(unsigned __int128)> bytes;
        std::ranges::copy(high, bytes.begin());
        std::ranges::copy(low, std::ranges::next(bytes.begin(), sizeof(std::uint64_t)));
        return bytes;
    }

    template <class U>
    static constexpr auto float_bits(U const value)
    {
        constexpr int digits = std::numeric_limits<U>::digits;
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return std::bit_cast<std::uint16_t>(value);
        else if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits)
            return std::bit_cast<std::uint32_t>(static_cast<std::float32_t>(value));
        else if constexpr (digits == std::numeric_limits<std::float32_t>::digits)
            return std::bit_cast<std::uint32_t>(value);
        else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
            return std::bit_cast<std::uint64_t>(value);
        else
            return std::bit_cast<unsigned __int128>(static_cast<std::float128_t>(value));
    }

    struct second_item {
        std::size_t items;
        std::size_t bytes;
    };

    static constexpr std::expected<second_item, std::errc> second_item_add(second_item const a, second_item const b)
    {
        second_item sum;
        if (ckd_add(&sum.items, a.items, b.items) || ckd_add(&sum.bytes, a.bytes, b.bytes)) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return sum;
    }

    template <class E, class R>
    static std::expected<second_item, std::errc> elements_of(R const &range, major_type)
    {
        std::size_t block;
        if (ckd_mul(&block, std::ranges::size(range), fixed_size<E>())) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        std::expected<second_item, std::errc> sum = second_item{1, head_size(std::ranges::size(range))};
        sum = sum.and_then([&](second_item const s) { return second_item_add(s, second_item{0, block}); });
        for (auto const &e : range) {
            if (!sum) [[unlikely]]
                return sum;
            sum = sum.and_then([&](second_item const s) {
                return second_item_of<E>(e).and_then([&](second_item const t) { return second_item_add(s, t); });
            });
        }
        return sum;
    }

    template <class T>
    static std::expected<second_item, std::errc> second_item_of(T const &value)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || std::same_as<U, __int128> ||
                      std::same_as<U, unsigned __int128>) {
            return second_item{0, 0};
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            std::expected<second_item, std::errc> sum = second_item{0, 0};
            if constexpr (!std::is_arithmetic_v<E> && !std::same_as<E, std::byte>)
                for (auto const &e : value)
                    sum = sum.and_then([&](second_item const s) {
                        return second_item_of<E>(e).and_then([&](second_item const t) { return second_item_add(s, t); });
                    });
            return sum;
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            std::expected<second_item, std::errc> sum = second_item{0, 0};
            template for (constexpr auto m : data_members<U>())
                sum = sum.and_then([&](second_item const s) {
                    return second_item_of<typename[:std::meta::type_of(m):]>(value.[:m:])
                        .and_then([&](second_item const t) { return second_item_add(s, t); });
                });
            return sum;
        } else if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            if (!value.has_value())
                return second_item{0, 0};
            return second_item_of<E>(*value).and_then(
                [](second_item const t) { return second_item_add(second_item{1, fixed_size<E>()}, t); });
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            std::size_t const n = std::ranges::size(value);
            std::size_t bytes;
            if (ckd_add(&bytes, head_size(n), n)) [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            return second_item{1, bytes};
        } else if constexpr (is_map<U>) {
            std::size_t const n = std::ranges::size(value);
            std::size_t block;
            if (ckd_mul(&block, n, fixed_size<typename U::key_type>() + fixed_size<typename U::mapped_type>()))
                [[unlikely]]
                return std::unexpected(std::errc::value_too_large);
            std::expected<second_item, std::errc> sum = second_item_add(second_item{1, head_size(n)}, second_item{0, block});
            for (auto const &[k, v] : value)
                sum = sum.and_then([&](second_item const s) {
                    return second_item_of<typename U::key_type>(k)
                        .and_then([&](second_item const t) { return second_item_add(s, t); })
                        .and_then([&](second_item const s2) {
                            return second_item_of<typename U::mapped_type>(v).and_then(
                                [&](second_item const t) { return second_item_add(s2, t); });
                        });
                });
            return sum;
        } else {
            return elements_of<std::ranges::range_value_t<U>>(value, major_type::array);
        }
    }

    struct cursor {
        std::span<char> out;
        std::size_t position;

        template <class E>
        void zero_initialized_copy(std::size_t const at)
        {
            constexpr std::size_t n = fixed_size<E>();
            std::ranges::copy(std::span<char const, n>{zero_initialized<E>().data(), n},
                              out.subspan(at).template first<n>().begin());
        }

        std::size_t head_write(std::size_t const at, major_type const major, std::uint64_t const argument)
        {
            std::size_t const size = head_size(argument);
            auto const field = out.subspan(at, size);
            if (size == initial_byte_size) {
                field.front() = static_cast<char>(std::to_underlying(major) << 5 | argument);
                return size;
            }
            std::size_t const width = size - initial_byte_size;
            field.front() = static_cast<char>(std::to_underlying(major) << 5 |
                                              (std::to_underlying(additional_information::one_byte_argument) +
                                               std::countr_zero(width)));
            auto const big = big_endian(argument);
            std::ranges::copy(std::span<char const>(big).last(width), field.subspan(initial_byte_size).begin());
            return size;
        }

        template <class E, class R>
        std::size_t elements_encode(R const &range)
        {
            constexpr std::size_t size = fixed_size<E>();
            std::size_t const data = position;
            position += std::ranges::size(range) * size;
            std::size_t at = data;
            for (auto const &e : range) {
                zero_initialized_copy<E>(at);
                value_encode<E>(at, e);
                at += size;
            }
            return data;
        }

        template <class V>
        void reference_encode(std::size_t const offset, V const &value)
        {
            using U = std::remove_cv_t<V>;
            std::size_t data = position;
            std::size_t length = 0;
            if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                if (value.has_value()) {
                    position += fixed_size<E>();
                    zero_initialized_copy<E>(data);
                    value_encode<E>(data, *value);
                    length = 1;
                }
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                length = std::ranges::size(value);
                position += head_write(position, is_text_range<U> ? major_type::text_string : major_type::byte_string,
                                       length);
                data = position;
                std::ranges::copy(std::as_bytes(std::span(value)), std::as_writable_bytes(out.subspan(data, length)).begin());
                position += length;
            } else if constexpr (is_map<U>) {
                using K = typename U::key_type;
                using M = typename U::mapped_type;
                length = std::ranges::size(value);
                position += head_write(position, major_type::map, length);
                data = position;
                position += length * (fixed_size<K>() + fixed_size<M>());
                std::size_t at = data;
                for (auto const &[k, v] : value) {
                    zero_initialized_copy<K>(at);
                    value_encode<K>(at, k);
                    at += fixed_size<K>();
                    zero_initialized_copy<M>(at);
                    value_encode<M>(at, v);
                    at += fixed_size<M>();
                }
            } else {
                length = std::ranges::size(value);
                position += head_write(position, major_type::array, length);
                data = elements_encode<std::ranges::range_value_t<U>>(value);
            }
            auto const field = out.subspan(offset).template first<dynamic_type_sizes>();
            std::ranges::copy(big_endian(static_cast<std::uint32_t>(data)),
                              field.template subspan<2, sizeof(std::uint32_t)>().begin());
            std::ranges::copy(big_endian(static_cast<std::uint32_t>(length)),
                              field.template last<sizeof(std::uint32_t)>().begin());
        }

        template <class T>
        void value_encode(std::size_t const offset, T const &value)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (std::same_as<U, bool>) {
                auto const field = out.subspan(offset).template first<1>();
                field.front() = static_cast<char>(field.front() | static_cast<char>(value));
            } else if constexpr (std::is_enum_v<U>) {
                value_encode<std::underlying_type_t<U>>(offset, std::to_underlying(value));
            } else if constexpr (std::same_as<U, __int128> || std::same_as<U, unsigned __int128>) {
                auto const field = out.subspan(offset).template first<fixed_size<U>()>();
                unsigned __int128 magnitude = static_cast<unsigned __int128>(value);
                if constexpr (std::same_as<U, __int128>) {
                    unsigned __int128 const sign = static_cast<unsigned __int128>(value >> 127);
                    field.front() = static_cast<char>(field.front() | static_cast<char>(sign & 1));
                    magnitude ^= sign;
                }
                std::ranges::copy(big_endian(magnitude), field.template last<sizeof(U)>().begin());
            } else if constexpr (std::unsigned_integral<U>) {
                auto const field = out.subspan(offset).template first<fixed_size<U>()>();
                std::ranges::copy(big_endian(value), field.template last<sizeof(U)>().begin());
            } else if constexpr (std::signed_integral<U>) {
                using M = std::make_unsigned_t<U>;
                auto const field = out.subspan(offset).template first<fixed_size<U>()>();
                M const sign = static_cast<M>(value >> (8 * sizeof(U) - 1));
                field.front() = static_cast<char>(field.front() | static_cast<char>((sign & 1) << 5));
                std::ranges::copy(big_endian(static_cast<M>(static_cast<M>(value) ^ sign)),
                                  field.template last<sizeof(U)>().begin());
            } else if constexpr (std::is_floating_point_v<U>) {
                auto const field = out.subspan(offset).template first<fixed_size<U>()>();
                auto const bits = big_endian(float_bits(value));
                std::ranges::copy(bits, field.template last<bits.size()>().begin());
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                auto const field = out.subspan(offset).template first<fixed_size<U>()>();
                if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                              std::same_as<E, std::byte>) {
                    std::ranges::copy(std::as_bytes(std::span(value)),
                                      std::as_writable_bytes(field.template last<n>()).begin());
                } else {
                    std::size_t at = offset + head_size(n);
                    for (auto const &e : value) {
                        value_encode<E>(at, e);
                        at += fixed_size<E>();
                    }
                }
            } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
                template for (constexpr auto m : data_members<U>())
                    value_encode<typename[:std::meta::type_of(m):]>(offset + member_offset<U, m>(), value.[:m:]);
            } else {
                reference_encode(offset, value);
            }
        }
    };


    template <class U>
    static consteval std::meta::info member_named(std::string_view const name)
    {
        for (auto const m : data_members<U>()) {
            auto const key = std::meta::u8identifier_of(m);
            if (std::ranges::equal(key, name, [](char8_t a, char b) { return a == static_cast<char8_t>(b); }))
                return m;
        }
        return std::meta::info{};
    }

    static consteval std::size_t step_end(std::string_view const path, std::size_t const at)
    {
        std::size_t end = at + 1;
        while (end < path.size() && path.substr(end, 1) != "." && path.substr(end, 1) != "[")
            ++end;
        return end;
    }

    static consteval std::size_t index_of(std::string_view const digits)
    {
        std::size_t value = 0;
        for (char const c : digits)
            value = value * 10 + static_cast<std::size_t>(c - '0');
        return value;
    }

    template <class V>
    static V unsigned_read(std::string_view const bytes, std::size_t const at)
    {
        std::array<char, sizeof(V)> big;
        std::ranges::copy(bytes.substr(at, sizeof(V)), big.begin());
        return std::byteswap(std::bit_cast<V>(big));
    }

    static unsigned __int128 unsigned128_read(std::string_view const bytes, std::size_t const at)
    {
        auto const high = static_cast<unsigned __int128>(unsigned_read<std::uint64_t>(bytes, at));
        auto const low = static_cast<unsigned __int128>(unsigned_read<std::uint64_t>(bytes, at + sizeof(std::uint64_t)));
        return high << 64 | low;
    }

    template <class T>
    static T fixed_value_read(std::string_view const bytes, std::size_t const at)
    {
        using U = std::remove_cv_t<T>;
        auto const head = static_cast<unsigned char>(bytes.substr(at, 1).front());
        if constexpr (std::same_as<U, bool>) {
            return (head & 1) != 0;
        } else if constexpr (std::is_enum_v<U>) {
            return static_cast<U>(fixed_value_read<std::underlying_type_t<U>>(bytes, at));
        } else if constexpr (std::same_as<U, __int128> || std::same_as<U, unsigned __int128>) {
            unsigned __int128 const magnitude = unsigned128_read(bytes, at + fixed_size<U>() - sizeof(U));
            if constexpr (std::same_as<U, __int128>) {
                unsigned __int128 const sign = -static_cast<unsigned __int128>(head & 1);
                return static_cast<U>(magnitude ^ sign);
            } else {
                return magnitude;
            }
        } else if constexpr (std::unsigned_integral<U>) {
            return unsigned_read<U>(bytes, at + initial_byte_size);
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(-static_cast<M>((head >> 5) & 1));
            return static_cast<U>(unsigned_read<M>(bytes, at + initial_byte_size) ^ sign);
        } else {
            using B = decltype(float_bits(U{}));
            if constexpr (std::same_as<B, unsigned __int128>) {
                auto const bits = unsigned128_read(bytes, at + fixed_size<U>() - sizeof(B));
                return static_cast<U>(std::bit_cast<std::float128_t>(bits));
            } else {
                auto const bits = unsigned_read<B>(bytes, at + fixed_size<U>() - sizeof(B));
                using F = std::conditional_t<sizeof(B) == sizeof(std::uint16_t), std::float16_t,
                                             std::conditional_t<sizeof(B) == sizeof(std::uint32_t), std::float32_t,
                                                                std::float64_t>>;
                return static_cast<U>(std::bit_cast<F>(bits));
            }
        }
    }

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    static std::expected<reference, error> reference_read(std::string_view const bytes, std::size_t const at,
                                                           std::size_t const floor, std::size_t const element)
    {
        std::size_t const data = unsigned_read<std::uint32_t>(bytes, at + 2);
        std::size_t const length = unsigned_read<std::uint32_t>(bytes, at + 7);
        std::size_t size;
        std::size_t end;
        if (data < floor || ckd_mul(&size, length, element) || ckd_add(&end, data, size) || end > bytes.size())
            [[unlikely]]
            return std::unexpected(error::too_little_data);
        return reference{data, length};
    }

    template <class T, fixed_string Path, std::size_t At>
    static consteval auto path_result()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; })
                return cbor::document<U>{};
            else if constexpr (is_fixed_text<U>)
                return std::string_view{};
            else if constexpr (is_optional<U>)
                return std::optional<typename U::value_type>{};
            else if constexpr (is_text_range<U> || is_byte_range<U>)
                return std::string_view{};
            else
                return U{};
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            return path_result<typename[:std::meta::type_of(m):], Path, end>();
        } else {
            constexpr std::size_t close = path.find(']', At);
            if constexpr (requires { fixed_length<U>::value; })
                return path_result<typename fixed_length<U>::element, Path, close + 1>();
            else
                return path_result<std::ranges::range_value_t<U>, Path, close + 1>();
        }
    }

    template <class T, fixed_string Path, std::size_t At>
    static consteval bool path_reads_wire()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            return !(std::is_class_v<U> && std::is_aggregate_v<U>) && !requires { fixed_length<U>::value; } &&
                   !std::is_arithmetic_v<U> && !std::is_enum_v<U> && !std::same_as<U, __int128> &&
                   !std::same_as<U, unsigned __int128>;
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            return path_reads_wire<typename[:std::meta::type_of(m):], Path, end>();
        } else {
            constexpr std::size_t close = path.find(']', At);
            if constexpr (requires { fixed_length<U>::value; })
                return path_reads_wire<typename fixed_length<U>::element, Path, close + 1>();
            else
                return true;
        }
    }

    template <class T, fixed_string Path, std::size_t At>
    static auto path_walk(std::string_view const bytes, std::size_t const offset, std::size_t const floor)
        -> std::expected<decltype(path_result<T, Path, At>()), error>
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; }) {
                return cbor::document<U>{bytes, offset, floor};
            } else if constexpr (is_fixed_text<U>) {
                return bytes.substr(offset + fixed_size<U>() - fixed_length<U>::value, fixed_length<U>::value);
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r = reference_read(bytes, offset, floor, fixed_size<E>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->length == 0)
                    return std::optional<E>{};
                return std::optional<E>{fixed_value_read<E>(bytes, r->data)};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = reference_read(bytes, offset, floor, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return bytes.substr(r->data, r->length);
            } else {
                return fixed_value_read<U>(bytes, offset);
            }
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            if constexpr (m == std::meta::info{})
                return no_fixed_size<T>();
            else
                return path_walk<typename[:std::meta::type_of(m):], Path, end>(bytes, offset + member_offset<U, m>(),
                                                                               floor);
        } else {
            constexpr std::size_t close = path.find(']', At);
            constexpr std::size_t i = index_of(path.substr(At + 1, close - At - 1));
            if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                if constexpr (i >= n)
                    return no_fixed_size<T>();
                else
                    return path_walk<E, Path, close + 1>(bytes, offset + head_size(n) + i * fixed_size<E>(), floor);
            } else {
                using E = std::ranges::range_value_t<U>;
                auto const r = reference_read(bytes, offset, floor, fixed_size<E>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (i >= r->length) [[unlikely]]
                    return std::unexpected(error::index_out_of_bounds);
                return path_walk<E, Path, close + 1>(bytes, r->data + i * fixed_size<E>(),
                                                     r->data + r->length * fixed_size<E>());
            }
        }
    }

    template <class T, fixed_string Path>
    friend auto at_path_compiled(cbor::document<T> const doc);
    template <class Writer, class T>
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend std::expected<void, std::errc> encode(Writer &writer, T const &value);

    template <class T>
    friend consteval std::size_t fixed_size();
#endif

    friend struct lazy;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    friend std::expected<T, error> lazy_get(lazy const &l);

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
    friend std::expected<lazy_elements<DepthMax>, error> lazy_elements_of(lazy const &array);

    template <std::size_t DepthMax>
    friend std::expected<lazy_entries<DepthMax>, error> lazy_entries_of(lazy const &map);

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &bytes);

    struct prefix {
        std::string_view document;
        std::vector<std::size_t> const &offsets;
        std::vector<bool> decoding;
    };

    static std::string_view magnitude_without_leading_zeros(std::string_view const magnitude)
    {
        std::size_t const first = magnitude.find_first_not_of('\0');
        return first == std::string_view::npos ? std::string_view{} : magnitude.substr(first);
    }

    static std::uint64_t magnitude_value(std::string_view const magnitude)
    {
        std::uint64_t n = 0;
        for (char const c : magnitude)
            n = n << 8 | static_cast<std::uint8_t>(c);
        return n;
    }

    static std::string magnitude_plus_one(std::string_view const magnitude)
    {
        std::string sum(1, '\0');
        sum.append(magnitude);
        for (auto digit = sum.rbegin(); digit != sum.rend(); ++digit) {
            *digit = static_cast<char>(static_cast<std::uint8_t>(*digit) + 1);
            if (*digit != '\0')
                break;
        }
        return std::string(magnitude_without_leading_zeros(sum));
    }

    static std::string magnitude_minus_one(std::string_view const magnitude)
    {
        std::string difference(magnitude);
        for (auto digit = difference.rbegin(); digit != difference.rend(); ++digit) {
            *digit = static_cast<char>(static_cast<std::uint8_t>(*digit) - 1);
            if (*digit != '\xff')
                break;
        }
        return std::string(magnitude_without_leading_zeros(difference));
    }

    enum class simple_float_information : std::uint8_t {
        simple_value_follows = 24,
        half_precision_float,
        single_precision_float,
        double_precision_float,
        break_stop_code = 31
    };

    struct head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    struct decoder {
        std::string_view bytes;
        std::expected<head, error> head_decode()
        {
            if (bytes.empty()) [[unlikely]]
                return std::unexpected(error::too_little_data);
            auto const initial = static_cast<std::uint8_t>(bytes.front());
            auto const major = static_cast<major_type>(initial >> 5);
            std::uint8_t const info = initial & 0x1f;
            if (info < std::to_underlying(additional_information::one_byte_argument)) {
                bytes.remove_prefix(1);
                return head{major, info, info};
            }
            if (info == std::to_underlying(additional_information::indefinite_length) &&
                major >= major_type::byte_string && major <= major_type::map) [[unlikely]]
                return std::unexpected(error::indefinite_length);
            if (info > std::to_underlying(additional_information::eight_byte_argument)) [[unlikely]]
                return std::unexpected(error::syntax_error);
            std::size_t const size =
                std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
            if (bytes.size() < 1 + size) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::string_view const rest = bytes.substr(1, size);
            std::uint64_t argument;
            switch (static_cast<additional_information>(info)) {
            case additional_information::one_byte_argument:
                argument = static_cast<std::uint8_t>(rest.at(0));
                break;
            case additional_information::two_byte_argument: {
                std::uint16_t v;
                std::memcpy(&v, rest.data(), 2);
                argument = std::byteswap(v);
            } break;
            case additional_information::four_byte_argument: {
                std::uint32_t v;
                std::memcpy(&v, rest.data(), 4);
                argument = std::byteswap(v);
            } break;
            default: {
                std::uint64_t v;
                std::memcpy(&v, rest.data(), 8);
                argument = std::byteswap(v);
            } break;
            }
            bytes.remove_prefix(1 + size);
            return head{major, info, argument};
        }

        std::expected<std::string_view, error> byte_string_decode(std::uint64_t length)
        {
            if (bytes.size() < length) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::string_view const string = bytes.substr(0, length);
            bytes.remove_prefix(length);
            return string;
        }

        std::expected<std::string_view, error> text_string_decode(std::uint64_t length)
        {
            auto const text = byte_string_decode(length);
#if CBOR_SIMDUTF
            if (text && !simdutf::validate_utf8(text->data(), text->size())) [[unlikely]]
                return std::unexpected(error::invalid_utf8_string);
#endif
            return text;
        }
    };

    struct document {
        std::shared_ptr<void const> owner;
        std::string_view bytes;
        std::vector<std::size_t> marks;
        std::size_t high_water_mark;

        void mark(decoder const &d)
        {
            std::size_t const offset = bytes.size() - d.bytes.size();
            if (offset > high_water_mark) {
                marks.push_back(offset);
                high_water_mark = offset;
            }
        }
    };

    struct string_sink {
        std::string bytes;

        std::expected<void, std::errc> reserve(std::size_t const size)
        {
            bytes.reserve(bytes.size() + size);
            return {};
        }

        std::expected<void, std::errc> append(std::string_view const part)
        {
            bytes.append(part);
            return {};
        }
    };

    struct no_marks {
        void mark(decoder const &)
        {
        }
    };

    struct precision {
        int significand_bits;
        int exponent_bias;
        std::uint32_t exponent_max;
    };

    static constexpr precision half_precision{10, 15, 31};
    static constexpr precision single_precision{std::numeric_limits<float>::digits - 1,
                                                std::numeric_limits<float>::max_exponent - 1,
                                                2 * std::numeric_limits<float>::max_exponent - 1};
    static constexpr precision double_precision{std::numeric_limits<double>::digits - 1,
                                                std::numeric_limits<double>::max_exponent - 1,
                                                2 * std::numeric_limits<double>::max_exponent - 1};

    static float float_decode_binary16(std::uint16_t half)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr int widen = f.significand_bits - h.significand_bits;
        std::uint32_t const sign =
            static_cast<std::uint32_t>(half >> (h.significand_bits + std::bit_width(h.exponent_max)))
            << (f.significand_bits + std::bit_width(f.exponent_max));
        std::uint32_t const exp = half >> h.significand_bits & h.exponent_max;
        std::uint32_t const frac = half & ((1u << h.significand_bits) - 1u);
        if (exp == h.exponent_max)
            return std::bit_cast<float>(sign | f.exponent_max << f.significand_bits | frac << widen);
        if (exp != 0)
            return std::bit_cast<float>(
                sign | (exp + f.exponent_bias - h.exponent_bias) << f.significand_bits | frac << widen);
        if (frac == 0)
            return std::bit_cast<float>(sign);
        int const shift = std::countl_zero(static_cast<std::uint16_t>(
            frac << (std::numeric_limits<std::uint16_t>::digits - 1 - h.significand_bits)));
        std::uint32_t const mant = (frac << shift) & ((1u << h.significand_bits) - 1u);
        return std::bit_cast<float>(
            sign |
            static_cast<std::uint32_t>(f.exponent_bias - (h.exponent_bias - 1) - shift)
                << f.significand_bits |
            mant << widen);
    }

    static std::uint16_t float_encode_binary16(float value)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr int narrow = f.significand_bits - h.significand_bits;
        std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
        std::uint32_t const sign = bits >> (f.significand_bits + std::bit_width(f.exponent_max))
                                               << (h.significand_bits + std::bit_width(h.exponent_max));
        std::uint32_t const exp32 = bits >> f.significand_bits & f.exponent_max;
        std::uint32_t const mant32 = bits & ((1u << f.significand_bits) - 1u);
        if (exp32 == f.exponent_max) {
            if (mant32 != 0)
                return static_cast<std::uint16_t>(h.exponent_max << h.significand_bits |
                                                  1u << (h.significand_bits - 1));
            return static_cast<std::uint16_t>(sign | h.exponent_max << h.significand_bits);
        }
        if (exp32 == 0)
            return static_cast<std::uint16_t>(sign);
        if (exp32 > static_cast<std::uint32_t>(f.exponent_bias - h.exponent_bias))
            return static_cast<std::uint16_t>(
                sign | (exp32 - (f.exponent_bias - h.exponent_bias)) << h.significand_bits |
                mant32 >> narrow);
        return static_cast<std::uint16_t>(sign | ((1u << f.significand_bits) | mant32) >>
                                                     (f.exponent_bias - 1 - static_cast<int>(exp32)));
    }

    static simple_float_information preferred_float_info(double value)
    {
        constexpr precision h = half_precision;
        constexpr precision f = single_precision;
        constexpr precision d = double_precision;
        std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
        std::uint32_t const exp = bits >> d.significand_bits & d.exponent_max;
        std::uint64_t const mant = bits & ((std::uint64_t{1} << d.significand_bits) - 1u);
        std::uint32_t const mant32 =
            static_cast<std::uint32_t>(mant >> (d.significand_bits - f.significand_bits));
        if (exp == d.exponent_max)
            return simple_float_information::half_precision_float;
        if (exp == 0)
            return mant == 0 ? simple_float_information::half_precision_float
                             : simple_float_information::double_precision_float;
        if ((mant & ((std::uint64_t{1} << (d.significand_bits - f.significand_bits)) - 1u)) != 0 ||
            exp < static_cast<std::uint32_t>(d.exponent_bias - f.exponent_bias + 1) ||
            exp > static_cast<std::uint32_t>(d.exponent_bias + f.exponent_bias))
            return simple_float_information::double_precision_float;
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias + h.exponent_bias))
            return (mant32 & ((1u << (f.significand_bits - h.significand_bits)) - 1u)) == 0
                       ? simple_float_information::half_precision_float
                       : simple_float_information::single_precision_float;
        if (exp >= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias - h.significand_bits + 1) &&
            exp <= static_cast<std::uint32_t>(d.exponent_bias - h.exponent_bias))
            return (mant32 & ((1u << (d.exponent_bias - 1 - static_cast<int>(exp))) - 1u)) == 0
                       ? simple_float_information::half_precision_float
                       : simple_float_information::single_precision_float;
        return simple_float_information::single_precision_float;
    }

    template <std::size_t DepthMax, class Host>
    static std::expected<typename Host::value, error>
    value_decode(decoder &d, Host &host, marks<Host> &shared, prefix *before, std::size_t depth,
                 std::optional<std::size_t> const mark)
    {
        if (depth > DepthMax) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        switch (h->major) {
        case major_type::unsigned_integer:
            return unsigned_integer_decode(host, h->argument);
        case major_type::negative_integer:
            return negative_integer_decode(host, h->argument);
        case major_type::byte_string: {
            auto const s = d.byte_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            return byte_string_decode(host, *s);
        }
        case major_type::text_string: {
            auto const s = d.text_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            return text_string_decode(host, *s);
        }
        case major_type::array: {
            auto array = array_decode(host, std::min<std::uint64_t>(h->argument, d.bytes.size()));
            if (mark)
                shared.at(*mark) = array;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = value_decode<DepthMax>(d, host, shared, before, depth + 1, std::nullopt);
                if (!element) [[unlikely]]
                    return element;
                array = array_append(host, std::move(array), std::move(*element));
            }
            return array;
        }
        case major_type::map: {
            auto map = map_decode(host, std::min<std::uint64_t>(h->argument, d.bytes.size() / 2));
            if (mark)
                shared.at(*mark) = map;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = value_decode<DepthMax>(d, host, shared, before, depth + 1, std::nullopt);
                if (!key) [[unlikely]]
                    return key;
                auto value = value_decode<DepthMax>(d, host, shared, before, depth + 1, std::nullopt);
                if (!value) [[unlikely]]
                    return value;
                map = map_insert(host, std::move(map), std::move(*key), std::move(*value));
            }
            return map;
        }
        case major_type::tag: {
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                std::size_t index = shared.size();
                if (before) {
                    std::size_t const at = before->document.size() - d.bytes.size();
                    auto const known = std::lower_bound(before->offsets.begin(), before->offsets.end(), at);
                    if (known != before->offsets.end() && *known == at)
                        index = static_cast<std::size_t>(known - before->offsets.begin());
                }
                if (index == shared.size())
                    shared.emplace_back();
                auto content = value_decode<DepthMax>(d, host, shared, before, depth + 1, index);
                if (!content) [[unlikely]]
                    return content;
                shared.at(index) = *content;
                return content;
            }
            if (h->argument == std::to_underlying(tag_number::unsigned_bignum) ||
                h->argument == std::to_underlying(tag_number::negative_bignum)) {
                bool const negative = h->argument == std::to_underlying(tag_number::negative_bignum);
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const bytes = d.byte_string_decode(r->argument);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                std::string_view const magnitude = magnitude_without_leading_zeros(*bytes);
                if (magnitude.size() <= sizeof(std::uint64_t)) {
                    if (negative)
                        return negative_integer_decode(host, magnitude_value(magnitude));
                    return unsigned_integer_decode(host, magnitude_value(magnitude));
                }
                if (negative)
                    return negative_bignum_decode(host, std::string_view(magnitude_plus_one(magnitude)));
                return unsigned_bignum_decode(host, magnitude);
            }
            if (h->argument == std::to_underlying(tag_number::sharedref)) {
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->major != major_type::unsigned_integer) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                if (r->argument > std::numeric_limits<std::size_t>::max()) [[unlikely]]
                    return std::unexpected(error::sharedref_index_out_of_range);
                std::size_t const index = static_cast<std::size_t>(r->argument);
                if (index >= shared.size()) [[unlikely]]
                    return std::unexpected(error::sharedref_index_not_marked);
                if (!shared.at(index) && before && index < before->offsets.size() &&
                    !before->decoding.at(index)) {
                    decoder earlier{before->document.substr(before->offsets.at(index))};
                    before->decoding.at(index) = true;
                    auto content = value_decode<DepthMax>(earlier, host, shared, before, depth + 1, index);
                    before->decoding.at(index) = false;
                    if (!content) [[unlikely]]
                        return content;
                    shared.at(index) = *content;
                }
                if (!shared.at(index)) [[unlikely]]
                    return std::unexpected(error::sharedref_not_complete);
                return *shared.at(index);
            }
            if constexpr (requires { tag_begin(host, h->argument); }) {
                std::optional<typename Host::value> object = tag_begin(host, h->argument);
                if (object) {
                    if (mark)
                        shared.at(*mark) = *object;
                    auto content = value_decode<DepthMax>(d, host, shared, before, depth + 1, std::nullopt);
                    if (!content) [[unlikely]]
                        return content;
                    return after_decode(host,
                                        registered_decode(host, std::move(*object), std::move(*content)));
                }
            }
            auto content = value_decode<DepthMax>(d, host, shared, before, depth + 1, std::nullopt);
            if (!content) [[unlikely]]
                return content;
            return tag_decode(host, h->argument, std::move(*content));
        }
        default:
            switch (static_cast<simple_float_information>(h->info)) {
            case simple_float_information::simple_value_follows:
                if (h->argument < simple_value_one_byte_min) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                return simple_value_decode(host, static_cast<std::uint8_t>(h->argument));
            case simple_float_information::half_precision_float:
                return float_decode(host, static_cast<double>(float_decode_binary16(
                                              static_cast<std::uint16_t>(h->argument))));
            case simple_float_information::single_precision_float:
                return float_decode(
                    host, static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(h->argument))));
            case simple_float_information::double_precision_float:
                return float_decode(host, std::bit_cast<double>(h->argument));
            default:
                return simple_value_decode(host, h->info);
            }
        }
    }

    template <std::size_t DepthMax, class Host>
    friend std::expected<typename Host::value, error> decode(Host &host, std::string_view bytes);

    template <class Writer>
    friend struct encoder;

    template <std::size_t DepthMax, sharedrefs Sharing, class Host, class Writer>
    friend std::expected<void, std::error_code> encode(Host &host, Writer &writer,
                                                       typename Host::value const &value);

    template <std::size_t, class, class, pass>
    friend class walker;

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> lazy_at(lazy const &l, std::int64_t index);

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> lazy_at(lazy const &l, std::string_view key);

    template <std::size_t DepthMax, class Host>
    friend std::expected<typename Host::value, error> lazy_decode(Host &host, lazy const &l);

    template <std::size_t DepthMax, class Host>
    friend std::expected<typename Host::value, error> path_decode(Host &host,
                                                                  std::span<path_step const> steps, lazy const &l);

    struct resolved {
        std::shared_ptr<document> source;
        head h;
        decoder d;
    };

    static std::expected<resolved, error> container_resolve(std::shared_ptr<document> source, std::size_t offset)
    {
        std::vector<std::size_t> followed;
        for (;;) {
            decoder d{source->bytes.substr(offset)};
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                return resolved{source, *h, d};
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                source->mark(d);
                offset = source->bytes.size() - d.bytes.size();
                continue;
            }
            if (h->argument == std::to_underlying(tag_number::encoded_cbor_data_item)) {
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const embedded = d.byte_string_decode(r->argument);
                if (!embedded) [[unlikely]]
                    return std::unexpected(embedded.error());
                source = std::make_shared<document>(source->owner, *embedded, std::vector<std::size_t>{}, 0);
                offset = 0;
                followed.clear();
                continue;
            }
            if (h->argument != std::to_underlying(tag_number::sharedref))
                return resolved{source, *h, d};
            if (std::ranges::find(followed, offset) != followed.end()) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            followed.push_back(offset);
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->major != major_type::unsigned_integer) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            std::vector<std::size_t> const &offsets = source->marks;
            if (r->argument >= offsets.size()) [[unlikely]]
                return std::unexpected(error::sharedref_index_not_marked);
            std::size_t const marked = offsets.at(static_cast<std::size_t>(r->argument));
            if (marked >= offset) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            offset = marked;
        }
    }

    template <std::size_t DepthMax>
    friend std::expected<std::size_t, error> doc_end(std::string_view bytes);

    static std::expected<void, error> typed_array_check(std::uint64_t const tag, std::size_t const size)
    {
        if (tag < std::to_underlying(tag_number::typed_array_first) ||
            tag > std::to_underlying(tag_number::typed_array_last) ||
            tag == std::to_underlying(tag_number::typed_array_reserved)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::uint64_t const f = tag >> 4 & 1;
        std::uint64_t const ll = tag & 3;
        if (size % (std::uint64_t{1} << (f + ll)) != 0) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        return {};
    }

    template <std::size_t DepthMax, class Marks>
    static std::expected<void, error> item_skip(decoder &d, Marks &marks, std::size_t const depth)
    {
        std::array<std::uint64_t, DepthMax + 2> left;
        std::size_t level = 0;
        left.at(0) = 1;
        for (;;) {
            while (left.at(level) == 0) {
                if (level == 0)
                    return {};
                --level;
            }
            --left.at(level);
            if (depth + level > DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            if (d.bytes.size() >= 9) {
                auto const initial = static_cast<std::uint8_t>(d.bytes.front());
                auto const major = static_cast<major_type>(initial >> 5);
                std::uint8_t const info = initial & 0x1f;
                if (info <= std::to_underlying(additional_information::eight_byte_argument) &&
                    major != major_type::tag && major != major_type::simple_float) {
                    std::uint64_t word;
                    std::memcpy(&word, d.bytes.substr(1, 8).data(), 8);
                    bool const immediate = info < std::to_underlying(additional_information::one_byte_argument);
                    std::size_t const size =
                        immediate ? 0
                                  : std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
                    std::uint64_t const argument =
                        immediate ? info : std::byteswap(word) >> ((64 - 8 * size) & 63);
                    d.bytes.remove_prefix(1 + size);
                    if (major == major_type::byte_string || major == major_type::text_string) {
                        if (argument > d.bytes.size()) [[unlikely]]
                            return std::unexpected(error::too_little_data);
                        d.bytes.remove_prefix(static_cast<std::size_t>(argument));
                    } else if (major == major_type::array) {
                        left.at(++level) = argument;
                    } else if (major == major_type::map) {
                        left.at(++level) = argument > std::numeric_limits<std::uint64_t>::max() / 2
                                               ? std::numeric_limits<std::uint64_t>::max()
                                               : argument * 2;
                    }
                    continue;
                }
            }
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            switch (h->major) {
            case major_type::byte_string:
            case major_type::text_string:
                if (auto const s = d.byte_string_decode(h->argument); !s) [[unlikely]]
                    return std::unexpected(s.error());
                break;
            case major_type::array:
                left.at(++level) = h->argument;
                break;
            case major_type::map:
                left.at(++level) = h->argument > std::numeric_limits<std::uint64_t>::max() / 2
                                       ? std::numeric_limits<std::uint64_t>::max()
                                       : h->argument * 2;
                break;
            case major_type::tag:
                if (h->argument == std::to_underlying(tag_number::shareable))
                    marks.mark(d);
                left.at(++level) = 1;
                break;
            case major_type::simple_float:
                if (h->info == std::to_underlying(simple_float_information::simple_value_follows) &&
                    h->argument < simple_value_one_byte_min) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                break;
            default:
                break;
            }
        }
    }
};

struct lazy {
    std::shared_ptr<internal::document> document;
    std::size_t offset;
};

template <std::size_t DepthMax>
struct lazy_elements {
    std::shared_ptr<internal::document> document;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<internal::document> document;
        std::size_t offset;
        std::uint64_t left;
        error failure;

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return lazy{document, offset};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            internal::decoder d{document->bytes.substr(offset)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            offset = document->bytes.size() - d.bytes.size();
            --left;
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }
    };

    iterator begin() const
    {
        return iterator{document, offset, count, error{}};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <std::size_t DepthMax>
struct lazy_entries {
    std::shared_ptr<internal::document> document;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<internal::document> document;
        std::size_t key;
        std::size_t value;
        std::uint64_t left;
        error failure;

        void value_find()
        {
            if (left == 0)
                return;
            internal::decoder d{document->bytes.substr(key)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            value = document->bytes.size() - d.bytes.size();
        }

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return std::pair{lazy{document, key}, lazy{document, value}};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            internal::decoder d{document->bytes.substr(value)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            key = document->bytes.size() - d.bytes.size();
            --left;
            value_find();
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }
    };

    iterator begin() const
    {
        iterator first{document, offset, offset, count, error{}};
        first.value_find();
        return first;
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

#if __cpp_impl_reflection
template <class T>
consteval std::size_t fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = internal::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (std::is_enum_v<U>)
        return fixed_size<std::underlying_type_t<U>>();
    else if constexpr (std::same_as<U, __int128> || std::same_as<U, unsigned __int128>)
        return internal::head_size(std::to_underlying(internal::tag_number::negative_bignum)) +
               internal::head_size(sizeof(U)) + sizeof(U);
    else if constexpr (std::is_integral_v<U> && std::has_single_bit(sizeof(U)) && sizeof(U) <= sizeof(std::uint64_t))
        return initial_byte_size + sizeof(U);
    else if constexpr (std::is_floating_point_v<U>) {
        constexpr int digits = std::numeric_limits<U>::digits;
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return initial_byte_size + sizeof(std::float16_t);
        else if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits ||
                           digits == std::numeric_limits<std::float32_t>::digits)
            return initial_byte_size + sizeof(std::float32_t);
        else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
            return initial_byte_size + sizeof(std::float64_t);
        else if constexpr (digits == internal::extended_precision_digits ||
                           digits == std::numeric_limits<std::float128_t>::digits)
            return internal::head_size(std::to_underlying(internal::tag_number::float128_big_endian)) +
                   internal::head_size(sizeof(std::float128_t)) + sizeof(std::float128_t);
        else
            return no_fixed_size<T>();
    } else if constexpr (requires { internal::fixed_length<U>::value; }) {
        using E = typename internal::fixed_length<U>::element;
        constexpr std::size_t n = internal::fixed_length<U>::value;
        if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                      std::same_as<E, std::byte>)
            return internal::head_size(n) + n;
        else
            return internal::head_size(n) + n * fixed_size<E>();
    } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>)
        return internal::struct_fixed_size<U>();
    else if constexpr (requires(U const &v) {
                           v.has_value();
                           *v;
                           typename U::value_type;
                       } && !requires { typename U::error_type; })
        return internal::dynamic_type_sizes;
    else if constexpr (std::ranges::sized_range<U>)
        return internal::dynamic_type_sizes;
    else
        return no_fixed_size<T>();
}

template <class T, std::meta::info Member>
consteval std::size_t member_offset()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::meta::parent_of(Member) != std::meta::dealias(^^U)) {
        return no_fixed_size<T>();
    } else {
        std::size_t offset = internal::head_size(internal::data_members<U>().size());
        template for (constexpr auto m : internal::data_members<U>()) {
            constexpr std::size_t key = std::meta::u8identifier_of(m).size();
            offset += internal::head_size(key) + key;
            if constexpr (m == Member)
                return offset;
            offset += fixed_size<typename[:std::meta::type_of(m):]>();
        }
        return no_fixed_size<T>();
    }
}


template <class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
std::expected<document<T>, error> decode(std::string_view const bytes)
{
    if (bytes.size() < fixed_size<T>()) [[unlikely]]
        return std::unexpected(error::too_little_data);
    return document<T>{bytes, 0, fixed_size<T>()};
}

template <class T, fixed_string Path>
auto at_path_compiled(document<T> const doc)
{
    auto const result = internal::path_walk<T, Path, 0>(doc.bytes, doc.offset, doc.floor);
    if constexpr (internal::path_reads_wire<T, Path, 0>())
        return result;
    else
        return *result;
}

template <class Writer, class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
std::expected<void, std::errc> encode(Writer &writer, T const &value)
{
    auto const second = internal::second_item_of(value);
    if (!second) [[unlikely]]
        return std::unexpected(second.error());
    constexpr std::size_t first = fixed_size<T>();
    std::size_t second_size;
    std::size_t size;
    if (ckd_add(&second_size, internal::head_size(second->items), second->bytes) ||
        !std::in_range<std::uint32_t>(second_size) || ckd_add(&size, first, second_size)) [[unlikely]]
        return std::unexpected(std::errc::value_too_large);
    return writer.resize_and_overwrite(size, [&](std::span<char> const out) {
        internal::cursor c{out, first};
        c.template zero_initialized_copy<T>(0);
        c.position += c.head_write(first, major_type::array, second->items);
        c.template value_encode<T>(0, value);
        return size;
    });
}
#endif

template <class Writer>
struct encoder {
    Writer &writer;
    std::array<char, 16384> block;
    std::size_t used = 0;

    explicit encoder(Writer &w) : writer(w)
    {
    }

    std::expected<void, std::errc> flush()
    {
        std::size_t const size = used;
        used = 0;
        return writer.append(std::string_view(block.data(), size));
    }

    std::expected<void, std::errc> room(std::size_t const size)
    {
        if (block.size() - used < size) [[unlikely]]
            return flush();
        return {};
    }

    void item_write(std::array<char, 9> const &item, std::size_t const size)
    {
        std::memcpy(std::span(block).subspan(used).data(), item.data(), item.size());
        used += size;
    }

    std::expected<void, std::errc> head_encode(major_type major, std::uint64_t argument)
    {
        if (auto const r = room(9); !r) [[unlikely]]
            return r;
        bool const immediate =
            argument < std::to_underlying(internal::additional_information::one_byte_argument);
        std::size_t const bytes =
            immediate ? 0 : std::bit_ceil(std::max<std::size_t>((std::bit_width(argument) + 7) / 8, 1));
        std::uint64_t const info =
            immediate ? argument
                      : std::to_underlying(internal::additional_information::one_byte_argument) +
                            static_cast<std::uint64_t>(std::countr_zero(bytes));
        std::uint64_t const big = std::byteswap(argument << ((64 - 8 * bytes) & 63));
        std::array<char, 9> head;
        std::get<0>(head) = static_cast<char>(std::to_underlying(major) << 5 | info);
        std::memcpy(std::span(head).template subspan<1>().data(), &big, sizeof big);
        std::size_t const size = 1 + bytes;
        item_write(head, size);
        return {};
    }

    std::expected<void, std::errc> byte_string_encode(std::string_view bytes)
    {
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        if (bytes.size() <= block.size() - used) {
            std::memcpy(std::span(block).subspan(used).data(), bytes.data(), bytes.size());
            used += bytes.size();
            return {};
        }
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        if (auto const r = writer.reserve(bytes.size()); !r) [[unlikely]]
            return r;
        return writer.append(bytes);
    }

    std::expected<void, std::errc> text_string_encode(std::string_view text)
    {
        if (auto const r = head_encode(major_type::text_string, text.size()); !r) [[unlikely]]
            return r;
        if (text.size() <= block.size() - used) {
            std::memcpy(std::span(block).subspan(used).data(), text.data(), text.size());
            used += text.size();
            return {};
        }
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        if (auto const r = writer.reserve(text.size()); !r) [[unlikely]]
            return r;
        return writer.append(text);
    }
    std::expected<void, std::errc> float_encode(double value)
    {
        if (auto const r = room(9); !r) [[unlikely]]
            return r;
        std::array<char, 9> item;
        std::size_t size;
        switch (internal::preferred_float_info(value)) {
        case internal::simple_float_information::half_precision_float: {
            std::get<0>(item) = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::half_precision_float));
            auto const v = std::byteswap(internal::float_encode_binary16(static_cast<float>(value)));
            std::memcpy(std::span(item).template subspan<1>().data(), &v, 2);
            size = 3;
        } break;
        case internal::simple_float_information::single_precision_float: {
            std::get<0>(item) = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::single_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint32_t>(static_cast<float>(value)));
            std::memcpy(std::span(item).template subspan<1>().data(), &v, 4);
            size = 5;
        } break;
        default: {
            std::get<0>(item) = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::double_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint64_t>(value));
            std::memcpy(std::span(item).template subspan<1>().data(), &v, 8);
            size = 9;
        } break;
        }
        item_write(item, size);
        return {};
    }

    template <std::unsigned_integral T>
    std::expected<void, std::errc> fixed_width_head_encode(major_type major, T argument)
    {
        if (auto const r = room(9); !r) [[unlikely]]
            return r;
        std::array<char, 9> head;
        std::get<0>(head) = static_cast<char>(
            std::to_underlying(major) << 5 |
            (std::to_underlying(internal::additional_information::one_byte_argument) + std::countr_zero(sizeof(T))));
        T const big = std::byteswap(argument);
        std::memcpy(std::span(head).template subspan<1>().data(), &big, sizeof big);
        item_write(head, 1 + sizeof(T));
        return {};
    }

    std::expected<void, std::errc> simple_value_encode(simple_value value)
    {
        if (auto const r = room(9); !r) [[unlikely]]
            return r;
        std::array<char, 9> item;
        std::get<0>(item) = static_cast<char>(std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(value));
        item_write(item, 1);
        return {};
    }

    template <std::unsigned_integral T>
        requires(!std::is_same_v<T, bool>)
    std::expected<void, std::errc> fixed_width_unsigned_encode(T value)
    {
        return fixed_width_head_encode(major_type::unsigned_integer, value);
    }

    template <std::signed_integral T>
    std::expected<void, std::errc> fixed_width_signed_encode(T value)
    {
        using U = std::make_unsigned_t<T>;
        U const sign = static_cast<U>(value >> (8 * sizeof(T) - 1));
        return fixed_width_head_encode(static_cast<major_type>(sign & 1), static_cast<U>(static_cast<U>(value) ^ sign));
    }

    template <std::floating_point T>
        requires(sizeof(T) == 4 || sizeof(T) == 8)
    std::expected<void, std::errc> fixed_width_float_encode(T value)
    {
        return fixed_width_head_encode(major_type::simple_float,
                                       std::bit_cast<std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>>(value));
    }
};

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> decode(Host &host, std::string_view bytes)
{
    internal::decoder d{bytes};
    internal::marks<Host> shared;
    return internal::value_decode<DepthMax>(d, host, shared, nullptr, 0, std::nullopt);
}

enum class pass { plain, count, write };

template <std::size_t DepthMax, sharedrefs Sharing, class Host, class Writer>
std::expected<void, std::error_code> encode_from(Host &host, Writer &writer, typename Host::value const &value,
                                                 std::size_t depth, bool embedded);

struct discarding_writer {
    std::expected<void, std::errc> reserve(std::size_t)
    {
        return {};
    }

    std::expected<void, std::errc> append(std::string_view)
    {
        return {};
    }
};

template <class Host>
struct sharing {
    std::unordered_map<typename Host::identity, std::uint64_t> seen;
    std::unordered_map<typename Host::identity, std::uint64_t> numbers;
    std::unordered_map<typename Host::identity, typename Host::value> replaced;
};

template <std::size_t DepthMax, class Host, class Writer, pass Pass>
class walker
{
    Host &host;
    encoder<Writer> out;
    sharing<Host> *shared;
    std::size_t depth;
    bool embedded;
    std::error_code failure;

    template <std::size_t, sharedrefs, class H, class W>
    friend std::expected<void, std::error_code> encode_from(H &host, W &writer, typename H::value const &value,
                                                            std::size_t depth, bool embedded);

    walker(Host &h, Writer &w, sharing<Host> *s, std::size_t const d, bool const e)
        : host(h), out{w}, shared(s), depth(d), embedded(e)
    {
    }

    void keep(std::expected<void, std::errc> const r)
    {
        if (!r && !failure) [[unlikely]]
            failure = std::make_error_code(r.error());
    }

    void keep_error(error const e)
    {
        if (!failure)
            failure = make_error_code(e);
    }

    void head(major_type const major, std::uint64_t const argument)
    {
        keep(out.head_encode(major, argument));
    }

    void key(typename Host::value const &item)
    {
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, key_identity(host, item));
    }

    void value(typename Host::value const &item)
    {
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, value_identity(host, item));
    }

    template <class Identity>
    void child(typename Host::value const &item, Identity const &identity)
    {
        if (failure) [[unlikely]]
            return;
        if (depth > DepthMax) [[unlikely]] {
            keep_error(error::nesting_depth_exceeded);
            return;
        }
        if constexpr (requires { embed_of(host, item); }) {
            bool const outer = embedded;
            embedded = false;
            if (!outer && embed_of(host, item)) {
                if constexpr (Pass != pass::count) {
                    internal::string_sink inner;
                    auto const r = encode_from<DepthMax, Pass == pass::plain ? sharedrefs::off : sharedrefs::on>(
                        host, inner, item, depth, true);
                    if (!r) [[unlikely]] {
                        if (!failure)
                            failure = r.error();
                        return;
                    }
                    head(major_type::tag, std::to_underlying(internal::tag_number::encoded_cbor_data_item));
                    keep(out.byte_string_encode(inner.bytes));
                }
                return;
            }
        }
        if constexpr (Pass == pass::count) {
            if (identity && ++shared->seen.try_emplace(*identity, 0).first->second > 1)
                return;
        }
        if constexpr (Pass == pass::write) {
            if (identity && shared->seen.at(*identity) > 1) {
                auto const number = shared->numbers.find(*identity);
                if (number != shared->numbers.end()) {
                    head(major_type::tag, std::to_underlying(internal::tag_number::sharedref));
                    head(major_type::unsigned_integer, number->second);
                    return;
                }
                shared->numbers.emplace(*identity, shared->numbers.size());
                head(major_type::tag, std::to_underlying(internal::tag_number::shareable));
            }
        }
        ++depth;
        describe(item, identity);
        --depth;
    }

    template <class Identity>
    typename Host::value content_of(typename Host::value const &item, Identity const &identity)
    {
        if constexpr (Pass == pass::plain) {
            return before_encode(host, item);
        } else {
            if (!identity)
                return before_encode(host, item);
            if constexpr (Pass == pass::count)
                return shared->replaced.emplace(*identity, before_encode(host, item)).first->second;
            else
                return shared->replaced.at(*identity);
        }
    }

    template <class Identity>
    void describe(typename Host::value const &item, Identity const &identity)
    {
        switch (kind_of(host, item)) {
        case kind::unsigned_integer:
            if constexpr (requires { unsigned_of(host, item); }) {
                head(major_type::unsigned_integer, unsigned_of(host, item));
                return;
            }
            break;
        case kind::negative_integer:
            if constexpr (requires { unsigned_of(host, item); }) {
                head(major_type::negative_integer, unsigned_of(host, item) - 1);
                return;
            }
            break;
        case kind::unsigned_bignum:
            if constexpr (requires { magnitude_of(host, item); }) {
                bignum(false, magnitude_of(host, item));
                return;
            }
            break;
        case kind::negative_bignum:
            if constexpr (requires { magnitude_of(host, item); }) {
                bignum(true, magnitude_of(host, item));
                return;
            }
            break;
        case kind::byte_string:
            if constexpr (requires { bytes_of(host, item); }) {
                keep(out.byte_string_encode(bytes_of(host, item)));
                return;
            }
            break;
        case kind::text_string:
            if constexpr (requires { text_of(host, item); }) {
                keep(out.text_string_encode(text_of(host, item)));
                return;
            }
            break;
        case kind::floating_point:
            if constexpr (requires { float_of(host, item); }) {
                keep(out.float_encode(float_of(host, item)));
                return;
            }
            break;
        case kind::simple_value:
            if constexpr (requires { simple_of(host, item); }) {
                simple(simple_of(host, item));
                return;
            }
            break;
        case kind::array:
            if constexpr (requires { array_size(host, item); }) {
                std::uint64_t const size = array_size(host, item);
                head(major_type::array, size);
                for (std::uint64_t i = 0; i < size; ++i)
                    value(array_at(host, item, i));
                return;
            }
            break;
        case kind::map:
            if constexpr (requires { map_size(host, item); }) {
                head(major_type::map, map_size(host, item));
                map_for_each(host, item,
                             [this](typename Host::value const &k, typename Host::value const &v) {
                                 key(k);
                                 value(v);
                             });
                return;
            }
            break;
        case kind::typed_array:
            if constexpr (requires { typed_array_of(host, item); }) {
                cbor::typed_array const a = typed_array_of(host, item);
                if (auto const r = internal::typed_array_check(a.tag, a.bytes.size()); !r) [[unlikely]] {
                    keep_error(r.error() == error::incorrect_type ? error::unsupported_value : r.error());
                    return;
                }
                head(major_type::tag, a.tag);
                keep(out.byte_string_encode(
                    std::string_view(reinterpret_cast<char const *>(a.bytes.data()), a.bytes.size())));
                return;
            }
            break;
        case kind::registered:
            if constexpr (requires { registered_tag(host, item); }) {
                head(major_type::tag, registered_tag(host, item));
                value(content_of(item, identity));
                return;
            }
            break;
        case kind::unsupported:
            break;
        }
        keep_error(error::unsupported_value);
    }

    void bignum(bool const negative, std::string_view const absolute)
    {
        std::string_view const m = internal::magnitude_without_leading_zeros(absolute);
        if (!negative) {
            if (m.size() <= sizeof(std::uint64_t)) {
                head(major_type::unsigned_integer, internal::magnitude_value(m));
                return;
            }
            head(major_type::tag, std::to_underlying(internal::tag_number::unsigned_bignum));
            keep(out.byte_string_encode(m));
            return;
        }
        std::string const n = internal::magnitude_minus_one(m);
        if (n.size() <= sizeof(std::uint64_t)) {
            head(major_type::negative_integer, internal::magnitude_value(n));
            return;
        }
        head(major_type::tag, std::to_underlying(internal::tag_number::negative_bignum));
        keep(out.byte_string_encode(n));
    }

    void simple(std::uint8_t const v)
    {
        if (v >= std::to_underlying(internal::simple_float_information::simple_value_follows) &&
            v < internal::simple_value_one_byte_min) [[unlikely]] {
            keep_error(error::reserved_simple_value);
            return;
        }
        head(major_type::simple_float, v);
    }

public:
    walker(walker const &) = delete;
    walker &operator=(walker const &) = delete;
};

template <std::size_t DepthMax, sharedrefs Sharing, class Host, class Writer>
std::expected<void, std::error_code> encode_from(Host &host, Writer &writer, typename Host::value const &value,
                                                std::size_t const depth, bool const embedded)
{
    if constexpr (Sharing == sharedrefs::off) {
        walker<DepthMax, Host, Writer, pass::plain> walk{host, writer, nullptr, depth, embedded};
        walk.value(value);
        walk.keep(walk.out.flush());
        if (walk.failure) [[unlikely]]
            return std::unexpected(walk.failure);
    } else {
        sharing<Host> shared;
        discarding_writer nothing;
        walker<DepthMax, Host, discarding_writer, pass::count> count{host, nothing, &shared, depth, embedded};
        count.value(value);
        if (count.failure) [[unlikely]]
            return std::unexpected(count.failure);
        walker<DepthMax, Host, Writer, pass::write> write{host, writer, &shared, depth, embedded};
        write.value(value);
        write.keep(write.out.flush());
        if (write.failure) [[unlikely]]
            return std::unexpected(write.failure);
    }
    return {};
}

template <std::size_t DepthMax, sharedrefs Sharing, class Host, class Writer>
std::expected<void, std::error_code> encode(Host &host, Writer &writer, typename Host::value const &value)
{
    return encode_from<DepthMax, Sharing>(host, writer, value, 0, false);
}

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view const bytes)
{
    internal::decoder d{bytes};
    internal::no_marks none;
    if (auto const r = internal::item_skip<DepthMax>(d, none, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    return bytes.size() - d.bytes.size();
}

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &bytes)
{
    return lazy{std::make_shared<internal::document>(bytes, *bytes, std::vector<std::size_t>{}, 0), 0};
}

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string bytes)
{
    return decode<DepthMax>(std::make_shared<std::string const>(std::move(bytes)));
}

template <std::size_t DepthMax>
std::expected<lazy, error> lazy_at(lazy const &l, std::int64_t const index)
{
    auto const found = internal::container_resolve(l.document, l.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major == major_type::array) {
        std::int64_t const size =
            h.argument > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(h.argument);
        std::int64_t const position = index < 0 ? index + size : index;
        if (position < 0 || position >= size) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        for (std::int64_t i = 0; i < position; ++i)
            if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
                return std::unexpected(r.error());
        return lazy{source, source->bytes.size() - d.bytes.size()};
    }
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        internal::decoder probe = d;
        auto const k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        bool const match = (k->major == major_type::unsigned_integer && index >= 0 &&
                            k->argument == static_cast<std::uint64_t>(index)) ||
                           (k->major == major_type::negative_integer && index < 0 &&
                            k->argument == static_cast<std::uint64_t>(-1 - index));
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (match)
            return lazy{source, source->bytes.size() - d.bytes.size()};
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <std::size_t DepthMax>
std::expected<lazy, error> lazy_at(lazy const &l, std::string_view const key)
{
    auto const found = internal::container_resolve(l.document, l.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        internal::decoder probe = d;
        auto const k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        bool match = false;
        if (k->major == major_type::text_string || k->major == major_type::byte_string) {
            auto const text = probe.byte_string_decode(k->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            match = *text == key;
        }
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (match)
            return lazy{source, source->bytes.size() - d.bytes.size()};
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
std::expected<T, error> lazy_get(lazy const &l)
{
    auto const found = internal::container_resolve(l.document, l.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
        bool negative = h.major == major_type::negative_integer;
        std::uint64_t argument = h.argument;
        if (h.major == major_type::tag &&
            (h.argument == std::to_underlying(internal::tag_number::unsigned_bignum) ||
             h.argument == std::to_underlying(internal::tag_number::negative_bignum))) {
            negative = h.argument == std::to_underlying(internal::tag_number::negative_bignum);
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            auto const bytes = d.byte_string_decode(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::string_view const magnitude = internal::magnitude_without_leading_zeros(*bytes);
            if (magnitude.size() > sizeof(std::uint64_t)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            argument = internal::magnitude_value(magnitude);
        } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if (!std::in_range<T>(argument) || (std::is_unsigned_v<T> && negative)) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        T const magnitude = static_cast<T>(argument);
        return static_cast<T>(negative ? -1 - magnitude : magnitude);
    } else if constexpr (std::is_same_v<T, double>) {
        if (h.major != major_type::simple_float) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        switch (static_cast<internal::simple_float_information>(h.info)) {
        case internal::simple_float_information::half_precision_float:
            return static_cast<double>(internal::float_decode_binary16(static_cast<std::uint16_t>(h.argument)));
        case internal::simple_float_information::single_precision_float:
            return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(h.argument)));
        case internal::simple_float_information::double_precision_float:
            return std::bit_cast<double>(h.argument);
        default:
            return std::unexpected(error::incorrect_type);
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (h.major != major_type::simple_float || (h.info != std::to_underlying(simple_value::false_value) &&
                                                    h.info != std::to_underlying(simple_value::true_value))) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return h.info == std::to_underlying(simple_value::true_value);
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        if (h.major != major_type::simple_float || h.info != std::to_underlying(simple_value::null)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return nullptr;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        if (h.major != major_type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return d.text_string_decode(h.argument);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        if (h.major != major_type::tag) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (auto const r = internal::typed_array_check(h.argument, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (r->major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        auto const bytes = d.byte_string_decode(r->argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        if (auto const c = internal::typed_array_check(h.argument, bytes->size()); !c) [[unlikely]]
            return std::unexpected(c.error());
        return typed_array{h.argument, std::as_bytes(std::span(*bytes))};
    } else {
        if (h.major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const bytes = d.byte_string_decode(h.argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        return std::as_bytes(std::span(*bytes));
    }
}

template <std::size_t DepthMax>
std::expected<lazy_elements<DepthMax>, error> lazy_elements_of(lazy const &array)
{
    auto const found = internal::container_resolve(array.document, array.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_elements<DepthMax>{source, source->bytes.size() - d.bytes.size(), h.argument};
}

template <std::size_t DepthMax>
std::expected<lazy_entries<DepthMax>, error> lazy_entries_of(lazy const &map)
{
    auto const found = internal::container_resolve(map.document, map.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_entries<DepthMax>{source, source->bytes.size() - d.bytes.size(), h.argument};
}

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> lazy_decode(Host &host, lazy const &l)
{
    internal::prefix before{l.document->bytes, l.document->marks, std::vector<bool>(l.document->marks.size())};
    internal::marks<Host> shared(before.offsets.size());
    internal::decoder d{l.document->bytes.substr(l.offset)};
    return internal::value_decode<DepthMax>(d, host, shared, &before, 0, std::nullopt);
}

constexpr std::expected<std::vector<path_step>, error> path_compile(std::string_view const source)
{
    auto const letter = [](char const c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto const digit = [](char const c) { return c >= '0' && c <= '9'; };
    std::vector<path_step> steps;
    std::string_view rest = source;
    if (rest.starts_with('$'))
        rest.remove_prefix(1);
    while (!rest.empty()) {
        if (rest.front() == '.') {
            rest.remove_prefix(1);
            if (rest.empty() || !letter(rest.front())) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::size_t n = 1;
            while (n < rest.size() && (letter(rest.at(n)) || digit(rest.at(n))))
                ++n;
            steps.push_back({path_step::kind::key, rest.substr(0, n), 0});
            rest.remove_prefix(n);
        } else if (rest.front() == '[') {
            rest.remove_prefix(1);
            if (rest.starts_with("*]")) {
                steps.push_back({path_step::kind::wildcard, {}, 0});
                rest.remove_prefix(2);
            } else if (rest.starts_with('"')) {
                std::size_t const close = rest.find('"', 1);
                if (close == std::string_view::npos || rest.substr(close + 1).empty() ||
                    rest.at(close + 1) != ']') [[unlikely]]
                    return std::unexpected(error::invalid_path);
                steps.push_back({path_step::kind::key, rest.substr(1, close - 1), 0});
                rest.remove_prefix(close + 2);
            } else {
                std::int64_t index = 0;
                auto const [end, failure] = std::from_chars(rest.data(), std::to_address(rest.end()), index);
                std::size_t const used = static_cast<std::size_t>(std::distance(rest.data(), end));
                if (failure != std::errc{} || used >= rest.size() || rest.at(used) != ']') [[unlikely]]
                    return std::unexpected(error::invalid_path);
                steps.push_back({path_step::kind::index, {}, index});
                rest.remove_prefix(used + 1);
            }
        } else if (rest.front() == ' ' || rest.front() == '\t' || rest.front() == '\n' ||
                   rest.front() == '\r') {
            rest.remove_prefix(1);
        } else [[unlikely]] {
            return std::unexpected(error::invalid_path);
        }
    }
    return steps;
}

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> path_decode(Host &host, std::span<path_step const> const steps,
                                                       lazy const &l)
{
    lazy at = l;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        path_step const &step = steps.subspan(i).front();
        if (step.kind == path_step::kind::key) {
            auto const next = lazy_at<DepthMax>(at, step.key);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            at = *next;
        } else if (step.kind == path_step::kind::index) {
            auto const next = lazy_at<DepthMax>(at, step.index);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            at = *next;
        } else {
            auto const elements = lazy_elements_of<DepthMax>(at);
            if (!elements) [[unlikely]]
                return std::unexpected(elements.error());
            auto array = array_decode(
                host, std::min<std::uint64_t>(elements->count, elements->document->bytes.size() - elements->offset));
            for (auto const element : *elements) {
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                auto value = path_decode<DepthMax>(host, steps.subspan(i + 1), *element);
                if (!value) [[unlikely]]
                    return value;
                array = array_append(host, std::move(array), std::move(*value));
            }
            return array;
        }
    }
    return lazy_decode<DepthMax>(host, at);
}

} // namespace cbor

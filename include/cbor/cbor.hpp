#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#ifdef __cpp_impl_reflection
#include <meta>
#include <stdckdint.h>
#include <stdfloat>
#endif
#include <string>
#include <bitset>
#include <ranges>
#include <tuple>
#include <variant>
#include <stdexcept>
#include <string_view>
#ifdef CBOR_SIMDUTF
#include <simdutf.h>
#endif
#include <system_error>
#include <type_traits>
#include <unordered_map>

#ifdef _MSC_VER
#define CBOR_ALWAYS_INLINE [[msvc::forceinline]]
#define CBOR_ASSUME(condition) __assume(condition)
#else
#define CBOR_ALWAYS_INLINE [[gnu::always_inline]]
#define CBOR_ASSUME(condition) [[assume(condition)]]
#endif
#include <utility>
#include <vector>

namespace cbor
{

#ifdef __SIZEOF_INT128__
__extension__ typedef __int128 int128;
__extension__ typedef unsigned __int128 uint128;
#endif

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
    number_out_of_range,
    cyclic_data_structure,
    unpopulated_table_index,
    nodelist_too_long,
    duplicate_key
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
        case error::cyclic_data_structure:
            return "cyclic data structure";
        case error::unpopulated_table_index:
            return "unpopulated table index";
        case error::nodelist_too_long:
            return "nodelist too long";
        case error::duplicate_key:
            return "duplicate key";
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
        case error::cyclic_data_structure:
        case error::nodelist_too_long:
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
        case error::unpopulated_table_index:
        case error::duplicate_key:
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

template <class Value>
struct binding {
    using value = Value;
    value unsigned_integer_decode(std::uint64_t argument) = delete;
    value negative_integer_decode(std::uint64_t argument) = delete;
    value unsigned_bignum_decode(std::string_view magnitude) = delete;
    value negative_bignum_decode(std::string_view magnitude) = delete;
    value byte_string_decode(std::string_view bytes) = delete;
    value text_string_decode(std::string_view text) = delete;
    value float_decode(double number) = delete;
    value simple_value_decode(std::uint8_t simple) = delete;
    value array_decode(std::uint64_t size) = delete;
    value array_append(value array, value element) = delete;
    value map_decode(std::uint64_t size) = delete;
    value map_key_decode(std::string_view key) = delete;
    value map_insert(value map, value key, value item) = delete;
    value tag_decode(std::uint64_t tag, value content) = delete;
    std::optional<value> tag_begin(std::uint64_t tag) = delete;
    value registered_decode(value object, value content) = delete;
    value after_decode(value object) = delete;
    bool cyclic_data_structures() = delete;
    kind kind_of(value const &item) = delete;
    value before_encode(value const &item) = delete;
    std::uint64_t unsigned_of(value const &item) = delete;
    std::string_view magnitude_of(value const &item) = delete;
    std::string_view bytes_of(value const &item) = delete;
    std::string_view text_of(value const &item) = delete;
    double float_of(value const &item) = delete;
    std::uint8_t simple_of(value const &item) = delete;
    std::uint64_t array_size(value const &item) = delete;
    value const &array_at(value const &item, std::uint64_t index) = delete;
    std::uint64_t map_size(value const &item) = delete;
    template <class F>
    void map_for_each(value const &item, F const &each) = delete;
    typed_array typed_array_of(value const &item) = delete;
    std::uint64_t registered_tag(value const &item) = delete;
    bool embed_of(value const &item) = delete;
    void value_identity(value const &item) = delete;
    void key_identity(value const &item) = delete;
};

template <class B>
concept language_binding = std::derived_from<B, binding<typename B::value>> &&
                           requires(B &b, typename B::value v, std::uint64_t const n, std::string_view const s, double const f,
                                    std::uint8_t const simple) {
    { b.unsigned_integer_decode(n) } -> std::same_as<typename B::value>;
    { b.negative_integer_decode(n) } -> std::same_as<typename B::value>;
    { b.unsigned_bignum_decode(s) } -> std::same_as<typename B::value>;
    { b.negative_bignum_decode(s) } -> std::same_as<typename B::value>;
    { b.byte_string_decode(s) } -> std::same_as<typename B::value>;
    { b.text_string_decode(s) } -> std::same_as<typename B::value>;
    { b.float_decode(f) } -> std::same_as<typename B::value>;
    { b.simple_value_decode(simple) } -> std::same_as<typename B::value>;
    { b.array_decode(n) } -> std::same_as<typename B::value>;
    { b.array_append(std::move(v), std::move(v)) } -> std::same_as<typename B::value>;
    { b.map_decode(n) } -> std::same_as<typename B::value>;
    { b.map_insert(std::move(v), std::move(v), std::move(v)) } -> std::same_as<typename B::value>;
    { b.tag_decode(n, std::move(v)) } -> std::same_as<typename B::value>;
};


template <std::size_t DepthMax, language_binding Binding>
std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view encoded);

template <class Writer>
struct encoder;

enum class sharedrefs { off, on };

enum class pass;

struct lazy;

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded);

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string_view encoded);

template <std::size_t DepthMax, std::same_as<std::string> Encoded>
std::expected<lazy, error> decode(Encoded &&encoded);

template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l);

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view encoded);

template <class T, class E>
struct result;

template <std::size_t DepthMax = 64>
result<std::string, error> inspect(std::string_view encoded);

template <std::size_t DepthMax, sharedrefs Sharing = sharedrefs::off, class Binding, class Writer>
std::expected<void, std::error_code> encode(Binding &binding, Writer &&target, typename Binding::value const &value);

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

template <class T>
class schema;

template <class T>
class databind;

struct lazy;

template <class T>
class owning_ref
{
    std::shared_ptr<void const> owner;
    T value;

    owning_ref(std::shared_ptr<void const> o, T v) : owner(std::move(o)), value(std::move(v))
    {
    }

    template <class>
    friend class schema;

    template <class>
    friend class databind;

    friend struct lazy;

public:
    T const &operator*() const &
    {
        return value;
    }

    T const *operator->() const &
    {
        return &value;
    }

    T const &operator*() const && = delete;
    T const *operator->() const && = delete;
};

template <class T>
using oref = owning_ref<T>;

template <class T, class E = error>
struct result;

template <class T, class E>
struct result : std::expected<T, E> {
    using std::expected<T, E>::expected;
};

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

class internal;

struct directory {
    std::size_t at;
    std::size_t count;
};

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
        typed_array_last = 87,
        reference = 6,
        basic_packed_cbor = 113,
        record_function = 114,
        straight_argument_first = 128,
        straight_argument_last = 135,
        inverted_argument_first = 136,
        inverted_argument_last = 143,
        split_basic_packed_cbor = 1113
    };

    template <class Binding>
    using marks = std::vector<std::optional<typename Binding::value>>;

    template <class V>
    static V unsigned_read(std::span<char const, sizeof(V)> const field)
    {
        std::array<char, sizeof(V)> big;
        std::ranges::copy(field, big.begin());
        return std::byteswap(std::bit_cast<V>(big));
    }

#ifdef __cpp_impl_reflection
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

    static constexpr std::size_t dynamic_type_sizes = 2 * initial_byte_size + sizeof(std::uint32_t);
    static constexpr std::size_t item_head = initial_byte_size + sizeof(std::uint32_t);
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
        return std::ranges::adjacent_find(keys) == keys.end();
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

    static constexpr void head_encode(std::vector<char> &encoded, major_type major, std::uint64_t argument)
    {
        std::size_t const size = head_size(argument);
        if (size == initial_byte_size) {
            encoded.push_back(static_cast<char>(std::to_underlying(major) << 5 | argument));
            return;
        }
        std::size_t const width = size - initial_byte_size;
        encoded.push_back(static_cast<char>(std::to_underlying(major) << 5 |
                                          (std::to_underlying(additional_information::one_byte_argument) +
                                           std::countr_zero(width))));
        for (std::size_t i = width; i-- > 0;)
            encoded.push_back(static_cast<char>(argument >> (8 * i)));
    }

    static constexpr void fixed_width_head_encode(std::vector<char> &encoded, major_type major, std::size_t width)
    {
        encoded.push_back(static_cast<char>(std::to_underlying(major) << 5 |
                                          (std::to_underlying(additional_information::one_byte_argument) +
                                           std::countr_zero(width))));
        encoded.resize(encoded.size() + width);
    }

    template <class Root, class T>
    static consteval void zero_initialized_encode(std::vector<char> &encoded)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            head_encode(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
        } else if constexpr (std::is_enum_v<U>) {
            zero_initialized_encode<Root, std::underlying_type_t<U>>(encoded);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            head_encode(encoded, major_type::tag, std::to_underlying(tag_number::unsigned_bignum));
            head_encode(encoded, major_type::byte_string, sizeof(U));
            encoded.resize(encoded.size() + sizeof(U));
#endif
        } else if constexpr (std::is_integral_v<U>) {
            fixed_width_head_encode(encoded, major_type::unsigned_integer, sizeof(U));
        } else if constexpr (std::is_floating_point_v<U>) {
            constexpr int digits = std::numeric_limits<U>::digits;
            if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float16_t));
            else if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits ||
                               digits == std::numeric_limits<std::float32_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float32_t));
            else if constexpr (digits == std::numeric_limits<std::float64_t>::digits)
                fixed_width_head_encode(encoded, major_type::simple_float, sizeof(std::float64_t));
            else {
                head_encode(encoded, major_type::tag, std::to_underlying(tag_number::float128_big_endian));
                head_encode(encoded, major_type::byte_string, sizeof(std::float128_t));
                encoded.resize(encoded.size() + sizeof(std::float128_t));
            }
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t>) {
                head_encode(encoded, major_type::text_string, n);
                encoded.resize(encoded.size() + n);
            } else if constexpr (std::same_as<E, unsigned char> || std::same_as<E, std::byte>) {
                head_encode(encoded, major_type::byte_string, n);
                encoded.resize(encoded.size() + n);
            } else {
                head_encode(encoded, major_type::array, n);
                for (std::size_t i = 0; i < n; ++i)
                    zero_initialized_encode<Root, E>(encoded);
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            head_encode(encoded, major_type::tag, *tag_number_of(^^U));
            straight_reference_encode<Root, U>(encoded);
            fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
            auto const count = big_endian(static_cast<std::uint32_t>(data_members<U>().size()));
            std::ranges::copy(count, encoded.end() - sizeof(std::uint32_t));
            template for (constexpr auto m : data_members<U>())
                zero_initialized_encode<Root, typename[:std::meta::type_of(m):]>(encoded);
        } else if constexpr (is_inline_optional<U>) {
            head_encode(encoded, major_type::array, 2);
            head_encode(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
            zero_initialized_encode<Root, typename U::value_type>(encoded);
        } else {
            head_encode(encoded, major_type::tag, std::to_underlying(tag_number::reference));
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
        head_encode(encoded, major_type::tag, std::to_underlying(tag_number::record_function));
        static constexpr auto members = members_of<U>();
        head_encode(encoded, major_type::array, members.size());
        template for (constexpr std::size_t i : std::define_static_array(std::views::iota(0uz, members.size()))) {
            if constexpr (has_integer_keys<U>) {
                constexpr std::int64_t key = U::keys.at(i);
                if (key >= 0)
                    head_encode(encoded, major_type::unsigned_integer, static_cast<std::uint64_t>(key));
                else
                    head_encode(encoded, major_type::negative_integer, static_cast<std::uint64_t>(-1 - key));
            } else {
                constexpr std::string_view key = key_of(members[i]);
                head_encode(encoded, major_type::text_string, key.size());
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
        std::to_underlying(tag_number::straight_argument_last) - std::to_underlying(tag_number::straight_argument_first) + 1;

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
            head_encode(encoded, major_type::tag, std::to_underlying(tag_number::straight_argument_first) + i);
        } else {
            head_encode(encoded, major_type::tag, std::to_underlying(tag_number::reference));
            head_encode(encoded, major_type::array, 2);
            head_encode(encoded, major_type::unsigned_integer, i - straight_argument_count);
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
        head_encode(encoded, major_type::tag, std::to_underlying(tag_number::basic_packed_cbor));
        head_encode(encoded, major_type::array, 2);
        fixed_width_head_encode(encoded, major_type::array, sizeof(std::uint32_t));
        template for (constexpr std::meta::info type : packing_table_of<Root>()) {
            constexpr auto keys = record_keys<typename[:type:]>();
            encoded.insert(encoded.end(), keys.begin(), keys.end());
        }
        return std::define_static_array(encoded);
    }

    template <std::unsigned_integral V>
    static constexpr std::array<char, sizeof(V)> big_endian(V const value)
    {
        return std::bit_cast<std::array<char, sizeof(V)>>(std::byteswap(value));
    }

#ifdef __SIZEOF_INT128__
    static constexpr std::array<char, sizeof(uint128)> big_endian(uint128 const value)
    {
        auto const high = big_endian(static_cast<std::uint64_t>(value >> 64));
        auto const low = big_endian(static_cast<std::uint64_t>(value));
        std::array<char, sizeof(uint128)> bytes;
        std::ranges::copy(high, bytes.begin());
        std::ranges::copy(low, std::ranges::next(bytes.begin(), sizeof(std::uint64_t)));
        return bytes;
    }
#endif

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
            overflow |= ckd_add(&bytes, bytes, n);
        }

        void block_add(std::size_t const count, std::size_t const size)
        {
            std::size_t block;
            overflow |= ckd_mul(&block, count, size);
            overflow |= ckd_add(&bytes, bytes, block);
        }

        template <class E, class R>
        void elements_add(R const &range)
        {
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

    static constexpr std::size_t head_padding = sizeof(std::uint64_t);

    template <bool Exact = false>
    CBOR_ALWAYS_INLINE static std::size_t head_write(std::span<char> const out, std::size_t const at, major_type const major,
                                  std::uint64_t const argument)
    {
        bool const immediate = argument < std::to_underlying(additional_information::one_byte_argument);
        std::size_t const width = immediate ? 0 : head_size(argument) - initial_byte_size;
        std::uint64_t const info =
            immediate ? argument
                      : std::to_underlying(additional_information::one_byte_argument) + std::countr_zero(width);
        auto const bytes = big_endian(argument << ((64 - 8 * width) & 63));
        if constexpr (Exact) {
            if (out.size() - at < initial_byte_size + sizeof(std::uint64_t)) [[unlikely]] {
                auto const field = out.subspan(at, initial_byte_size + width);
                field.front() = static_cast<char>(std::to_underlying(major) << 5 | info);
                std::copy_n(bytes.begin(), width, field.subspan(initial_byte_size).begin());
                return initial_byte_size + width;
            }
        }
        auto const field = out.subspan(at).template first<initial_byte_size + sizeof(std::uint64_t)>();
        field.front() = static_cast<char>(std::to_underlying(major) << 5 | info);
        std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(std::uint64_t)>().begin());
        return initial_byte_size + width;
    }

    template <class Root, class E>
    CBOR_ALWAYS_INLINE static void zero_initialized_copy(std::span<char, fixed_size<E, Root>()> const field)
    {
        constexpr std::size_t n = fixed_size<E, Root>();
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

    CBOR_ALWAYS_INLINE static void u32_write(std::span<char> const out, std::size_t const at, std::size_t const value)
    {
        auto const b = big_endian(static_cast<std::uint32_t>(value));
        std::ranges::copy(b, out.subspan(at).template first<sizeof(std::uint32_t)>().begin());
    }

    CBOR_ALWAYS_INLINE static void item_head_write(std::span<char> const out, std::size_t const at, major_type const major,
                                                  std::size_t const length)
    {
        out[at] = static_cast<char>(std::to_underlying(major) << 5 |
                                    (std::to_underlying(additional_information::one_byte_argument) + 2));
        u32_write(out, at + 1, length);
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
        u32_write(out, directory_at<Root>() + sizeof(std::uint32_t) * j, item);
        field[1] = static_cast<char>((m & 1) << 5 | (std::to_underlying(additional_information::one_byte_argument) + 2));
        auto const n = big_endian(static_cast<std::uint32_t>(m >> 1));
        std::ranges::copy(n, field.template last<sizeof(std::uint32_t)>().begin());
        std::size_t const data = item + item_head;
        if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            item_head_write(out, item, major_type::array, value.has_value() ? 1 : 0);
            c.position = data;
            if (value.has_value()) {
                c.position += fixed_size<E, Root>();
                auto const element = out.subspan(data).template first<fixed_size<E, Root>()>();
                zero_initialized_copy<Root, E>(element);
                c = value_encode<Root, E, Exact>(out, element, *value, c);
            }
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            std::size_t const length = std::ranges::size(value);
            item_head_write(out, item, is_text_range<U> ? major_type::text_string : major_type::byte_string, length);
            auto const from = std::as_bytes(std::span(value));
            std::copy(from.begin(), from.end(), std::as_writable_bytes(out.subspan(data, length)).begin());
            c.position = data + length;
        } else if constexpr (is_map<U>) {
            using K = typename U::key_type;
            using M = typename U::mapped_type;
            std::size_t const length = std::ranges::size(value);
            item_head_write(out, item, major_type::map, length);
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
        } else {
            using E = std::ranges::range_value_t<U>;
            std::size_t const length = std::ranges::size(value);
            item_head_write(out, item, major_type::array, length);
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
            field.front() = static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                              (std::to_underlying(simple_value::false_value) + static_cast<int>(value)));
        } else if constexpr (std::is_enum_v<U>) {
            position = value_encode<Root, std::underlying_type_t<U>, Exact>(out, field, std::to_underlying(value), position);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 magnitude = static_cast<uint128>(value);
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = static_cast<uint128>(value >> 127);
                field.front() = static_cast<char>(std::to_underlying(major_type::tag) << 5 |
                                                  (std::to_underlying(tag_number::unsigned_bignum) +
                                                   static_cast<int>(sign & 1)));
                magnitude ^= sign;
            }
            auto const bytes = big_endian(magnitude);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            auto const bytes = big_endian(value);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(value >> (8 * sizeof(U) - 1));
            field.front() = static_cast<char>((sign & 1) << 5 |
                                              (std::to_underlying(additional_information::one_byte_argument) +
                                               std::countr_zero(sizeof(U))));
            auto const bytes = big_endian(static_cast<M>(static_cast<M>(value) ^ sign));
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::is_floating_point_v<U>) {
            auto const bits = big_endian(float_bits(value));
            std::copy(bits.begin(), bits.end(), field.template last<bits.size()>().begin());
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                          std::same_as<E, std::byte>) {
                auto const from = std::as_bytes(std::span(value));
                std::copy(from.begin(), from.end(), std::as_writable_bytes(field.template last<n>()).begin());
            } else {
                constexpr std::size_t head = head_size(n);
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
                    static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                      std::to_underlying(simple_value::true_value));
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
        auto const m = std::ranges::find_if(members, [name](std::meta::info const m) {
            return key_of(m) == name;
        });
        return m == members.end() ? std::meta::info{} : *m;
    }

    static consteval std::size_t step_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find_first_of(path.substr(at + 1), std::string_view(".[")) -
                                        path.begin());
    }

    static consteval std::size_t index_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find(path.substr(at + 1), ']') - path.begin());
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

#ifdef __SIZEOF_INT128__
    static uint128 unsigned128_read(std::span<char const, sizeof(uint128)> const field)
    {
        auto const high = static_cast<uint128>(unsigned_read<std::uint64_t>(field.first<sizeof(std::uint64_t)>()));
        auto const low = static_cast<uint128>(unsigned_read<std::uint64_t>(field.last<sizeof(std::uint64_t)>()));
        return high << 64 | low;
    }
#endif

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
            uint128 const magnitude = unsigned128_read(field.template last<sizeof(U)>());
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = -static_cast<uint128>(head & 1);
                return static_cast<U>(magnitude ^ sign);
            } else {
                return magnitude;
            }
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            return unsigned_read<U>(field.template last<sizeof(U)>());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(-static_cast<M>((head >> 5) & 1));
            return static_cast<U>(unsigned_read<M>(field.template last<sizeof(M)>()) ^ sign);
        } else {
            using B = decltype(float_bits(U{}));
#ifdef __SIZEOF_INT128__
            if constexpr (std::same_as<B, uint128>) {
                auto const bits = unsigned128_read(field.template last<sizeof(B)>());
                return static_cast<U>(std::bit_cast<std::float128_t>(bits));
            } else
#endif
            {
                auto const bits = unsigned_read<B>(field.template last<sizeof(B)>());
                using F = std::conditional_t<sizeof(B) == sizeof(std::uint16_t), std::float16_t,
                                             std::conditional_t<sizeof(B) == sizeof(std::uint32_t), std::float32_t,
                                                                std::float64_t>>;
                return static_cast<U>(std::bit_cast<F>(bits));
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
        constexpr std::size_t n = head_size(*tag_number_of(^^U));
        std::span<char const, n> const expected{zero_initialized<Root, U>().data(), n};
        return std::ranges::equal(field.template first<n>(), expected);
    }

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    struct decode_cursor {
        cbor::directory dir;
        std::size_t index;
        std::size_t at;
        std::size_t end;
    };

    static constexpr unsigned char reference_tag_byte =
        std::to_underlying(major_type::tag) << 5 | std::to_underlying(tag_number::reference);

    template <class Root>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> shared_index_read(std::span<char const, dynamic_type_sizes> const field)
    {
        constexpr std::size_t fillers = shared_first_of<Root>() - shared_first;
        auto const info = static_cast<unsigned char>(field[1]);
        if (static_cast<unsigned char>(field[0]) != reference_tag_byte ||
            (info & 0xdf) != std::to_underlying(additional_information::one_byte_argument) + 2) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const m = 2 * std::size_t{unsigned_read<std::uint32_t>(field.subspan<2, sizeof(std::uint32_t)>())} + (info >> 5);
        if (m < fillers) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        return m - fillers;
    }

    template <major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> item_length_read(std::string_view const encoded, std::size_t const item,
                                                                     std::size_t const end, std::size_t const element)
    {
        std::size_t size;
        if (item > end || end - item < item_head) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (static_cast<unsigned char>(encoded[item]) !=
            (std::to_underlying(Major) << 5 | (std::to_underlying(additional_information::one_byte_argument) + 2))) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length =
            unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(item + 1).template first<sizeof(std::uint32_t)>());
        if (ckd_mul(&size, length, element) || size > end - item - item_head) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return length;
    }

    CBOR_ALWAYS_INLINE static std::size_t directory_entry(std::string_view const encoded, cbor::directory const dir, std::size_t const j)
    {
        return unsigned_read<std::uint32_t>(
            std::span<char const>(encoded).subspan(dir.at + sizeof(std::uint32_t) * j).template first<sizeof(std::uint32_t)>());
    }

    template <class Root, major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> reference_read(std::string_view const encoded,
                                                           std::span<char const, dynamic_type_sizes> const field,
                                                           cbor::directory const dir, std::size_t const element)
    {
        auto const j = shared_index_read<Root>(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        return item_read<Root, Major>(encoded, dir, *j, element);
    }

    template <class Root, major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> item_read(std::string_view const encoded, cbor::directory const dir,
                                                                        std::size_t const j, std::size_t const element)
    {
        if (j >= dir.count) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        constexpr std::size_t fillers = shared_first_of<Root>() - 1 - packing_table_of<Root>().size();
        std::size_t const items_at = dir.at + sizeof(std::uint32_t) * dir.count + fillers;
        std::size_t const items_end = encoded.size() - fixed_size<Root, Root>();
        std::size_t const item = directory_entry(encoded, dir, j);
        std::size_t const end = j + 1 < dir.count ? directory_entry(encoded, dir, j + 1) : items_end;
        if (item < items_at || end > items_end) [[unlikely]]
            return std::unexpected(error::syntax_error);
        auto const length = item_length_read<Major>(encoded, item, end, element);
        if (!length) [[unlikely]]
            return std::unexpected(length.error());
        return reference{item + item_head, *length};
    }

    template <class Root, major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> reference_take(std::string_view const encoded,
                                                           std::span<char const, dynamic_type_sizes> const field,
                                                           decode_cursor &c, std::size_t const element)
    {
        auto const j = shared_index_read<Root>(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        if (*j != c.index || *j >= c.dir.count) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        if (directory_entry(encoded, c.dir, *j) != c.at) [[unlikely]]
            return std::unexpected(error::syntax_error);
        auto const length = item_length_read<Major>(encoded, c.at, c.end, element);
        if (!length) [[unlikely]]
            return std::unexpected(length.error());
        reference const r{c.at + item_head, *length};
        c.index += 1;
        c.at = r.data + *length * element;
        return r;
    }

    template <class T>
    static std::expected<cbor::directory, error> directory_read(std::string_view const encoded)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        constexpr std::size_t least = directory_at<T>() + fillers + fixed_size<T, T>();
        if (encoded.size() < least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (!std::ranges::equal(encoded.substr(0, 4), std::span(prefix).first(4)) ||
            !std::ranges::equal(encoded.substr(8, prefix.size() - 8), std::span(prefix).subspan(8)) ||
            static_cast<unsigned char>(encoded[prefix.size()]) != 0x5a) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length = unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(prefix.size() + 1).template first<4>());
        std::size_t const count = length / 4;
        if (length % 4 != 0 ||
            unsigned_read<std::uint32_t>(std::span<char const>(encoded).subspan(4).template first<4>()) != shared_first_of<T>() + count) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (length > encoded.size() - least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (std::ranges::any_of(encoded.substr(directory_at<T>() + length, fillers), [](char const c) { return c != '\xf7'; })) [[unlikely]]
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
                                          Path.view().substr(At) | std::views::pairwise, [](auto const pair) {
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
        } else if constexpr (path.substr(At, 1) == ".") {
            if constexpr (!(std::is_class_v<U> && std::is_aggregate_v<U>) || requires { fixed_length<U>::value; } || is_optional<U>) {
                return false;
            } else {
                constexpr std::size_t end = step_end(path, At);
                constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
                if constexpr (m == std::meta::info{})
                    return false;
                else
                    return path_valid<typename[:std::meta::type_of(m):], Path, end>();
            }
        } else if constexpr (path.substr(At, 1) == "[" && index_end(path, At) < path.size()) {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (is_fixed_string<U> || is_text_range<U> || is_byte_range<U> || is_map<U> || is_optional<U>) {
                return false;
            } else if constexpr (requires { fixed_length<U>::value; }) {
                if constexpr (close != At + 1 && index_of<T>(path.substr(At + 1, close - At - 1)) >= fixed_length<U>::value)
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
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
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
        -> cbor::result<typename decltype(path_result<Root, T, Path, At>())::type>
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_fixed_string<U>) {
                constexpr std::size_t n = fixed_length<U>::value;
                constexpr std::size_t head = fixed_size<U, Root>() - n;
                std::span<char const, head> const expected{zero_initialized<Root, U>().data(), head};
                if (!std::ranges::equal(field.template first<head>(), expected)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return std::string_view(field.template last<n>());
            } else if constexpr (is_inline_optional<U>) {
                using E = typename U::value_type;
                using X = typename decltype(path_result<Root, E, Path, At>())::type;
                if (static_cast<unsigned char>(field.template subspan<1, 1>().front()) !=
                    (std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(simple_value::true_value)))
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
                return encoded.substr(r->data, r->length);
            } else if constexpr (is_map<U>) {
                auto const r = reference_read<Root, major_type::map>(
                    encoded, field, floor, fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
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
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
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
                constexpr std::size_t head = head_size(n);
                std::span<char const, head> const expected{zero_initialized<Root, U>().data(), head};
                if (!std::ranges::equal(field.template first<head>(), expected)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if constexpr (close == At + 1) {
                    std::size_t const i = std::get<index_slot<Path, At>()>(indexes);
                    if (i >= n) [[unlikely]]
                        return std::unexpected(error::index_out_of_bounds);
                    return path_walk<Root, E, Path, close + 1>(
                        encoded, field.subspan(head + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(), floor, indexes);
                } else {
                    constexpr std::size_t i = index_of<T>(path.substr(At + 1, close - At - 1));
                    return path_walk<Root, E, Path, close + 1>(
                        encoded, field.template subspan<head + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(), floor, indexes);
                }
            } else {
                using E = std::ranges::range_value_t<U>;
                std::size_t i;
                if constexpr (close == At + 1)
                    i = std::get<index_slot<Path, At>()>(indexes);
                else
                    i = index_of<T>(path.substr(At + 1, close - At - 1));
                auto const r = reference_read<Root, major_type::array>(encoded, field, floor, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (i >= r->length) [[unlikely]]
                    return std::unexpected(error::index_out_of_bounds);
                return path_walk<Root, E, Path, close + 1>(
                    encoded, std::span<char const>(encoded).subspan(r->data + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(),
                    floor, indexes);
            }
        }
    }

    template <std::size_t DepthMax, class Root, class T>
    static std::expected<void, error> value_read(T &out, std::string_view const encoded,
                                                  std::span<char const, fixed_size<T, Root>()> const field,
                                                  decode_cursor &floor, std::size_t const depth)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; }) {
            if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> done;
            template for (constexpr std::meta::info m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                if (done)
                    done = value_read<DepthMax, Root>(out.[:m:], encoded,
                                                field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(), floor,
                                                depth);
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
                    done = value_read<DepthMax, Root>(
                        std::span(out).template subspan<i, 1>().front(), encoded,
                        field.template subspan<head_size(n) + i * fixed_size<E, Root>(), fixed_size<E, Root>()>(), floor, depth);
            }
            return done;
        } else if constexpr (is_inline_optional<U>) {
            if (static_cast<unsigned char>(field.template subspan<1, 1>().front()) !=
                (std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(simple_value::true_value))) {
                out.reset();
                return {};
            }
            return value_read<DepthMax, Root>(
                out.emplace(), encoded,
                field.template subspan<inline_optional_head, fixed_size<typename U::value_type, Root>()>(), floor, depth);
        } else if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            auto const r = reference_take<Root, major_type::array>(encoded, field, floor, fixed_size<E, Root>());
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->length > 1) [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (r->length == 0) {
                out.reset();
                return {};
            }
            if (depth == DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            E element{};
            if (auto const e = value_read<DepthMax, Root>(
                    element, encoded, std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>(),
                    floor, depth + 1);
                !e) [[unlikely]]
                return e;
            out = std::move(element);
            return {};
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            auto const r = reference_take<Root, is_text_range<U> ? major_type::text_string : major_type::byte_string>(encoded, field, floor, 1);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            auto const part = encoded.substr(r->data, r->length);
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
            auto const r = reference_take<Root, major_type::map>(encoded, field, floor, pair);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->length != 0 && depth == DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            out.clear();
            for (std::size_t i = 0; i < r->length; ++i) {
                auto const at = std::span<char const>(encoded).subspan(r->data + i * pair).template first<pair>();
                K key{};
                V value{};
                if (auto const e = value_read<DepthMax, Root>(key, encoded, at.template first<fixed_size<K, Root>()>(), floor, depth + 1);
                    !e) [[unlikely]]
                    return e;
                if (auto const e = value_read<DepthMax, Root>(value, encoded, at.template last<fixed_size<V, Root>()>(), floor, depth + 1);
                    !e) [[unlikely]]
                    return e;
                out.insert_or_assign(std::move(key), std::move(value));
            }
            return {};
        } else if constexpr (std::ranges::sized_range<U>) {
            using E = std::ranges::range_value_t<U>;
            auto const r = reference_take<Root, major_type::array>(encoded, field, floor, fixed_size<E, Root>());
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->length != 0 && depth == DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            out.clear();
            out.reserve(r->length);
            for (std::size_t i = 0; i < r->length; ++i) {
                E element{};
                auto const at =
                    std::span<char const>(encoded).subspan(r->data + i * fixed_size<E, Root>()).template first<fixed_size<E, Root>()>();
                if (auto const e = value_read<DepthMax, Root>(element, encoded, at, floor, depth + 1); !e) [[unlikely]]
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

    template <class T, std::size_t DepthMax>
    static std::expected<void, error> root_read(T &out, std::string_view const encoded)
    {
        auto const dir = directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        std::size_t const root = encoded.size() - fixed_size<T, T>();
        auto const field = std::span<char const>(encoded).subspan(root).template first<fixed_size<T, T>()>();
        if (!class_tag_valid<T, T>(field)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        decode_cursor c{*dir, 0, dir->at + 4 * dir->count + fillers, root};
        if (auto const r = value_read<DepthMax, T>(out, encoded, field, c, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (c.index != dir->count || c.at != root) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return {};
    }

    template <class>
    friend class cbor::schema;

#endif

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded);

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
        std::string_view encoded;
        std::expected<head, error> head_decode()
        {
            if (encoded.empty()) [[unlikely]]
                return std::unexpected(error::too_little_data);
            auto const initial = static_cast<std::uint8_t>(encoded.front());
            auto const major = static_cast<major_type>(initial >> 5);
            std::uint8_t const info = initial & 0x1f;
            if (info < std::to_underlying(additional_information::one_byte_argument)) {
                encoded.remove_prefix(1);
                return head{major, info, info};
            }
            if (info == std::to_underlying(additional_information::indefinite_length) &&
                major >= major_type::byte_string && major <= major_type::map) [[unlikely]]
                return std::unexpected(error::indefinite_length);
            if (info > std::to_underlying(additional_information::eight_byte_argument)) [[unlikely]]
                return std::unexpected(error::syntax_error);
            std::size_t const size =
                std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
            if (encoded.size() < 1 + size) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::string_view const rest = encoded.substr(1, size);
            std::uint64_t argument;
            switch (static_cast<additional_information>(info)) {
            case additional_information::one_byte_argument:
                argument = static_cast<std::uint8_t>(rest.front());
                break;
            case additional_information::two_byte_argument:
                argument = unsigned_read<std::uint16_t>(std::span<char const>(rest).first<2>());
                break;
            case additional_information::four_byte_argument:
                argument = unsigned_read<std::uint32_t>(std::span<char const>(rest).template first<4>());
                break;
            default:
                argument = unsigned_read<std::uint64_t>(std::span<char const>(rest).first<8>());
                break;
            }
            encoded.remove_prefix(1 + size);
            return head{major, info, argument};
        }

        std::expected<std::string_view, error> byte_string_decode(std::uint64_t length)
        {
            if (encoded.size() < length) [[unlikely]]
                return std::unexpected(error::too_little_data);
            std::string_view const string = encoded.substr(0, length);
            encoded.remove_prefix(length);
            return string;
        }

        std::expected<std::string_view, error> text_string_decode(std::uint64_t length)
        {
            auto const text = byte_string_decode(length);
#ifdef CBOR_SIMDUTF
            if (text && !simdutf::validate_utf8(text->data(), text->size())) [[unlikely]]
                return std::unexpected(error::invalid_utf8_string);
#endif
            return text;
        }
    };

    struct sharing_decoder : decoder {
        std::string_view message;
        std::vector<std::size_t> marks;
        std::size_t high_water_mark;

        void mark(decoder const &at)
        {
            std::size_t const offset = message.size() - at.encoded.size();
            if (offset > high_water_mark) {
                marks.push_back(offset);
                high_water_mark = offset;
            }
        }

        std::expected<std::size_t, error> reference_follow(std::size_t const tag_at)
        {
            auto const n = head_decode();
            if (!n) [[unlikely]]
                return std::unexpected(n.error());
            if (n->major != major_type::unsigned_integer) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            if (n->argument >= marks.size()) [[unlikely]]
                return std::unexpected(error::sharedref_index_not_marked);
            std::size_t const marked = marks.at(static_cast<std::size_t>(n->argument));
            if (marked >= tag_at) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            return marked;
        }
    };

    struct document {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::vector<std::size_t> marks;
        std::size_t high_water_mark;

        void mark(decoder const &d)
        {
            std::size_t const offset = encoded.size() - d.encoded.size();
            if (offset > high_water_mark) {
                marks.push_back(offset);
                high_water_mark = offset;
            }
        }
    };

    struct string_sink {
        std::string encoded;

        std::expected<void, std::errc> append(std::string_view const part)
        {
            encoded.append(part);
            return {};
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }

        template <class Op>
        std::expected<void, std::errc> resize_and_overwrite(std::size_t const size, Op op)
        {
            std::size_t const at = encoded.size();
            encoded.resize_and_overwrite(at + size, [&](char *const p, std::size_t const n) {
                return at + op(std::span<char>(p, n).subspan(at, size));
            });
            return {};
        }
    };

    template <class C>
    struct container_message {
        C &container;

        std::expected<void, std::errc> append(std::string_view const part)
        {
            std::size_t const at = std::ranges::size(container);
            container.resize(at + part.size());
            std::ranges::transform(part, std::ranges::next(std::ranges::begin(container), at),
                                   [](char const c) { return static_cast<std::ranges::range_value_t<C>>(c); });
            return {};
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }
    };

    template <class B>
    struct span_message {
        std::span<B> out;
        std::size_t used;

        std::expected<void, std::errc> append(std::string_view const part)
        {
            if (part.size() > out.size() - used) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
            std::ranges::transform(part, out.subspan(used).begin(), [](char const c) { return static_cast<B>(c); });
            used += part.size();
            return {};
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }
    };

    template <class C>
    static constexpr bool byte_container = requires(C &c) {
        c.resize(std::size_t{});
        requires sizeof(std::ranges::range_value_t<C>) == 1;
        requires std::ranges::contiguous_range<C>;
    };

    template <class Target>
    static decltype(auto) message_of(Target &&target, std::size_t const hint)
    {
        using U = std::remove_cvref_t<Target>;
        if constexpr (requires { typename U::element_type; } && requires { std::span(target); } &&
                      !requires { target.resize(std::size_t{}); })
            return span_message<typename U::element_type>{target, 0};
        else if constexpr (byte_container<U>)
            return container_message<U>{target};
        else
            return target.allocate(hint);
    }

#ifdef __cpp_impl_reflection
    template <class T>
    static std::expected<std::size_t, std::errc> encoded_size(second_item<T> const &second)
    {
        std::size_t second_size;
        std::size_t size;
        if (second.overflow || ckd_mul(&second_size, second.items, sizeof(std::uint32_t)) ||
            ckd_add(&second_size, second_size, second.bytes) ||
            ckd_add(&size, directory_at<T>() + shared_first_of<T>() - 1 - packing_table_of<T>().size() + fixed_size<T, T>(),
                    second_size) ||
            !std::in_range<std::uint32_t>(size)) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return size;
    }

    template <bool Exact, class T>
    CBOR_ALWAYS_INLINE static std::size_t encoded_write(std::span<char> const encoded, T const &value, second_item<T> const &second)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first_of<T>() - 1 - packing_table_of<T>().size();
        std::size_t const size = encoded.size() - (Exact ? 0 : head_padding);
        std::ranges::copy(prefix, encoded.begin());
        u32_write(encoded, 4, shared_first_of<T>() + second.items);
        item_head_write(encoded, prefix.size(), major_type::byte_string, sizeof(std::uint32_t) * second.items);
        std::size_t const data = directory_at<T>() + sizeof(std::uint32_t) * second.items;
        std::ranges::fill(encoded.subspan(data, fillers),
                          static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                            std::to_underlying(simple_value::undefined)));
        auto const root = encoded.subspan(size - fixed_size<T, T>()).template first<fixed_size<T, T>()>();
        zero_initialized_copy<T, T>(root);
        value_encode<T, T, Exact>(encoded, root, value, encode_cursor{data + fillers, 0});
        return size;
    }
#endif

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

    static constexpr float float_decode_binary16(std::uint16_t half)
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

    static constexpr std::uint16_t float_encode_binary16(float value)
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

    static constexpr simple_float_information preferred_float_info(double value)
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
        if (static_cast<double>(static_cast<float>(value)) != value)
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

    template <std::size_t DepthMax, class Binding>
    static std::expected<typename Binding::value, error>
    value_decode(decoder &d, Binding &binding, marks<Binding> &shared, prefix *before, std::size_t depth,
                 std::optional<std::size_t> const mark)
    {
        if (depth > DepthMax) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        switch (h->major) {
        case major_type::unsigned_integer:
            return binding.unsigned_integer_decode(h->argument);
        case major_type::negative_integer:
            return binding.negative_integer_decode(h->argument);
        case major_type::byte_string: {
            auto const s = d.byte_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            return binding.byte_string_decode(*s);
        }
        case major_type::text_string: {
            auto const s = d.text_string_decode(h->argument);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            return binding.text_string_decode(*s);
        }
        case major_type::array: {
            auto array = binding.array_decode(std::min<std::uint64_t>(h->argument, d.encoded.size()));
            if constexpr (requires { binding.cyclic_data_structures(); })
                if (mark && binding.cyclic_data_structures())
                    shared.at(*mark) = array;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
                if (!element) [[unlikely]]
                    return element;
                array = binding.array_append(std::move(array), std::move(*element));
            }
            return array;
        }
        case major_type::map: {
            auto map = binding.map_decode(std::min<std::uint64_t>(h->argument, d.encoded.size() / 2));
            if constexpr (requires { binding.cyclic_data_structures(); })
                if (mark && binding.cyclic_data_structures())
                    shared.at(*mark) = map;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                if constexpr (requires(std::string_view const t) { binding.map_key_decode(t); }) {
                    decoder probe = d;
                    auto const k = probe.head_decode();
                    if (k && k->major == major_type::text_string) {
                        auto const t = probe.text_string_decode(k->argument);
                        if (!t) [[unlikely]]
                            return std::unexpected(t.error());
                        d = probe;
                        auto key = binding.map_key_decode(*t);
                        auto value = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
                        if (!value) [[unlikely]]
                            return value;
                        map = binding.map_insert(std::move(map), std::move(key), std::move(*value));
                        continue;
                    }
                }
                auto key = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
                if (!key) [[unlikely]]
                    return key;
                auto value = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
                if (!value) [[unlikely]]
                    return value;
                map = binding.map_insert(std::move(map), std::move(*key), std::move(*value));
            }
            return map;
        }
        case major_type::tag: {
            if (depth + 1 > DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                std::size_t index = shared.size();
                if (before) {
                    std::size_t const at = before->document.size() - d.encoded.size();
                    auto const known = std::lower_bound(before->offsets.begin(), before->offsets.end(), at);
                    if (known != before->offsets.end() && *known == at)
                        index = static_cast<std::size_t>(known - before->offsets.begin());
                }
                if (index == shared.size())
                    shared.emplace_back();
                auto content = value_decode<DepthMax>(d, binding, shared, before, depth + 1, index);
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
                        return binding.negative_integer_decode(magnitude_value(magnitude));
                    return binding.unsigned_integer_decode(magnitude_value(magnitude));
                }
                if (negative)
                    return binding.negative_bignum_decode(std::string_view(magnitude_plus_one(magnitude)));
                return binding.unsigned_bignum_decode(magnitude);
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
                    before->offsets.at(index) < before->document.size() - d.encoded.size() &&
                    !before->decoding.at(index)) {
                    decoder earlier{before->document.substr(before->offsets.at(index))};
                    before->decoding.at(index) = true;
                    auto content = value_decode<DepthMax>(earlier, binding, shared, before, depth + 1, index);
                    before->decoding.at(index) = false;
                    if (!content) [[unlikely]]
                        return content;
                    shared.at(index) = *content;
                }
                if (!shared.at(index)) [[unlikely]]
                    return std::unexpected(error::sharedref_not_complete);
                return *shared.at(index);
            }
            if constexpr (requires { binding.tag_begin(h->argument); }) {
                std::optional<typename Binding::value> object = binding.tag_begin(h->argument);
                if (object) {
                    if constexpr (requires { binding.cyclic_data_structures(); })
                        if (mark && binding.cyclic_data_structures())
                            shared.at(*mark) = *object;
                    auto content = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
                    if (!content) [[unlikely]]
                        return content;
                    return binding.after_decode(binding.registered_decode(std::move(*object), std::move(*content)));
                }
            }
            auto content = value_decode<DepthMax>(d, binding, shared, before, depth + 1, std::nullopt);
            if (!content) [[unlikely]]
                return content;
            return binding.tag_decode(h->argument, std::move(*content));
        }
        default:
            switch (static_cast<simple_float_information>(h->info)) {
            case simple_float_information::simple_value_follows:
                if (h->argument < simple_value_one_byte_min) [[unlikely]]
                    return std::unexpected(error::syntax_error);
                return binding.simple_value_decode(static_cast<std::uint8_t>(h->argument));
            case simple_float_information::half_precision_float:
                return binding.float_decode(static_cast<double>(float_decode_binary16(
                                              static_cast<std::uint16_t>(h->argument))));
            case simple_float_information::single_precision_float:
                return binding.float_decode(static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(h->argument))));
            case simple_float_information::double_precision_float:
                return binding.float_decode(std::bit_cast<double>(h->argument));
            default:
                return binding.simple_value_decode(h->info);
            }
        }
    }

    template <std::size_t DepthMax, language_binding Binding>
    friend std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view encoded);

    template <class Writer>
    friend struct encoder;

    template <std::size_t DepthMax, sharedrefs Sharing, class Binding, class Writer>
    friend std::expected<void, std::error_code> encode(Binding &binding, Writer &&target,
                                                       typename Binding::value const &value);

    template <std::size_t, class, class, pass>
    friend class walker;

    friend struct lazy;

    template <std::size_t DepthMax, class Binding>
    friend std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l);

public:
    struct selector {
        enum class kind { key, index, wildcard } kind;
        std::size_t key_at;
        std::size_t key_size;
        std::int64_t index;
    };

    struct query {
        std::vector<selector> selectors;
        std::string keys;
    };

    struct parsed {
        std::size_t at;
        bool separated;
    };

    static constexpr int no_indicator = -1;
    static constexpr int immediate_indicator = -2;

    static constexpr bool blank(char const c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    static constexpr std::size_t blank_end(std::string_view const text, std::size_t at)
    {
        while (at < text.size() && blank(text.at(at)))
            ++at;
        return at;
    }

    static constexpr bool digit(char const c)
    {
        return c >= '0' && c <= '9';
    }

    static constexpr int hex_digit_value(char const c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    static constexpr std::expected<void, error> head_append(std::string &out, major_type const major,
                                                            std::uint64_t const argument, int const indicator)
    {
        int info;
        if (indicator == no_indicator)
            info = argument < 24 ? 0 : 24 + std::countr_zero(std::bit_ceil(static_cast<unsigned>((std::bit_width(argument) + 7) / 8)));
        else if (indicator == immediate_indicator)
            info = 0;
        else
            info = indicator;
        if (info == 0) {
            if (argument >= 24) [[unlikely]]
                return std::unexpected(error::invalid_path);
            out.push_back(static_cast<char>(std::to_underlying(major) << 5 | static_cast<int>(argument)));
            return {};
        }
        if (info < 24 || info > 27) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t const size = std::size_t{1} << (info - 24);
        if (size < 8 && argument >> (8 * size) != 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        out.push_back(static_cast<char>(std::to_underlying(major) << 5 | info));
        for (std::size_t i = size; i > 0; --i)
            out.push_back(static_cast<char>(argument >> (8 * (i - 1))));
        return {};
    }

    struct indicated {
        std::size_t at;
        int indicator;
    };

    static constexpr std::expected<indicated, error> indicator_parse(std::string_view const text, std::size_t const at)
    {
        if (at >= text.size() || text.at(at) != '_')
            return indicated{at, no_indicator};
        if (at + 1 < text.size() && text.at(at + 1) == 'i')
            return indicated{at + 2, immediate_indicator};
        if (at + 1 < text.size() && text.at(at + 1) >= '0' && text.at(at + 1) <= '3')
            return indicated{at + 2, 24 + (text.at(at + 1) - '0')};
        if (at + 1 < text.size() && digit(text.at(at + 1))) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return indicated{at + 1, std::to_underlying(additional_information::indefinite_length)};
    }

    static constexpr void utf8_append(std::string &out, std::uint32_t const c)
    {
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xc0 | c >> 6));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | c >> 12));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | c >> 18));
            out.push_back(static_cast<char>(0x80 | (c >> 12 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c >> 6 & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }

    struct code_unit {
        std::size_t at;
        std::uint32_t value;
    };

    static constexpr std::expected<code_unit, error> hex4_parse(std::string_view const text, std::size_t const at)
    {
        if (at + 4 > text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::uint32_t value = 0;
        for (char const c : text.substr(at, 4)) {
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = value << 4 | static_cast<std::uint32_t>(v);
        }
        return code_unit{at + 4, value};
    }

    static constexpr std::expected<std::size_t, error> quoted_parse(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        char const quote = text.at(at++);
        for (;;) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text.at(at++);
            if (c == quote)
                return at;
            if (static_cast<unsigned char>(c) < 0x20) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const e = text.at(at++);
            switch (e) {
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case '/':
            case '\\':
            case '\'':
            case '"':
                out.push_back(e);
                break;
            case 'u': {
                std::uint32_t c1 = 0;
                if (at < text.size() && text.at(at) == '{') {
                    std::size_t const close = text.find('}', at);
                    if (close == std::string_view::npos || close == at + 1 || close > at + 9) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    for (char const h : text.substr(at + 1, close - at - 1)) {
                        int const v = hex_digit_value(h);
                        if (v < 0) [[unlikely]]
                            return std::unexpected(error::invalid_path);
                        c1 = c1 << 4 | static_cast<std::uint32_t>(v);
                    }
                    at = close + 1;
                    if (c1 > 0x10ffff || (c1 >= 0xd800 && c1 < 0xe000)) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    utf8_append(out, c1);
                    break;
                }
                auto const next = hex4_parse(text, at);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                at = next->at;
                c1 = next->value;
                if (c1 >= 0xdc00 && c1 < 0xe000) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (c1 >= 0xd800 && c1 < 0xdc00) {
                    if (at + 2 > text.size() || text.at(at) != '\\' || text.at(at + 1) != 'u') [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    auto const low = hex4_parse(text, at + 2);
                    if (!low || low->value < 0xdc00 || low->value >= 0xe000) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    std::uint32_t const c2 = low->value;
                    at = low->at;
                    c1 = 0x10000 + ((c1 - 0xd800) << 10) + (c2 - 0xdc00);
                }
                utf8_append(out, c1);
                break;
            }
            [[unlikely]] default:
                return std::unexpected(error::invalid_path);
            }
        }
    }

    static constexpr std::expected<std::size_t, error> hex_string_parse(std::string_view const text, std::size_t at,
                                                                        std::string &out)
    {
        int high = -1;
        for (;; ++at) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text.at(at);
            if (c == '\'')
                break;
            if (blank(c))
                continue;
            int const v = hex_digit_value(c);
            if (v < 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (high < 0) {
                high = v;
            } else {
                out.push_back(static_cast<char>(high << 4 | v));
                high = -1;
            }
        }
        if (high >= 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> base64_string_parse(std::string_view const text, std::size_t at,
                                                                           std::string &out)
    {
        std::uint32_t bits = 0;
        int count = 0;
        int padding = 0;
        for (;; ++at) {
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text.at(at);
            if (c == '\'')
                break;
            if (blank(c))
                continue;
            int v;
            if (c >= 'A' && c <= 'Z')
                v = c - 'A';
            else if (c >= 'a' && c <= 'z')
                v = c - 'a' + 26;
            else if (digit(c))
                v = c - '0' + 52;
            else if (c == '+' || c == '-')
                v = 62;
            else if (c == '/' || c == '_')
                v = 63;
            else if (c == '=') {
                ++padding;
                continue;
            } else [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (padding != 0) [[unlikely]]
                return std::unexpected(error::invalid_path);
            bits = bits << 6 | static_cast<std::uint32_t>(v);
            if (++count == 4) {
                out.push_back(static_cast<char>(bits >> 16));
                out.push_back(static_cast<char>(bits >> 8));
                out.push_back(static_cast<char>(bits));
                bits = 0;
                count = 0;
            }
        }
        if (count == 1 || (padding != 0 && count + padding != 4)) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (count == 2)
            out.push_back(static_cast<char>(bits >> 4));
        if (count == 3) {
            out.push_back(static_cast<char>(bits >> 10));
            out.push_back(static_cast<char>(bits >> 2));
        }
        return at + 1;
    }

    static constexpr std::expected<void, error> float_append(std::string &out, double const value, int const indicator)
    {
        bool const nan = value != value;
        bool const infinite = !nan && (value > std::numeric_limits<double>::max() || value < -std::numeric_limits<double>::max());
        simple_float_information const preferred = preferred_float_info(value);
        int info;
        if (indicator == no_indicator)
            info = std::to_underlying(preferred);
        else if (indicator >= std::to_underlying(simple_float_information::half_precision_float) &&
                 indicator <= std::to_underlying(simple_float_information::double_precision_float))
            info = indicator;
        else [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exact =
            nan || infinite || info >= std::to_underlying(preferred) ||
            (info == std::to_underlying(simple_float_information::single_precision_float) &&
             static_cast<double>(static_cast<float>(value)) == value);
        if (!exact) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::uint64_t argument;
        if (info == std::to_underlying(simple_float_information::half_precision_float))
            argument = nan ? 0x7e00 : float_encode_binary16(static_cast<float>(value));
        else if (info == std::to_underlying(simple_float_information::single_precision_float))
            argument = nan ? 0x7fc00000 : std::bit_cast<std::uint32_t>(static_cast<float>(value));
        else
            argument = nan ? 0x7ff8000000000000 : std::bit_cast<std::uint64_t>(value);
        return head_append(out, major_type::simple_float, argument, info);
    }

    static constexpr std::expected<std::size_t, error> number_parse(std::string_view const text, std::size_t at,
                                                                    std::string &out)
    {
        bool negative = false;
        char const sign = text.at(at);
        if (sign == '+' || sign == '-')
            negative = text.at(at++) == '-';
        auto const word = [&](std::string_view const w) { return text.substr(at).starts_with(w); };
        if (word("Infinity") || word("NaN")) {
            bool const nan = word("NaN");
            if (sign == '+' || (nan && sign == '-')) [[unlikely]]
                return std::unexpected(error::invalid_path);
            at += nan ? 3 : 8;
            auto const next = indicator_parse(text, at);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            int const indicator = next->indicator;
            double const infinity = std::numeric_limits<double>::infinity();
            auto const r = float_append(out, nan ? std::numeric_limits<double>::quiet_NaN() : negative ? -infinity : infinity,
                                        indicator);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            return next->at;
        }
        int base = 10;
        if (word("0x") || word("0X")) {
            base = 16;
            at += 2;
        } else if (word("0o")) {
            base = 8;
            at += 2;
        } else if (word("0b")) {
            base = 2;
            at += 2;
        }
        std::uint64_t mantissa = 0;
        std::size_t digits = 0;
        int exponent = 0;
        bool overflow = false;
        bool fraction = false;
        bool real = false;
        std::size_t const first = at;
        for (; at < text.size(); ++at) {
            char const c = text.at(at);
            if (c == '.' && !fraction && (base == 10 || base == 16)) {
                fraction = true;
                real = true;
                continue;
            }
            int const v = hex_digit_value(c);
            if (v < 0 || v >= base)
                break;
            ++digits;
            if (mantissa > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(v)) / static_cast<std::uint64_t>(base)) {
                overflow = true;
                continue;
            }
            mantissa = mantissa * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(v);
            if (fraction)
                --exponent;
        }
        if (digits == 0) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (base == 10 && text.at(first) == '0' && first + 1 < at && digit(text.at(first + 1))) [[unlikely]]
            return std::unexpected(error::invalid_path);
        bool const exponent_part = at < text.size() && ((base == 10 && (text.at(at) == 'e' || text.at(at) == 'E')) ||
                                                        (base == 16 && (text.at(at) == 'p' || text.at(at) == 'P')));
        if (base == 16 && real && !exponent_part) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (exponent_part) {
            real = true;
            ++at;
            bool exponent_negative = false;
            if (at < text.size() && (text.at(at) == '+' || text.at(at) == '-'))
                exponent_negative = text.at(at++) == '-';
            int e = 0;
            std::size_t const e_first = at;
            while (at < text.size() && digit(text.at(at)) && e < 100000)
                e = e * 10 + (text.at(at++) - '0');
            if (at == e_first || (at < text.size() && digit(text.at(at)))) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (base == 16)
                exponent = 4 * exponent + (exponent_negative ? -e : e);
            else
                exponent += exponent_negative ? -e : e;
        } else if (base == 16) {
            exponent *= 4;
        }
        auto const next = indicator_parse(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        int const indicator = next->indicator;
        if (overflow) [[unlikely]]
            return std::unexpected(error::invalid_path);
        if (!real) {
            if (negative && mantissa != 0) {
                if (auto const r = head_append(out, major_type::negative_integer, mantissa - 1, indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
            } else if (auto const r = head_append(out, major_type::unsigned_integer, mantissa, indicator); !r) [[unlikely]] {
                return std::unexpected(r.error());
            }
            return next->at;
        }
        constexpr std::uint64_t exact_max = std::uint64_t{1} << std::numeric_limits<double>::digits;
        double value;
        if (mantissa == 0) {
            value = 0.0;
        } else if (base == 16) {
            if (mantissa > exact_max) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = static_cast<double>(mantissa);
            for (int i = 0; i < exponent; ++i)
                value *= 2.0;
            for (int i = 0; i > exponent; --i)
                value *= 0.5;
        } else {
            constexpr std::array<double, 23> powers{1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
                                                    1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
            while (exponent > 22 && mantissa <= exact_max / 10) {
                mantissa *= 10;
                --exponent;
            }
            while (exponent < 0 && mantissa % 10 == 0) {
                mantissa /= 10;
                ++exponent;
            }
            if (mantissa > exact_max || exponent > 22 || exponent < -22) [[unlikely]]
                return std::unexpected(error::invalid_path);
            value = exponent >= 0 ? static_cast<double>(mantissa) * powers.at(static_cast<std::size_t>(exponent))
                                  : static_cast<double>(mantissa) / powers.at(static_cast<std::size_t>(-exponent));
        }
        if (auto const r = float_append(out, negative ? -value : value, indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next->at;
    }

    static constexpr std::expected<parsed, error> separator_parse(std::string_view const text, std::size_t const at)
    {
        std::size_t next = blank_end(text, at);
        bool separated = next != at;
        if (next < text.size() && text.at(next) == ',') {
            next = blank_end(text, next + 1);
            separated = true;
        }
        return parsed{next, separated};
    }

    static constexpr std::expected<std::size_t, error> container_parse(std::string_view const text, std::size_t at,
                                                                       std::string &out, std::size_t const depth,
                                                                       std::size_t const depth_max)
    {
        bool const map = text.at(at++) == '{';
        char const close = map ? '}' : ']';
        auto const after = indicator_parse(text, at);
        if (!after) [[unlikely]]
            return std::unexpected(after.error());
        int const indicator = after->indicator;
        at = blank_end(text, after->at);
        std::string items;
        std::uint64_t count = 0;
        bool separated = true;
        while (at < text.size() && text.at(at) != close) {
            if (!separated) [[unlikely]]
                return std::unexpected(error::invalid_path);
            auto next = literal_parse(text, at, items, depth + 1, depth_max);
            if (!next) [[unlikely]]
                return next;
            if (map) {
                std::size_t const colon = blank_end(text, *next);
                if (colon >= text.size() || text.at(colon) != ':') [[unlikely]]
                    return std::unexpected(error::invalid_path);
                next = literal_parse(text, blank_end(text, colon + 1), items, depth + 1, depth_max);
                if (!next) [[unlikely]]
                    return next;
            }
            ++count;
            auto const s = separator_parse(text, *next);
            if (!s) [[unlikely]]
                return std::unexpected(s.error());
            at = s->at;
            separated = s->separated;
        }
        if (at >= text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        major_type const major = map ? major_type::map : major_type::array;
        if (indicator == std::to_underlying(additional_information::indefinite_length)) {
            out.push_back(static_cast<char>(std::to_underlying(major) << 5 | indicator));
            out += items;
            out.push_back('\xff');
        } else {
            if (auto const r = head_append(out, major, count, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += items;
        }
        return at + 1;
    }

    static constexpr std::expected<std::size_t, error> string_finish(std::string_view const text, std::size_t const at,
                                                                     std::string &out, major_type const major,
                                                                     std::string_view const content)
    {
        auto const next = indicator_parse(text, at);
        if (!next) [[unlikely]]
            return std::unexpected(next.error());
        int const indicator = next->indicator;
        if (indicator == std::to_underlying(additional_information::indefinite_length)) {
            if (!content.empty()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            out.push_back(static_cast<char>(std::to_underlying(major) << 5 | indicator));
            out.push_back('\xff');
            return next->at;
        }
        if (auto const r = head_append(out, major, content.size(), indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        out += content;
        return next->at;
    }

    static constexpr std::expected<std::size_t, error> literal_parse(std::string_view const text, std::size_t const at,
                                                                     std::string &out, std::size_t const depth,
                                                                     std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        if (at >= text.size()) [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::string_view const rest = text.substr(at);
        char const c = rest.front();
        if (c == '[' || c == '{')
            return container_parse(text, at, out, depth, depth_max);
        if (c == '"' || c == '\'') {
            std::string content;
            auto const next = quoted_parse(text, at, content);
            if (!next) [[unlikely]]
                return next;
            return string_finish(text, *next, out, c == '"' ? major_type::text_string : major_type::byte_string, content);
        }
        if (rest.starts_with("h'") || rest.starts_with("b64'")) {
            std::string content;
            bool const hex = rest.front() == 'h';
            auto const next = hex ? hex_string_parse(text, at + 2, content) : base64_string_parse(text, at + 4, content);
            if (!next) [[unlikely]]
                return next;
            return string_finish(text, *next, out, major_type::byte_string, content);
        }
        if (rest.starts_with("<<")) {
            std::string content;
            std::size_t next = blank_end(text, at + 2);
            bool separated = true;
            while (!text.substr(next).starts_with(">>")) {
                if (!separated || next >= text.size()) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                auto const item = literal_parse(text, next, content, depth + 1, depth_max);
                if (!item) [[unlikely]]
                    return item;
                auto const s = separator_parse(text, *item);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                next = s->at;
                separated = s->separated;
            }
            return string_finish(text, next + 2, out, major_type::byte_string, content);
        }
        constexpr std::array<std::string_view, 4> names{"false", "true", "null", "undefined"};
        for (std::size_t i = 0; i < names.size(); ++i)
            if (rest.starts_with(names.at(i))) {
                out.push_back(static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                                (std::to_underlying(simple_value::false_value) + i)));
                return at + names.at(i).size();
            }
        if (rest.starts_with("simple(")) {
            std::size_t next = blank_end(text, at + 7);
            std::size_t const first = next;
            unsigned value = 0;
            while (next < text.size() && digit(text.at(next)) && value < 1000)
                value = value * 10 + static_cast<unsigned>(text.at(next++) - '0');
            if (next == first || (text.at(first) == '0' && next > first + 1)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            next = blank_end(text, next);
            if (next >= text.size() || text.at(next) != ')' || value > 255 ||
                (value >= 24 && value < simple_value_one_byte_min)) [[unlikely]]
                return std::unexpected(error::invalid_path);
            if (auto const r = head_append(out, major_type::simple_float, value, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next + 1;
        }
        if (!(digit(c) || c == '-' || c == '+' || c == '.' || rest.starts_with("Infinity") || rest.starts_with("NaN")))
            [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t digits_end = at;
        while (digits_end < text.size() && digit(text.at(digits_end)))
            ++digits_end;
        auto const tag_open = indicator_parse(text, digits_end);
        int const indicator = tag_open ? tag_open->indicator : no_indicator;
        if (digits_end != at && tag_open && tag_open->at < text.size() && text.at(tag_open->at) == '(' &&
            indicator != std::to_underlying(additional_information::indefinite_length)) {
            if (text.at(at) == '0' && digits_end > at + 1) [[unlikely]]
                return std::unexpected(error::invalid_path);
            std::uint64_t number = 0;
            for (char const d : text.substr(at, digits_end - at)) {
                if (number > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(d - '0')) / 10) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                number = number * 10 + static_cast<std::uint64_t>(d - '0');
            }
            if (auto const r = head_append(out, major_type::tag, number, indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const content = literal_parse(text, blank_end(text, tag_open->at + 1), out, depth + 1, depth_max);
            if (!content) [[unlikely]]
                return content;
            std::size_t const close = blank_end(text, *content);
            if (close >= text.size() || text.at(close) != ')') [[unlikely]]
                return std::unexpected(error::invalid_path);
            return close + 1;
        }
        return number_parse(text, at, out);
    }

    struct raw_head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
        std::size_t at;
    };

    static constexpr std::expected<raw_head, error> raw_head_read(std::string_view const encoded, std::size_t const at)
    {
        if (at >= encoded.size()) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const initial = static_cast<std::uint8_t>(encoded.at(at));
        auto const major = static_cast<major_type>(initial >> 5);
        std::uint8_t const info = initial & 0x1f;
        if (info < std::to_underlying(additional_information::one_byte_argument) ||
            info == std::to_underlying(additional_information::indefinite_length))
            return raw_head{major, info, info, at + 1};
        if (info > std::to_underlying(additional_information::eight_byte_argument)) [[unlikely]]
            return std::unexpected(error::syntax_error);
        std::size_t const size = std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
        if (encoded.size() - at - 1 < size) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::uint64_t argument = 0;
        for (char const c : encoded.substr(at + 1, size))
            argument = argument << 8 | static_cast<std::uint8_t>(c);
        return raw_head{major, info, argument, at + 1 + size};
    }

    static constexpr bool break_at(std::string_view const encoded, std::size_t const at)
    {
        return at < encoded.size() && static_cast<std::uint8_t>(encoded.at(at)) == 0xff;
    }

    static constexpr std::expected<std::size_t, error> canonical_append(std::string &out, std::string_view const encoded,
                                                                        std::size_t const at, std::size_t const depth,
                                                                        std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = raw_head_read(encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool const indefinite = h->info == std::to_underlying(additional_information::indefinite_length);
        std::size_t next = h->at;
        switch (h->major) {
        case major_type::unsigned_integer:
        case major_type::negative_integer:
            if (indefinite) [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (auto const r = head_append(out, h->major, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        case major_type::byte_string:
        case major_type::text_string: {
            std::string content;
            if (!indefinite) {
                if (encoded.size() - next < h->argument) [[unlikely]]
                    return std::unexpected(error::too_little_data);
                content = encoded.substr(next, static_cast<std::size_t>(h->argument));
                next += static_cast<std::size_t>(h->argument);
            } else {
                while (!break_at(encoded, next)) {
                    auto const chunk = raw_head_read(encoded, next);
                    if (!chunk) [[unlikely]]
                        return std::unexpected(chunk.error());
                    if (chunk->major != h->major || chunk->info == std::to_underlying(additional_information::indefinite_length) ||
                        encoded.size() - chunk->at < chunk->argument) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                    content += encoded.substr(chunk->at, static_cast<std::size_t>(chunk->argument));
                    next = chunk->at + static_cast<std::size_t>(chunk->argument);
                }
                ++next;
            }
            if (auto const r = head_append(out, h->major, content.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            out += content;
            return next;
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            std::vector<std::pair<std::string, std::string>> items;
            for (std::uint64_t i = 0; indefinite ? !break_at(encoded, next) : i < h->argument; ++i) {
                std::pair<std::string, std::string> item;
                auto const first = canonical_append(item.first, encoded, next, depth + 1, depth_max);
                if (!first) [[unlikely]]
                    return first;
                next = *first;
                if (map) {
                    auto const second = canonical_append(item.second, encoded, next, depth + 1, depth_max);
                    if (!second) [[unlikely]]
                        return second;
                    next = *second;
                }
                items.push_back(std::move(item));
            }
            if (indefinite)
                ++next;
            if (map) {
                std::ranges::sort(items);
                if (std::ranges::adjacent_find(items, {}, &std::pair<std::string, std::string>::first) != items.end()) [[unlikely]]
                    return std::unexpected(error::duplicate_key);
            }
            if (auto const r = head_append(out, h->major, items.size(), no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            for (auto const &[first, second] : items)
                out += first + second;
            return next;
        }
        case major_type::tag:
            if (indefinite) [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (auto const r = head_append(out, major_type::tag, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return canonical_append(out, encoded, next, depth + 1, depth_max);
        default:
            break;
        }
        constexpr std::uint8_t half = std::to_underlying(simple_float_information::half_precision_float);
        constexpr std::uint8_t single = std::to_underlying(simple_float_information::single_precision_float);
        constexpr std::uint8_t twice = std::to_underlying(simple_float_information::double_precision_float);
        if (h->info < half) {
            if (h->info == std::to_underlying(simple_float_information::simple_value_follows) && h->argument < simple_value_one_byte_min)
                [[unlikely]]
                return std::unexpected(error::syntax_error);
            if (auto const r = head_append(out, major_type::simple_float, h->argument, no_indicator); !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (h->info > twice) [[unlikely]]
            return std::unexpected(error::syntax_error);
        std::uint64_t sign;
        std::uint64_t exponent_all_ones;
        std::uint64_t significand;
        double value;
        if (h->info == half) {
            sign = h->argument >> 15;
            exponent_all_ones = (h->argument >> 10 & 0x1f) == 0x1f;
            significand = (h->argument & 0x3ff) << 42;
            value = static_cast<double>(float_decode_binary16(static_cast<std::uint16_t>(h->argument)));
        } else if (h->info == single) {
            sign = h->argument >> 31;
            exponent_all_ones = (h->argument >> 23 & 0xff) == 0xff;
            significand = (h->argument & 0x7fffff) << 29;
            value = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(h->argument)));
        } else {
            sign = h->argument >> 63;
            exponent_all_ones = (h->argument >> 52 & 0x7ff) == 0x7ff;
            significand = h->argument & 0xfffffffffffff;
            value = std::bit_cast<double>(h->argument);
        }
        if (exponent_all_ones && significand != 0) {
            if (auto const r = head_append(out, major_type::simple_float, sign << 63 | std::uint64_t{0x7ff} << 52 | significand, twice);
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return next;
        }
        if (auto const r = float_append(out, value == 0 ? 0.0 : value, no_indicator); !r) [[unlikely]]
            return std::unexpected(r.error());
        return next;
    }

    static constexpr std::expected<query, error> query_parse(std::string_view const text, bool const literals,
                                                             std::size_t const depth_max)
    {
        query q;
        if (text.empty() || text.front() != '$') [[unlikely]]
            return std::unexpected(error::invalid_path);
        std::size_t at = 1;
        auto const name_first = [](char const c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || static_cast<unsigned char>(c) >= 0x80;
        };
        for (;;) {
            std::size_t const segment = blank_end(text, at);
            if (segment == text.size()) {
                if (segment != at) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                break;
            }
            at = segment;
            std::size_t const key_at = q.keys.size();
            if (text.at(at) == '.') {
                ++at;
                if (at < text.size() && text.at(at) == '*') {
                    q.selectors.push_back({selector::kind::wildcard, 0, 0, 0});
                    ++at;
                    continue;
                }
                std::size_t end = at;
                while (end < text.size() && (name_first(text.at(end)) || (end != at && digit(text.at(end)))))
                    ++end;
                if (end == at) [[unlikely]]
                    return std::unexpected(error::invalid_path);
                if (auto const r = head_append(q.keys, major_type::text_string, end - at, no_indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
                q.keys += text.substr(at, end - at);
                q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                at = end;
                continue;
            }
            if (text.at(at) != '[') [[unlikely]]
                return std::unexpected(error::invalid_path);
            at = blank_end(text, at + 1);
            if (at >= text.size()) [[unlikely]]
                return std::unexpected(error::invalid_path);
            char const c = text.at(at);
            std::size_t end;
            if (c == '*') {
                q.selectors.push_back({selector::kind::wildcard, 0, 0, 0});
                end = at + 1;
            } else if (c == '\'' || c == '"') {
                std::string name;
                auto const next = quoted_parse(text, at, name);
                if (!next) [[unlikely]]
                    return std::unexpected(next.error());
                if (auto const r = head_append(q.keys, major_type::text_string, name.size(), no_indicator); !r) [[unlikely]]
                    return std::unexpected(r.error());
                q.keys += name;
                q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                end = *next;
            } else {
                std::size_t digits_at = at + (c == '-' ? 1 : 0);
                std::size_t digits_end = digits_at;
                while (digits_end < text.size() && digit(text.at(digits_end)))
                    ++digits_end;
                std::size_t const close = blank_end(text, digits_end);
                bool const integer = digits_end != digits_at && digits_end - digits_at <= 16 && close < text.size() &&
                                     text.at(close) == ']' &&
                                     !(text.at(digits_at) == '0' && (digits_end > digits_at + 1 || c == '-'));
                if (integer) {
                    std::int64_t value = 0;
                    for (char const d : text.substr(digits_at, digits_end - digits_at))
                        value = value * 10 + (d - '0');
                    constexpr std::int64_t exact_max = (std::int64_t{1} << 53) - 1;
                    if (value > exact_max) [[unlikely]]
                        return std::unexpected(error::invalid_path);
                    q.selectors.push_back({selector::kind::index, 0, 0, c == '-' ? -value : value});
                    end = digits_end;
                } else if (literals) {
                    std::string literal;
                    auto const next = literal_parse(text, at, literal, 0, depth_max);
                    if (!next) [[unlikely]]
                        return std::unexpected(next.error());
                    if (auto const r = canonical_append(q.keys, literal, 0, 0, depth_max); !r) [[unlikely]]
                        return std::unexpected(r.error());
                    q.selectors.push_back({selector::kind::key, key_at, q.keys.size() - key_at, 0});
                    end = *next;
                } else [[unlikely]] {
                    return std::unexpected(error::invalid_path);
                }
            }
            at = blank_end(text, end);
            if (at >= text.size() || text.at(at) != ']') [[unlikely]]
                return std::unexpected(error::invalid_path);
            ++at;
        }
        if (q.selectors.size() > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        return q;
    }

    template <std::size_t DepthMax>
    static result<lazy, error> key_find(lazy const &node, std::string_view key);

    template <std::size_t DepthMax>
    static std::expected<std::size_t, error> shared_resolve(document const &doc, std::size_t at);

    static constexpr std::expected<std::size_t, error> literal_end(std::string_view const literal, std::size_t const at,
                                                                   std::size_t const depth, std::size_t const depth_max)
    {
        if (depth > depth_max) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = raw_head_read(literal, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::size_t next = h->at;
        switch (h->major) {
        case major_type::byte_string:
        case major_type::text_string:
            if (literal.size() - next < h->argument) [[unlikely]]
                return std::unexpected(error::too_little_data);
            return next + static_cast<std::size_t>(h->argument);
        case major_type::array:
        case major_type::map:
            for (std::uint64_t i = 0; i < (h->major == major_type::map ? 2 : 1) * h->argument; ++i) {
                auto const end = literal_end(literal, next, depth + 1, depth_max);
                if (!end) [[unlikely]]
                    return end;
                next = *end;
            }
            return next;
        case major_type::tag:
            return literal_end(literal, next, depth + 1, depth_max);
        default:
            return next;
        }
    }

    struct float_key {
        bool nan;
        std::uint64_t widened;
        double value;
    };

    static float_key float_key_of(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info == std::to_underlying(simple_float_information::half_precision_float))
            return {(argument >> 10 & 0x1f) == 0x1f && (argument & 0x3ff) != 0,
                    (argument >> 15) << 63 | (argument & 0x3ff) << 42,
                    static_cast<double>(float_decode_binary16(static_cast<std::uint16_t>(argument)))};
        if (info == std::to_underlying(simple_float_information::single_precision_float))
            return {(argument >> 23 & 0xff) == 0xff && (argument & 0x7fffff) != 0,
                    (argument >> 31) << 63 | (argument & 0x7fffff) << 29,
                    static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(argument)))};
        return {(argument >> 52 & 0x7ff) == 0x7ff && (argument & 0xfffffffffffff) != 0,
                (argument >> 63) << 63 | (argument & 0xfffffffffffff), std::bit_cast<double>(argument)};
    }

    template <std::size_t DepthMax>
    static std::expected<std::size_t, error> document_item_end(document &doc, std::size_t at, std::size_t depth);

    template <std::size_t DepthMax>
    static std::expected<bool, error> key_equal(document &doc, std::size_t at, std::string_view literal, std::size_t literal_at,
                                                std::size_t depth);

    template <std::size_t DepthMax, class Binding>
    static result<typename Binding::value, error> query_walk(Binding &binding, std::span<selector const> selectors,
                                                             std::string_view keys, lazy const &root);

private:

    struct resolved {
        std::shared_ptr<document> source;
        head h;
        decoder d;
    };

    static std::expected<resolved, error> container_resolve(std::shared_ptr<document> source, std::size_t offset)
    {
        std::vector<std::size_t> followed;
        for (;;) {
            decoder d{source->encoded.substr(offset)};
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                return resolved{source, *h, d};
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                source->mark(d);
                offset = source->encoded.size() - d.encoded.size();
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
    friend std::expected<std::size_t, error> doc_end(std::string_view encoded);

    template <std::size_t DepthMax>
    friend result<std::string, error> inspect(std::string_view encoded);

    static std::string encoding_indicator(std::uint8_t const info, std::uint64_t const argument)
    {
        if (info < std::to_underlying(additional_information::one_byte_argument))
            return {};
        std::uint8_t const preferred =
            argument < std::to_underlying(additional_information::one_byte_argument)
                ? 0
                : static_cast<std::uint8_t>(std::to_underlying(additional_information::one_byte_argument) +
                                            std::countr_zero(std::bit_ceil(static_cast<unsigned>((std::bit_width(argument) + 7) / 8))));
        if (info == preferred)
            return {};
        return {'_', static_cast<char>('0' + info - std::to_underlying(additional_information::one_byte_argument))};
    }

    static std::string decimal_of(std::uint64_t const n)
    {
        std::array<char, std::numeric_limits<std::uint64_t>::digits10 + 1> text;
        auto const end = std::to_chars(text.data(), std::to_address(text.end()), n).ptr;
        return std::string(text.data(), end);
    }

    static std::string hex_of(std::string_view const bytes)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out;
        out.reserve(2 * bytes.size());
        for (char const c : bytes) {
            out.push_back(digits.at(static_cast<std::uint8_t>(c) >> 4));
            out.push_back(digits.at(static_cast<std::uint8_t>(c) & 0xf));
        }
        return out;
    }

    static std::string quoted_of(std::string_view const text)
    {
        constexpr std::string_view digits = "0123456789abcdef";
        std::string out = "\"";
        for (char const c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<std::uint8_t>(c) < 0x20) {
                    out += "\\u00";
                    out.push_back(digits.at(static_cast<std::uint8_t>(c) >> 4));
                    out.push_back(digits.at(static_cast<std::uint8_t>(c) & 0xf));
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
        out.push_back('"');
        return out;
    }

    static std::string number_of(double const value)
    {
        if (std::isinf(value))
            return value > 0 ? "Infinity" : "-Infinity";
        std::array<char, 400> text;
        double const magnitude = std::fabs(value);
        auto const end = std::to_chars(text.data(), std::to_address(text.end()), value,
                                       magnitude == 0 || (magnitude >= 1e-7 && magnitude < 1e21) ? std::chars_format::fixed
                                                                                               : std::chars_format::scientific)
                             .ptr;
        std::string out(text.data(), end);
        std::size_t const e = out.find('e');
        std::string mantissa = out.substr(0, e);
        if (mantissa.find('.') == std::string::npos)
            mantissa += ".0";
        if (e == std::string::npos)
            return mantissa;
        std::string_view exponent = std::string_view(out).substr(e + 1);
        char const sign = exponent.front();
        exponent.remove_prefix(1);
        exponent.remove_prefix(std::min(exponent.find_first_not_of('0'), exponent.size() - 1));
        return mantissa + "e" + sign + std::string(exponent);
    }

    static std::string float_of(std::uint8_t const info, std::uint64_t const argument)
    {
        constexpr std::array<std::uint64_t, 3> quiet_nan{0x7e00, 0x7fc00000, 0x7ff8000000000000};
        std::size_t const width = info - std::to_underlying(simple_float_information::half_precision_float);
        double value;
        if (info == std::to_underlying(simple_float_information::half_precision_float))
            value = static_cast<double>(float_decode_binary16(static_cast<std::uint16_t>(argument)));
        else if (info == std::to_underlying(simple_float_information::single_precision_float))
            value = static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(argument)));
        else
            value = std::bit_cast<double>(argument);
        std::string const indicator =
            width == 0 ? std::string{} : std::string{'_', static_cast<char>('1' + width)};
        if (std::isnan(value)) {
            if (argument == quiet_nan.at(width))
                return "NaN" + indicator;
            std::string bytes(std::size_t{2} << width, '\0');
            for (std::size_t i = 0; i < bytes.size(); ++i)
                bytes.at(i) = static_cast<char>(argument >> (8 * (bytes.size() - 1 - i)));
            return "float'" + hex_of(bytes) + "'";
        }
        std::size_t const preferred = std::to_underlying(preferred_float_info(value)) -
                                      std::to_underlying(simple_float_information::half_precision_float);
        return number_of(value) + (width == preferred ? std::string{} : indicator);
    }

    struct diagnostic_head {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    static std::expected<diagnostic_head, error> diagnostic_head_decode(decoder &d)
    {
        if (d.encoded.empty()) [[unlikely]]
            return std::unexpected(error::too_little_data);
        auto const initial = static_cast<std::uint8_t>(d.encoded.front());
        auto const major = static_cast<major_type>(initial >> 5);
        std::uint8_t const info = initial & 0x1f;
        if (info != std::to_underlying(additional_information::indefinite_length))
            return d.head_decode().transform([](head const h) { return diagnostic_head{h.major, h.info, h.argument}; });
        if (major == major_type::unsigned_integer || major == major_type::negative_integer || major == major_type::tag)
            [[unlikely]]
            return std::unexpected(error::syntax_error);
        d.encoded.remove_prefix(1);
        return diagnostic_head{major, info, 0};
    }

    static bool break_found(decoder &d)
    {
        if (d.encoded.empty() || static_cast<std::uint8_t>(d.encoded.front()) != 0xff)
            return false;
        d.encoded.remove_prefix(1);
        return true;
    }

    template <std::size_t DepthMax>
    static std::expected<void, error> diagnostic_write(std::string &out, decoder &d, std::size_t const depth)
    {
        if (depth > DepthMax) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        auto const h = diagnostic_head_decode(d);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool const indefinite = h->info == std::to_underlying(additional_information::indefinite_length);
        switch (h->major) {
        case major_type::unsigned_integer:
            out += decimal_of(h->argument) + encoding_indicator(h->info, h->argument);
            return {};
        case major_type::negative_integer:
            out += h->argument == std::numeric_limits<std::uint64_t>::max() ? std::string("-18446744073709551616")
                                                                            : "-" + decimal_of(h->argument + 1);
            out += encoding_indicator(h->info, h->argument);
            return {};
        case major_type::byte_string:
        case major_type::text_string: {
            bool const text = h->major == major_type::text_string;
            if (!indefinite) {
                auto const s = text ? d.text_string_decode(h->argument) : d.byte_string_decode(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                out += text ? quoted_of(*s) : "h'" + hex_of(*s) + "'";
                out += encoding_indicator(h->info, h->argument);
                return {};
            }
            if (break_found(d)) {
                out += text ? "\"\"_" : "''_";
                return {};
            }
            out += "(_ ";
            for (bool first = true; !break_found(d); first = false) {
                if (!first)
                    out += ", ";
                if (d.encoded.empty() || static_cast<major_type>(static_cast<std::uint8_t>(d.encoded.front()) >> 5) != h->major ||
                    (static_cast<std::uint8_t>(d.encoded.front()) & 0x1f) == std::to_underlying(additional_information::indefinite_length))
                    [[unlikely]]
                    return std::unexpected(d.encoded.empty() ? error::too_little_data : error::syntax_error);
                if (auto const r = diagnostic_write<DepthMax>(out, d, depth + 1); !r) [[unlikely]]
                    return r;
            }
            out += ")";
            return {};
        }
        case major_type::array:
        case major_type::map: {
            bool const map = h->major == major_type::map;
            out += map ? "{" : "[";
            std::string const indicator = indefinite ? std::string("_") : encoding_indicator(h->info, h->argument);
            if (!indicator.empty())
                out += indicator + " ";
            for (std::uint64_t i = 0; indefinite ? !break_found(d) : i < h->argument; ++i) {
                if (i != 0)
                    out += ", ";
                if (auto const r = diagnostic_write<DepthMax>(out, d, depth + 1); !r) [[unlikely]]
                    return r;
                if (map) {
                    out += ": ";
                    if (auto const r = diagnostic_write<DepthMax>(out, d, depth + 1); !r) [[unlikely]]
                        return r;
                }
            }
            out += map ? "}" : "]";
            return {};
        }
        case major_type::tag: {
            out += decimal_of(h->argument) + encoding_indicator(h->info, h->argument) + "(";
            if (auto const r = diagnostic_write<DepthMax>(out, d, depth + 1); !r) [[unlikely]]
                return r;
            out += ")";
            return {};
        }
        default:
            break;
        }
        switch (h->info) {
        case std::to_underlying(simple_value::false_value):
            out += "false";
            return {};
        case std::to_underlying(simple_value::true_value):
            out += "true";
            return {};
        case std::to_underlying(simple_value::null):
            out += "null";
            return {};
        case std::to_underlying(simple_value::undefined):
            out += "undefined";
            return {};
        case std::to_underlying(simple_float_information::simple_value_follows):
            if (h->argument < simple_value_one_byte_min) [[unlikely]]
                return std::unexpected(error::syntax_error);
            out += "simple(" + decimal_of(h->argument) + ")";
            return {};
        case std::to_underlying(simple_float_information::half_precision_float):
        case std::to_underlying(simple_float_information::single_precision_float):
        case std::to_underlying(simple_float_information::double_precision_float):
            out += float_of(h->info, h->argument);
            return {};
        [[unlikely]] case std::to_underlying(additional_information::indefinite_length):
            return std::unexpected(error::syntax_error);
        default:
            out += "simple(" + decimal_of(h->argument) + ")";
            return {};
        }
    }

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
            if (d.encoded.size() >= 9) {
                auto const initial = static_cast<std::uint8_t>(d.encoded.front());
                auto const major = static_cast<major_type>(initial >> 5);
                std::uint8_t const info = initial & 0x1f;
                if (info <= std::to_underlying(additional_information::eight_byte_argument) &&
                    major != major_type::tag &&
                    (major != major_type::simple_float || info != std::to_underlying(additional_information::one_byte_argument))) {
                    bool const immediate = info < std::to_underlying(additional_information::one_byte_argument);
                    std::size_t const size =
                        immediate ? 0
                                  : std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
                    std::uint64_t argument = info;
                    if (info == std::to_underlying(additional_information::one_byte_argument)) {
                        argument = static_cast<std::uint8_t>(d.encoded.at(1));
                    } else if (!immediate) {
                        argument = unsigned_read<std::uint64_t>(std::span<char const>(d.encoded.substr(1, 8)).first<8>()) >>
                                   ((64 - 8 * size) & 63);
                    }
                    d.encoded.remove_prefix(1 + size);
                    if (major == major_type::byte_string || major == major_type::text_string) {
                        if (argument > d.encoded.size()) [[unlikely]]
                            return std::unexpected(error::too_little_data);
                        d.encoded.remove_prefix(static_cast<std::size_t>(argument));
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

#ifdef __cpp_impl_reflection
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
#ifdef __SIZEOF_INT128__
    static constexpr bool is_wide_integer = std::same_as<U, int128> || std::same_as<U, uint128>;
#else
    static constexpr bool is_wide_integer = false;
#endif

    template <class U>
    static constexpr bool is_byte = std::same_as<U, std::byte> || std::same_as<U, unsigned char>;

    template <class U>
    static constexpr bool is_byte_container = requires {
        typename U::value_type;
        requires is_byte<typename U::value_type>;
        requires std::same_as<U, std::vector<typename U::value_type>> || is_std_array<U>;
    };

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

    template <class U, class V>
    static std::expected<void, error> integer_read(decoder &d, V &out)
    {
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major == major_type::unsigned_integer) {
            if (!std::in_range<U>(h->argument)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            out = static_cast<V>(static_cast<U>(h->argument));
            return {};
        }
        if (h->major != major_type::negative_integer) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if constexpr (std::is_unsigned_v<U>) {
            return std::unexpected(error::number_out_of_range);
        } else {
            if (h->argument > static_cast<std::uint64_t>(std::numeric_limits<U>::max())) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            out = static_cast<V>(static_cast<U>(-1 - static_cast<U>(h->argument)));
            return {};
        }
    }

    template <class U>
    static bool head_accepted(head const &h)
    {
        if constexpr (std::same_as<U, bool>)
            return h.major == major_type::simple_float && (h.info == std::to_underlying(simple_value::false_value) ||
                                                           h.info == std::to_underlying(simple_value::true_value));
        else if constexpr (std::same_as<U, std::nullptr_t>)
            return h.major == major_type::simple_float && h.info == std::to_underlying(simple_value::null);
        else if constexpr (std::same_as<U, simple_value>)
            return h.major == major_type::simple_float &&
                   h.info <= std::to_underlying(simple_float_information::simple_value_follows);
        else if constexpr (std::is_floating_point_v<U>)
            return h.major == major_type::simple_float &&
                   h.info >= std::to_underlying(simple_float_information::half_precision_float) &&
                   h.info <= std::to_underlying(simple_float_information::double_precision_float);
        else if constexpr (is_wide_integer<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer ||
                   (h.major == major_type::tag && (h.argument == std::to_underlying(tag_number::unsigned_bignum) ||
                                                   h.argument == std::to_underlying(tag_number::negative_bignum)));
        else if constexpr (std::is_unsigned_v<U>)
            return h.major == major_type::unsigned_integer;
        else if constexpr (std::is_integral_v<U> || std::is_enum_v<U>)
            return h.major == major_type::unsigned_integer || h.major == major_type::negative_integer;
        else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>)
            return h.major == major_type::text_string;
        else if constexpr (std::same_as<U, std::span<std::byte const>> || is_byte_container<U>)
            return h.major == major_type::byte_string;
        else if constexpr (is_optional<U>)
            return (h.major == major_type::simple_float && h.info == std::to_underlying(simple_value::null)) ||
                   head_accepted<typename U::value_type>(h);
        else if constexpr (is_tagged<U>)
            return h.major == major_type::tag && h.argument == U::number;
        else if constexpr (is_std_variant<U>)
            return false;
        else if constexpr (is_std_tuple<U> || is_std_array<U>)
            return h.major == major_type::array && h.argument == std::tuple_size_v<U>;
        else if constexpr (is_map<U>)
            return h.major == major_type::map;
        else if constexpr (requires { typename U::value_type; std::declval<U &>().push_back(std::declval<typename U::value_type>()); })
            return h.major == major_type::array;
        else
            return h.major == major_type::map;
    }

#ifdef __SIZEOF_INT128__
    template <class U>
    static std::expected<void, error> wide_integer_read(decoder &d, U &out)
    {
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        bool negative = h->major == major_type::negative_integer;
        uint128 magnitude = h->argument;
        if (h->major == major_type::tag) {
            if (h->argument != std::to_underlying(tag_number::unsigned_bignum) &&
                h->argument != std::to_underlying(tag_number::negative_bignum)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            negative = h->argument == std::to_underlying(tag_number::negative_bignum);
            auto const b = d.head_decode();
            if (!b) [[unlikely]]
                return std::unexpected(b.error());
            if (b->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            auto const bytes = d.byte_string_decode(b->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::string_view const digits = magnitude_without_leading_zeros(*bytes);
            if (digits.size() > sizeof(uint128)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            magnitude = 0;
            for (char const c : digits)
                magnitude = magnitude << 8 | static_cast<std::uint8_t>(c);
        } else if (h->major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if constexpr (std::same_as<U, uint128>) {
            if (negative) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            out = magnitude;
        } else {
            if (magnitude > static_cast<uint128>(std::numeric_limits<int128>::max())) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            out = negative ? -1 - static_cast<int128>(magnitude) : static_cast<int128>(magnitude);
        }
        return {};
    }
#endif

    template <std::size_t DepthMax, class U, std::size_t I = 0>
    static std::expected<void, error> variant_read(sharing_decoder &d, U &out, head const &h, std::size_t const depth)
    {
        if constexpr (I == std::variant_size_v<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            using A = std::variant_alternative_t<I, U>;
            if (head_accepted<A>(h))
                return generic_value_read<DepthMax>(d, out.template emplace<I>(), depth);
            return variant_read<DepthMax, U, I + 1>(d, out, h, depth);
        }
    }

    template <std::size_t DepthMax, class U>
    static std::expected<void, error> generic_read(sharing_decoder &d, U &out, std::size_t const depth)
    {
        for (;;) {
            std::size_t const tag_at = d.message.size() - d.encoded.size();
            decoder look = d;
            auto const h = look.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                break;
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                d.encoded = look.encoded;
                d.mark(d);
                continue;
            }
            if (h->argument != std::to_underlying(tag_number::sharedref))
                break;
            d.encoded = look.encoded;
            auto const target = d.reference_follow(tag_at);
            if (!target) [[unlikely]]
                return std::unexpected(target.error());
            std::string_view const rest = d.encoded;
            d.encoded = d.message.substr(*target);
            auto const r = generic_value_read<DepthMax>(d, out, depth + 1);
            d.encoded = rest;
            return r;
        }
        return generic_value_read<DepthMax>(d, out, depth);
    }

    template <std::size_t DepthMax, class U>
    static std::expected<void, error> generic_value_read(sharing_decoder &d, U &out, std::size_t const depth)
    {
        if (depth > DepthMax) [[unlikely]]
            return std::unexpected(error::nesting_depth_exceeded);
        if constexpr (std::same_as<U, bool>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::simple_float || (h->info != std::to_underlying(simple_value::false_value) &&
                                                         h->info != std::to_underlying(simple_value::true_value)))
                [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out = h->info == std::to_underlying(simple_value::true_value);
            return {};
        } else if constexpr (std::same_as<U, simple_value>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (!head_accepted<U>(*h)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            if (h->info == std::to_underlying(simple_float_information::simple_value_follows) &&
                h->argument < simple_value_one_byte_min) [[unlikely]]
                return std::unexpected(error::syntax_error);
            out = static_cast<simple_value>(h->argument);
            return {};
        } else if constexpr (is_wide_integer<U>) {
            return wide_integer_read(d, out);
        } else if constexpr (is_std_variant<U>) {
            decoder probe = d;
            auto const h = probe.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            return variant_read<DepthMax>(d, out, *h, depth);
        } else if constexpr (std::is_enum_v<U>) {
            return integer_read<std::underlying_type_t<U>>(d, out);
        } else if constexpr (std::is_integral_v<U>) {
            return integer_read<U>(d, out);
        } else if constexpr (std::is_floating_point_v<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::simple_float) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            switch (static_cast<simple_float_information>(h->info)) {
            case simple_float_information::half_precision_float:
                out = static_cast<U>(float_decode_binary16(static_cast<std::uint16_t>(h->argument)));
                return {};
            case simple_float_information::single_precision_float:
                out = static_cast<U>(std::bit_cast<float>(static_cast<std::uint32_t>(h->argument)));
                return {};
            case simple_float_information::double_precision_float:
                out = static_cast<U>(std::bit_cast<double>(h->argument));
                return {};
            [[unlikely]] default:
                return std::unexpected(error::incorrect_type);
            }
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::simple_float || h->info != std::to_underlying(simple_value::null)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return {};
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::text_string) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            auto const text = d.text_string_decode(h->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
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
                out.resize(view.size());
                std::ranges::transform(view, out.begin(), [](std::byte const b) { return static_cast<typename U::value_type>(b); });
            }
            return {};
        } else if constexpr (is_optional<U>) {
            if (!d.encoded.empty() && static_cast<std::uint8_t>(d.encoded.front()) ==
                                        (std::to_underlying(major_type::simple_float) << 5 |
                                         std::to_underlying(simple_value::null))) {
                d.encoded.remove_prefix(1);
                out.reset();
                return {};
            }
            return generic_read<DepthMax>(d, out.emplace(), depth);
        } else if constexpr (is_tagged<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag || h->argument != U::number) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            return generic_read<DepthMax>(d, out.content, depth + 1);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array || h->argument != std::tuple_size_v<U>) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            std::expected<void, error> r;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, std::tuple_size_v<U>))) {
                if (r)
                    r = generic_read<DepthMax>(d, std::get<i>(out), depth + 1);
            }
            return r;
        } else if constexpr (is_map<U>) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::map) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                typename U::key_type key{};
                if (auto const r = generic_read<DepthMax>(d, key, depth + 1); !r) [[unlikely]]
                    return r;
                typename U::mapped_type value{};
                if (auto const r = generic_read<DepthMax>(d, value, depth + 1); !r) [[unlikely]]
                    return r;
                out.insert_or_assign(std::move(key), std::move(value));
            }
            return {};
        } else if constexpr (requires { out.push_back(std::declval<typename U::value_type>()); }) {
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::array) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            out.clear();
            out.reserve(std::min<std::uint64_t>(h->argument, d.encoded.size()));
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                if (auto const r = generic_read<DepthMax>(d, out.emplace_back(), depth + 1); !r) [[unlikely]]
                    return r;
            }
            return {};
        } else {
            return struct_read<DepthMax>(d, out, depth);
        }
    }

    template <class U, std::size_t I>
    static bool key_matches(head const &k, std::string_view const text)
    {
        if constexpr (has_integer_keys<U>) {
            constexpr std::int64_t key = U::keys.at(I);
            if constexpr (key >= 0)
                return k.major == major_type::unsigned_integer && k.argument == static_cast<std::uint64_t>(key);
            else
                return k.major == major_type::negative_integer && k.argument == static_cast<std::uint64_t>(-1 - key);
        } else {
            constexpr std::string_view name = key_of(members_of<U>()[I]);
            return k.major == major_type::text_string && text == name;
        }
    }

    template <std::size_t DepthMax, class U>
    static std::expected<void, error> struct_read(sharing_decoder &d, U &out, std::size_t const depth)
    {
        static constexpr auto members = members_of<U>();
        constexpr std::size_t count = members.size();
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            std::size_t key_at = d.message.size() - d.encoded.size();
            auto k = d.head_decode();
            while (k && k->major == major_type::tag && k->argument == std::to_underlying(tag_number::shareable)) {
                d.mark(d);
                key_at = d.message.size() - d.encoded.size();
                k = d.head_decode();
            }
            decoder referenced{};
            bool const indirect = k && k->major == major_type::tag && k->argument == std::to_underlying(tag_number::sharedref);
            if (indirect) {
                auto const target = d.reference_follow(key_at);
                if (!target) [[unlikely]]
                    return std::unexpected(target.error());
                referenced = decoder{d.message.substr(*target)};
                k = referenced.head_decode();
            }
            decoder &from = indirect ? referenced : d;
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::string_view text;
            if (k->major == major_type::text_string) {
                auto const t = from.text_string_decode(k->argument);
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
                }
            }
            bool matched = false;
            std::expected<void, error> r;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, count))) {
                if (!matched && key_matches<U, i>(*k, text)) {
                    matched = true;
                    found.set(i);
                    r = generic_read<DepthMax>(d, out.[:members[i]:], depth + 1);
                }
            }
            if (!matched) {
                if (auto const s = item_skip<DepthMax>(d, d, depth + 1); !s) [[unlikely]]
                    return s;
            } else if (!r) [[unlikely]] {
                return r;
            }
        }
        template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, count))) {
            using M = std::remove_cvref_t<decltype(out.[:members[i]:])>;
            if constexpr (!is_optional<M>)
                if (!found.test(i)) [[unlikely]]
                    return std::unexpected(error::key_not_found);
        }
        return {};
    }

    template <class U>
    static bool member_present(U const &value)
    {
        if constexpr (is_optional<U>)
            return value.has_value();
        else
            return true;
    }

    static std::size_t float_size(double const value)
    {
        switch (preferred_float_info(value)) {
        case simple_float_information::half_precision_float:
            return initial_byte_size + sizeof(std::uint16_t);
        case simple_float_information::single_precision_float:
            return initial_byte_size + sizeof(std::uint32_t);
        default:
            return initial_byte_size + sizeof(std::uint64_t);
        }
    }

    template <class U>
    static std::size_t generic_size(U const &value)
    {
        if constexpr (std::same_as<U, bool> || std::same_as<U, std::nullptr_t>) {
            return initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return head_size(std::to_underlying(value));
#ifdef __SIZEOF_INT128__
        } else if constexpr (is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? static_cast<uint128>(-1 - value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return head_size(static_cast<std::uint64_t>(magnitude));
            std::size_t const digits = sizeof(uint128) - std::countl_zero(magnitude) / 8;
            return initial_byte_size + head_size(digits) + digits;
#endif
        } else if constexpr (is_std_variant<U>) {
            return std::visit([](auto const &e) { return generic_size(e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return generic_size(std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>)
                return head_size(value < 0 ? static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value))
                                           : static_cast<std::uint64_t>(value));
            else
                return head_size(value);
        } else if constexpr (std::is_floating_point_v<U>) {
            return float_size(static_cast<double>(value));
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view> ||
                             std::same_as<U, std::span<std::byte const>> || is_byte_container<U>) {
            return head_size(value.size()) + value.size();
        } else if constexpr (is_optional<U>) {
            return value ? generic_size(*value) : initial_byte_size;
        } else if constexpr (is_tagged<U>) {
            return head_size(U::number) + generic_size(value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            return head_size(std::tuple_size_v<U>) +
                   std::apply([](auto const &...e) { return (std::size_t{0} + ... + generic_size(e)); }, value);
        } else if constexpr (is_map<U>) {
            std::size_t size = head_size(value.size());
            for (auto const &[k, v] : value)
                size += generic_size(k) + generic_size(v);
            return size;
        } else if constexpr (requires { value.size(); typename U::value_type; }) {
            std::size_t size = head_size(value.size());
            for (auto const &e : value)
                size += generic_size(e);
            return size;
        } else {
            static constexpr auto members = members_of<U>();
            std::size_t size = 0;
            std::size_t present = 0;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                auto const &m = value.[:members[i]:];
                if (member_present(m)) {
                    ++present;
                    if constexpr (has_integer_keys<U>) {
                        constexpr std::int64_t key = U::keys.at(i);
                        size += generic_size(key);
                    } else {
                        constexpr std::string_view name = key_of(members[i]);
                        size += head_size(name.size()) + name.size();
                    }
                    if constexpr (is_optional<std::remove_cvref_t<decltype(m)>>)
                        size += generic_size(*m);
                    else
                        size += generic_size(m);
                }
            }
            return head_size(present) + size;
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
        constexpr auto simple = [](std::uint8_t const info) {
            return static_cast<char>(std::to_underlying(major_type::simple_float) << 5 | info);
        };
        if constexpr (std::same_as<U, bool>) {
            out.subspan(at).front() = simple(value ? std::to_underlying(simple_value::true_value)
                                                   : std::to_underlying(simple_value::false_value));
            return at + initial_byte_size;
        } else if constexpr (std::same_as<U, std::nullptr_t>) {
            out.subspan(at).front() = simple(std::to_underlying(simple_value::null));
            return at + initial_byte_size;
        } else if constexpr (std::same_as<U, simple_value>) {
            return at + head_write(out, at, major_type::simple_float, std::to_underlying(value));
#ifdef __SIZEOF_INT128__
        } else if constexpr (is_wide_integer<U>) {
            bool negative = false;
            if constexpr (std::same_as<U, int128>)
                negative = value < 0;
            uint128 const magnitude =
                negative ? static_cast<uint128>(-1 - value) : static_cast<uint128>(value);
            if (magnitude <= std::numeric_limits<std::uint64_t>::max())
                return at + head_write(out, at,
                                       negative ? major_type::negative_integer : major_type::unsigned_integer,
                                       static_cast<std::uint64_t>(magnitude));
            std::size_t const digits = sizeof(uint128) - std::countl_zero(magnitude) / 8;
            at += head_write(out, at, major_type::tag,
                             std::to_underlying(negative ? tag_number::negative_bignum : tag_number::unsigned_bignum));
            at += head_write(out, at, major_type::byte_string, digits);
            auto const bytes = big_endian(magnitude);
            return bytes_write(out, at, std::span<char const>(bytes).last(digits));
#endif
        } else if constexpr (is_std_variant<U>) {
            return std::visit([&](auto const &e) { return generic_write(out, at, e); }, value);
        } else if constexpr (std::is_enum_v<U>) {
            return generic_write(out, at, std::to_underlying(value));
        } else if constexpr (std::is_integral_v<U>) {
            if constexpr (std::is_signed_v<U>) {
                if (value < 0)
                    return at + head_write(out, at, major_type::negative_integer,
                                           static_cast<std::uint64_t>(-1 - static_cast<std::int64_t>(value)));
            }
            return at + head_write(out, at, major_type::unsigned_integer, static_cast<std::uint64_t>(value));
        } else if constexpr (std::is_floating_point_v<U>) {
            double const d = static_cast<double>(value);
            switch (preferred_float_info(d)) {
            case simple_float_information::half_precision_float:
                out.subspan(at).front() = simple(std::to_underlying(simple_float_information::half_precision_float));
                return bytes_write(out, at + initial_byte_size, big_endian(float_encode_binary16(static_cast<float>(d))));
            case simple_float_information::single_precision_float:
                out.subspan(at).front() = simple(std::to_underlying(simple_float_information::single_precision_float));
                return bytes_write(out, at + initial_byte_size, big_endian(std::bit_cast<std::uint32_t>(static_cast<float>(d))));
            default:
                out.subspan(at).front() = simple(std::to_underlying(simple_float_information::double_precision_float));
                return bytes_write(out, at + initial_byte_size, big_endian(std::bit_cast<std::uint64_t>(d)));
            }
        } else if constexpr (std::same_as<U, std::string> || std::same_as<U, std::string_view>) {
            at += head_write(out, at, major_type::text_string, value.size());
            return bytes_write(out, at, value);
        } else if constexpr (std::same_as<U, std::span<std::byte const>> || is_byte_container<U>) {
            at += head_write(out, at, major_type::byte_string, value.size());
            if constexpr (std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>)
                std::ranges::copy(value, std::as_writable_bytes(out.subspan(at, value.size())).begin());
            else
                std::ranges::transform(value, out.subspan(at, value.size()).begin(),
                                       [](auto const b) { return static_cast<char>(b); });
            return at + value.size();
        } else if constexpr (is_optional<U>) {
            if (!value) {
                out.subspan(at).front() = simple(std::to_underlying(simple_value::null));
                return at + initial_byte_size;
            }
            return generic_write(out, at, *value);
        } else if constexpr (is_tagged<U>) {
            at += head_write(out, at, major_type::tag, U::number);
            return generic_write(out, at, value.content);
        } else if constexpr (is_std_tuple<U> || is_std_array<U>) {
            at += head_write(out, at, major_type::array, std::tuple_size_v<U>);
            std::apply([&](auto const &...e) { ((at = generic_write(out, at, e)), ...); }, value);
            return at;
        } else if constexpr (is_map<U>) {
            at += head_write(out, at, major_type::map, value.size());
            for (auto const &[k, v] : value) {
                at = generic_write(out, at, k);
                at = generic_write(out, at, v);
            }
            return at;
        } else if constexpr (requires { value.size(); typename U::value_type; }) {
            at += head_write(out, at, major_type::array, value.size());
            for (auto const &e : value)
                at = generic_write(out, at, e);
            return at;
        } else {
            static constexpr auto members = members_of<U>();
            std::size_t present = 0;
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                if (member_present(value.[:members[i]:]))
                    ++present;
            }
            at += head_write(out, at, major_type::map, present);
            template for (constexpr std::size_t i : std::define_static_array(std::views::iota(std::size_t{0}, members.size()))) {
                auto const &m = value.[:members[i]:];
                if (member_present(m)) {
                    if constexpr (has_integer_keys<U>) {
                        constexpr std::int64_t key = U::keys.at(i);
                        at = generic_write(out, at, key);
                    } else {
                        constexpr std::string_view name = key_of(members[i]);
                        at += head_write(out, at, major_type::text_string, name.size());
                        at = bytes_write(out, at, name);
                    }
                    if constexpr (is_optional<std::remove_cvref_t<decltype(m)>>)
                        at = generic_write(out, at, *m);
                    else
                        at = generic_write(out, at, m);
                }
            }
            return at;
        }
    }
#endif
};

template <std::size_t DepthMax = 64, class Binding>
result<typename Binding::value, error> at_path(Binding &binding, std::string_view path, lazy const &l);

template <fixed_string Path, std::size_t DepthMax = 64, class Binding>
    requires(internal::query_parse(Path.view(), true, DepthMax).has_value())
result<typename Binding::value, error> at_path(Binding &binding, lazy const &l);

struct lazy {
    std::shared_ptr<internal::document> document;
    std::size_t offset;

    template <std::same_as<std::string> Encoded>
    static result<lazy> from(Encoded &&encoded);
    static result<lazy> from(std::string_view encoded);
    static result<lazy> from(std::shared_ptr<std::string const> encoded);
    static result<lazy> from(std::shared_ptr<void const> owner, std::string_view encoded);

    template <std::size_t DepthMax = 64>
    result<lazy> at(std::string_view key) const;

    template <std::size_t DepthMax = 64>
    result<lazy> at(std::int64_t index) const;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    result<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               owning_ref<T>, T>> get() const;

    template <std::size_t DepthMax = 64>
    result<lazy_elements<DepthMax>> elements() const;

    template <std::size_t DepthMax = 64>
    result<lazy_entries<DepthMax>> entries() const;
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
            internal::decoder d{document->encoded.substr(offset)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            offset = document->encoded.size() - d.encoded.size();
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
            internal::decoder d{document->encoded.substr(key)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            value = document->encoded.size() - d.encoded.size();
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
            internal::decoder d{document->encoded.substr(value)};
            if (auto const r = internal::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            key = document->encoded.size() - d.encoded.size();
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

#ifdef __cpp_impl_reflection
template <class Root>
consteval bool tags_registered()
{
    std::vector<std::uint64_t> numbers;
    for (std::meta::info const type : internal::packing_table_of<Root>()) {
        auto const number = internal::tag_number_of(type);
        if (!number)
            return false;
        numbers.push_back(*number);
    }
    std::ranges::sort(numbers);
    return std::ranges::adjacent_find(numbers) == numbers.end();
}

template <class T, class Root>
consteval std::size_t internal::fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = internal::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (internal::has_fixed_underlying_type<U>)
        return fixed_size<std::underlying_type_t<U>, Root>();
#ifdef __SIZEOF_INT128__
    else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>)
        return internal::head_size(std::to_underlying(internal::tag_number::negative_bignum)) +
               internal::head_size(sizeof(U)) + sizeof(U);
#endif
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
            return internal::head_size(n) + n * fixed_size<E, Root>();
    } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
        if constexpr (!std::meta::bases_of(^^U, std::meta::access_context::unchecked()).empty() ||
                      !internal::keys_unique(internal::data_members<U>())) {
            return no_fixed_size<U>();
        } else {
            std::size_t size = internal::head_size(*internal::tag_number_of(^^U)) +
                               internal::straight_reference_size<Root, U>() + internal::item_head;
            template for (constexpr auto m : internal::data_members<U>()) {
                if constexpr (!std::meta::has_identifier(m) || std::meta::is_bit_field(m) || !std::meta::is_public(m))
                    return no_fixed_size<U>();
                size += fixed_size<typename[:std::meta::type_of(m):], Root>();
            }
            return size;
        }
    } else if constexpr (internal::is_inline_optional<U>)
        return internal::inline_optional_head + fixed_size<typename U::value_type, Root>();
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

template <class T, std::meta::info Member, class Root>
consteval std::size_t internal::member_offset()
{
    using U = std::remove_cv_t<T>;
    if constexpr (std::meta::parent_of(Member) != std::meta::dealias(^^U)) {
        return no_fixed_size<T>();
    } else {
        std::size_t offset = internal::head_size(*internal::tag_number_of(^^U)) +
                             internal::straight_reference_size<Root, U>() + internal::item_head;
        template for (constexpr auto m : internal::data_members<U>()) {
            if constexpr (m == Member)
                return offset;
            offset += fixed_size<typename[:std::meta::type_of(m):], Root>();
        }
        return no_fixed_size<T>();
    }
}

template <class T>
class databind
{
public:
    template <std::size_t DepthMax = 64>
    static result<owning_ref<T>> decode(std::string_view const encoded)
    {
        auto copy = std::make_shared<std::string const>(encoded);
        auto value = read<DepthMax>(*copy);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        return owning_ref<T>(std::move(copy), std::move(*value));
    }

    template <std::size_t DepthMax = 64, std::same_as<std::string> Encoded>
    static result<owning_ref<T>> decode(Encoded &&encoded)
    {
        auto owner = std::make_shared<std::string const>(std::move(encoded));
        std::string_view const view = *owner;
        return decode<DepthMax>(std::move(owner), view);
    }

    template <std::size_t DepthMax = 64>
    static result<owning_ref<T>> decode(std::shared_ptr<void const> owner, std::string_view const encoded)
    {
        if (!owner) [[unlikely]]
            throw std::logic_error("cbor::databind::decode: the owner of the encoded data item is empty");
        auto value = read<DepthMax>(encoded);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        return owning_ref<T>(std::move(owner), std::move(*value));
    }

    CBOR_ALWAYS_INLINE static result<std::string, std::errc> encode(T const &value)
    {
        std::size_t const size = internal::generic_size(value);
        std::string out;
        out.resize_and_overwrite(size + internal::head_padding, [&](char *const p, std::size_t const n) {
            return internal::generic_write(std::span<char>(p, n), 0, value);
        });
        return out;
    }

    template <class Target>
    CBOR_ALWAYS_INLINE static result<std::size_t, std::errc> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        std::size_t const size = internal::generic_size(value);
        std::size_t const padded = size + internal::head_padding;
        if constexpr (std::same_as<U, std::string>) {
            std::size_t const at = target.size();
            target.resize_and_overwrite(at + padded, [&](char *const p, std::size_t const n) {
                return internal::generic_write(std::span<char>(p, n), at, value);
            });
            return size;
        } else if constexpr (internal::byte_container<U> &&
                             requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            target.resize(at + padded);
            internal::generic_write(std::span<char>(target), at, value);
            target.resize(at + size);
            return size;
        } else if constexpr (!internal::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (out.size() >= padded) {
                internal::generic_write(out.first(padded), 0, value);
                return size;
            }
        }
        std::string encoded(padded, '\0');
        encoded.resize(internal::generic_write(std::span<char>(encoded), 0, value));
        decltype(auto) message = internal::message_of(target, encoded.size());
        if (auto const r = message.append(encoded); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        return encoded.size();
    }

private:
    template <std::size_t DepthMax>
    static result<T> read(std::string_view const encoded)
    {
        internal::sharing_decoder d{{encoded}, encoded, {}, 0};
        T out{};
        if (auto const r = internal::generic_read<DepthMax>(d, out, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (!d.encoded.empty()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return out;
    }
};

template <class T>
class schema
{
public:
    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t fixed_size()
    {
        return internal::fixed_size<U, T>();
    }

    template <std::meta::info Member, class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static consteval std::size_t member_offset()
    {
        return internal::member_offset<U, Member, T>();
    }

    template <class U = T>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    class accessor
    {
        static constexpr bool listed = internal::is_list<U> || internal::is_map<U>;

        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::span<char const, internal::fixed_size<U, T>()> field;
        cbor::directory dir;
        [[no_unique_address]] std::conditional_t<listed, internal::reference, std::monostate> items;

        accessor(std::string_view const b, std::span<char const, internal::fixed_size<U, T>()> const f, cbor::directory const d)
            requires(!listed)
            : encoded(b), field(f), dir(d)
        {
        }

        accessor(std::string_view const b, std::span<char const, internal::fixed_size<U, T>()> const f, cbor::directory const d,
                 internal::reference const r)
            requires(listed)
            : encoded(b), field(f), dir(d), items(r)
        {
        }

        accessor(std::shared_ptr<void const> o, std::string_view const b, std::span<char const, internal::fixed_size<U, T>()> const f,
                 cbor::directory const d)
            requires(!listed)
            : owner(std::move(o)), encoded(b), field(f), dir(d)
        {
        }

        friend class schema;

        friend class internal;

    public:
        template <fixed_string Path, std::convertible_to<std::size_t>... Index>
            requires(((Path.view().starts_with('@') && internal::path_valid<U, Path, 1>()) ||
                      (Path.view().starts_with('$') && internal::path_valid<T, Path, 1>())) &&
                     sizeof...(Index) == internal::index_slots<Path>())
        CBOR_ALWAYS_INLINE auto at(Index const... indexes) const &
        {
            std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
            constexpr std::string_view path = Path.view();
            if constexpr (path.starts_with('$') && std::same_as<U, T>) {
                return internal::path_walk<T, T, Path, 1>(encoded, field, dir, i);
            } else if constexpr (path.starts_with('$')) {
                constexpr std::size_t root = internal::fixed_size<T, T>();
                return internal::path_walk<T, T, Path, 1>(
                    encoded, std::span<char const>(encoded).subspan(encoded.size() - root).template first<root>(), dir, i);
            } else if constexpr (internal::is_list<U> && path.size() > 1) {
                using E = std::ranges::range_value_t<U>;
                constexpr std::size_t close = internal::index_end(path, 1);
                using X = typename decltype(internal::path_result<T, E, Path, close + 1>())::type;
                std::size_t at;
                if constexpr (close == 2)
                    at = std::get<0>(i);
                else
                    at = internal::index_of<U>(path.substr(2, close - 2));
                if (at >= items.length) [[unlikely]]
                    return result<X>(std::unexpect, error::index_out_of_bounds);
                return internal::path_walk<T, E, Path, close + 1>(
                    encoded, std::span<char const>(encoded).subspan(items.data + at * internal::fixed_size<E, T>()).template first<internal::fixed_size<E, T>()>(),
                    dir, i);
            } else {
                return internal::path_walk<T, U, Path, 1>(encoded, field, dir, i);
            }
        }

        template <fixed_string Path, std::convertible_to<std::size_t>... Index>
        auto at(Index const... indexes) const && = delete;

        std::size_t size() const
            requires(listed)
        {
            return items.length;
        }
    };

    static result<accessor<>> path(std::shared_ptr<void const> owner, std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        if (!owner) [[unlikely]]
            throw std::logic_error("cbor::schema::path: the owner of the encoded data item is empty");
        auto const dir = internal::directory_read<T>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        auto const root = std::span<char const>(encoded).subspan(encoded.size() - fixed_size()).template first<fixed_size()>();
        if (!internal::class_tag_valid<T, T>(root)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return accessor<>(std::move(owner), encoded, root, *dir);
    }

    static result<accessor<>> path(std::string_view const encoded)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        auto copy = std::make_shared<std::string const>(encoded);
        std::string_view const view = *copy;
        return path(std::move(copy), view);
    }

    template <std::same_as<std::string> Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static result<accessor<>> path(Encoded &&encoded)
    {
        auto owner = std::make_shared<std::string const>(std::move(encoded));
        std::string_view const view = *owner;
        return path(std::move(owner), view);
    }


    template <std::size_t DepthMax = 64>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static result<owning_ref<T>> decode(std::string_view const encoded)
    {
        auto copy = std::make_shared<std::string const>(encoded);
        T value{};
        if (auto const r = internal::root_read<T, DepthMax>(value, *copy); !r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(copy), std::move(value));
    }

    template <std::size_t DepthMax = 64, std::same_as<std::string> Encoded>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static result<owning_ref<T>> decode(Encoded &&encoded)
    {
        auto owner = std::make_shared<std::string const>(std::move(encoded));
        std::string_view const view = *owner;
        return decode<DepthMax>(std::move(owner), view);
    }

    template <std::size_t DepthMax = 64>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    static result<owning_ref<T>> decode(std::shared_ptr<void const> owner, std::string_view const encoded)
    {
        if (!owner) [[unlikely]]
            throw std::logic_error("cbor::schema::decode: the owner of the encoded data item is empty");
        T value{};
        if (auto const r = internal::root_read<T, DepthMax>(value, encoded); !r) [[unlikely]]
            return std::unexpected(r.error());
        return owning_ref<T>(std::move(owner), std::move(value));
    }

    CBOR_ALWAYS_INLINE static result<std::string, std::errc> encode(T const &value)
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    {
        internal::second_item<T> second;
        second.add(value);
        auto const size = internal::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(size.error());
        std::string out;
        out.resize_and_overwrite(*size + internal::head_padding, [&](char *const p, std::size_t const n) {
            return internal::encoded_write<false>(std::span<char>(p, n), value, second);
        });
        return out;
    }

    template <class Target>
        requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static result<std::size_t, std::errc> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        internal::second_item<T> second;
        second.add(value);
        auto const size = internal::encoded_size<T>(second);
        if (!size) [[unlikely]]
            return std::unexpected(size.error());
        std::size_t const padded = *size + internal::head_padding;
        CBOR_ASSUME(padded >= fixed_size());
        if constexpr (std::same_as<U, std::string>) {
            std::size_t const at = target.size();
            target.resize_and_overwrite(at + padded, [&](char *const p, std::size_t const n) {
                return at + internal::encoded_write<false>(std::span<char>(p, n).subspan(at), value, second);
            });
            return *size;
        } else if constexpr (internal::byte_container<U> && requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            target.reserve(at + padded);
            std::ranges::fill_n(std::back_inserter(target), padded, char{});
            internal::encoded_write<false>(std::span<char>(target).subspan(at), value, second);
            target.resize(at + *size);
            return *size;
        } else if constexpr (!internal::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (out.size() >= padded) {
                internal::encoded_write<false>(out.first(padded), value, second);
                return *size;
            }
        }
        decltype(auto) message = internal::message_of(target, *size);
        if constexpr (requires { std::span<char>(message); }) {
            std::span<char> const out(message);
            if (out.size() < *size) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
            internal::encoded_write<true>(out.first(*size), value, second);
            if (auto const r = message.done(*size); !r) [[unlikely]]
                return std::unexpected(r.error());
            return *size;
        } else {
            std::string encoded(padded, '\0');
            encoded.resize(internal::encoded_write<false>(std::span<char>(encoded), value, second));
            if (auto const r = message.append(encoded); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return encoded.size();
        }
    }
};
#endif

template <class Writer>
struct encoder {
    Writer &writer;
    std::array<char, 16384> block;
    std::size_t used = 0;
    std::size_t written = 0;

    explicit encoder(Writer &w) : writer(w)
    {
    }

    std::expected<void, std::errc> flush()
    {
        std::size_t const size = used;
        used = 0;
        written += size;
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
        std::ranges::copy(item, std::span(block).subspan(used).begin());
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
        std::ranges::copy(std::bit_cast<std::array<char, sizeof big>>(big), std::span(head).template subspan<1>().begin());
        std::size_t const size = 1 + bytes;
        item_write(head, size);
        return {};
    }

    std::expected<void, std::errc> byte_string_encode(std::string_view bytes)
    {
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        if (bytes.size() <= block.size() - used) {
            std::ranges::copy(bytes, std::span(block).subspan(used).begin());
            used += bytes.size();
            return {};
        }
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        written += bytes.size();
        return writer.append(bytes);
    }

    std::expected<void, std::errc> text_string_encode(std::string_view text)
    {
        if (auto const r = head_encode(major_type::text_string, text.size()); !r) [[unlikely]]
            return r;
        if (text.size() <= block.size() - used) {
            std::ranges::copy(text, std::span(block).subspan(used).begin());
            used += text.size();
            return {};
        }
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        written += text.size();
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
            std::ranges::copy(std::bit_cast<std::array<char, sizeof v>>(v), std::span(item).template subspan<1>().begin());
            size = 3;
        } break;
        case internal::simple_float_information::single_precision_float: {
            std::get<0>(item) = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::single_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint32_t>(static_cast<float>(value)));
            std::ranges::copy(std::bit_cast<std::array<char, sizeof v>>(v), std::span(item).template subspan<1>().begin());
            size = 5;
        } break;
        default: {
            std::get<0>(item) = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::double_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint64_t>(value));
            std::ranges::copy(std::bit_cast<std::array<char, sizeof v>>(v), std::span(item).template subspan<1>().begin());
            size = 9;
        } break;
        }
        item_write(item, size);
        return {};
    }

    template <std::unsigned_integral T>
        requires(sizeof(T) <= sizeof(std::uint64_t))
    std::expected<void, std::errc> fixed_width_head_encode(major_type major, T argument)
    {
        if (auto const r = room(9); !r) [[unlikely]]
            return r;
        std::array<char, 9> head;
        std::get<0>(head) = static_cast<char>(
            std::to_underlying(major) << 5 |
            (std::to_underlying(internal::additional_information::one_byte_argument) + std::countr_zero(sizeof(T))));
        T const big = std::byteswap(argument);
        std::ranges::copy(std::bit_cast<std::array<char, sizeof big>>(big), std::span(head).template subspan<1>().begin());
        item_write(head, 1 + sizeof(T));
        return {};
    }

    std::expected<void, std::errc> simple_value_encode(simple_value value)
    {
        if (std::to_underlying(value) >= std::to_underlying(internal::simple_float_information::simple_value_follows))
            [[unlikely]]
            return std::unexpected(std::errc::invalid_argument);
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

template <std::size_t DepthMax, language_binding Binding>
std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view encoded)
{
    internal::decoder d{encoded};
    internal::marks<Binding> shared;
    return internal::value_decode<DepthMax>(d, binding, shared, nullptr, 0, std::nullopt);
}

enum class pass { plain, count, write };

template <std::size_t DepthMax, sharedrefs Sharing, class Binding, class Writer>
std::expected<std::size_t, std::error_code> encode_from(Binding &binding, Writer &writer, typename Binding::value const &value,
                                                        std::size_t depth, bool embedded);

struct discarding_writer {
    std::expected<void, std::errc> append(std::string_view)
    {
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

template <class Binding>
struct sharing {
    std::unordered_map<typename Binding::identity, std::uint64_t> seen;
    std::unordered_map<typename Binding::identity, std::uint64_t> numbers;
    std::uint64_t next = 0;
    std::unordered_map<typename Binding::identity, typename Binding::value> replaced;
};

template <std::size_t DepthMax, class Binding, class Writer, pass Pass>
class walker
{
    Binding &binding;
    encoder<Writer> out;
    sharing<Binding> *shared;
    std::size_t depth;
    bool embedded;
    std::error_code failure;

    template <std::size_t, sharedrefs, class H, class W>
    friend std::expected<std::size_t, std::error_code> encode_from(H &binding, W &writer, typename H::value const &value,
                                                                   std::size_t depth, bool embedded);

    walker(Binding &h, Writer &w, sharing<Binding> *s, std::size_t const d, bool const e)
        : binding(h), out{w}, shared(s), depth(d), embedded(e)
    {
    }

    void keep(std::expected<void, std::errc> const r)
    {
        if (!r && !failure) [[unlikely]]
            failure = std::make_error_code(r.error() == std::errc{} ? std::errc::io_error : r.error());
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

    void key(typename Binding::value const &item)
    {
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, binding.key_identity(item));
    }

    void value(typename Binding::value const &item)
    {
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, binding.value_identity(item));
    }

    template <class Identity>
    void child(typename Binding::value const &item, Identity const &identity)
    {
        if (failure) [[unlikely]]
            return;
        if (depth > DepthMax) [[unlikely]] {
            keep_error(error::nesting_depth_exceeded);
            return;
        }
        if constexpr (requires { binding.embed_of(item); }) {
            bool const outer = embedded;
            embedded = false;
            if (!outer && binding.embed_of(item)) {
                if constexpr (Pass != pass::count) {
                    internal::string_sink inner;
                    auto const r = encode_from<DepthMax, Pass == pass::plain ? sharedrefs::off : sharedrefs::on>(
                        binding, inner, item, depth, true);
                    if (!r) [[unlikely]] {
                        if (!failure)
                            failure = r.error();
                        return;
                    }
                    head(major_type::tag, std::to_underlying(internal::tag_number::encoded_cbor_data_item));
                    keep(out.byte_string_encode(inner.encoded));
                }
                return;
            }
        }
        if constexpr (Pass == pass::count) {
            if (identity && ++shared->seen.try_emplace(*identity, 0).first->second > 1)
                return;
        }
        if constexpr (Pass == pass::write) {
            if (identity && !shared->numbers.empty()) {
                auto const number = shared->numbers.find(*identity);
                if (number != shared->numbers.end()) {
                    if (number->second < shared->next) {
                        head(major_type::tag, std::to_underlying(internal::tag_number::sharedref));
                        head(major_type::unsigned_integer, number->second);
                        return;
                    }
                    number->second = shared->next++;
                    head(major_type::tag, std::to_underlying(internal::tag_number::shareable));
                }
            }
        }
        ++depth;
        describe(item, identity);
        --depth;
    }

    template <class Identity>
    typename Binding::value content_of(typename Binding::value const &item, Identity const &identity)
    {
        if constexpr (Pass == pass::plain) {
            return binding.before_encode(item);
        } else {
            if (!identity)
                return binding.before_encode(item);
            if constexpr (Pass == pass::count)
                return shared->replaced.emplace(*identity, binding.before_encode(item)).first->second;
            else
                return shared->replaced.at(*identity);
        }
    }

    template <class Identity>
    void describe(typename Binding::value const &item, Identity const &identity)
    {
        kind const k = binding.kind_of(item);
        if constexpr (Pass == pass::count)
            if (k != kind::array && k != kind::map && k != kind::registered)
                return;
        switch (k) {
        case kind::unsigned_integer:
            if constexpr (requires { binding.unsigned_of(item); }) {
                head(major_type::unsigned_integer, binding.unsigned_of(item));
                return;
            }
            break;
        case kind::negative_integer:
            if constexpr (requires { binding.unsigned_of(item); }) {
                head(major_type::negative_integer, binding.unsigned_of(item) - 1);
                return;
            }
            break;
        case kind::unsigned_bignum:
            if constexpr (requires { binding.magnitude_of(item); }) {
                bignum(false, binding.magnitude_of(item));
                return;
            }
            break;
        case kind::negative_bignum:
            if constexpr (requires { binding.magnitude_of(item); }) {
                bignum(true, binding.magnitude_of(item));
                return;
            }
            break;
        case kind::byte_string:
            if constexpr (requires { binding.bytes_of(item); }) {
                keep(out.byte_string_encode(binding.bytes_of(item)));
                return;
            }
            break;
        case kind::text_string:
            if constexpr (requires { binding.text_of(item); }) {
                keep(out.text_string_encode(binding.text_of(item)));
                return;
            }
            break;
        case kind::floating_point:
            if constexpr (requires { binding.float_of(item); }) {
                keep(out.float_encode(binding.float_of(item)));
                return;
            }
            break;
        case kind::simple_value:
            if constexpr (requires { binding.simple_of(item); }) {
                simple(binding.simple_of(item));
                return;
            }
            break;
        case kind::array:
            if constexpr (requires { binding.array_size(item); }) {
                std::uint64_t const size = binding.array_size(item);
                head(major_type::array, size);
                for (std::uint64_t i = 0; i < size; ++i)
                    value(binding.array_at(item, i));
                return;
            }
            break;
        case kind::map:
            if constexpr (requires { binding.map_size(item); }) {
                head(major_type::map, binding.map_size(item));
                binding.map_for_each(item,
                             [this](typename Binding::value const &k, typename Binding::value const &v) {
                                 key(k);
                                 value(v);
                             });
                return;
            }
            break;
        case kind::typed_array:
            if constexpr (requires { binding.typed_array_of(item); }) {
                if (depth > DepthMax) [[unlikely]] {
                    keep_error(error::nesting_depth_exceeded);
                    return;
                }
                cbor::typed_array const a = binding.typed_array_of(item);
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
            if constexpr (requires { binding.registered_tag(item); }) {
                head(major_type::tag, binding.registered_tag(item));
                if constexpr (Pass == pass::plain)
                    value(binding.before_encode(item));
                else
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
            if (depth > DepthMax) [[unlikely]] {
                keep_error(error::nesting_depth_exceeded);
                return;
            }
            head(major_type::tag, std::to_underlying(internal::tag_number::unsigned_bignum));
            keep(out.byte_string_encode(m));
            return;
        }
        if (m.empty()) [[unlikely]] {
            keep_error(error::unsupported_value);
            return;
        }
        std::string const n = internal::magnitude_minus_one(m);
        if (n.size() <= sizeof(std::uint64_t)) {
            head(major_type::negative_integer, internal::magnitude_value(n));
            return;
        }
        if (depth > DepthMax) [[unlikely]] {
            keep_error(error::nesting_depth_exceeded);
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

template <std::size_t DepthMax, class Binding>
bool cycle_find(Binding &binding, typename Binding::value const &item,
                std::vector<typename Binding::identity> &path)
{
    auto const identity = binding.value_identity(item);
    if (identity && std::ranges::find(path, *identity) != path.end())
        return true;
    if (path.size() > DepthMax)
        return false;
    if (identity)
        path.push_back(*identity);
    bool found = false;
    switch (binding.kind_of(item)) {
    case kind::array:
        if constexpr (requires { binding.array_size(item); })
            for (std::uint64_t i = 0; !found && i < binding.array_size(item); ++i)
                found = cycle_find<DepthMax>(binding, binding.array_at(item, i), path);
        break;
    case kind::map:
        if constexpr (requires { binding.map_size(item); })
            binding.map_for_each(item,
                                 [&](typename Binding::value const &k, typename Binding::value const &v) {
                                     found = found || cycle_find<DepthMax>(binding, k, path) ||
                                             cycle_find<DepthMax>(binding, v, path);
                                 });
        break;
    default:
        break;
    }
    if (identity)
        path.pop_back();
    return found;
}

template <std::size_t DepthMax, sharedrefs Sharing, class Binding, class Writer>
std::expected<std::size_t, std::error_code> encode_from(Binding &binding, Writer &writer, typename Binding::value const &value,
                                                        std::size_t const depth, bool const embedded)
{
    if constexpr (Sharing == sharedrefs::off) {
        walker<DepthMax, Binding, Writer, pass::plain> walk{binding, writer, nullptr, depth, embedded};
        walk.value(value);
        walk.keep(walk.out.flush());
        if (walk.failure) [[unlikely]] {
            if constexpr (requires { binding.value_identity(value); }) {
                std::vector<typename Binding::identity> path;
                if (walk.failure == make_error_code(error::nesting_depth_exceeded) &&
                    cycle_find<DepthMax>(binding, value, path))
                    return std::unexpected(make_error_code(error::cyclic_data_structure));
            }
            return std::unexpected(walk.failure);
        }
        return walk.out.written;
    } else {
        sharing<Binding> shared;
        discarding_writer nothing;
        walker<DepthMax, Binding, discarding_writer, pass::count> count{binding, nothing, &shared, depth, embedded};
        count.value(value);
        if (count.failure) [[unlikely]]
            return std::unexpected(count.failure);
        for (auto const &[identity, times] : shared.seen)
            if (times > 1)
                shared.numbers.emplace(identity, std::numeric_limits<std::uint64_t>::max());
        walker<DepthMax, Binding, Writer, pass::write> write{binding, writer, &shared, depth, embedded};
        write.value(value);
        write.keep(write.out.flush());
        if (write.failure) [[unlikely]]
            return std::unexpected(write.failure);
        return write.out.written;
    }
}

template <std::size_t DepthMax, sharedrefs Sharing, class Binding, class Writer>
std::expected<void, std::error_code> encode(Binding &binding, Writer &&target, typename Binding::value const &value)
{
    decltype(auto) message = internal::message_of(target, 0);
    auto const size = encode_from<DepthMax, Sharing>(binding, message, value, 0, false);
    if (!size) [[unlikely]]
        return std::unexpected(size.error());
    if (auto const r = message.done(*size); !r) [[unlikely]]
        return std::unexpected(std::make_error_code(r.error()));
    return {};
}

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view const encoded)
{
    internal::decoder d{encoded};
    internal::no_marks none;
    if (auto const r = internal::item_skip<DepthMax>(d, none, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    return encoded.size() - d.encoded.size();
}

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded)
{
    if (!encoded) [[unlikely]]
        throw std::logic_error("cbor::decode: the encoded data item is empty");
    return lazy{std::make_shared<internal::document>(encoded, *encoded, std::vector<std::size_t>{}, 0), 0};
}

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string_view const encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax, std::same_as<std::string> Encoded>
std::expected<lazy, error> decode(Encoded &&encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(std::move(encoded)));
}

template <>
struct result<lazy, error> : std::expected<lazy, error> {
    using std::expected<lazy, cbor::error>::expected;

    template <std::size_t DepthMax = 64>
    result at(std::string_view const key) const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).at<DepthMax>(key);
    }

    template <std::size_t DepthMax = 64>
    result at(std::int64_t const index) const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).at<DepthMax>(index);
    }

    template <class T>
    auto get() const -> decltype((**this).template get<T>())
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).get<T>();
    }

    template <std::size_t DepthMax = 64>
    cbor::result<lazy_elements<DepthMax>> elements() const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).elements<DepthMax>();
    }

    template <std::size_t DepthMax = 64>
    cbor::result<lazy_entries<DepthMax>> entries() const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).entries<DepthMax>();
    }
};

inline result<lazy> lazy::from(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    if (!owner) [[unlikely]]
        throw std::logic_error("cbor::lazy::from: the owner of the encoded data item is empty");
    return lazy{std::make_shared<internal::document>(std::move(owner), encoded, std::vector<std::size_t>{}, 0), 0};
}

inline result<lazy> lazy::from(std::shared_ptr<std::string const> encoded)
{
    std::string_view const view = *encoded;
    return from(std::move(encoded), view);
}

template <std::same_as<std::string> Encoded>
result<lazy> lazy::from(Encoded &&encoded)
{
    return from(std::make_shared<std::string const>(std::move(encoded)));
}

inline result<lazy> lazy::from(std::string_view const encoded)
{
    return from(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax>
result<lazy> lazy::at(std::string_view const key) const
{
    auto const found = internal::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        internal::decoder probe = d;
        auto k = probe.head_decode();
        while (k && k->major == major_type::tag &&
               k->argument == std::to_underlying(internal::tag_number::shareable))
            k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        if (k->major == major_type::tag && k->argument == std::to_underlying(internal::tag_number::sharedref)) {
            auto const target = internal::container_resolve(source, source->encoded.size() - d.encoded.size());
            if (!target) [[unlikely]]
                return std::unexpected(target.error());
            probe = target->d;
            k = target->h;
        }
        bool match = false;
        if (k->major == major_type::text_string) {
            auto const text = probe.byte_string_decode(k->argument);
            if (!text) [[unlikely]]
                return std::unexpected(text.error());
            match = *text == key;
        }
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (match) {
            std::size_t const value = source->encoded.size() - d.encoded.size();
            return lazy{source, value};
        }
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}
template <std::size_t DepthMax>
result<lazy> lazy::at(std::int64_t const index) const
{
    auto const found = internal::container_resolve(document, offset);
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
        std::size_t const element = source->encoded.size() - d.encoded.size();
        return lazy{source, element};
    }
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        internal::decoder probe = d;
        auto k = probe.head_decode();
        while (k && k->major == major_type::tag &&
               k->argument == std::to_underlying(internal::tag_number::shareable))
            k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        if (k->major == major_type::tag && k->argument == std::to_underlying(internal::tag_number::sharedref)) {
            auto const target = internal::container_resolve(source, source->encoded.size() - d.encoded.size());
            if (!target) [[unlikely]]
                return std::unexpected(target.error());
            probe = target->d;
            k = target->h;
        }
        bool const match = (k->major == major_type::unsigned_integer && index >= 0 &&
                            k->argument == static_cast<std::uint64_t>(index)) ||
                           (k->major == major_type::negative_integer && index < 0 &&
                            k->argument == static_cast<std::uint64_t>(-1 - index));
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (match) {
            std::size_t const value = source->encoded.size() - d.encoded.size();
            return lazy{source, value};
        }
        if (auto const r = internal::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}
template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
result<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           owning_ref<T>, T>> lazy::get() const
{
    auto const found = internal::container_resolve(document, offset);
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
            auto const content = internal::shared_resolve<64>(*source, source->encoded.size() - d.encoded.size());
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            d = internal::decoder{source->encoded.substr(*content)};
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
        [[unlikely]] default:
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
        auto const text = d.text_string_decode(h.argument);
        if (!text) [[unlikely]]
            return std::unexpected(text.error());
        return owning_ref<T>(source->owner, *text);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        if (h.major != major_type::tag) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (auto const r = internal::typed_array_check(h.argument, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const content = internal::shared_resolve<64>(*source, source->encoded.size() - d.encoded.size());
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        d = internal::decoder{source->encoded.substr(*content)};
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
        return owning_ref<T>(source->owner, typed_array{h.argument, std::as_bytes(std::span(*bytes))});
    } else {
        if (h.major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const bytes = d.byte_string_decode(h.argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        return owning_ref<T>(source->owner, std::as_bytes(std::span(*bytes)));
    }
}
template <std::size_t DepthMax>
result<lazy_elements<DepthMax>> lazy::elements() const
{
    auto const found = internal::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_elements<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax>
result<lazy_entries<DepthMax>> lazy::entries() const
{
    auto const found = internal::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_entries<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l)
{
    internal::prefix before{l.document->encoded, l.document->marks, std::vector<bool>(l.document->marks.size())};
    internal::marks<Binding> shared(before.offsets.size());
    internal::decoder d{l.document->encoded.substr(l.offset)};
    return internal::value_decode<DepthMax>(d, binding, shared, &before, 0, std::nullopt);
}

template <std::size_t DepthMax>
result<std::string, error> inspect(std::string_view const encoded)
{
    internal::decoder d{encoded};
    std::string out;
    if (auto const r = internal::diagnostic_write<DepthMax>(out, d, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (!d.encoded.empty()) [[unlikely]]
        return std::unexpected(error::syntax_error);
    return out;
}

template <std::size_t DepthMax>
std::expected<std::size_t, error> internal::shared_resolve(document const &doc, std::size_t at)
{
    for (;;) {
        auto const h = raw_head_read(doc.encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag || h->info == std::to_underlying(additional_information::indefinite_length))
            return at;
        if (h->argument == std::to_underlying(tag_number::shareable)) {
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(tag_number::sharedref))
            return at;
        auto const n = raw_head_read(doc.encoded, h->at);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (n->major != major_type::unsigned_integer) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        if (n->argument >= doc.marks.size()) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        std::size_t const marked = doc.marks.at(static_cast<std::size_t>(n->argument));
        if (marked >= at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        at = marked;
    }
}

template <std::size_t DepthMax>
std::expected<std::size_t, error> internal::document_item_end(document &doc, std::size_t const at, std::size_t const depth)
{
    decoder d{doc.encoded.substr(at)};
    if (auto const r = item_skip<DepthMax>(d, doc, depth); !r) [[unlikely]]
        return std::unexpected(r.error());
    return doc.encoded.size() - d.encoded.size();
}

template <std::size_t DepthMax>
std::expected<bool, error> internal::key_equal(document &doc, std::size_t const start, std::string_view const literal,
                                               std::size_t const literal_at, std::size_t const depth)
{
    if (depth > DepthMax) [[unlikely]]
        return std::unexpected(error::nesting_depth_exceeded);
    auto const at = shared_resolve<DepthMax>(doc, start);
    if (!at) [[unlikely]]
        return std::unexpected(at.error());
    auto const h = raw_head_read(doc.encoded, *at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const l = raw_head_read(literal, literal_at);
    if (!l) [[unlikely]]
        return std::unexpected(l.error());
    if (h->info == std::to_underlying(additional_information::indefinite_length)) [[unlikely]]
        return std::unexpected(error::indefinite_length);
    if (h->major != l->major)
        return false;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == l->argument;
    case major_type::byte_string:
    case major_type::text_string:
        if (doc.encoded.size() - h->at < h->argument) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return h->argument == l->argument &&
               doc.encoded.substr(h->at, static_cast<std::size_t>(h->argument)) ==
                   literal.substr(l->at, static_cast<std::size_t>(l->argument));
    case major_type::array: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        std::size_t k = l->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = key_equal<DepthMax>(doc, d, literal, k, depth + 1);
            if (!equal || !*equal)
                return equal;
            auto const d_end = document_item_end<DepthMax>(doc, d, depth + 1);
            if (!d_end) [[unlikely]]
                return std::unexpected(d_end.error());
            auto const k_end = literal_end(literal, k, depth + 1, DepthMax);
            if (!k_end) [[unlikely]]
                return std::unexpected(k_end.error());
            d = *d_end;
            k = *k_end;
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != l->argument)
            return false;
        std::size_t d = h->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const value_at = document_item_end<DepthMax>(doc, d, depth + 1);
            if (!value_at) [[unlikely]]
                return std::unexpected(value_at.error());
            auto const pair_end = document_item_end<DepthMax>(doc, *value_at, depth + 1);
            if (!pair_end) [[unlikely]]
                return std::unexpected(pair_end.error());
            bool paired = false;
            std::size_t k = l->at;
            for (std::uint64_t j = 0; j < l->argument && !paired; ++j) {
                auto const k_value = literal_end(literal, k, depth + 1, DepthMax);
                if (!k_value) [[unlikely]]
                    return std::unexpected(k_value.error());
                auto const k_end = literal_end(literal, *k_value, depth + 1, DepthMax);
                if (!k_end) [[unlikely]]
                    return std::unexpected(k_end.error());
                auto const key_same = key_equal<DepthMax>(doc, d, literal, k, depth + 1);
                if (!key_same) [[unlikely]]
                    return key_same;
                if (*key_same) {
                    for (std::size_t earlier = h->at; earlier < d;) {
                        auto const twin = key_equal<DepthMax>(doc, earlier, literal, k, depth + 1);
                        if (!twin) [[unlikely]]
                            return twin;
                        if (*twin) [[unlikely]]
                            return std::unexpected(error::duplicate_key);
                        auto const earlier_value = document_item_end<DepthMax>(doc, earlier, depth + 1);
                        if (!earlier_value) [[unlikely]]
                            return std::unexpected(earlier_value.error());
                        auto const earlier_end = document_item_end<DepthMax>(doc, *earlier_value, depth + 1);
                        if (!earlier_end) [[unlikely]]
                            return std::unexpected(earlier_end.error());
                        earlier = *earlier_end;
                    }
                    auto const value_same = key_equal<DepthMax>(doc, *value_at, literal, *k_value, depth + 1);
                    if (!value_same) [[unlikely]]
                        return value_same;
                    paired = *value_same;
                }
                k = *k_end;
            }
            if (!paired)
                return false;
            d = *pair_end;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != l->argument)
            return false;
        return key_equal<DepthMax>(doc, h->at, literal, l->at, depth + 1);
    default:
        break;
    }
    constexpr std::uint8_t half = std::to_underlying(simple_float_information::half_precision_float);
    constexpr std::uint8_t twice = std::to_underlying(simple_float_information::double_precision_float);
    bool const h_float = h->info >= half && h->info <= twice;
    bool const l_float = l->info >= half && l->info <= twice;
    if (h_float != l_float)
        return false;
    if (!h_float)
        return h->argument == l->argument;
    float_key const a = float_key_of(h->info, h->argument);
    float_key const b = float_key_of(l->info, l->argument);
    if (a.nan || b.nan)
        return a.nan && b.nan && a.widened == b.widened;
    return a.value == b.value;
}

template <std::size_t DepthMax>
result<lazy, error> internal::key_find(lazy const &node, std::string_view const key)
{
    auto const found = container_resolve(node.document, node.offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    decoder text_key{key};
    auto const literal = text_key.head_decode();
    bool const text = literal && literal->major == major_type::text_string;
    for (std::uint64_t i = 0; i < h.argument; ++i) {
        std::size_t const start = source->encoded.size() - d.encoded.size();
        decoder probe = d;
        for (;;) {
            decoder look = probe;
            auto const k = look.head_decode();
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            if (k->major == major_type::tag && k->argument == std::to_underlying(tag_number::shareable)) {
                probe = look;
                continue;
            }
            if (k->major == major_type::tag && k->argument == std::to_underlying(tag_number::sharedref)) {
                auto const n = look.head_decode();
                if (!n) [[unlikely]]
                    return std::unexpected(n.error());
                if (n->major != major_type::unsigned_integer) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                if (n->argument >= source->marks.size()) [[unlikely]]
                    return std::unexpected(error::sharedref_index_not_marked);
                std::size_t const marked = source->marks.at(static_cast<std::size_t>(n->argument));
                if (marked >= start) [[unlikely]]
                    return std::unexpected(error::sharedref_not_complete);
                probe = decoder{source->encoded.substr(marked)};
            }
            break;
        }
        bool match = false;
        decoder look = probe;
        auto const k = look.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        if (text && k->major == major_type::text_string) {
            auto const content = look.byte_string_decode(k->argument);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            match = *content == text_key.encoded;
        }
        if (auto const r = item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (!text) {
            auto const equal = key_equal<DepthMax>(*source, start, key, 0, 0);
            if (!equal) [[unlikely]]
                return std::unexpected(equal.error());
            match = *equal;
        }
        if (match)
            return lazy{source, source->encoded.size() - d.encoded.size()};
        if (auto const r = item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <std::size_t DepthMax, class Binding>
result<typename Binding::value, error> internal::query_walk(Binding &binding, std::span<selector const> const selectors,
                                                            std::string_view const keys, lazy const &root)
{
    if (!root.document) [[unlikely]]
        throw std::logic_error("cbor::at_path: the lazy holds no document");
    bool const nodelist =
        std::ranges::any_of(selectors, [](selector const &s) { return s.kind == selector::kind::wildcard; });
    std::size_t const limit = root.document->encoded.size();
    std::vector<lazy> nodes{root};
    std::vector<lazy> next;
    for (selector const &s : selectors) {
        next.clear();
        for (lazy const &node : nodes) {
            error failure{};
            if (s.kind == selector::kind::wildcard) {
                auto const found = container_resolve(node.document, node.offset);
                if (!found) [[unlikely]]
                    return std::unexpected(found.error());
                if (found->h.major == major_type::array) {
                    auto const elements = node.elements<DepthMax>();
                    if (!elements) [[unlikely]]
                        return std::unexpected(elements.error());
                    for (auto const element : *elements) {
                        if (!element) [[unlikely]]
                            return std::unexpected(element.error());
                        if (next.size() == limit) [[unlikely]]
                            return std::unexpected(error::nodelist_too_long);
                        next.push_back(*element);
                    }
                } else if (found->h.major == major_type::map) {
                    auto const entries = node.entries<DepthMax>();
                    if (!entries) [[unlikely]]
                        return std::unexpected(entries.error());
                    for (auto const entry : *entries) {
                        if (!entry) [[unlikely]]
                            return std::unexpected(entry.error());
                        if (next.size() == limit) [[unlikely]]
                            return std::unexpected(error::nodelist_too_long);
                        next.push_back(entry->second);
                    }
                } else {
                    failure = error::not_indexable;
                }
            } else {
                auto const child = s.kind == selector::kind::index
                                       ? node.at<DepthMax>(s.index)
                                       : key_find<DepthMax>(node, keys.substr(s.key_at, s.key_size));
                if (child) {
                    if (next.size() == limit) [[unlikely]]
                        return std::unexpected(error::nodelist_too_long);
                    next.push_back(*child);
                } else {
                    failure = child.error();
                }
            }
            if (failure == error{})
                continue;
            if (!nodelist || (failure != error::not_indexable && failure != error::index_out_of_bounds &&
                              failure != error::key_not_found)) [[unlikely]]
                return std::unexpected(failure);
        }
        std::swap(nodes, next);
    }
    if (!nodelist) {
        auto value = lazy_decode<DepthMax>(binding, nodes.front());
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        return std::move(*value);
    }
    auto array = binding.array_decode(nodes.size());
    for (lazy const &node : nodes) {
        auto value = lazy_decode<DepthMax>(binding, node);
        if (!value) [[unlikely]]
            return std::unexpected(value.error());
        array = binding.array_append(std::move(array), std::move(*value));
    }
    return array;
}

template <std::size_t DepthMax, class Binding>
result<typename Binding::value, error> at_path(Binding &binding, std::string_view const path, lazy const &l)
{
    auto const q = internal::query_parse(path, false, DepthMax);
    if (!q) [[unlikely]]
        return std::unexpected(q.error());
    return internal::query_walk<DepthMax>(binding, q->selectors, q->keys, l);
}

template <fixed_string Path, std::size_t DepthMax, class Binding>
    requires(internal::query_parse(Path.view(), true, DepthMax).has_value())
result<typename Binding::value, error> at_path(Binding &binding, lazy const &l)
{
    constexpr std::size_t count = internal::query_parse(Path.view(), true, DepthMax)->selectors.size();
    constexpr std::size_t size = internal::query_parse(Path.view(), true, DepthMax)->keys.size();
    constexpr auto compiled = [] {
        auto const q = *internal::query_parse(Path.view(), true, DepthMax);
        std::pair<std::array<internal::selector, count>, std::array<char, size>> c{};
        std::ranges::copy(q.selectors, c.first.begin());
        std::ranges::copy(q.keys, c.second.begin());
        return c;
    }();
    return internal::query_walk<DepthMax>(binding, std::span<internal::selector const>(compiled.first),
                                          std::string_view(compiled.second.data(), size), l);
}

}

template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};

template <>
struct std::is_error_condition_enum<cbor::condition> : std::true_type {};

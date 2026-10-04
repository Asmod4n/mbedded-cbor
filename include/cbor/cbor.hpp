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
    unpopulated_table_index
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
std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view bytes);

template <class Writer>
struct encoder;

enum class sharedrefs { off, on };

enum class pass;

struct lazy;

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &bytes);

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string bytes);

template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l);

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

struct path_step {
    enum class kind { key, index, wildcard } kind;
    std::string_view key;
    std::int64_t index;
};

template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> path_decode(Binding &binding, std::span<path_step const> steps, lazy const &l);

template <std::size_t DepthMax>
std::expected<std::size_t, error> doc_end(std::string_view bytes);

template <std::size_t DepthMax, sharedrefs Sharing = sharedrefs::off, class Binding, class Writer>
std::expected<void, std::error_code> encode(Binding &binding, Writer &&target, typename Binding::value const &value);

template <class T, class E = error>
struct result : std::expected<T, E> {
    using std::expected<T, E>::expected;
};

#ifdef __cpp_impl_reflection
template <class T>
consteval std::size_t fixed_size();

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

template <class T, std::meta::info Member>
consteval std::size_t member_offset();

template <class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
result<std::string, std::errc> encode(T const &value);

struct directory {
    std::size_t at;
    std::size_t count;
};

template <class T>
struct document {
    std::shared_ptr<void const> owner;
    std::string_view bytes;
    std::span<char const, fixed_size<T>()> field;
    directory dir;
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
auto at_path_compiled(document<T> const &doc);

template <class E>
struct array;

template <class K, class V>
struct map;

template <std::uint64_t Number, class T>
struct tagged {
    static constexpr std::uint64_t number = Number;
    T content;
};

template <class T, std::size_t DepthMax = 64>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
result<T> decode(std::string_view bytes);

namespace generic
{
template <class T, std::size_t DepthMax = 64>
result<T> decode(std::string_view bytes);

template <class T>
result<std::string, std::errc> encode(T const &value);

template <class T, class Target>
result<std::size_t, std::errc> encode(T const &value, Target &&target);
}
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

    template <class Root, class T>
    static consteval void zero_initialized_encode(std::vector<char> &bytes)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            head_encode(bytes, major_type::simple_float, std::to_underlying(simple_value::false_value));
        } else if constexpr (std::is_enum_v<U>) {
            zero_initialized_encode<Root, std::underlying_type_t<U>>(bytes);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            head_encode(bytes, major_type::tag, std::to_underlying(tag_number::unsigned_bignum));
            head_encode(bytes, major_type::byte_string, sizeof(U));
            bytes.resize(bytes.size() + sizeof(U));
#endif
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
                    zero_initialized_encode<Root, E>(bytes);
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            head_encode(bytes, major_type::tag, record_reference_tag<Root, U>());
            fixed_width_head_encode(bytes, major_type::array, sizeof(std::uint32_t));
            auto const count = big_endian(static_cast<std::uint32_t>(data_members<U>().size()));
            std::ranges::copy(count, bytes.end() - sizeof(std::uint32_t));
            template for (constexpr auto m : data_members<U>())
                zero_initialized_encode<Root, typename[:std::meta::type_of(m):]>(bytes);
        } else if constexpr (is_inline_optional<U>) {
            head_encode(bytes, major_type::array, 2);
            head_encode(bytes, major_type::simple_float, std::to_underlying(simple_value::false_value));
            zero_initialized_encode<Root, typename U::value_type>(bytes);
        } else {
            head_encode(bytes, major_type::tag, std::to_underlying(tag_number::reference));
            fixed_width_head_encode(bytes, major_type::unsigned_integer, sizeof(std::uint32_t));
        }
    }

    template <class Root, class T>
    static consteval std::span<char const> zero_initialized()
    {
        std::vector<char> bytes;
        zero_initialized_encode<Root, T>(bytes);
        return std::define_static_array(bytes);
    }

    template <class U>
    static consteval std::span<char const> record_keys()
    {
        std::vector<char> bytes;
        head_encode(bytes, major_type::tag, std::to_underlying(tag_number::record_function));
        static constexpr auto members = members_of<U>();
        head_encode(bytes, major_type::array, members.size());
        template for (constexpr std::size_t i : std::define_static_array(std::views::iota(0uz, members.size()))) {
            if constexpr (has_integer_keys<U>) {
                constexpr std::int64_t key = U::keys.at(i);
                if (key >= 0)
                    head_encode(bytes, major_type::unsigned_integer, static_cast<std::uint64_t>(key));
                else
                    head_encode(bytes, major_type::negative_integer, static_cast<std::uint64_t>(-1 - key));
            } else {
                constexpr std::string_view key = key_of(members[i]);
                head_encode(bytes, major_type::text_string, key.size());
                bytes.insert(bytes.end(), key.begin(), key.end());
            }
        }
        return std::define_static_array(bytes);
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
        if (types.size() > std::to_underlying(tag_number::straight_argument_last) -
                               std::to_underlying(tag_number::straight_argument_first) + 1)
            std::unreachable();
        return std::define_static_array(types);
    }

    template <class Root, class U>
    static consteval std::uint64_t record_reference_tag()
    {
        constexpr auto types = packing_table_of<Root>();
        return std::to_underlying(tag_number::straight_argument_first) +
               static_cast<std::uint64_t>(std::ranges::find(types, std::meta::dealias(^^std::remove_cv_t<U>)) - types.begin());
    }

    template <class Root>
    static consteval std::span<char const> packing_prefix_of()
    {
        std::vector<char> bytes;
        head_encode(bytes, major_type::tag, std::to_underlying(tag_number::basic_packed_cbor));
        head_encode(bytes, major_type::array, 2);
        fixed_width_head_encode(bytes, major_type::array, sizeof(std::uint32_t));
        template for (constexpr std::meta::info type : packing_table_of<Root>()) {
            constexpr auto keys = record_keys<typename[:type:]>();
            bytes.insert(bytes.end(), keys.begin(), keys.end());
        }
        return std::define_static_array(bytes);
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
            block_add(std::ranges::size(range), fixed_size<E>());
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
                    bytes_add(fixed_size<typename U::value_type>());
                    add<typename U::value_type>(*value);
                }
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                items += 1;
                bytes_add(item_head + std::ranges::size(value));
            } else if constexpr (is_map<U>) {
                items += 1;
                bytes_add(item_head);
                block_add(std::ranges::size(value),
                          fixed_size<typename U::key_type>() + fixed_size<typename U::mapped_type>());
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
    CBOR_ALWAYS_INLINE static void zero_initialized_copy(std::span<char, fixed_size<E>()> const field)
    {
        constexpr std::size_t n = fixed_size<E>();
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
        constexpr std::size_t size = fixed_size<E>();
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
        u32_write(out, directory_at<Root>() + sizeof(std::uint32_t) * j, item);
        field[1] = static_cast<char>((j & 1) << 5 | (std::to_underlying(additional_information::one_byte_argument) + 2));
        auto const n = big_endian(static_cast<std::uint32_t>(j >> 1));
        std::ranges::copy(n, field.template last<sizeof(std::uint32_t)>().begin());
        std::size_t const data = item + item_head;
        if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            item_head_write(out, item, major_type::array, value.has_value() ? 1 : 0);
            c.position = data;
            if (value.has_value()) {
                c.position += fixed_size<E>();
                auto const element = out.subspan(data).template first<fixed_size<E>()>();
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
            c.position = data + length * (fixed_size<K>() + fixed_size<M>());
            std::size_t at = data;
            for (auto const &[k, v] : value) {
                auto const key = out.subspan(at).template first<fixed_size<K>()>();
                zero_initialized_copy<Root, K>(key);
                c = value_encode<Root, K, Exact>(out, key, k, c);
                at += fixed_size<K>();
                auto const mapped = out.subspan(at).template first<fixed_size<M>()>();
                zero_initialized_copy<Root, M>(mapped);
                c = value_encode<Root, M, Exact>(out, mapped, v, c);
                at += fixed_size<M>();
            }
        } else {
            using E = std::ranges::range_value_t<U>;
            std::size_t const length = std::ranges::size(value);
            item_head_write(out, item, major_type::array, length);
            c.position = data + length * fixed_size<E>();
            c = elements_encode<Root, E, Exact>(out, data, value, c);
        }
        return c;
    }

    template <class Root, class T, bool Exact>
    CBOR_ALWAYS_INLINE static encode_cursor value_encode(std::span<char> const out, std::span<char, fixed_size<T>()> const field,
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
                    position = value_encode<Root, E, Exact>(out, field.subspan(at).template first<fixed_size<E>()>(), e, position);
                    at += fixed_size<E>();
                }
            }
        } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
            template for (constexpr auto m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                position = value_encode<Root, M, Exact>(out, field.template subspan<member_offset<U, m>(), fixed_size<M>()>(),
                                           value.[:m:], position);
            }
        } else if constexpr (is_inline_optional<U>) {
            if (value.has_value()) {
                using E = typename U::value_type;
                field.template subspan<1, 1>().front() =
                    static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                      std::to_underlying(simple_value::true_value));
                position = value_encode<Root, E, Exact>(out, field.template subspan<inline_optional_head, fixed_size<E>()>(), *value,
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
    static T fixed_value_read(std::span<char const, fixed_size<T>()> const field)
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
    CBOR_ALWAYS_INLINE static bool fixed_head_valid(std::span<char const, fixed_size<T>()> const field)
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

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    struct decode_cursor {
        directory dir;
        std::size_t index;
        std::size_t at;
        std::size_t end;
    };

    static constexpr unsigned char reference_tag_byte =
        std::to_underlying(major_type::tag) << 5 | std::to_underlying(tag_number::reference);

    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> shared_index_read(std::span<char const, dynamic_type_sizes> const field)
    {
        auto const info = static_cast<unsigned char>(field[1]);
        if (static_cast<unsigned char>(field[0]) != reference_tag_byte ||
            (info & 0xdf) != std::to_underlying(additional_information::one_byte_argument) + 2) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return 2 * std::size_t{unsigned_read<std::uint32_t>(field.subspan<2, sizeof(std::uint32_t)>())} + (info >> 5);
    }

    template <major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> item_length_read(std::string_view const bytes, std::size_t const item,
                                                                     std::size_t const end, std::size_t const element)
    {
        std::size_t size;
        if (item > end || end - item < item_head) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (static_cast<unsigned char>(bytes[item]) !=
            (std::to_underlying(Major) << 5 | (std::to_underlying(additional_information::one_byte_argument) + 2))) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length =
            unsigned_read<std::uint32_t>(std::span<char const>(bytes).subspan(item + 1).template first<sizeof(std::uint32_t)>());
        if (ckd_mul(&size, length, element) || size > end - item - item_head) [[unlikely]]
            return std::unexpected(error::too_little_data);
        return length;
    }

    static std::size_t directory_entry(std::string_view const bytes, directory const dir, std::size_t const j)
    {
        return unsigned_read<std::uint32_t>(
            std::span<char const>(bytes).subspan(dir.at + sizeof(std::uint32_t) * j).template first<sizeof(std::uint32_t)>());
    }

    template <major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> reference_read(std::string_view const bytes,
                                                           std::span<char const, dynamic_type_sizes> const field,
                                                           directory const dir, std::size_t const element)
    {
        auto const j = shared_index_read(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        if (*j >= dir.count) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        std::size_t const item = directory_entry(bytes, dir, *j);
        std::size_t const end = *j + 1 < dir.count ? directory_entry(bytes, dir, *j + 1) : bytes.size();
        auto const length = item_length_read<Major>(bytes, item, end, element);
        if (!length) [[unlikely]]
            return std::unexpected(length.error());
        return reference{item + item_head, *length};
    }

    template <major_type Major>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> reference_take(std::string_view const bytes,
                                                           std::span<char const, dynamic_type_sizes> const field,
                                                           decode_cursor &c, std::size_t const element)
    {
        auto const j = shared_index_read(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        if (*j != c.index || *j >= c.dir.count) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        if (directory_entry(bytes, c.dir, *j) != c.at) [[unlikely]]
            return std::unexpected(error::syntax_error);
        auto const length = item_length_read<Major>(bytes, c.at, c.end, element);
        if (!length) [[unlikely]]
            return std::unexpected(length.error());
        reference const r{c.at + item_head, *length};
        c.index += 1;
        c.at = r.data + *length * element;
        return r;
    }

    template <class T>
    static std::expected<directory, error> directory_read(std::string_view const bytes)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first - 1 - packing_table_of<T>().size();
        constexpr std::size_t least = directory_at<T>() + fillers + fixed_size<T>();
        if (bytes.size() < least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (!std::ranges::equal(bytes.substr(0, 4), std::span(prefix).first(4)) ||
            !std::ranges::equal(bytes.substr(8, prefix.size() - 8), std::span(prefix).subspan(8)) ||
            static_cast<unsigned char>(bytes[prefix.size()]) != 0x5a) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::size_t const length = unsigned_read<std::uint32_t>(std::span<char const>(bytes).subspan(prefix.size() + 1).template first<4>());
        std::size_t const count = length / 4;
        if (length % 4 != 0 ||
            unsigned_read<std::uint32_t>(std::span<char const>(bytes).subspan(4).template first<4>()) != shared_first + count) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (length > bytes.size() - least) [[unlikely]]
            return std::unexpected(error::too_little_data);
        if (std::ranges::any_of(bytes.substr(directory_at<T>() + length, fillers), [](char const c) { return c != '\xf7'; })) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return directory{directory_at<T>(), count};
    }

    template <class T, fixed_string Path, std::size_t At>
    static consteval auto path_result()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; })
                return std::type_identity<cbor::document<U>>{};
            else if constexpr (is_fixed_string<U>)
                return std::type_identity<std::string_view>{};
            else if constexpr (is_optional<U>)
                return std::type_identity<
                    std::optional<typename decltype(path_result<typename U::value_type, Path, At>())::type>>{};
            else if constexpr (is_text_range<U> || is_byte_range<U>)
                return std::type_identity<std::string_view>{};
            else if constexpr (is_map<U>)
                return std::type_identity<cbor::map<typename U::key_type, typename U::mapped_type>>{};
            else if constexpr (std::ranges::sized_range<U> && !requires { fixed_length<U>::value; })
                return std::type_identity<cbor::array<std::ranges::range_value_t<U>>>{};
            else
                return std::type_identity<U>{};
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            return path_result<typename[:std::meta::type_of(m):], Path, end>();
        } else {
            constexpr std::size_t close = index_end(path, At);
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
            return !(std::is_class_v<U> && std::is_aggregate_v<U>) && !requires { fixed_length<U>::value; };
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            return path_reads_wire<typename[:std::meta::type_of(m):], Path, end>();
        } else {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (requires { fixed_length<U>::value; })
                return path_reads_wire<typename fixed_length<U>::element, Path, close + 1>();
            else
                return true;
        }
    }

    template <class T, fixed_string Path, std::size_t At>
    CBOR_ALWAYS_INLINE static auto path_walk(std::string_view const bytes, std::span<char const, fixed_size<T>()> const field,
                          directory const floor)
        -> std::conditional_t<path_reads_wire<T, Path, At>(),
                              std::expected<typename decltype(path_result<T, Path, At>())::type, error>,
                              typename decltype(path_result<T, Path, At>())::type>
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; }) {
                return cbor::document<U>{{}, bytes, field, floor};
            } else if constexpr (is_fixed_string<U>) {
                return std::string_view(field.template last<fixed_length<U>::value>());
            } else if constexpr (is_inline_optional<U>) {
                using E = typename U::value_type;
                using X = typename decltype(path_result<E, Path, At>())::type;
                if (static_cast<unsigned char>(field.template subspan<1, 1>().front()) !=
                    (std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(simple_value::true_value)))
                    return std::optional<X>{};
                return std::optional<X>{
                    path_walk<E, Path, At>(bytes, field.template subspan<inline_optional_head, fixed_size<E>()>(), floor)};
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r = reference_read<major_type::array>(bytes, field, floor, fixed_size<E>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                using X = typename decltype(path_result<E, Path, At>())::type;
                if (r->length == 0)
                    return std::optional<X>{};
                auto const element = std::span<char const>(bytes).subspan(r->data).template first<fixed_size<E>()>();
                if constexpr (path_reads_wire<E, Path, At>()) {
                    auto x = path_walk<E, Path, At>(bytes, element, floor);
                    if (!x) [[unlikely]]
                        return std::unexpected(x.error());
                    return std::optional<X>{std::move(*x)};
                } else {
                    return std::optional<X>{path_walk<E, Path, At>(bytes, element, floor)};
                }
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = reference_read<is_text_range<U> ? major_type::text_string : major_type::byte_string>(bytes, field, floor, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return bytes.substr(r->data, r->length);
            } else if constexpr (is_map<U>) {
                using K = typename U::key_type;
                using V = typename U::mapped_type;
                auto const r = reference_read<major_type::map>(bytes, field, floor, fixed_size<K>() + fixed_size<V>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return cbor::map<K, V>{bytes, r->data, r->length, floor};
            } else if constexpr (std::ranges::sized_range<U> && !requires { fixed_length<U>::value; }) {
                using E = std::ranges::range_value_t<U>;
                auto const r = reference_read<major_type::array>(bytes, field, floor, fixed_size<E>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return cbor::array<E>{bytes, r->data, r->length, floor};
            } else {
                if (!fixed_head_valid<U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return fixed_value_read<U>(field);
            }
        } else if constexpr (path.substr(At, 1) == ".") {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::meta::info m = member_named<U>(path.substr(At + 1, end - At - 1));
            if constexpr (m == std::meta::info{})
                return no_fixed_size<T>();
            else {
                using M = typename[:std::meta::type_of(m):];
                return path_walk<M, Path, end>(bytes, field.template subspan<member_offset<U, m>(), fixed_size<M>()>(),
                                               floor);
            }
        } else {
            constexpr std::size_t close = index_end(path, At);
            constexpr std::size_t i = index_of<T>(path.substr(At + 1, close - At - 1));
            if constexpr (is_fixed_string<U> || is_text_range<U> || is_byte_range<U>) {
                return no_fixed_size<T>();
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                if constexpr (i >= n)
                    return no_fixed_size<T>();
                else
                    return path_walk<E, Path, close + 1>(
                        bytes, field.template subspan<head_size(n) + i * fixed_size<E>(), fixed_size<E>()>(), floor);
            } else {
                using E = std::ranges::range_value_t<U>;
                auto const r = reference_read<major_type::array>(bytes, field, floor, fixed_size<E>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (i >= r->length) [[unlikely]]
                    return std::unexpected(error::index_out_of_bounds);
                return path_walk<E, Path, close + 1>(
                    bytes, std::span<char const>(bytes).subspan(r->data + i * fixed_size<E>()).template first<fixed_size<E>()>(),
                    floor);
            }
        }
    }

    template <std::size_t DepthMax, class T>
    static std::expected<void, error> value_read(T &out, std::string_view const bytes,
                                                  std::span<char const, fixed_size<T>()> const field,
                                                  decode_cursor &floor, std::size_t const depth)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::is_class_v<U> && std::is_aggregate_v<U> && !requires { fixed_length<U>::value; }) {
            std::expected<void, error> done;
            template for (constexpr std::meta::info m : data_members<U>()) {
                using M = typename[:std::meta::type_of(m):];
                if (done)
                    done = value_read<DepthMax>(out.[:m:], bytes,
                                                field.template subspan<member_offset<U, m>(), fixed_size<M>()>(), floor,
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
                    done = value_read<DepthMax>(
                        std::span(out).template subspan<i, 1>().front(), bytes,
                        field.template subspan<head_size(n) + i * fixed_size<E>(), fixed_size<E>()>(), floor, depth);
            }
            return done;
        } else if constexpr (is_inline_optional<U>) {
            if (static_cast<unsigned char>(field.template subspan<1, 1>().front()) !=
                (std::to_underlying(major_type::simple_float) << 5 | std::to_underlying(simple_value::true_value))) {
                out.reset();
                return {};
            }
            return value_read<DepthMax>(
                out.emplace(), bytes,
                field.template subspan<inline_optional_head, fixed_size<typename U::value_type>()>(), floor, depth);
        } else if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            auto const r = reference_take<major_type::array>(bytes, field, floor, fixed_size<E>());
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
            if (auto const e = value_read<DepthMax>(
                    element, bytes, std::span<char const>(bytes).subspan(r->data).template first<fixed_size<E>()>(),
                    floor, depth + 1);
                !e) [[unlikely]]
                return e;
            out = std::move(element);
            return {};
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            auto const r = reference_take<is_text_range<U> ? major_type::text_string : major_type::byte_string>(bytes, field, floor, 1);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            auto const part = bytes.substr(r->data, r->length);
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
            constexpr std::size_t pair = fixed_size<K>() + fixed_size<V>();
            auto const r = reference_take<major_type::map>(bytes, field, floor, pair);
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->length != 0 && depth == DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            out.clear();
            for (std::size_t i = 0; i < r->length; ++i) {
                auto const at = std::span<char const>(bytes).subspan(r->data + i * pair).template first<pair>();
                K key{};
                V value{};
                if (auto const e = value_read<DepthMax>(key, bytes, at.template first<fixed_size<K>()>(), floor, depth + 1);
                    !e) [[unlikely]]
                    return e;
                if (auto const e = value_read<DepthMax>(value, bytes, at.template last<fixed_size<V>()>(), floor, depth + 1);
                    !e) [[unlikely]]
                    return e;
                out.insert_or_assign(std::move(key), std::move(value));
            }
            return {};
        } else if constexpr (std::ranges::sized_range<U>) {
            using E = std::ranges::range_value_t<U>;
            auto const r = reference_take<major_type::array>(bytes, field, floor, fixed_size<E>());
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->length != 0 && depth == DepthMax) [[unlikely]]
                return std::unexpected(error::nesting_depth_exceeded);
            out.clear();
            out.reserve(r->length);
            for (std::size_t i = 0; i < r->length; ++i) {
                E element{};
                auto const at =
                    std::span<char const>(bytes).subspan(r->data + i * fixed_size<E>()).template first<fixed_size<E>()>();
                if (auto const e = value_read<DepthMax>(element, bytes, at, floor, depth + 1); !e) [[unlikely]]
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
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend result<T> decode(std::string_view const bytes);

    template <class T>
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend std::expected<document<T>, error> view(std::string_view const bytes);

    template <class T>
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend std::expected<document<T>, error> view(std::shared_ptr<void const> owner, std::string_view const bytes);

    template <class T>
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend result<std::string, std::errc> encode(T const &value);

    template <class T, class Target>
        requires std::is_class_v<T> && std::is_aggregate_v<T>
    friend result<std::size_t, std::errc> encode(T const &value, Target &&target);

    template <class T, fixed_string Path>
    friend auto at_path_compiled(cbor::document<T> const &doc);

    template <class E>
    friend struct cbor::array;

    template <class K, class V>
    friend struct cbor::map;


    template <class T>
    friend consteval std::size_t fixed_size();
#endif

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

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
#ifdef CBOR_SIMDUTF
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

        std::expected<void, std::errc> append(std::string_view const part)
        {
            bytes.append(part);
            return {};
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }

        template <class Op>
        std::expected<void, std::errc> resize_and_overwrite(std::size_t const size, Op op)
        {
            std::size_t const at = bytes.size();
            bytes.resize_and_overwrite(at + size, [&](char *const p, std::size_t const n) {
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
    static std::expected<std::size_t, std::errc> encoded_size(second_item const &second)
    {
        std::size_t second_size;
        std::size_t size;
        if (second.overflow || ckd_mul(&second_size, second.items, sizeof(std::uint32_t)) ||
            ckd_add(&second_size, second_size, second.bytes) ||
            ckd_add(&size, directory_at<T>() + shared_first - 1 - packing_table_of<T>().size() + fixed_size<T>(),
                    second_size) ||
            !std::in_range<std::uint32_t>(size)) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return size;
    }

    template <bool Exact, class T>
    CBOR_ALWAYS_INLINE static std::size_t encoded_write(std::span<char> const bytes, T const &value, second_item const &second)
    {
        static constexpr auto prefix = packing_prefix_of<T>();
        constexpr std::size_t fillers = shared_first - 1 - packing_table_of<T>().size();
        std::size_t const size = bytes.size() - (Exact ? 0 : head_padding);
        std::ranges::copy(prefix, bytes.begin());
        u32_write(bytes, 4, shared_first + second.items);
        item_head_write(bytes, prefix.size(), major_type::byte_string, sizeof(std::uint32_t) * second.items);
        std::size_t const data = directory_at<T>() + sizeof(std::uint32_t) * second.items;
        std::ranges::fill(bytes.subspan(data, fillers),
                          static_cast<char>(std::to_underlying(major_type::simple_float) << 5 |
                                            std::to_underlying(simple_value::undefined)));
        auto const root = bytes.subspan(size - fixed_size<T>()).template first<fixed_size<T>()>();
        zero_initialized_copy<T, T>(root);
        value_encode<T, T, Exact>(bytes, root, value, encode_cursor{data + fillers, 0});
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
            auto array = binding.array_decode(std::min<std::uint64_t>(h->argument, d.bytes.size()));
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
            auto map = binding.map_decode(std::min<std::uint64_t>(h->argument, d.bytes.size() / 2));
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
                    std::size_t const at = before->document.size() - d.bytes.size();
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
                    before->offsets.at(index) < before->document.size() - d.bytes.size() &&
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
    friend std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view bytes);

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

    template <std::size_t DepthMax, class Binding>
    friend std::expected<typename Binding::value, error> path_decode(Binding &binding,
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
                    bool const immediate = info < std::to_underlying(additional_information::one_byte_argument);
                    std::size_t const size =
                        immediate ? 0
                                  : std::size_t{1} << (info - std::to_underlying(additional_information::one_byte_argument));
                    std::uint64_t argument = info;
                    if (info == std::to_underlying(additional_information::one_byte_argument)) {
                        argument = static_cast<std::uint8_t>(d.bytes.at(1));
                    } else if (!immediate) {
                        argument = unsigned_read<std::uint64_t>(std::span<char const>(d.bytes.substr(1, 8)).first<8>()) >>
                                   ((64 - 8 * size) & 63);
                    }
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

#ifdef __cpp_impl_reflection
    template <class T, std::size_t DepthMax>
    friend result<T> generic::decode(std::string_view bytes);

    template <class T>
    friend result<std::string, std::errc> generic::encode(T const &value);


    template <class T, class Target>
    friend result<std::size_t, std::errc> generic::encode(T const &value, Target &&target);

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
    static std::expected<void, error> variant_read(decoder &d, U &out, head const &h, std::size_t const depth)
    {
        if constexpr (I == std::variant_size_v<U>) {
            return std::unexpected(error::incorrect_type);
        } else {
            using A = std::variant_alternative_t<I, U>;
            if (head_accepted<A>(h))
                return generic_read<DepthMax>(d, out.template emplace<I>(), depth);
            return variant_read<DepthMax, U, I + 1>(d, out, h, depth);
        }
    }

    template <std::size_t DepthMax, class U>
    static std::expected<void, error> generic_read(decoder &d, U &out, std::size_t const depth)
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
            if (!d.bytes.empty() && static_cast<std::uint8_t>(d.bytes.front()) ==
                                        (std::to_underlying(major_type::simple_float) << 5 |
                                         std::to_underlying(simple_value::null))) {
                d.bytes.remove_prefix(1);
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
            out.reserve(std::min<std::uint64_t>(h->argument, d.bytes.size()));
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
    static std::expected<void, error> struct_read(decoder &d, U &out, std::size_t const depth)
    {
        static constexpr auto members = members_of<U>();
        constexpr std::size_t count = members.size();
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::map) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        std::bitset<count> found;
        no_marks none;
        for (std::uint64_t entry = 0; entry < h->argument; ++entry) {
            auto const k = d.head_decode();
            if (!k) [[unlikely]]
                return std::unexpected(k.error());
            std::string_view text;
            if (k->major == major_type::text_string) {
                auto const t = d.text_string_decode(k->argument);
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
                if (auto const s = item_skip<DepthMax>(d, none, depth + 1); !s) [[unlikely]]
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

struct lazy {
    std::shared_ptr<internal::document> document;
    std::size_t offset;

    static result<lazy> from(std::string &&bytes);
    static result<lazy> from(std::shared_ptr<std::string const> bytes);
    static result<lazy> from(std::shared_ptr<void const> owner, std::string_view bytes);

    template <std::size_t DepthMax = 64>
    result<lazy> at(std::string_view key) const;

    template <std::size_t DepthMax = 64>
    result<lazy> at(std::int64_t index) const;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
    result<T> get() const;

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

#ifdef __cpp_impl_reflection
template <class T>
consteval std::size_t fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = internal::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (internal::has_fixed_underlying_type<U>)
        return fixed_size<std::underlying_type_t<U>>();
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
            return internal::head_size(n) + n * fixed_size<E>();
    } else if constexpr (std::is_class_v<U> && std::is_aggregate_v<U>) {
        if constexpr (!std::meta::bases_of(^^U, std::meta::access_context::unchecked()).empty() ||
                      !internal::keys_unique(internal::data_members<U>())) {
            return no_fixed_size<U>();
        } else {
            std::size_t size = internal::head_size(std::to_underlying(internal::tag_number::straight_argument_last)) +
                               internal::initial_byte_size + sizeof(std::uint32_t);
            template for (constexpr auto m : internal::data_members<U>()) {
                if constexpr (!std::meta::has_identifier(m) || std::meta::is_bit_field(m) || !std::meta::is_public(m))
                    return no_fixed_size<U>();
                size += fixed_size<typename[:std::meta::type_of(m):]>();
            }
            return size;
        }
    } else if constexpr (internal::is_inline_optional<U>)
        return internal::inline_optional_head + fixed_size<typename U::value_type>();
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
        std::size_t offset = internal::head_size(std::to_underlying(internal::tag_number::straight_argument_last)) +
                             internal::initial_byte_size + sizeof(std::uint32_t);
        template for (constexpr auto m : internal::data_members<U>()) {
            if constexpr (m == Member)
                return offset;
            offset += fixed_size<typename[:std::meta::type_of(m):]>();
        }
        return no_fixed_size<T>();
    }
}



template <class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
std::expected<document<T>, error> view(std::string_view const bytes)
{
    auto const dir = internal::directory_read<T>(bytes);
    if (!dir) [[unlikely]]
        return std::unexpected(dir.error());
    return document<T>{{}, bytes, std::span<char const>(bytes).subspan(bytes.size() - fixed_size<T>()).template first<fixed_size<T>()>(), *dir};
}

template <class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
std::expected<document<T>, error> view(std::shared_ptr<void const> owner, std::string_view const bytes)
{
    auto const dir = internal::directory_read<T>(bytes);
    if (!dir) [[unlikely]]
        return std::unexpected(dir.error());
    return document<T>{std::move(owner), bytes, std::span<char const>(bytes).subspan(bytes.size() - fixed_size<T>()).template first<fixed_size<T>()>(), *dir};
}

template <class T, std::size_t DepthMax>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
result<T> decode(std::string_view const bytes)
{
    auto const doc = view<T>(bytes);
    if (!doc) [[unlikely]]
        return std::unexpected(doc.error());
    constexpr std::size_t fillers = internal::shared_first - 1 - internal::packing_table_of<T>().size();
    std::size_t const root = bytes.size() - fixed_size<T>();
    internal::decode_cursor c{doc->dir, 0, doc->dir.at + 4 * doc->dir.count + fillers, root};
    T out{};
    if (auto const r = internal::value_read<DepthMax>(out, doc->bytes, doc->field, c, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (c.index != doc->dir.count || c.at != root) [[unlikely]]
        return std::unexpected(error::syntax_error);
    return out;
}

namespace generic
{
template <class T, std::size_t DepthMax>
result<T> decode(std::string_view const bytes)
{
    internal::decoder d{bytes};
    T out{};
    if (auto const r = internal::generic_read<DepthMax>(d, out, 0); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (!d.bytes.empty()) [[unlikely]]
        return std::unexpected(error::syntax_error);
    return out;
}

template <class T>
CBOR_ALWAYS_INLINE inline result<std::string, std::errc> encode(T const &value)
{
    std::size_t const size = internal::generic_size(value);
    std::string out;
    out.resize_and_overwrite(size + internal::head_padding, [&](char *const p, std::size_t const n) {
        return internal::generic_write(std::span<char>(p, n), 0, value);
    });
    return out;
}

template <class T, class Target>
CBOR_ALWAYS_INLINE inline result<std::size_t, std::errc> encode(T const &value, Target &&target)
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
    std::string bytes(padded, '\0');
    bytes.resize(internal::generic_write(std::span<char>(bytes), 0, value));
    decltype(auto) message = internal::message_of(target, bytes.size());
    if (auto const r = message.append(bytes); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = message.done(bytes.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    return bytes.size();
}
}

template <class T, fixed_string Path>
CBOR_ALWAYS_INLINE inline auto at_path_compiled(document<T> const &doc)
{
    return internal::path_walk<T, Path, 0>(doc.bytes, doc.field, doc.dir);
}

template <class E>
struct array {
    std::string_view bytes;
    std::size_t data;
    std::size_t length;
    directory dir;

    std::size_t size() const
    {
        return length;
    }

    auto at(std::size_t const index) const
        -> std::expected<typename decltype(internal::path_result<E, fixed_string{""}, 0>())::type, error>
    {
        if (index >= length) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return internal::path_walk<E, fixed_string{""}, 0>(
            bytes, std::span<char const>(bytes).subspan(data + index * fixed_size<E>()).template first<fixed_size<E>()>(),
            dir);
    }

    struct iterator {
        using difference_type = std::ptrdiff_t;
        using value_type = decltype(std::declval<array const &>().at(0));

        array const *over;
        std::size_t index;

        value_type operator*() const
        {
            return over->at(index);
        }
        iterator &operator++()
        {
            ++index;
            return *this;
        }
        void operator++(int)
        {
            ++index;
        }
        bool operator==(std::default_sentinel_t) const
        {
            return index == over->length;
        }
    };

    iterator begin() const
    {
        return iterator{this, 0};
    }
    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <class K, class V>
struct map {
    std::string_view bytes;
    std::size_t data;
    std::size_t length;
    directory dir;

    static constexpr std::size_t pair_size = fixed_size<K>() + fixed_size<V>();

    std::size_t size() const
    {
        return length;
    }

    auto key_at(std::size_t const index) const
        -> std::expected<typename decltype(internal::path_result<K, fixed_string{""}, 0>())::type, error>
    {
        if (index >= length) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return internal::path_walk<K, fixed_string{""}, 0>(
            bytes, std::span<char const>(bytes).subspan(data + index * pair_size).template first<fixed_size<K>()>(), dir);
    }

    auto value_at(std::size_t const index) const
        -> std::expected<typename decltype(internal::path_result<V, fixed_string{""}, 0>())::type, error>
    {
        if (index >= length) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        return internal::path_walk<V, fixed_string{""}, 0>(
            bytes, std::span<char const>(bytes).subspan(data + index * pair_size + fixed_size<K>()).template first<fixed_size<V>()>(),
            dir);
    }

    struct iterator {
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<decltype(std::declval<map const &>().key_at(0)),
                                     decltype(std::declval<map const &>().value_at(0))>;

        map const *over;
        std::size_t index;

        value_type operator*() const
        {
            return {over->key_at(index), over->value_at(index)};
        }
        iterator &operator++()
        {
            ++index;
            return *this;
        }
        void operator++(int)
        {
            ++index;
        }
        bool operator==(std::default_sentinel_t) const
        {
            return index == over->length;
        }
    };

    iterator begin() const
    {
        return iterator{this, 0};
    }
    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <class T>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
CBOR_ALWAYS_INLINE inline result<std::string, std::errc> encode(T const &value)
{
    internal::second_item second;
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

template <class T, class Target>
    requires std::is_class_v<T> && std::is_aggregate_v<T>
CBOR_ALWAYS_INLINE inline result<std::size_t, std::errc> encode(T const &value, Target &&target)
{
    using U = std::remove_cvref_t<Target>;
    internal::second_item second;
    second.add(value);
    auto const size = internal::encoded_size<T>(second);
    if (!size) [[unlikely]]
        return std::unexpected(size.error());
    std::size_t const padded = *size + internal::head_padding;
    CBOR_ASSUME(padded >= fixed_size<T>());
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
        std::string bytes(padded, '\0');
        bytes.resize(internal::encoded_write<false>(std::span<char>(bytes), value, second));
        if (auto const r = message.append(bytes); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (auto const r = message.done(bytes.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        return bytes.size();
    }
}

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
std::expected<typename Binding::value, error> decode(Binding &binding, std::string_view bytes)
{
    internal::decoder d{bytes};
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
    cbor::result<T> get() const
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

inline result<lazy> lazy::from(std::shared_ptr<void const> owner, std::string_view const bytes)
{
    return lazy{std::make_shared<internal::document>(std::move(owner), bytes, std::vector<std::size_t>{}, 0), 0};
}

inline result<lazy> lazy::from(std::shared_ptr<std::string const> bytes)
{
    std::string_view const view = *bytes;
    return from(std::move(bytes), view);
}

inline result<lazy> lazy::from(std::string &&bytes)
{
    return from(std::make_shared<std::string const>(std::move(bytes)));
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
            auto const target = internal::container_resolve(source, source->bytes.size() - d.bytes.size());
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
            std::size_t const value = source->bytes.size() - d.bytes.size();
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
        std::size_t const element = source->bytes.size() - d.bytes.size();
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
            auto const target = internal::container_resolve(source, source->bytes.size() - d.bytes.size());
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
            std::size_t const value = source->bytes.size() - d.bytes.size();
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
result<T> lazy::get() const
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
        return *text;
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
result<lazy_elements<DepthMax>> lazy::elements() const
{
    auto const found = internal::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_elements<DepthMax>{source, source->bytes.size() - d.bytes.size(), h.argument};
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
    return lazy_entries<DepthMax>{source, source->bytes.size() - d.bytes.size(), h.argument};
}
template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l)
{
    internal::prefix before{l.document->bytes, l.document->marks, std::vector<bool>(l.document->marks.size())};
    internal::marks<Binding> shared(before.offsets.size());
    internal::decoder d{l.document->bytes.substr(l.offset)};
    return internal::value_decode<DepthMax>(d, binding, shared, &before, 0, std::nullopt);
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

template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> path_decode(Binding &binding, std::span<path_step const> const steps,
                                                       lazy const &l)
{
    lazy at = l;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        path_step const &step = steps.subspan(i).front();
        if (step.kind == path_step::kind::key) {
            auto const next = at.at<DepthMax>(step.key);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            at = *next;
        } else if (step.kind == path_step::kind::index) {
            auto const next = at.at<DepthMax>(step.index);
            if (!next) [[unlikely]]
                return std::unexpected(next.error());
            at = *next;
        } else {
            auto const elements = at.elements<DepthMax>();
            if (!elements) [[unlikely]]
                return std::unexpected(elements.error());
            auto array = binding.array_decode(std::min<std::uint64_t>(elements->count, elements->document->bytes.size() - elements->offset));
            for (auto const element : *elements) {
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                auto value = path_decode<DepthMax>(binding, steps.subspan(i + 1), *element);
                if (!value) [[unlikely]]
                    return value;
                array = binding.array_append(std::move(array), std::move(*value));
            }
            return array;
        }
    }
    return lazy_decode<DepthMax>(binding, at);
}

}

template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};

template <>
struct std::is_error_condition_enum<cbor::condition> : std::true_type {};

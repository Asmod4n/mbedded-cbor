#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <optional>
#include <span>
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
    reserved_simple_value
};

enum class condition { not_well_formed = 1, not_valid, not_supported };

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
            return {static_cast<int>(condition::not_supported), *this};
        case error::invalid_utf8_string:
        case error::inadmissible_type_for_tag_content:
        case error::sharedref_index_not_marked:
        case error::sharedref_index_out_of_range:
        case error::sharedref_not_complete:
        case error::reserved_simple_value:
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

template <class Tag>
struct customization_point {
    template <class... Args>
    constexpr decltype(auto) operator()(Args &&...args) const
    {
        return tag_invoke(static_cast<Tag const &>(*this), std::forward<Args>(args)...);
    }
};

inline constexpr struct unsigned_integer_decode_t : customization_point<unsigned_integer_decode_t> {
} unsigned_integer_decode;
inline constexpr struct negative_integer_decode_t : customization_point<negative_integer_decode_t> {
} negative_integer_decode;
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
inline constexpr struct value_encode_t : customization_point<value_encode_t> {
} value_encode;
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

template <std::size_t DepthMax, sharedrefs Sharing = sharedrefs::off, class Host, class Writer>
std::expected<void, std::error_code> encode(Host &host, Writer &writer, typename Host::value const &value);

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

    enum class tag_number : std::uint64_t { shareable = 28, sharedref = 29 };

    template <class Host>
    using marks = std::vector<std::optional<typename Host::value>>;

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
    static std::expected<typename Host::value, error> value_decode(decoder &d, Host &host,
                                                                   marks<Host> &shared, std::size_t depth,
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
            auto array = array_decode(host);
            if (mark)
                shared.at(*mark) = array;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = value_decode<DepthMax>(d, host, shared, depth + 1, std::nullopt);
                if (!element) [[unlikely]]
                    return element;
                array = array_append(host, std::move(array), std::move(*element));
            }
            return array;
        }
        case major_type::map: {
            auto map = map_decode(host);
            if (mark)
                shared.at(*mark) = map;
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = value_decode<DepthMax>(d, host, shared, depth + 1, std::nullopt);
                if (!key) [[unlikely]]
                    return key;
                auto value = value_decode<DepthMax>(d, host, shared, depth + 1, std::nullopt);
                if (!value) [[unlikely]]
                    return value;
                map = map_insert(host, std::move(map), std::move(*key), std::move(*value));
            }
            return map;
        }
        case major_type::tag: {
            if (h->argument == std::to_underlying(tag_number::shareable)) {
                std::size_t const index = shared.size();
                shared.emplace_back();
                auto content = value_decode<DepthMax>(d, host, shared, depth + 1, index);
                if (!content) [[unlikely]]
                    return content;
                if (!shared.at(index))
                    shared.at(index) = *content;
                return content;
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
                if (!shared.at(index)) [[unlikely]]
                    return std::unexpected(error::sharedref_not_complete);
                return *shared.at(index);
            }
            auto content = value_decode<DepthMax>(d, host, shared, depth + 1, std::nullopt);
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

    template <std::size_t, class, class, class>
    friend class visitor;
};

template <class Writer>
struct encoder {
    Writer &writer;

    std::expected<void, std::errc> head_encode(major_type major, std::uint64_t argument)
    {
        std::array<char, 9> head;
        std::size_t size;
        char const initial = static_cast<char>(std::to_underlying(major) << 5);
        if (argument < std::to_underlying(internal::additional_information::one_byte_argument)) {
            std::get<0>(head) = static_cast<char>(initial | argument);
            size = 1;
        } else if (argument <= 0xff) {
            std::get<0>(head) = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::one_byte_argument));
            std::get<1>(head) = static_cast<char>(argument);
            size = 2;
        } else if (argument <= 0xffff) {
            std::get<0>(head) = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::two_byte_argument));
            auto const v = std::byteswap(static_cast<std::uint16_t>(argument));
            std::memcpy(std::span(head).template subspan<1>().data(), &v, 2);
            size = 3;
        } else if (argument <= 0xffffffff) {
            std::get<0>(head) = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::four_byte_argument));
            auto const v = std::byteswap(static_cast<std::uint32_t>(argument));
            std::memcpy(std::span(head).template subspan<1>().data(), &v, 4);
            size = 5;
        } else {
            std::get<0>(head) = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::eight_byte_argument));
            auto const v = std::byteswap(argument);
            std::memcpy(std::span(head).template subspan<1>().data(), &v, 8);
            size = 9;
        }
        return writer.append(std::string_view(head.data(), size));
    }

    std::expected<void, std::errc> byte_string_encode(std::string_view bytes)
    {
        if (auto const r = writer.reserve(9 + bytes.size()); !r) [[unlikely]]
            return r;
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        return writer.append(bytes);
    }

    std::expected<void, std::errc> text_string_encode(std::string_view text)
    {
        if (auto const r = writer.reserve(9 + text.size()); !r) [[unlikely]]
            return r;
        if (auto const r = head_encode(major_type::text_string, text.size()); !r) [[unlikely]]
            return r;
        return writer.append(text);
    }
    std::expected<void, std::errc> float_encode(double value)
    {
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
        return writer.append(std::string_view(item.data(), size));
    }
};

template <std::size_t DepthMax, class Host>
std::expected<typename Host::value, error> decode(Host &host, std::string_view bytes)
{
    internal::decoder d{bytes};
    internal::marks<Host> shared;
    return internal::value_decode<DepthMax>(d, host, shared, 0, std::nullopt);
}

enum class pass { plain, count, write };

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
};

template <std::size_t DepthMax, class Host, class Writer, class Pass>
class visitor
{
    Host &host;
    encoder<Writer> out;
    sharing<Host> *shared;
    std::size_t depth = 0;
    std::error_code failure;

    template <std::size_t, sharedrefs, class H, class W>
    friend std::expected<void, std::error_code> encode(H &host, W &writer, typename H::value const &value);

    visitor(Host &h, Writer &w, sharing<Host> *s) : host(h), out{w}, shared(s)
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

    template <class Identity>
    void child(typename Host::value const &item, Identity const &identity)
    {
        if (failure) [[unlikely]]
            return;
        if (depth > DepthMax) [[unlikely]] {
            keep_error(error::nesting_depth_exceeded);
            return;
        }
        if constexpr (Pass::value == pass::count) {
            if (identity && ++shared->seen[*identity] > 1)
                return;
        }
        if constexpr (Pass::value == pass::write) {
            if (identity && shared->seen.at(*identity) > 1) {
                auto const number = shared->numbers.find(*identity);
                if (number != shared->numbers.end()) {
                    keep(out.head_encode(major_type::tag,
                                         std::to_underlying(internal::tag_number::sharedref)));
                    keep(out.head_encode(major_type::unsigned_integer, number->second));
                    return;
                }
                shared->numbers.emplace(*identity, shared->numbers.size());
                keep(out.head_encode(major_type::tag, std::to_underlying(internal::tag_number::shareable)));
            }
        }
        ++depth;
        value_encode(host, item, *this);
        --depth;
    }

public:
    visitor(visitor const &) = delete;
    visitor &operator=(visitor const &) = delete;

    void unsigned_integer(std::uint64_t const n)
    {
        keep(out.head_encode(major_type::unsigned_integer, n));
    }

    void negative_integer(std::uint64_t const argument)
    {
        keep(out.head_encode(major_type::negative_integer, argument));
    }

    void byte_string(std::string_view const bytes)
    {
        keep(out.byte_string_encode(bytes));
    }

    void text_string(std::string_view const text)
    {
        keep(out.text_string_encode(text));
    }

    void floating_point(double const value)
    {
        keep(out.float_encode(value));
    }

    void simple_value(std::uint8_t const value)
    {
        if (value >= std::to_underlying(internal::simple_float_information::simple_value_follows) &&
            value < internal::simple_value_one_byte_min) [[unlikely]] {
            keep_error(error::reserved_simple_value);
            return;
        }
        keep(out.head_encode(major_type::simple_float, value));
    }

    void array(std::uint64_t const size)
    {
        keep(out.head_encode(major_type::array, size));
    }

    void map(std::uint64_t const size)
    {
        keep(out.head_encode(major_type::map, size));
    }

    void tag(std::uint64_t const number)
    {
        keep(out.head_encode(major_type::tag, number));
    }

    void key(typename Host::value const &item)
    {
        if constexpr (Pass::value == pass::plain)
            child(item, std::false_type{});
        else
            child(item, key_identity(host, item));
    }

    void value(typename Host::value const &item)
    {
        if constexpr (Pass::value == pass::plain)
            child(item, std::false_type{});
        else
            child(item, value_identity(host, item));
    }
};

template <std::size_t DepthMax, sharedrefs Sharing, class Host, class Writer>
std::expected<void, std::error_code> encode(Host &host, Writer &writer, typename Host::value const &value)
{
    if constexpr (Sharing == sharedrefs::off) {
        visitor<DepthMax, Host, Writer, std::integral_constant<pass, pass::plain>> visit{host, writer,
                                                                                         nullptr};
        visit.value(value);
        if (visit.failure) [[unlikely]]
            return std::unexpected(visit.failure);
    } else {
        sharing<Host> shared;
        discarding_writer nothing;
        visitor<DepthMax, Host, discarding_writer, std::integral_constant<pass, pass::count>> count{
            host, nothing, &shared};
        count.value(value);
        if (count.failure) [[unlikely]]
            return std::unexpected(count.failure);
        visitor<DepthMax, Host, Writer, std::integral_constant<pass, pass::write>> write{host, writer,
                                                                                         &shared};
        write.value(value);
        if (write.failure) [[unlikely]]
            return std::unexpected(write.failure);
    }
    return {};
}

} // namespace cbor

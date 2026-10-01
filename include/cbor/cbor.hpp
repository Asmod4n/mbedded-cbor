#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#if CBOR_SIMDUTF
#include <simdutf.h>
#endif
#include <system_error>
#include <utility>

namespace cbor
{

enum class error {
    too_little_data,
    syntax_error,
    indefinite_length,
    invalid_utf8_string,
    nesting_depth_exceeded
};

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

class internal
{
    enum class additional_information : std::uint8_t {
        one_byte_argument = 24,
        two_byte_argument,
        four_byte_argument,
        eight_byte_argument,
        indefinite_length = 31
    };

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
            std::uint64_t argument;
            switch (static_cast<additional_information>(info)) {
            case additional_information::one_byte_argument:
                argument = static_cast<std::uint8_t>(bytes[1]);
                break;
            case additional_information::two_byte_argument: {
                std::uint16_t v;
                std::memcpy(&v, bytes.data() + 1, 2);
                argument = std::byteswap(v);
            } break;
            case additional_information::four_byte_argument: {
                std::uint32_t v;
                std::memcpy(&v, bytes.data() + 1, 4);
                argument = std::byteswap(v);
            } break;
            default: {
                std::uint64_t v;
                std::memcpy(&v, bytes.data() + 1, 8);
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

    static float float_decode_binary16(std::uint16_t half)
    {
        std::uint32_t const sign = half & 0x8000u;
        std::uint32_t const exp = half & 0x7c00u;
        std::uint32_t const frac = half & 0x03ffu;
        if (exp == 0x7c00u)
            return std::bit_cast<float>(sign << 16 | 0x7f800000u | frac << 13);
        if (exp != 0)
            return std::bit_cast<float>(sign << 16 | ((exp >> 10) + (127 - 15)) << 23 | frac << 13);
        if (frac == 0)
            return std::bit_cast<float>(sign << 16);
        int const shift = std::countl_zero(static_cast<std::uint16_t>(frac << 5));
        std::uint32_t const mant = (frac << shift) & 0x03ffu;
        return std::bit_cast<float>(sign << 16 | static_cast<std::uint32_t>(127 - 14 - shift) << 23 |
                                    mant << 13);
    }

    static std::uint16_t float_encode_binary16(float value)
    {
        std::uint32_t const bits = std::bit_cast<std::uint32_t>(value);
        std::uint32_t const sign = bits >> 31;
        std::uint32_t const exp32 = bits >> 23 & 0xffu;
        std::uint32_t const mant32 = bits & 0x7fffffu;
        std::uint32_t exp16;
        std::uint32_t mant16;
        if (exp32 == 0xff) {
            if (mant32 != 0)
                return 0x7e00u;
            exp16 = 0x1f;
            mant16 = 0;
        } else if (exp32 == 0) {
            exp16 = 0;
            mant16 = 0;
        } else if (exp32 >= 113) {
            exp16 = exp32 - 112;
            mant16 = mant32 >> 13;
        } else {
            exp16 = 0;
            mant16 = (0x800000u | mant32) >> (126 - exp32);
        }
        return static_cast<std::uint16_t>(sign << 15 | exp16 << 10 | mant16);
    }

    static simple_float_information preferred_float_info(double value)
    {
        std::uint64_t const bits = std::bit_cast<std::uint64_t>(value);
        std::uint32_t const exp = bits >> 52 & 0x7ffu;
        std::uint64_t const mant = bits & 0xfffffffffffffu;
        if (exp == 0x7ff)
            return simple_float_information::half_precision_float;
        if (exp == 0)
            return mant == 0 ? simple_float_information::half_precision_float
                             : simple_float_information::double_precision_float;
        if ((mant & 0x1fffffffu) != 0 || exp < 897 || exp > 1150)
            return simple_float_information::double_precision_float;
        if (exp >= 1009 && exp <= 1038)
            return (mant >> 29 & 0x1fffu) == 0 ? simple_float_information::half_precision_float
                                               : simple_float_information::single_precision_float;
        if (exp >= 999 && exp <= 1008)
            return (mant >> 29 & ((1u << (1022 - exp)) - 1u)) == 0
                       ? simple_float_information::half_precision_float
                       : simple_float_information::single_precision_float;
        return simple_float_information::single_precision_float;
    }

    template <std::size_t DepthMax, class Host>
    static std::expected<typename Host::value, error> value_decode(decoder &d, Host &host, std::size_t depth)
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
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto element = value_decode<DepthMax>(d, host, depth + 1);
                if (!element) [[unlikely]]
                    return element;
                array = array_append(host, std::move(array), std::move(*element));
            }
            return array;
        }
        case major_type::map: {
            auto map = map_decode(host);
            for (std::uint64_t i = 0; i < h->argument; ++i) {
                auto key = value_decode<DepthMax>(d, host, depth + 1);
                if (!key) [[unlikely]]
                    return key;
                auto value = value_decode<DepthMax>(d, host, depth + 1);
                if (!value) [[unlikely]]
                    return value;
                map = map_insert(host, std::move(map), std::move(*key), std::move(*value));
            }
            return map;
        }
        case major_type::tag: {
            auto content = value_decode<DepthMax>(d, host, depth + 1);
            if (!content) [[unlikely]]
                return content;
            return tag_decode(host, h->argument, std::move(*content));
        }
        default:
            switch (static_cast<simple_float_information>(h->info)) {
            case simple_float_information::simple_value_follows:
                if (h->argument < 32) [[unlikely]]
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
            head[0] = static_cast<char>(initial | argument);
            size = 1;
        } else if (argument <= 0xff) {
            head[0] = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::one_byte_argument));
            head[1] = static_cast<char>(argument);
            size = 2;
        } else if (argument <= 0xffff) {
            head[0] = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::two_byte_argument));
            auto const v = std::byteswap(static_cast<std::uint16_t>(argument));
            std::memcpy(head.data() + 1, &v, 2);
            size = 3;
        } else if (argument <= 0xffffffff) {
            head[0] = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::four_byte_argument));
            auto const v = std::byteswap(static_cast<std::uint32_t>(argument));
            std::memcpy(head.data() + 1, &v, 4);
            size = 5;
        } else {
            head[0] = static_cast<char>(
                initial | std::to_underlying(internal::additional_information::eight_byte_argument));
            auto const v = std::byteswap(argument);
            std::memcpy(head.data() + 1, &v, 8);
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
            item[0] = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::half_precision_float));
            auto const v = std::byteswap(internal::float_encode_binary16(static_cast<float>(value)));
            std::memcpy(item.data() + 1, &v, 2);
            size = 3;
        } break;
        case internal::simple_float_information::single_precision_float: {
            item[0] = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::single_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint32_t>(static_cast<float>(value)));
            std::memcpy(item.data() + 1, &v, 4);
            size = 5;
        } break;
        default: {
            item[0] = static_cast<char>(
                std::to_underlying(major_type::simple_float) << 5 |
                std::to_underlying(internal::simple_float_information::double_precision_float));
            auto const v = std::byteswap(std::bit_cast<std::uint64_t>(value));
            std::memcpy(item.data() + 1, &v, 8);
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
    return internal::value_decode<DepthMax>(d, host, 0);
}

} // namespace cbor

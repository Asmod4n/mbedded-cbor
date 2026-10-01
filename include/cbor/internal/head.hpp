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

namespace cbor::internal
{

enum class error { too_little_data, syntax_error, indefinite_length, invalid_utf8_string };

struct head {
    std::uint8_t major;
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
        std::uint8_t const major = initial >> 5;
        std::uint8_t const info = initial & 0x1f;
        if (info < 24) {
            bytes.remove_prefix(1);
            return head{major, info, info};
        }
        if (info == 31 && major >= 2 && major <= 5) [[unlikely]]
            return std::unexpected(error::indefinite_length);
        if (info > 27) [[unlikely]]
            return std::unexpected(error::syntax_error);
        std::size_t const size = std::size_t{1} << (info - 24);
        if (bytes.size() < 1 + size) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::uint64_t argument;
        switch (info) {
        case 24:
            argument = static_cast<std::uint8_t>(bytes[1]);
            break;
        case 25: {
            std::uint16_t v;
            std::memcpy(&v, bytes.data() + 1, 2);
            argument = std::byteswap(v);
        } break;
        case 26: {
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

template <class Writer>
struct encoder {
    Writer &writer;

    std::expected<void, std::errc> head_encode(std::uint8_t major, std::uint64_t argument)
    {
        std::array<char, 9> head;
        std::size_t size;
        char const initial = static_cast<char>(major << 5);
        if (argument < 24) {
            head[0] = static_cast<char>(initial | argument);
            size = 1;
        } else if (argument <= 0xff) {
            head[0] = static_cast<char>(initial | 24);
            head[1] = static_cast<char>(argument);
            size = 2;
        } else if (argument <= 0xffff) {
            head[0] = static_cast<char>(initial | 25);
            auto const v = std::byteswap(static_cast<std::uint16_t>(argument));
            std::memcpy(head.data() + 1, &v, 2);
            size = 3;
        } else if (argument <= 0xffffffff) {
            head[0] = static_cast<char>(initial | 26);
            auto const v = std::byteswap(static_cast<std::uint32_t>(argument));
            std::memcpy(head.data() + 1, &v, 4);
            size = 5;
        } else {
            head[0] = static_cast<char>(initial | 27);
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
        if (auto const r = head_encode(2, bytes.size()); !r) [[unlikely]]
            return r;
        return writer.append(bytes);
    }

    std::expected<void, std::errc> text_string_encode(std::string_view text)
    {
        if (auto const r = writer.reserve(9 + text.size()); !r) [[unlikely]]
            return r;
        if (auto const r = head_encode(3, text.size()); !r) [[unlikely]]
            return r;
        return writer.append(text);
    }
};

} // namespace cbor::internal

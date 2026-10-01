#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>

namespace cbor::internal
{

enum class error { too_little_data, syntax_error, indefinite_length };

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
};

template <class StringWriter>
struct encoder {
    StringWriter &string_writer;
    std::span<char> free;

    void head_encode(std::uint8_t major, std::uint64_t argument)
    {
        if (free.size() < 9) [[unlikely]]
            free = string_writer.grow(free.size(), 9);
        char const initial = static_cast<char>(major << 5);
        if (argument < 24) {
            free[0] = static_cast<char>(initial | argument);
            free = free.subspan(1);
        } else if (argument <= 0xff) {
            free[0] = static_cast<char>(initial | 24);
            free[1] = static_cast<char>(argument);
            free = free.subspan(2);
        } else if (argument <= 0xffff) {
            free[0] = static_cast<char>(initial | 25);
            auto const v = std::byteswap(static_cast<std::uint16_t>(argument));
            std::memcpy(free.data() + 1, &v, 2);
            free = free.subspan(3);
        } else if (argument <= 0xffffffff) {
            free[0] = static_cast<char>(initial | 26);
            auto const v = std::byteswap(static_cast<std::uint32_t>(argument));
            std::memcpy(free.data() + 1, &v, 4);
            free = free.subspan(5);
        } else {
            free[0] = static_cast<char>(initial | 27);
            auto const v = std::byteswap(argument);
            std::memcpy(free.data() + 1, &v, 8);
            free = free.subspan(9);
        }
    }
};

} // namespace cbor::internal

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <system_error>

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
        std::errc const e = writer.append(std::string_view(head.data(), size));
        if (e != std::errc{}) [[unlikely]]
            return std::unexpected(e);
        return {};
    }
};

} // namespace cbor::internal

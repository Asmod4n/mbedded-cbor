#pragma once

#include "lazy.hpp"

#include <array>
#include <functional>
#include <utility>

namespace site
{

template <class Target>
struct encoder_over {
    std::shared_ptr<void> target;
    cbor::encoder<Target> encoder;

    template <class Argument>
    encoder_over(std::shared_ptr<void> t, Argument &&argument)
        : target(std::move(t)), encoder(std::forward<Argument>(argument))
    {
    }
};

inline std::string bytes_text(std::span<std::byte const> const bytes)
{
    return number_text(bytes.size()) + " bytes: " + hex_text({reinterpret_cast<char const *>(bytes.data()), bytes.size()});
}

template <>
struct kind<cbor::item> {
    static constexpr std::string_view name = "cbor::item";
    static std::string text(cbor::item const &v)
    {
        return "item of " + item_notation(v);
    }
};

template <>
struct kind<std::reference_wrapper<cbor::item const>> {
    static constexpr std::string_view name = "std::reference_wrapper<cbor::item const>";
    static std::string text(std::reference_wrapper<cbor::item const> const v)
    {
        return "reference to item of " + item_notation(v.get());
    }
};

template <>
struct kind<std::span<cbor::item const>> {
    static constexpr std::string_view name = "std::span<cbor::item const>";
    static std::string text(std::span<cbor::item const> const v)
    {
        std::string out = "span of " + number_text(v.size()) + (v.size() == 1 ? " item" : " items");
        char const *separator = ": ";
        for (cbor::item const &node : v) {
            out += separator + item_notation(node);
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<std::pair<std::uint64_t, cbor::item const *>> {
    static constexpr std::string_view name = "std::pair<std::uint64_t, cbor::item const *>";
    static std::string text(std::pair<std::uint64_t, cbor::item const *> const &v)
    {
        return "pair of " + number_text(v.first) + " and " + kind<cbor::item const *>::text(v.second);
    }
};

template <>
struct kind<cbor::simple_value> {
    static constexpr std::string_view name = "cbor::simple_value";
    static std::string text(cbor::simple_value const v)
    {
        switch (v) {
        case cbor::simple_value::false_value:
            return "cbor::simple_value::false_value";
        case cbor::simple_value::true_value:
            return "cbor::simple_value::true_value";
        case cbor::simple_value::null:
            return "cbor::simple_value::null";
        case cbor::simple_value::undefined:
            return "cbor::simple_value::undefined";
        }
        return "cbor::simple_value{" + number_text(std::to_underlying(v)) + "}";
    }
};

template <>
struct kind<std::span<std::string_view const>> {
    static constexpr std::string_view name = "std::span<std::string_view const>";
    static std::string text(std::span<std::string_view const> const v)
    {
        std::string out = "span of " + number_text(v.size()) + (v.size() == 1 ? " piece" : " pieces");
        char const *separator = ": ";
        for (std::string_view const piece : v) {
            out += separator + hex_text(piece);
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<cbor::ref<std::span<std::string_view const>>> {
    static constexpr std::string_view name = "cbor::ref<std::span<std::string_view const>>";
    static std::string text(cbor::ref<std::span<std::string_view const>> const &v)
    {
        return "ref to " + kind<std::span<std::string_view const>>::text(*v);
    }
};

template <>
struct kind<cbor::iov_writer> {
    static constexpr std::string_view name = "cbor::iov_writer";
    static std::string text(cbor::iov_writer const &)
    {
        return "iov_writer";
    }
};

template <>
struct kind<encoder_over<std::string>> {
    static constexpr std::string_view name = "cbor::encoder<std::string>";
    static std::string text(encoder_over<std::string> const &)
    {
        return "encoder over a std::string";
    }
};

template <>
struct kind<encoder_over<std::span<char>>> {
    static constexpr std::string_view name = "cbor::encoder<std::span<char>>";
    static std::string text(encoder_over<std::span<char>> const &)
    {
        return "encoder over a std::span<char>";
    }
};

template <>
struct kind<encoder_over<cbor::iov_writer>> {
    static constexpr std::string_view name = "cbor::encoder<cbor::iov_writer>";
    static std::string text(encoder_over<cbor::iov_writer> const &)
    {
        return "encoder over a cbor::iov_writer";
    }
};

template <>
struct kind<std::array<char, 16>> {
    static constexpr std::string_view name = "std::array<char, 16>";
    static std::string text(std::array<char, 16> const &v)
    {
        return string_text({v.data(), v.size()});
    }
};

template <>
struct kind<std::array<std::uint8_t, 2>> {
    static constexpr std::string_view name = "std::array<std::uint8_t, 2>";
    static std::string text(std::array<std::uint8_t, 2> const &v)
    {
        return "{" + number_text(unsigned{v[0]}) + ", " + number_text(unsigned{v[1]}) + "}";
    }
};

template <>
struct kind<std::span<char>> {
    static constexpr std::string_view name = "std::span<char>";
    static std::string text(std::span<char> const v)
    {
        return bytes_text(std::as_bytes(v));
    }
};

template <>
struct kind<std::span<std::uint8_t, 2>> {
    static constexpr std::string_view name = "std::span<std::uint8_t, 2>";
    static std::string text(std::span<std::uint8_t, 2> const v)
    {
        return bytes_text(std::as_bytes(v));
    }
};

template <>
struct kind<std::span<std::byte const, 2>> {
    static constexpr std::string_view name = "std::span<std::byte const, 2>";
    static std::string text(std::span<std::byte const, 2> const v)
    {
        return bytes_text(v);
    }
};

template <>
struct kind<cbor::typed_array> {
    static constexpr std::string_view name = "cbor::typed_array";
    static std::string text(cbor::typed_array const &v)
    {
        return "typed_array of tag " + number_text(v.tag) + " over " + bytes_text(v.bytes);
    }
};

}

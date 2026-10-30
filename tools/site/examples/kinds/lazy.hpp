#pragma once

#include "core.hpp"

namespace site
{

constexpr std::string_view type_names[] = {
    "unsigned_integer", "negative_integer", "byte_string",  "text_string",     "array",           "map",
    "tag",              "false_value",      "true_value",   "null",            "undefined",       "simple_value",
    "half_float",       "single_float",     "double_float", "unsigned_bignum", "negative_bignum", "typed_array"};

static_assert(std::size(type_names) == std::to_underlying(cbor::type::typed_array) + 1);

inline std::string notation_of(std::string_view const encoded)
{
    auto const text = cbor::diagnostic_notation(encoded);
    return text ? *text : kind<cbor::error>::text(text.error());
}

inline std::string item_notation(cbor::item const &node)
{
    std::string encoded;
    if (auto const written = cbor::encode(node, encoded); !written) [[unlikely]]
        return kind<cbor::error>::text(written.error());
    return notation_of(encoded);
}

inline std::string lazy_notation(cbor::lazy const &node)
{
    auto const tree = node.clone();
    if (!tree) [[unlikely]]
        return kind<cbor::error>::text(tree.error());
    return item_notation(**tree);
}

template <>
struct kind<cbor::type> {
    static constexpr std::string_view name = "cbor::type";
    static std::string text(cbor::type const t)
    {
        return "cbor::type::" + std::string(type_names[std::to_underlying(t)]);
    }
};

template <>
struct kind<cbor::key> {
    static constexpr std::string_view name = "cbor::key";
    static std::string text(cbor::key const k)
    {
        return "cbor::key{" + number_text(k.number) + "}";
    }
};

template <>
struct kind<std::string_view> {
    static constexpr std::string_view name = "std::string_view";
    static std::string text(std::string_view const v)
    {
        return string_text(v);
    }
};

template <>
struct kind<std::span<std::byte const>> {
    static constexpr std::string_view name = "std::span<std::byte const>";
    static std::string text(std::span<std::byte const> const v)
    {
        return number_text(v.size()) + " bytes: " +
               hex_text({reinterpret_cast<char const *>(v.data()), v.size()});
    }
};

template <class T>
struct kind<cbor::ref<T>> {
    static constexpr std::string_view name = kind<T>::name == "std::string_view" ? "cbor::ref<std::string_view>"
                                                                                : "cbor::ref<std::span<std::byte const>>";
    static std::string text(cbor::ref<T> const &v)
    {
        return "ref to " + kind<T>::text(*v);
    }
};

template <>
struct kind<cbor::lazy> {
    static constexpr std::string_view name = "cbor::lazy";
    static std::string text(cbor::lazy const &v)
    {
        return "lazy of " + lazy_notation(v);
    }
};

template <>
struct kind<cbor::nodelist> {
    static constexpr std::string_view name = "cbor::nodelist";
    static std::string text(cbor::nodelist const &v)
    {
        std::string out = "nodelist of " + number_text(v.size()) + (v.size() == 1 ? " node" : " nodes");
        char const *separator = ": ";
        for (cbor::lazy const &n : v) {
            out += separator + lazy_notation(n);
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<cbor::lazy_elements> {
    static constexpr std::string_view name = "cbor::lazy_elements";
    static std::string text(cbor::lazy_elements const &v)
    {
        std::string out = "lazy_elements of " + number_text(v.size()) + (v.size() == 1 ? " element" : " elements");
        char const *separator = ": ";
        for (auto const &e : v) {
            out += separator + (e ? lazy_notation(*e) : kind<cbor::error>::text(e.error()));
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<cbor::lazy_entries> {
    static constexpr std::string_view name = "cbor::lazy_entries";
    static std::string text(cbor::lazy_entries const &v)
    {
        std::string out = "lazy_entries of " + number_text(v.size()) + (v.size() == 1 ? " pair" : " pairs");
        char const *separator = ": ";
        for (auto const &e : v) {
            out += separator +
                   (e ? lazy_notation(e->first) + ": " + lazy_notation(e->second) : kind<cbor::error>::text(e.error()));
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<cbor::owning<cbor::item>> {
    static constexpr std::string_view name = "cbor::owning<cbor::item>";
    static std::string text(cbor::owning<cbor::item> const &v)
    {
        return "owning item of " + item_notation(*v);
    }
};

template <>
struct kind<cbor::item const *> {
    static constexpr std::string_view name = "cbor::item const *";
    static std::string text(cbor::item const *const v)
    {
        return "pointer to item of " + item_notation(*v);
    }
};

}

#pragma once

#include "lazy.hpp"

#include "../../../../test/doc_types.hpp"

#include <format>
#include <optional>
#include <set>
#include <string>

namespace site
{

template <class T>
struct pointer_kind {
    static std::string text(T const *const v)
    {
        return "pointer to " + kind<T>::text(*v);
    }
};

template <class T>
struct ref_kind {
    static std::string text(cbor::ref<T> const &v)
    {
        return "ref to " + kind<T>::text(*v);
    }
};

template <>
struct kind<cbor::tag> {
    static constexpr std::string_view name = "cbor::tag";
    static std::string text(cbor::tag const v)
    {
        return "cbor::tag{" + number_text(v.number) + "}";
    }
};

template <>
struct kind<std::nullopt_t> {
    static constexpr std::string_view name = "std::nullopt_t";
    static std::string text(std::nullopt_t)
    {
        return "std::nullopt";
    }
};

template <>
struct kind<std::optional<std::uint8_t>> {
    static constexpr std::string_view name = "std::optional<std::uint8_t>";
    static std::string text(std::optional<std::uint8_t> const &v)
    {
        return v ? "std::optional<std::uint8_t>{" + number_text(static_cast<unsigned>(*v)) + "}"
                 : "std::nullopt";
    }
};

template <>
struct kind<reference_structs::person> {
    static constexpr std::string_view name = "person";
    static std::string text(reference_structs::person const &v)
    {
        return std::format("person{{{:?}, {}}}", v.name, number_text(v.age));
    }
};

template <>
struct kind<reference_structs::person const *> : pointer_kind<reference_structs::person> {
    static constexpr std::string_view name = "person const *";
};

template <>
struct kind<cbor::ref<reference_structs::person>> : ref_kind<reference_structs::person> {
    static constexpr std::string_view name = "cbor::ref<person>";
};

template <>
struct kind<reference_structs::header> {
    static constexpr std::string_view name = "header";
    static std::string text(reference_structs::header const &v)
    {
        return std::format("header{{{:?}}}", v.content_type);
    }
};

template <>
struct kind<reference_structs::senml> {
    static constexpr std::string_view name = "senml";
    static std::string text(reference_structs::senml const &v)
    {
        return std::format("senml{{{:?}, {}}}", v.n, number_text(v.v));
    }
};

template <>
struct kind<reference_structs::account> {
    static constexpr std::string_view name = "account";
    static std::string text(reference_structs::account const &v)
    {
        return std::format("account{{{:?}, {}}}", v.user, number_text(static_cast<unsigned>(v.level)));
    }
};

template <>
struct kind<reference_structs::reading> {
    static constexpr std::string_view name = "reading";
    static std::string text(reference_structs::reading const &v)
    {
        return "reading{" + number_text(static_cast<unsigned>(v.id)) + ", " +
               (v.unit ? number_text(static_cast<unsigned>(*v.unit)) : std::string("std::nullopt")) + "}";
    }
};

template <>
struct kind<cbor::ref<reference_structs::reading>> : ref_kind<reference_structs::reading> {
    static constexpr std::string_view name = "cbor::ref<reading>";
};

template <>
struct kind<reference_structs::trailer> {
    static constexpr std::string_view name = "trailer";
    static std::string text(reference_structs::trailer const &v)
    {
        return "trailer{" + number_text(static_cast<unsigned>(v.checksum)) + "}";
    }
};

template <>
struct kind<std::set<std::uint8_t>> {
    static constexpr std::string_view name = "std::set<std::uint8_t>";
    static std::string text(std::set<std::uint8_t> const &v)
    {
        std::string out = "std::set<std::uint8_t>{";
        char const *separator = "";
        for (std::uint8_t const e : v) {
            out += separator + number_text(static_cast<unsigned>(e));
            separator = ", ";
        }
        return out + "}";
    }
};

} // namespace site

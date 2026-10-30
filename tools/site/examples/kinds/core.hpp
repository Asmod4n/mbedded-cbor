#pragma once

#include "../value.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <variant>

namespace site
{

constexpr std::string_view error_names[] = {"",
                                            "too_little_data",
                                            "syntax_error",
                                            "indefinite_length",
                                            "nesting_depth_exceeded",
                                            "inadmissible_type_for_tag_content",
                                            "sharedref_index_not_marked",
                                            "sharedref_index_out_of_range",
                                            "sharedref_not_complete",
                                            "reserved_simple_value",
                                            "unsupported_value",
                                            "not_indexable",
                                            "index_out_of_bounds",
                                            "key_not_found",
                                            "invalid_path",
                                            "incorrect_type",
                                            "number_out_of_range",
                                            "cyclic_data_structure",
                                            "unpopulated_table_index",
                                            "nodelist_too_long",
                                            "duplicate_key",
                                            "no_buffer_space",
                                            "value_too_large",
                                            "not_enough_memory",
                                            "io_error",
                                            "string_length_exceeded",
                                            "container_elements_exceeded",
                                            "not_singular_query"};

static_assert(std::size(error_names) == std::to_underlying(cbor::error::not_singular_query) + 1);

template <>
struct kind<cbor::error> {
    static constexpr std::string_view name = "cbor::error";
    static std::string text(cbor::error const e)
    {
        auto const i = static_cast<std::size_t>(std::to_underlying(e));
        return i < std::size(error_names) ? "cbor::error::" + std::string(error_names[i]) : "cbor::error(" + number_text(i) + ")";
    }
};

template <>
struct kind<std::monostate> {
    static constexpr std::string_view name = "void";
    static std::string text(std::monostate)
    {
        return "void";
    }
};

inline value value_of(std::expected<void, cbor::error> const v)
{
    value out = v ? value_of(std::monostate{}) : value_of(v.error());
    out.expected = true;
    return out;
}

template <class T>
value expected_view_of(value const &owner, std::expected<T, cbor::error> v)
{
    value out = v ? view_of(owner, std::move(*v)) : value_of(v.error());
    out.expected = true;
    return out;
}

template <class T>
struct number_kind {
    static std::string text(T const v)
    {
        return number_text(v);
    }
};

template <>
struct kind<int> : number_kind<int> {
    static constexpr std::string_view name = "int";
};

template <>
struct kind<long> : number_kind<long> {
    static constexpr std::string_view name = "long";
};

template <>
struct kind<long long> : number_kind<long long> {
    static constexpr std::string_view name = "long long";
};

template <>
struct kind<unsigned> : number_kind<unsigned> {
    static constexpr std::string_view name = "unsigned";
};

template <>
struct kind<unsigned long> : number_kind<unsigned long> {
    static constexpr std::string_view name = "unsigned long";
};

template <>
struct kind<unsigned long long> : number_kind<unsigned long long> {
    static constexpr std::string_view name = "unsigned long long";
};

template <>
struct kind<signed char> : number_kind<int> {
    static constexpr std::string_view name = "signed char";
};

template <>
struct kind<unsigned char> : number_kind<unsigned> {
    static constexpr std::string_view name = "unsigned char";
};

template <>
struct kind<short> : number_kind<int> {
    static constexpr std::string_view name = "short";
};

template <>
struct kind<unsigned short> : number_kind<unsigned> {
    static constexpr std::string_view name = "unsigned short";
};

template <>
struct kind<double> : number_kind<double> {
    static constexpr std::string_view name = "double";
};

template <>
struct kind<float> : number_kind<float> {
    static constexpr std::string_view name = "float";
};

template <>
struct kind<bool> {
    static constexpr std::string_view name = "bool";
    static std::string text(bool const v)
    {
        return v ? "true" : "false";
    }
};

template <>
struct kind<std::nullptr_t> {
    static constexpr std::string_view name = "std::nullptr_t";
    static std::string text(std::nullptr_t)
    {
        return "nullptr";
    }
};

inline std::string hex_text(std::string_view const bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (char const c : bytes) {
        auto const b = static_cast<unsigned char>(c);
        if (!out.empty())
            out += ' ';
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

inline bool printable(std::string_view const text)
{
    return std::ranges::all_of(text, [](char const c) { return c >= ' ' && c <= '~'; });
}

inline std::string string_text(std::string_view const text)
{
    if (printable(text))
        return std::format("{:?}", text);
    return number_text(text.size()) + " bytes: " + hex_text(text);
}

template <>
struct kind<std::string> {
    static constexpr std::string_view name = "std::string";
    static std::string text(std::string const &v)
    {
        return string_text(v);
    }
};

template <>
struct kind<std::shared_ptr<std::string const>> {
    static constexpr std::string_view name = "std::shared_ptr<std::string const>";
    static std::string text(std::shared_ptr<std::string const> const &v)
    {
        return "shared_ptr to " + string_text(*v);
    }
};

template <>
struct kind<std::span<char const>> {
    static constexpr std::string_view name = "std::span<char const>";
    static std::string text(std::span<char const> const v)
    {
        return number_text(v.size()) + " bytes: " + hex_text({v.data(), v.size()});
    }
};

template <std::string_view const &First, std::string_view const &Second, std::string_view const &Third>
struct joined_name {
    static constexpr auto chars = [] {
        std::array<char, First.size() + Second.size() + Third.size()> out{};
        auto at = std::ranges::copy(First, out.begin()).out;
        at = std::ranges::copy(Second, at).out;
        std::ranges::copy(Third, at);
        return out;
    }();
    static constexpr std::string_view value{chars.data(), chars.size()};
};

}

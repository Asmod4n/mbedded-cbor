#pragma once

#include "lazy.hpp"

#include <array>

namespace site
{

template <std::string_view const &...Parts>
struct name_joined {
    static constexpr auto characters = [] {
        std::array<char, (Parts.size() + ...)> out{};
        auto at = out.begin();
        ((at = std::ranges::copy(Parts, at).out), ...);
        return out;
    }();
    static constexpr std::string_view value{characters.data(), characters.size()};
};

constexpr std::string_view query_name_start = "cbor::jsonpath::query<";
constexpr std::string_view template_name_end = ">";

template <class T>
struct kind<cbor::jsonpath::query<T>> {
    static constexpr std::string_view name = name_joined<query_name_start, kind<T>::name, template_name_end>::value;
    static std::string text(cbor::jsonpath::query<T> const &)
    {
        return std::string(name);
    }
};

}

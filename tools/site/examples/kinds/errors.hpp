#pragma once

#include "lazy.hpp"

#include <system_error>

namespace site
{

template <>
struct kind<std::error_code> {
    static constexpr std::string_view name = "std::error_code";
    static std::string text(std::error_code const &e)
    {
        return "std::error_code " + std::string(e.category().name()) + ":" + number_text(e.value());
    }
};

template <>
struct kind<std::error_condition> {
    static constexpr std::string_view name = "std::error_condition";
    static std::string text(std::error_condition const &e)
    {
        return "std::error_condition " + std::string(e.category().name()) + ":" + number_text(e.value());
    }
};

template <>
struct kind<std::error_category const *> {
    static constexpr std::string_view name = "std::error_category";
    static std::string text(std::error_category const *const c)
    {
        return std::format("std::error_category {:?}", std::string_view(c->name()));
    }
};

template <>
struct kind<char const *> {
    static constexpr std::string_view name = "char const *";
    static std::string text(char const *const p)
    {
        return std::format("{:?}", std::string_view(p));
    }
};

}

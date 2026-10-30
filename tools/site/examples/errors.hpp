#pragma once

#include "lazy.hpp"

namespace site
{

inline result code_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const e = argument_of<cbor::error>(c, 0, "a cbor::error");
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    return value_of(std::error_code{**e});
}

inline result code_made_by_name(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const e = argument_of<cbor::error>(c, 0, "a cbor::error");
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    return value_of(cbor::make_error_code(**e));
}

inline result category_given(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(&cbor::category());
}

template <class T, auto Read>
result error_member(call const &c)
{
    auto const self = self_of<T>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(Read(**self));
}

template <auto Read>
result category_by_condition(call const &c)
{
    auto const self = self_of<std::error_category const *>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const condition = argument_of<int>(c, 0, "an int");
    if (!condition) [[unlikely]]
        return std::unexpected(condition.error());
    return value_of(Read(***self, **condition));
}

inline result error_code_enum(call const &c)
{
    if (c.template_arguments.size() != 1 || c.template_arguments.front() != "cbor::error") [[unlikely]]
        return std::unexpected("std::is_error_code_enum_v takes cbor::error here");
    return value_of(std::is_error_code_enum_v<cbor::error>);
}

template <std::size_t Limit>
result limit_read(call const &)
{
    return value_of(Limit);
}

inline constexpr entry errors_functions[] = {
    {"std::error_code{}", code_made},
    {"cbor::make_error_code()", code_made_by_name},
    {"cbor::category()", category_given},
    {"std::error_code.message()", error_member<std::error_code, [](std::error_code const &e) { return e.message(); }>},
    {"std::error_code.value()", error_member<std::error_code, [](std::error_code const &e) { return e.value(); }>},
    {"std::error_code.category()", error_member<std::error_code, [](std::error_code const &e) { return &e.category(); }>},
    {"std::error_code.default_error_condition()",
     error_member<std::error_code, [](std::error_code const &e) { return e.default_error_condition(); }>},
    {"std::error_condition.message()",
     error_member<std::error_condition, [](std::error_condition const &e) { return e.message(); }>},
    {"std::error_condition.value()", error_member<std::error_condition, [](std::error_condition const &e) { return e.value(); }>},
    {"std::error_condition.category()",
     error_member<std::error_condition, [](std::error_condition const &e) { return &e.category(); }>},
    {"std::error_category.name()",
     error_member<std::error_category const *, [](std::error_category const *const k) { return k->name(); }>},
    {"std::error_category.message()",
     category_by_condition<[](std::error_category const &k, int const n) { return k.message(n); }>},
    {"std::error_category.default_error_condition()",
     category_by_condition<[](std::error_category const &k, int const n) { return k.default_error_condition(n); }>},
    {"std::is_error_code_enum_v", error_code_enum},
    {"cbor::limits::nesting_depth", limit_read<cbor::limits::nesting_depth>},
    {"cbor::limits::string_length", limit_read<cbor::limits::string_length>},
    {"cbor::limits::container_elements", limit_read<cbor::limits::container_elements>},
};

}

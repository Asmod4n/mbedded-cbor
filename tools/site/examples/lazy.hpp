#pragma once

#include "kinds/kinds.hpp"

#include <cstdint>

namespace site
{

template <class T>
struct type_tag {
};

template <class F>
result typed(std::string_view const name, F const f)
{
    if (name == "int")
        return f(type_tag<int>{});
    if (name == "long")
        return f(type_tag<long>{});
    if (name == "unsigned")
        return f(type_tag<unsigned>{});
    if (name == "std::int8_t")
        return f(type_tag<std::int8_t>{});
    if (name == "std::int16_t")
        return f(type_tag<std::int16_t>{});
    if (name == "std::int32_t")
        return f(type_tag<std::int32_t>{});
    if (name == "std::int64_t")
        return f(type_tag<std::int64_t>{});
    if (name == "std::uint8_t")
        return f(type_tag<std::uint8_t>{});
    if (name == "std::uint16_t")
        return f(type_tag<std::uint16_t>{});
    if (name == "std::uint32_t")
        return f(type_tag<std::uint32_t>{});
    if (name == "std::uint64_t")
        return f(type_tag<std::uint64_t>{});
    if (name == "std::size_t")
        return f(type_tag<std::size_t>{});
    if (name == "double")
        return f(type_tag<double>{});
    if (name == "bool")
        return f(type_tag<bool>{});
    if (name == "std::nullptr_t")
        return f(type_tag<std::nullptr_t>{});
    if (name == "std::string_view")
        return f(type_tag<std::string_view>{});
    if (name == "std::span<std::byte const>")
        return f(type_tag<std::span<std::byte const>>{});
    if (name == "cbor::nodelist")
        return f(type_tag<cbor::nodelist>{});
    if (name == "cbor::lazy")
        return f(type_tag<cbor::lazy>{});
    return std::unexpected(std::string(name) + " is not a type known here");
}

inline std::expected<std::string_view, fault> text_argument(call const &c, std::size_t const i)
{
    if (i < c.arguments.size()) {
        if (auto const *t = held_if<std::string>(c.arguments[i]))
            return std::string_view(*t);
        if (auto const *t = held_if<std::string_view>(c.arguments[i]))
            return *t;
    }
    return std::unexpected("the argument " + number_text(i + 1) + " is not a text");
}

inline std::expected<std::uint64_t, fault> unsigned_argument(call const &c, std::size_t const i)
{
    if (i < c.arguments.size()) {
        value const &a = c.arguments[i];
        if (auto const *n = held_if<int>(a); n && *n >= 0)
            return static_cast<std::uint64_t>(*n);
        if (auto const *n = held_if<unsigned>(a))
            return *n;
        if (auto const *n = held_if<long long>(a); n && *n >= 0)
            return static_cast<std::uint64_t>(*n);
        if (auto const *n = held_if<unsigned long long>(a))
            return *n;
        if (auto const *n = held_if<unsigned long>(a))
            return *n;
    }
    return std::unexpected("the argument " + number_text(i + 1) + " is not an unsigned number");
}

inline std::expected<std::int64_t, fault> integer_argument(call const &c, std::size_t const i)
{
    if (i < c.arguments.size()) {
        value const &a = c.arguments[i];
        if (auto const *n = held_if<int>(a))
            return *n;
        if (auto const *n = held_if<long long>(a))
            return *n;
        if (auto const *n = held_if<unsigned>(a))
            return *n;
    }
    return std::unexpected("the argument " + number_text(i + 1) + " is not an integer");
}

inline result lazy_at(call const &c)
{
    auto const self = self_of<cbor::lazy>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (auto const key = text_argument(c, 0))
        return value_of((*self)->at(*key));
    if (auto const k = held_if<cbor::key>(c.arguments.front()))
        return value_of((*self)->at(*k));
    auto const index = unsigned_argument(c, 0);
    if (!index) [[unlikely]]
        return std::unexpected("at takes a text, a cbor::key or an index");
    return value_of((*self)->at(static_cast<std::size_t>(*index)));
}

inline result key_made(call const &c)
{
    auto const n = integer_argument(c, 0);
    if (!n || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::key takes one integer");
    return value_of(cbor::key{*n});
}

inline result lazy_get(call const &c)
{
    auto const self = self_of<cbor::lazy>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("get takes one type: get<int>()");
    return typed(c.template_arguments.front(), [&]<class T>(type_tag<T>) -> result {
        if constexpr (std::is_same_v<T, cbor::nodelist> || std::is_same_v<T, cbor::lazy>)
            return std::unexpected("get does not take " + c.template_arguments.front());
        else
            return value_of((*self)->template get<T>());
    });
}

inline result lazy_at_path(call const &c)
{
    auto const self = self_of<cbor::lazy>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (c.template_arguments.empty() || c.template_arguments.size() > 2) [[unlikely]]
        return std::unexpected("at_path takes a path and a type: at_path<\"$.a\", int>()");
    std::string_view const path = c.template_arguments.front();
    std::string_view const type =
        c.template_arguments.size() == 2 ? std::string_view(c.template_arguments[1]) : "cbor::lazy";
    if (!cbor::jsonpath::compile<cbor::lazy>(path) && cbor::jsonpath::compile<cbor::nodelist>(path) &&
        type != "cbor::nodelist") [[unlikely]]
        return std::unexpected("the path is not a singular query, so it does not compile; cbor::nodelist reads it");
    return typed(type, [&]<class T>(type_tag<T>) -> result {
        auto const query = cbor::jsonpath::compile<T>(path);
        if (!query) [[unlikely]]
            return std::unexpected("the path does not compile: " + kind<cbor::error>::text(query.error()));
        return value_of((*self)->at_path(*query));
    });
}

template <auto Member>
result lazy_member(call const &c)
{
    auto const self = self_of<cbor::lazy>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(((*self)->*Member)());
}

template <class T>
result view_size(call const &c)
{
    auto const self = self_of<T>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of(static_cast<std::size_t>((*self)->size()));
}

inline result nodelist_index(call const &c)
{
    auto const self = self_of<cbor::nodelist>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    auto const i = unsigned_argument(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    if (*i >= (*self)->size()) [[unlikely]]
        return std::unexpected("the index is outside the nodelist");
    return value_of((**self)[static_cast<std::size_t>(*i)]);
}

inline result owning_item_arrow(call const &c)
{
    auto const self = self_of<cbor::owning<cbor::item>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, &***self);
}

template <class T>
result ref_star(call const &c)
{
    auto const self = self_of<cbor::ref<T>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, ***self);
}

inline result item_type(call const &c)
{
    auto const self = self_of<cbor::item const *>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of((**self)->type);
}

inline result take_ownership(call const &c)
{
    auto const bytes = argument_of<std::string>(c, 0, "a std::string");
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return value_of(cbor::lazy::take_ownership(std::string(**bytes)));
}

inline result lazy_view(call const &c)
{
    auto const owner = argument_of<std::shared_ptr<std::string const>>(c, 0, "a std::shared_ptr<std::string const>");
    if (!owner) [[unlikely]]
        return std::unexpected(owner.error());
    return value_of(cbor::lazy::view(**owner));
}

inline result moved(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return c.arguments.front();
}

inline result string_shared(call const &c)
{
    if (c.template_arguments.size() != 1 || c.template_arguments.front() != "std::string const") [[unlikely]]
        return std::unexpected("std::make_shared takes std::string const here");
    auto const text = argument_of<std::string>(c, 0, "a std::string");
    if (!text) [[unlikely]]
        return std::unexpected(text.error());
    return value_of(std::make_shared<std::string const>(**text));
}

inline constexpr entry lazy_functions[] = {
    {"std::move()", moved},
    {"std::make_shared()", string_shared},
    {"cbor::key{}", key_made},
    {"cbor::key()", key_made},
    {"cbor::lazy::take_ownership()", take_ownership},
    {"cbor::lazy::view()", lazy_view},
    {"cbor::lazy.at()", lazy_at},
    {"cbor::lazy.operator[]()", lazy_at},
    {"cbor::lazy.get()", lazy_get},
    {"cbor::lazy.at_path()", lazy_at_path},
    {"cbor::lazy.size()", lazy_member<&cbor::lazy::size>},
    {"cbor::lazy.empty()", lazy_member<&cbor::lazy::empty>},
    {"cbor::lazy.type()", lazy_member<&cbor::lazy::type>},
    {"cbor::lazy.elements()", lazy_member<&cbor::lazy::elements>},
    {"cbor::lazy.entries()", lazy_member<&cbor::lazy::entries>},
    {"cbor::lazy.clone()", lazy_member<&cbor::lazy::clone>},
    {"cbor::lazy_elements.size()", view_size<cbor::lazy_elements>},
    {"cbor::lazy_entries.size()", view_size<cbor::lazy_entries>},
    {"cbor::nodelist.size()", view_size<cbor::nodelist>},
    {"cbor::nodelist.operator[]()", nodelist_index},
    {"cbor::owning<cbor::item>.operator->()", owning_item_arrow},
    {"cbor::item const *.type", item_type},
    {"cbor::ref<std::string_view>.operator*()", ref_star<std::string_view>},
    {"cbor::ref<std::span<std::byte const>>.operator*()", ref_star<std::span<std::byte const>>},
};

}

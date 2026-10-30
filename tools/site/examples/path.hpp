#pragma once

#include "lazy.hpp"

namespace site
{

template <class F>
result path_typed(value const &v, F const f)
{
    result out = std::unexpected("the argument 2 is not a cbor::jsonpath::query");
    auto const tried = [&]<class T>(type_tag<T>) {
        if (auto const *q = held_if<cbor::jsonpath::query<T>>(v)) {
            out = f(*q);
            return true;
        }
        return false;
    };
    (void)(tried(type_tag<int>{}) || tried(type_tag<long>{}) || tried(type_tag<long long>{}) ||
           tried(type_tag<unsigned>{}) || tried(type_tag<unsigned long>{}) || tried(type_tag<unsigned long long>{}) ||
           tried(type_tag<signed char>{}) || tried(type_tag<short>{}) || tried(type_tag<unsigned char>{}) ||
           tried(type_tag<unsigned short>{}) || tried(type_tag<double>{}) || tried(type_tag<bool>{}) ||
           tried(type_tag<std::nullptr_t>{}) || tried(type_tag<std::string_view>{}) ||
           tried(type_tag<std::span<std::byte const>>{}) || tried(type_tag<cbor::nodelist>{}) ||
           tried(type_tag<cbor::lazy>{}));
    return out;
}

template <class T>
result query_applied(value const &encoded, cbor::jsonpath::query<T> const &q)
{
    if (auto const *s = held_if<std::string>(encoded)) {
        std::string const &bytes = *s;
        if constexpr (requires { cbor::at_path(bytes, q); })
            return value_of(cbor::at_path(bytes, q));
    }
    if (auto const *s = held_if<std::string_view>(encoded)) {
        if constexpr (requires { cbor::at_path(*s, q); })
            return value_of(cbor::at_path(*s, q));
    }
    if (auto const *o = held_if<std::shared_ptr<std::string const>>(encoded)) {
        if constexpr (requires { cbor::at_path(*o, q); })
            return value_of(cbor::at_path(*o, q));
    }
    return std::unexpected("no cbor::at_path takes " + std::string(encoded.kind) + " and a " +
                           std::string(kind<cbor::jsonpath::query<T>>::name));
}

inline result at_path_free(call const &c)
{
    if (auto const ok = arguments_counted(c, c.template_arguments.empty() ? 2 : 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (c.template_arguments.empty())
        return path_typed(c.arguments[1], [&](auto const &q) { return query_applied(c.arguments[0], q); });
    if (c.template_arguments.size() != 2) [[unlikely]]
        return std::unexpected("cbor::at_path takes a path and a type: cbor::at_path<\"$.a\", int>(bytes)");
    std::string_view const path = c.template_arguments.front();
    return typed(c.template_arguments[1], [&]<class T>(type_tag<T>) -> result {
        if constexpr (!cbor::scalar_result<T> && !cbor::view_result<T>)
            return std::unexpected("cbor::at_path with a path at compile time does not take " + c.template_arguments[1] +
                                   "; a query from cbor::jsonpath::compile reads it");
        else {
            if (!cbor::jsonpath::compile<cbor::nodelist>(path)) [[unlikely]]
                return std::unexpected("the path is not valid, so it does not compile");
            auto const query = cbor::jsonpath::compile<T>(path);
            if (!query) [[unlikely]]
                return std::unexpected("the path is not a singular query, so it does not compile");
            return query_applied(c.arguments.front(), *query);
        }
    });
}

inline result query_compiled(call const &c)
{
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::jsonpath::compile takes one type: compile<int>(\"$.a\")");
    auto const path = text_argument(c, 0);
    if (!path || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::jsonpath::compile takes the path as one text");
    return typed(c.template_arguments.front(), [&]<class T>(type_tag<T>) -> result {
        if constexpr (cbor::path_result<T>)
            return value_of(cbor::jsonpath::compile<T>(*path));
        else
            return std::unexpected("cbor::jsonpath::compile does not take " + c.template_arguments.front());
    });
}

inline result nodelist_empty(call const &c)
{
    auto const self = self_of<cbor::nodelist>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*self)->empty());
}

inline result shared_string_star(call const &c)
{
    auto const self = self_of<std::shared_ptr<std::string const>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of(std::string(***self));
}

inline constexpr entry path_functions[] = {
    {"cbor::at_path()", at_path_free},
    {"cbor::jsonpath::compile()", query_compiled},
    {"cbor::nodelist.empty()", nodelist_empty},
    {"std::shared_ptr<std::string const>.operator*()", shared_string_star},
};

}

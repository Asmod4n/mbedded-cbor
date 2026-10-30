#pragma once

#include <cbor/cbor.hpp>

#include <charconv>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace site
{

template <class T>
struct kind;

struct value {
    std::string_view kind;
    void const *type = nullptr;
    std::shared_ptr<void> held;
    std::string (*text)(void const *) = nullptr;
    bool expected = false;
};

using fault = std::string;

using result = std::expected<value, fault>;

template <class T>
void const *type_of()
{
    return &kind<T>::name;
}

template <class T>
std::string text_of(void const *const p)
{
    return kind<T>::text(*static_cast<T const *>(p));
}

template <class T>
value value_of(T v)
{
    return {kind<T>::name, type_of<T>(), std::make_shared<T>(std::move(v)), text_of<T>, false};
}

template <class T>
value value_of(std::expected<T, cbor::error> v)
{
    value out = v ? value_of(std::move(*v)) : value_of(v.error());
    out.expected = true;
    return out;
}

template <class T>
value view_of(value const &owner, T v)
{
    auto const both = std::make_shared<std::pair<std::shared_ptr<void>, T>>(owner.held, std::move(v));
    return {kind<T>::name, type_of<T>(), std::shared_ptr<void>(both, &both->second), text_of<T>, false};
}

template <class T>
T const *held_if(value const &v)
{
    if (v.type != type_of<T>())
        return nullptr;
    return static_cast<T const *>(v.held.get());
}

template <class T>
T *changed_if(value const &v)
{
    if (v.type != type_of<T>())
        return nullptr;
    return static_cast<T *>(v.held.get());
}

struct call {
    value const *self;
    std::span<std::string const> template_arguments;
    std::span<value const> arguments;
};

using function = result (*)(call const &);

struct entry {
    std::string_view name;
    function run;
    bool changes = false;
};

template <class T>
std::string number_text(T const v)
{
    char buffer[64];
    auto const r = std::to_chars(buffer, buffer + sizeof buffer, v);
    return {buffer, r.ptr};
}

template <class T>
std::expected<T const *, fault> argument_of(call const &c, std::size_t const i, std::string_view const what)
{
    if (i >= c.arguments.size() || held_if<T>(c.arguments[i]) == nullptr) [[unlikely]]
        return std::unexpected("the argument " + number_text(i + 1) + " is not " + std::string(what));
    return held_if<T>(c.arguments[i]);
}

inline std::expected<void, fault> arguments_counted(call const &c, std::size_t const n)
{
    if (c.arguments.size() != n) [[unlikely]]
        return std::unexpected("the call takes " + number_text(n) + (n == 1 ? " argument" : " arguments"));
    return {};
}

template <class T>
std::expected<T const *, fault> self_of(call const &c)
{
    if (c.self == nullptr || held_if<T>(*c.self) == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    return held_if<T>(*c.self);
}

}

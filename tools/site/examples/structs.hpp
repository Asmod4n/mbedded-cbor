#pragma once

#include "lazy.hpp"

#include "../../../test/doc_types.hpp"

#include <concepts>
#include <optional>
#include <set>
#include <tuple>
#include <utility>

namespace site
{

template <class M>
std::expected<M, fault> member_converted(value const &a, std::size_t const i)
{
    auto const refused = [&] {
        return std::unexpected("the argument " + number_text(i + 1) + " is not " +
                               std::string(kind<M>::name));
    };
    if constexpr (std::integral<M> && !std::same_as<M, bool>) {
        auto const fitted = [&](auto const n) -> std::expected<M, fault> {
            if (!std::in_range<M>(n)) [[unlikely]]
                return std::unexpected("the argument " + number_text(i + 1) + " does not fit " +
                                       std::string(kind<M>::name));
            return static_cast<M>(n);
        };
        if (auto const *n = held_if<int>(a))
            return fitted(*n);
        if (auto const *n = held_if<unsigned>(a))
            return fitted(*n);
        if (auto const *n = held_if<long long>(a))
            return fitted(*n);
        if (auto const *n = held_if<unsigned long long>(a))
            return fitted(*n);
        return refused();
    } else if constexpr (std::floating_point<M>) {
        auto const exact = [&](auto const n) -> std::expected<M, fault> {
            auto const m = static_cast<M>(n);
            if (static_cast<decltype(n)>(m) != n) [[unlikely]]
                return std::unexpected("the argument " + number_text(i + 1) + " is not exact as " +
                                       std::string(kind<M>::name));
            return m;
        };
        if (auto const *n = held_if<float>(a))
            return exact(*n);
        if (auto const *n = held_if<double>(a))
            return exact(*n);
        if (auto const *n = held_if<int>(a))
            return exact(*n);
        return refused();
    } else if constexpr (std::same_as<M, std::optional<std::uint8_t>>) {
        if (held_if<std::nullopt_t>(a) != nullptr)
            return M{};
        auto const n = member_converted<std::uint8_t>(a, i);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        return M{*n};
    } else {
        if (auto const *v = held_if<M>(a))
            return *v;
        return refused();
    }
}

template <class T, class... M>
result aggregate_made(call const &c)
{
    if (auto const ok = arguments_counted(c, sizeof...(M)); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return [&]<std::size_t... I>(std::index_sequence<I...>) -> result {
        std::tuple<std::expected<M, fault>...> const members{member_converted<M>(c.arguments[I], I)...};
        fault refused;
        ((refused.empty() && !std::get<I>(members) ? (void)(refused = std::get<I>(members).error()) : void()),
         ...);
        if (!refused.empty()) [[unlikely]]
            return std::unexpected(refused);
        return value_of(T{*std::get<I>(members)...});
    }(std::index_sequence_for<M...>{});
}

template <class E>
result container_made(call const &c, std::string_view const element)
{
    if (c.template_arguments.size() != 1 || c.template_arguments.front() != element) [[unlikely]]
        return std::unexpected("the container takes " + std::string(element) + " here");
    E out;
    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
        auto e = member_converted<typename E::value_type>(c.arguments[i], i);
        if (!e) [[unlikely]]
            return std::unexpected(e.error());
        out.insert(out.end(), std::move(*e));
    }
    return value_of(std::move(out));
}

inline result nullopt_read(call const &)
{
    return value_of(std::nullopt);
}

inline result tag_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = unsigned_argument(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return value_of(cbor::tag{*n});
}

inline result set_made(call const &c)
{
    return container_made<std::set<std::uint8_t>>(c, "std::uint8_t");
}

template <class T>
result structs_encode(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const v = member_converted<T>(c.arguments.front(), 0);
    if (!v) [[unlikely]]
        return std::unexpected(v.error());
    return value_of(cbor::structs<T>::encode(*v));
}

template <class T>
result structs_decode(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const bytes = argument_of<std::string>(c, 0, "a std::string");
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return value_of(cbor::structs<T>::decode(std::string(**bytes)));
}

template <class T>
result ref_arrow(call const &c)
{
    auto const self = self_of<cbor::ref<T>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (*self)->operator->());
}

template <class C, auto Member>
result field_read(call const &c)
{
    auto const self = self_of<C const *>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (**self)->*Member);
}

template <class T>
result describe_members_had(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(cbor::has_describe_members<T>);
}

inline result has_describe_members_read(call const &c)
{
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::has_describe_members takes one type");
    std::string_view const t = c.template_arguments.front();
    if (t == "person")
        return describe_members_had<reference_structs::person>(c);
    if (t == "header")
        return describe_members_had<reference_structs::header>(c);
    if (t == "senml")
        return describe_members_had<reference_structs::senml>(c);
    if (t == "account")
        return describe_members_had<reference_structs::account>(c);
    if (t == "reading")
        return describe_members_had<reference_structs::reading>(c);
    if (t == "trailer")
        return describe_members_had<reference_structs::trailer>(c);
    if (t == "device")
        return describe_members_had<reference_structs::device>(c);
    return std::unexpected(std::string(t) + " is not a type known here");
}

inline constexpr entry structs_functions[] = {
    {"std::nullopt", nullopt_read},
    {"std::set{}", set_made},
    {"person{}", aggregate_made<reference_structs::person, std::string, int>},
    {"header{}", aggregate_made<reference_structs::header, std::string>},
    {"senml{}", aggregate_made<reference_structs::senml, std::string, double>},
    {"account{}", aggregate_made<reference_structs::account, std::string, std::uint8_t>},
    {"reading{}", aggregate_made<reference_structs::reading, std::uint8_t, std::optional<std::uint8_t>>},
    {"trailer{}", aggregate_made<reference_structs::trailer, std::uint8_t>},
    {"device{}", aggregate_made<reference_structs::device, std::string, std::uint8_t>},
    {"cbor::has_describe_members", has_describe_members_read},
    {"cbor::structs<person>::encode()", structs_encode<reference_structs::person>},
    {"cbor::structs<header>::encode()", structs_encode<reference_structs::header>},
    {"cbor::structs<senml>::encode()", structs_encode<reference_structs::senml>},
    {"cbor::structs<account>::encode()", structs_encode<reference_structs::account>},
    {"cbor::structs<reading>::encode()", structs_encode<reference_structs::reading>},
    {"cbor::structs<trailer>::encode()", structs_encode<reference_structs::trailer>},
    {"cbor::structs<device>::encode()", structs_encode<reference_structs::device>},
    {"cbor::structs<std::set<std::uint8_t>>::encode()", structs_encode<std::set<std::uint8_t>>},
    {"cbor::tag{}", tag_made},
    {"cbor::structs<person>::decode()", structs_decode<reference_structs::person>},
    {"cbor::structs<header>::decode()", structs_decode<reference_structs::header>},
    {"cbor::structs<reading>::decode()", structs_decode<reference_structs::reading>},
    {"cbor::ref<person>.operator->()", ref_arrow<reference_structs::person>},
    {"person const *.name", field_read<reference_structs::person, &reference_structs::person::name>},
    {"person const *.age", field_read<reference_structs::person, &reference_structs::person::age>},
};

} // namespace site

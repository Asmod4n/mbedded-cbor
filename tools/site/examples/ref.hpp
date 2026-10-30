#pragma once

#include "lazy.hpp"

namespace site
{

template <class T>
result structs_decoded(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (auto const *owner = held_if<std::shared_ptr<std::string const>>(c.arguments.front()))
        return value_of(cbor::structs<T>::decode(*owner));
    auto const bytes = argument_of<std::string>(c, 0, "a std::string or a std::shared_ptr<std::string const>");
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return value_of(cbor::structs<T>::decode(std::string(**bytes)));
}

template <class T>
result ref_cloned(call const &c)
{
    auto const self = self_of<cbor::ref<T>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*self)->clone());
}

template <class T>
result person_arrow(call const &c)
{
    auto const self = self_of<T>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (*self)->operator->());
}

template <auto Member>
result person_field(call const &c)
{
    auto const self = self_of<reference_ref::person const *>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (**self)->*Member);
}

inline constexpr entry ref_functions[] = {
    {"cbor::structs<std::string_view>::decode()", structs_decoded<std::string_view>},
    {"cbor::structs<person>::decode()", structs_decoded<reference_ref::person>},
    {"cbor::ref<std::string_view>.clone()", ref_cloned<std::string_view>},
    {"cbor::ref<person>.clone()", ref_cloned<reference_ref::person>},
    {"cbor::ref<person>.operator->()", person_arrow<cbor::ref<reference_ref::person>>},
    {"cbor::owning<person>.operator->()", person_arrow<cbor::owning<reference_ref::person>>},
    {"person const *.name", person_field<&reference_ref::person::name>},
    {"person const *.age", person_field<&reference_ref::person::age>},
};

}

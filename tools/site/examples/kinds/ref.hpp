#pragma once

#include "lazy.hpp"

#include <format>

namespace reference_ref
{

struct person {
    std::string_view name;
    int age;
};

}

namespace site
{

inline std::string person_text(reference_ref::person const &p)
{
    return std::format("person{{{:?}, {}}}", p.name, p.age);
}

template <>
struct kind<reference_ref::person const *> {
    static constexpr std::string_view name = "person const *";
    static std::string text(reference_ref::person const *const p)
    {
        return "pointer to " + person_text(*p);
    }
};

template <>
struct kind<cbor::ref<reference_ref::person>> {
    static constexpr std::string_view name = "cbor::ref<person>";
    static std::string text(cbor::ref<reference_ref::person> const &r)
    {
        return "ref to " + person_text(*r);
    }
};

template <>
struct kind<cbor::owning<reference_ref::person>> {
    static constexpr std::string_view name = "cbor::owning<person>";
    static std::string text(cbor::owning<reference_ref::person> const &o)
    {
        return "owning of " + person_text(*o);
    }
};

}

#pragma once

#include "structs.hpp"

#include <array>
#include <format>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace site
{

using doc_reference_schema::car;
using doc_reference_schema::cat;
using doc_reference_schema::dog;
using doc_reference_schema::home;
using doc_reference_schema::point;
using doc_reference_schema::property;
using doc_reference_schema::tire;

inline std::string float_text(float const v)
{
    return number_text(v) + "f";
}

template <>
struct kind<tire> {
    static constexpr std::string_view name = "tire";
    static std::string text(tire const &v)
    {
        return "tire{" + number_text(v.diameter) + ", " + float_text(v.pressure) + "}";
    }
};

template <>
struct kind<point> {
    static constexpr std::string_view name = "point";
    static std::string text(point const &v)
    {
        return "point{" + float_text(v.x) + ", " + float_text(v.y) + "}";
    }
};

inline std::string elements_text(std::vector<tire> const &v)
{
    std::string out = "{";
    char const *separator = "";
    for (tire const &e : v) {
        out += separator + kind<tire>::text(e);
        separator = ", ";
    }
    return out + "}";
}

inline std::string elements_text(std::vector<double> const &v)
{
    std::string out = "{";
    char const *separator = "";
    for (double const e : v) {
        out += separator + number_text(e);
        separator = ", ";
    }
    return out + "}";
}

template <>
struct kind<std::vector<tire>> {
    static constexpr std::string_view name = "std::vector<tire>";
    static std::string text(std::vector<tire> const &v)
    {
        return "std::vector<tire>" + elements_text(v);
    }
};

template <>
struct kind<std::vector<double>> {
    static constexpr std::string_view name = "std::vector<double>";
    static std::string text(std::vector<double> const &v)
    {
        return "std::vector<double>" + elements_text(v);
    }
};

template <>
struct kind<car> {
    static constexpr std::string_view name = "car";
    static std::string text(car const &v)
    {
        return std::format("car{{{:?}, {}, {}, {}}}", v.make, number_text(static_cast<unsigned>(v.seats)),
                           elements_text(v.tires), elements_text(v.track));
    }
};

template <>
struct kind<car const *> : pointer_kind<car> {
    static constexpr std::string_view name = "car const *";
};

template <>
struct kind<cbor::ref<car>> : ref_kind<car> {
    static constexpr std::string_view name = "cbor::ref<car>";
};

template <>
struct kind<cbor::description_place> {
    static constexpr std::string_view name = "cbor::description_place";
    static std::string text(cbor::description_place const v)
    {
        return v == cbor::description_place::apart ? "cbor::description_place::apart"
                                                   : "cbor::description_place::inside";
    }
};

template <>
struct kind<char> {
    static constexpr std::string_view name = "char";
    static std::string text(char const v)
    {
        if (v >= ' ' && v <= '~')
            return std::format("{:?}", v);
        return "'\\x" + hex_text(std::span<char const>(std::array<char, 1>{v})) + "'";
    }
};

template <>
struct kind<cbor::schema<car>::accessor<car>> {
    static constexpr std::string_view name = "cbor::schema<car>::accessor<car>";
    static std::string text(cbor::schema<car>::accessor<car> const &)
    {
        return "accessor of a car";
    }
};

template <>
struct kind<cbor::schema<tire>::accessor<tire>> {
    static constexpr std::string_view name = "cbor::schema<tire>::accessor<tire>";
    static std::string text(cbor::schema<tire>::accessor<tire> const &)
    {
        return "accessor of a tire";
    }
};

template <>
struct kind<cbor::schema<car>::accessor<car, false>> {
    static constexpr std::string_view name = "cbor::schema<car>::accessor<car, false>";
    static std::string text(cbor::schema<car>::accessor<car, false> const &)
    {
        return "unverified accessor of a car";
    }
};

template <>
struct kind<cbor::schema<car>::accessor<std::vector<tire>>> {
    static constexpr std::string_view name = "cbor::schema<car>::accessor<std::vector<tire>>";
    static std::string text(cbor::schema<car>::accessor<std::vector<tire>> const &v)
    {
        return "accessor of a std::vector<tire> of " + number_text(v.size()) +
               (v.size() == 1 ? " element" : " elements");
    }
};

template <>
struct kind<cbor::schema<car>::accessor<std::vector<tire>>::iterator> {
    static constexpr std::string_view name = "cbor::schema<car>::accessor<std::vector<tire>>::iterator";
    static std::string text(cbor::schema<car>::accessor<std::vector<tire>>::iterator const &)
    {
        return "iterator of an accessor of a std::vector<tire>";
    }
};

template <>
struct kind<cbor::schema<car>::accessor<cbor::fixed_element<tire>>> {
    static constexpr std::string_view name = "cbor::schema<car>::accessor<cbor::fixed_element<tire>>";
    static std::string text(cbor::schema<car>::accessor<cbor::fixed_element<tire>> const &)
    {
        return "accessor of a tire in a std::vector<tire>";
    }
};

template <>
struct kind<cbor::schema<home>::accessor<home>> {
    static constexpr std::string_view name = "cbor::schema<home>::accessor<home>";
    static std::string text(cbor::schema<home>::accessor<home> const &)
    {
        return "accessor of a home";
    }
};

template <>
struct kind<cbor::schema<home>::accessor<std::variant<dog, cat>>> {
    static constexpr std::string_view name = "cbor::schema<home>::accessor<std::variant<dog, cat>>";
    static std::string text(cbor::schema<home>::accessor<std::variant<dog, cat>> const &v)
    {
        return std::string("accessor of a std::variant<dog, cat> that holds a ") + (v.index() == 0 ? "dog" : "cat");
    }
};

template <>
struct kind<cbor::schema<property>::accessor<property>> {
    static constexpr std::string_view name = "cbor::schema<property>::accessor<property>";
    static std::string text(cbor::schema<property>::accessor<property> const &)
    {
        return "accessor of a property";
    }
};

template <>
struct kind<cbor::schema<property>::accessor<std::variant<bool, std::int64_t, std::string>>> {
    static constexpr std::string_view name = "cbor::schema<property>::accessor<std::variant<bool, std::int64_t, std::string>>";
    static std::string text(cbor::schema<property>::accessor<std::variant<bool, std::int64_t, std::string>> const &v)
    {
        constexpr std::array<std::string_view, 3> held{"bool", "std::int64_t", "std::string"};
        return "accessor of a std::variant<bool, std::int64_t, std::string> that holds a " +
               std::string(held.at(v.index()));
    }
};

template <>
struct kind<cbor::schema<home>::accessor<dog>> {
    static constexpr std::string_view name = "cbor::schema<home>::accessor<dog>";
    static std::string text(cbor::schema<home>::accessor<dog> const &)
    {
        return "accessor of a dog";
    }
};

template <>
struct kind<cbor::schema<home>::accessor<cat>> {
    static constexpr std::string_view name = "cbor::schema<home>::accessor<cat>";
    static std::string text(cbor::schema<home>::accessor<cat> const &)
    {
        return "accessor of a cat";
    }
};

template <>
struct kind<cbor::typed_array_view<double>> {
    static constexpr std::string_view name = "cbor::typed_array_view<double>";
    static std::string text(cbor::typed_array_view<double> const &v)
    {
        std::string out = "typed_array_view<double> of " + number_text(v.size()) +
                          (v.size() == 1 ? " element" : " elements");
        char const *separator = ": ";
        for (double const e : v) {
            out += separator + number_text(e);
            separator = ", ";
        }
        return out;
    }
};

template <>
struct kind<cbor::typed_array_view<double>::iterator> {
    static constexpr std::string_view name = "cbor::typed_array_view<double>::iterator";
    static std::string text(cbor::typed_array_view<double>::iterator const &)
    {
        return "iterator of a typed_array_view<double>";
    }
};

} // namespace site

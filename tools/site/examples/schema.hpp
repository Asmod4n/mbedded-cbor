#pragma once

#include "lazy.hpp"
#include "structs.hpp"

#include "../../../test/doc_types.hpp"

#include <numeric>
#include <vector>

namespace site
{

inline std::expected<std::vector<std::size_t>, fault> indexes_read(call const &c, std::size_t const first)
{
    std::vector<std::size_t> out;
    for (std::size_t i = first; i < c.arguments.size(); ++i) {
        auto const n = unsigned_argument(c, i);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (!std::in_range<std::size_t>(*n)) [[unlikely]]
            return std::unexpected("the argument " + number_text(i + 1) + " does not fit std::size_t");
        out.push_back(static_cast<std::size_t>(*n));
    }
    return out;
}

template <class F>
result placed(call const &c, std::size_t const at, F const &f)
{
    if (c.template_arguments.size() <= at || c.template_arguments[at] == "cbor::description_place::inside")
        return f.template operator()<cbor::description_place::inside>();
    if (c.template_arguments[at] == "cbor::description_place::apart")
        return f.template operator()<cbor::description_place::apart>();
    return std::unexpected(c.template_arguments[at] + " is not a cbor::description_place");
}

template <cbor::fixed_string P, cbor::fixed_string... Rest, class F>
result path_chosen(std::string_view const path, F const &f)
{
    if (path == P.view())
        return f.template operator()<P>();
    if constexpr (sizeof...(Rest) == 0)
        return std::unexpected("the path \"" + std::string(path) + "\" is not compiled into this page");
    else
        return path_chosen<Rest...>(path, f);
}

inline fault compile_refused(std::string_view const call, std::string_view const path,
                             std::size_t const indexes)
{
    return std::string(call) + "<\"" + std::string(path) + "\"> with " + number_text(indexes) +
           (indexes == 1 ? " index" : " indexes") + " does not compile";
}

template <class A, cbor::fixed_string... Paths>
result accessor_at_path(call const &c)
{
    auto const self = self_of<A>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("at_path takes one path: at_path<\"@.a\">()");
    auto const i = indexes_read(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    A const &a = **self;
    return path_chosen<Paths...>(c.template_arguments.front(), [&]<cbor::fixed_string P>() -> result {
        if (i->empty()) {
            if constexpr (requires { a.template at_path<P>(); })
                return expected_view_of(*c.self, a.template at_path<P>());
        } else if (i->size() == 1) {
            if constexpr (requires { a.template at_path<P>(std::size_t{}); })
                return expected_view_of(*c.self, a.template at_path<P>(i->front()));
        }
        return std::unexpected(compile_refused("at_path", P.view(), i->size()));
    });
}

template <class A, cbor::fixed_string... Paths>
result accessor_view(call const &c)
{
    auto const self = self_of<A>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (c.template_arguments.size() != 1 || !c.arguments.empty()) [[unlikely]]
        return std::unexpected("view takes one path and no index here: view<\"$.a\">()");
    A const &a = **self;
    return path_chosen<Paths...>(c.template_arguments.front(), [&]<cbor::fixed_string P>() -> result {
        if constexpr (requires { a.template view<P>(); })
            return expected_view_of(*c.self, a.template view<P>());
        else
            return std::unexpected(compile_refused("view", P.view(), 0));
    });
}

template <class T, bool Verified, cbor::fixed_string... Paths>
result schema_at_path(call const &c)
{
    if (c.template_arguments.empty() || c.template_arguments.size() > 2) [[unlikely]]
        return std::unexpected("at_path takes a path and a cbor::description_place: at_path<\"$.a\">(bytes)");
    auto const encoded = text_argument(c, 0);
    if (!encoded) [[unlikely]]
        return std::unexpected(encoded.error());
    auto const i = indexes_read(c, 1);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    std::string_view const name = Verified ? "at_path" : "unverified_at_path";
    return placed(c, 1, [&]<cbor::description_place D>() -> result {
        return path_chosen<Paths...>(c.template_arguments.front(), [&]<cbor::fixed_string P>() -> result {
            if constexpr (Verified) {
                if (i->empty()) {
                    if constexpr (requires { cbor::schema<T>::template at_path<P, D>(*encoded); })
                        return value_of(cbor::schema<T>::template at_path<P, D>(*encoded));
                } else if (i->size() == 1) {
                    if constexpr (requires {
                                      cbor::schema<T>::template at_path<P, D>(*encoded, std::size_t{});
                                  })
                        return value_of(cbor::schema<T>::template at_path<P, D>(*encoded, i->front()));
                }
            } else {
                if (i->empty()) {
                    if constexpr (requires { cbor::schema<T>::template unverified_at_path<P, D>(*encoded); })
                        return value_of(cbor::schema<T>::template unverified_at_path<P, D>(*encoded));
                } else if (i->size() == 1) {
                    if constexpr (requires {
                                      cbor::schema<T>::template unverified_at_path<P, D>(*encoded,
                                                                                         std::size_t{});
                                  })
                        return value_of(
                            cbor::schema<T>::template unverified_at_path<P, D>(*encoded, i->front()));
                }
            }
            return std::unexpected(compile_refused(name, P.view(), i->size()));
        });
    });
}

template <class T>
result schema_encode(call const &c)
{
    if (c.template_arguments.size() > 1) [[unlikely]]
        return std::unexpected("encode takes one cbor::description_place");
    if (c.arguments.empty() || c.arguments.size() > 2) [[unlikely]]
        return std::unexpected("encode takes a value and an optional std::string target");
    auto const v = member_converted<T>(c.arguments.front(), 0);
    if (!v) [[unlikely]]
        return std::unexpected(v.error());
    return placed(c, 0, [&]<cbor::description_place D>() -> result {
        if (c.arguments.size() == 1)
            return value_of(cbor::schema<T>::template encode<D>(*v));
        auto *const target = changed_if<std::string>(c.arguments[1]);
        if (target == nullptr) [[unlikely]]
            return std::unexpected("the argument 2 is not a std::string");
        return value_of(cbor::schema<T>::template encode<D>(*v, *target));
    });
}

template <class T, bool Verified>
result schema_decode(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const bytes = argument_of<std::string>(c, 0, "a std::string");
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return placed(c, 0, [&]<cbor::description_place D>() -> result {
        if constexpr (Verified)
            return value_of(cbor::schema<T>::template decode<D>(std::string(**bytes)));
        else
            return value_of(cbor::schema<T>::template unverified_decode<D>(std::string(**bytes)));
    });
}

template <class T, bool Verified>
result schema_path(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (auto const *const owner = held_if<std::shared_ptr<std::string const>>(c.arguments.front()))
        return placed(c, 0, [&]<cbor::description_place D>() -> result {
            if constexpr (Verified)
                return value_of(cbor::schema<T>::template path<D>(*owner));
            else
                return value_of(cbor::schema<T>::template unverified_path<D>(*owner));
        });
    auto const bytes =
        argument_of<std::string>(c, 0, "a std::string or a std::shared_ptr<std::string const>");
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return placed(c, 0, [&]<cbor::description_place D>() -> result {
        if constexpr (Verified)
            return value_of(cbor::schema<T>::template path<D>(std::string(**bytes)));
        else
            return value_of(cbor::schema<T>::template unverified_path<D>(std::string(**bytes)));
    });
}

template <class T>
result schema_description(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(cbor::schema<T>::description());
}

template <class T>
result fixed_size_read(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (c.template_arguments.empty())
        return value_of(cbor::schema<T>::fixed_size());
    std::string_view const u = c.template_arguments.front();
    if (u == "std::uint8_t")
        return value_of(cbor::schema<T>::template fixed_size<std::uint8_t>());
    if (u == "std::uint16_t")
        return value_of(cbor::schema<T>::template fixed_size<std::uint16_t>());
    if (u == "float")
        return value_of(cbor::schema<T>::template fixed_size<float>());
    if (u == "double")
        return value_of(cbor::schema<T>::template fixed_size<double>());
    if (u == "std::string")
        return value_of(cbor::schema<T>::template fixed_size<std::string>());
    if (u == "tire")
        return value_of(cbor::schema<T>::template fixed_size<tire>());
    return std::unexpected("fixed_size<" + std::string(u) + "> is not compiled into this page");
}

inline result tire_member_offset(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("member_offset takes one member: member_offset<&tire::diameter>()");
    std::string_view const m = c.template_arguments.front();
    if (m == "&tire::diameter")
        return value_of(cbor::schema<tire>::member_offset<&tire::diameter>());
    if (m == "&tire::pressure")
        return value_of(cbor::schema<tire>::member_offset<&tire::pressure>());
    return std::unexpected("member_offset<" + std::string(m) + "> is not compiled into this page");
}

inline result car_member_offset(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (c.template_arguments.size() != 1) [[unlikely]]
        return std::unexpected("member_offset takes one member: member_offset<&car::make>()");
    std::string_view const m = c.template_arguments.front();
    if (m == "&car::make")
        return value_of(cbor::schema<car>::member_offset<&car::make>());
    if (m == "&car::seats")
        return value_of(cbor::schema<car>::member_offset<&car::seats>());
    if (m == "&car::tires")
        return value_of(cbor::schema<car>::member_offset<&car::tires>());
    if (m == "&car::track")
        return value_of(cbor::schema<car>::member_offset<&car::track>());
    return std::unexpected("member_offset<" + std::string(m) + "> is not compiled into this page");
}

template <class A>
result accessor_size(call const &c)
{
    auto const self = self_of<A>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*self)->size());
}

template <class A, bool End>
result range_end(call const &c)
{
    auto const self = self_of<A>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    A const &a = **self;
    if constexpr (End)
        return view_of(*c.self, a.end());
    else
        return view_of(*c.self, a.begin());
}

inline result element_iterator_star(call const &c)
{
    auto const self = self_of<cbor::schema<car>::accessor<std::vector<tire>>::iterator>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, ***self);
}

inline result element_iterator_index(call const &c)
{
    auto const self = self_of<cbor::schema<car>::accessor<std::vector<tire>>::iterator>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = integer_argument(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return view_of(*c.self, (**self)[static_cast<std::ptrdiff_t>(*n)]);
}

template <auto Member>
result track_member(call const &c)
{
    auto const self = self_of<cbor::typed_array_view<double>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(((*self)->*Member)());
}

template <bool Back>
result track_end_element(call const &c)
{
    auto const self = self_of<cbor::typed_array_view<double>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if ((*self)->empty()) [[unlikely]]
        return std::unexpected("the view is empty; in C++ front and back throw std::logic_error");
    return value_of(Back ? (*self)->back() : (*self)->front());
}

inline result track_at(call const &c)
{
    auto const self = self_of<cbor::typed_array_view<double>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const i = indexes_read(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    return value_of((*self)->at(i->front()));
}

inline result track_index(call const &c)
{
    auto const self = self_of<cbor::typed_array_view<double>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const i = indexes_read(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    if (i->front() >= (*self)->size()) [[unlikely]]
        return std::unexpected("the index is outside the view; in C++ operator[] throws std::logic_error");
    return value_of((**self)[i->front()]);
}

template <bool Last>
result track_part(call const &c)
{
    auto const self = self_of<cbor::typed_array_view<double>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = indexes_read(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return value_of(Last ? (*self)->last(n->front()) : (*self)->first(n->front()));
}

inline result accumulated(call const &c)
{
    if (auto const ok = arguments_counted(c, 3); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const first = argument_of<cbor::typed_array_view<double>::iterator>(
        c, 0, "an iterator of a cbor::typed_array_view<double>");
    if (!first) [[unlikely]]
        return std::unexpected(first.error());
    auto const last = argument_of<cbor::typed_array_view<double>::iterator>(
        c, 1, "an iterator of a cbor::typed_array_view<double>");
    if (!last) [[unlikely]]
        return std::unexpected(last.error());
    auto const init = argument_of<double>(c, 2, "a double");
    if (!init) [[unlikely]]
        return std::unexpected(init.error());
    return value_of(std::accumulate(**first, **last, **init));
}

inline result string_copied(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const text = argument_of<std::string>(c, 0, "a std::string");
    if (!text) [[unlikely]]
        return std::unexpected(text.error());
    return value_of(std::string(**text));
}

inline result string_size(call const &c)
{
    auto const self = self_of<std::string>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*self)->size());
}

inline result string_index(call const &c)
{
    auto const self = self_of<std::string>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const i = indexes_read(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    if (i->front() > (*self)->size()) [[unlikely]]
        return std::unexpected(
            "the index is past the end of the std::string; in C++ the behaviour is undefined");
    return value_of((**self)[i->front()]);
}

inline result span_size(call const &c)
{
    auto const self = self_of<std::span<char const>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*self)->size());
}

inline result vector_made(call const &c)
{
    if (c.template_arguments.size() == 1 && c.template_arguments.front() == "tire")
        return container_made<std::vector<tire>>(c, "tire");
    return container_made<std::vector<double>>(c, "double");
}

template <cbor::description_place D>
result place_read(call const &)
{
    return value_of(D);
}

inline constexpr entry schema_functions[] = {
    {"tire{}", aggregate_made<tire, std::uint16_t, float>},
    {"point{}", aggregate_made<point, float, float>},
    {"car{}", aggregate_made<car, std::string, std::uint8_t, std::vector<tire>, std::vector<double>>},
    {"std::vector{}", vector_made},
    {"std::string()", string_copied},
    {"std::string.size()", string_size},
    {"std::string.operator[]()", string_index},
    {"std::span<char const>.size()", span_size},
    {"std::accumulate()", accumulated},
    {"cbor::description_place::inside", place_read<cbor::description_place::inside>},
    {"cbor::description_place::apart", place_read<cbor::description_place::apart>},
    {"cbor::schema<tire>::encode()", schema_encode<tire>},
    {"cbor::schema<point>::encode()", schema_encode<point>},
    {"cbor::schema<car>::encode()", schema_encode<car>},
    {"cbor::schema<car>::decode()", schema_decode<car, true>},
    {"cbor::schema<tire>::decode()", schema_decode<tire, true>},
    {"cbor::schema<car>::unverified_decode()", schema_decode<car, false>},
    {"cbor::schema<car>::path()", schema_path<car, true>},
    {"cbor::schema<tire>::path()", schema_path<tire, true>},
    {"cbor::schema<car>::unverified_path()", schema_path<car, false>},
    {"cbor::schema<car>::at_path()",
     schema_at_path<car, true, "$.seats", "$.make", "$.tires[1].diameter", "$.tires[].diameter",
                    "$.tires[].pressure", "$.track[2]">},
    {"cbor::schema<car>::unverified_at_path()",
     schema_at_path<car, false, "$.seats", "$.tires[1].diameter", "$.tires[].diameter">},
    {"cbor::schema<tire>::at_path()", schema_at_path<tire, true, "$.diameter", "$.pressure">},
    {"cbor::schema<point>::at_path()", schema_at_path<point, true, "$.x", "$.y">},
    {"cbor::schema<car>::description()", schema_description<car>},
    {"cbor::schema<tire>::fixed_size()", fixed_size_read<tire>},
    {"cbor::schema<car>::fixed_size()", fixed_size_read<car>},
    {"cbor::schema<point>::fixed_size()", fixed_size_read<point>},
    {"cbor::schema<tire>::member_offset()", tire_member_offset},
    {"cbor::schema<car>::member_offset()", car_member_offset},
    {"cbor::ref<car>.operator->()", ref_arrow<car>},
    {"car const *.make", field_read<car, &car::make>},
    {"car const *.seats", field_read<car, &car::seats>},
    {"car const *.tires", field_read<car, &car::tires>},
    {"car const *.track", field_read<car, &car::track>},
    {"cbor::schema<car>::accessor<car>.at_path()",
     accessor_at_path<cbor::schema<car>::accessor<car>, "$.make", "$.seats", "$.tires", "$.tires[]",
                      "$.tires[1].diameter">},
    {"cbor::schema<car>::accessor<car>.view()", accessor_view<cbor::schema<car>::accessor<car>, "$.track">},
    {"cbor::schema<car>::accessor<car, false>.at_path()",
     accessor_at_path<cbor::schema<car>::accessor<car, false>, "$.seats", "$.tires[1].diameter">},
    {"cbor::schema<car>::accessor<std::vector<tire>>.at_path()",
     accessor_at_path<cbor::schema<car>::accessor<std::vector<tire>>, "@[1].diameter", "@[]", "$.seats">},
    {"cbor::schema<car>::accessor<std::vector<tire>>.size()",
     accessor_size<cbor::schema<car>::accessor<std::vector<tire>>>},
    {"cbor::schema<car>::accessor<std::vector<tire>>.begin()",
     range_end<cbor::schema<car>::accessor<std::vector<tire>>, false>},
    {"cbor::schema<car>::accessor<std::vector<tire>>.end()",
     range_end<cbor::schema<car>::accessor<std::vector<tire>>, true>},
    {"cbor::schema<car>::accessor<std::vector<tire>>::iterator.operator*()", element_iterator_star},
    {"cbor::schema<car>::accessor<std::vector<tire>>::iterator.operator[]()", element_iterator_index},
    {"cbor::schema<car>::accessor<cbor::fixed_element<tire>>.at_path()",
     accessor_at_path<cbor::schema<car>::accessor<cbor::fixed_element<tire>>, "@.diameter", "@.pressure",
                      "$.seats">},
    {"cbor::typed_array_view<double>.size()", track_member<&cbor::typed_array_view<double>::size>},
    {"cbor::typed_array_view<double>.empty()", track_member<&cbor::typed_array_view<double>::empty>},
    {"cbor::typed_array_view<double>.begin()", range_end<cbor::typed_array_view<double>, false>},
    {"cbor::typed_array_view<double>.end()", range_end<cbor::typed_array_view<double>, true>},
    {"cbor::typed_array_view<double>.front()", track_end_element<false>},
    {"cbor::typed_array_view<double>.back()", track_end_element<true>},
    {"cbor::typed_array_view<double>.at()", track_at},
    {"cbor::typed_array_view<double>.operator[]()", track_index},
    {"cbor::typed_array_view<double>.first()", track_part<false>},
    {"cbor::typed_array_view<double>.last()", track_part<true>},
};

} // namespace site

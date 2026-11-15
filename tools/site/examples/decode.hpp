// Gives the calls of the console on cbor::item and the tree of cbor::decode.
#pragma once

#include "lazy.hpp"

#include <array>
#include <functional>
#include <utility>

namespace site
{

inline value node_view_of(value const &owner, cbor::item const &node)
{
    return {kind<cbor::item>::name, type_of<cbor::item>(),
            std::shared_ptr<void>(owner.held, const_cast<cbor::item *>(&node)), text_of<cbor::item>, false};
}

inline std::expected<cbor::item const *, fault> node_of(value const &v)
{
    if (auto const *node = held_if<cbor::item>(v))
        return node;
    if (auto const *pointer = held_if<cbor::item const *>(v))
        return *pointer;
    return std::unexpected("the value is not a cbor::item");
}

template <class F, std::size_t... I>
result type_typed(std::size_t const i, F const f, std::index_sequence<I...>)
{
    result out = std::unexpected("not a cbor::type");
    static_cast<void>(
        ((i == I ? (out = f(std::integral_constant<cbor::type, static_cast<cbor::type>(I)>{}), true) : false) || ...));
    return out;
}

inline result item_get(call const &c)
{
    if (c.self == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    auto const node = node_of(*c.self);
    if (!node) [[unlikely]]
        return std::unexpected(node.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    constexpr std::string_view prefix = "cbor::type::";
    if (c.template_arguments.size() != 1 || !c.template_arguments.front().starts_with(prefix)) [[unlikely]]
        return std::unexpected("get takes one cbor::type: get<cbor::type::array>()");
    auto const found = std::ranges::find(type_names, std::string_view(c.template_arguments.front()).substr(prefix.size()));
    if (found == std::end(type_names)) [[unlikely]]
        return std::unexpected(c.template_arguments.front() + " is not a cbor::type");
    return type_typed(static_cast<std::size_t>(found - std::begin(type_names)),
                      [&]<cbor::type K>(std::integral_constant<cbor::type, K>) -> result {
                          return expected_view_of(*c.self, (*node)->template get<K>());
                      },
                      std::make_index_sequence<std::size(type_names)>{});
}

inline result item_type_of(call const &c)
{
    if (c.self == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    auto const node = node_of(*c.self);
    if (!node) [[unlikely]]
        return std::unexpected(node.error());
    return value_of((*node)->type);
}

inline result item_additional_information(call const &c)
{
    if (c.self == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    auto const node = node_of(*c.self);
    if (!node) [[unlikely]]
        return std::unexpected(node.error());
    return value_of((*node)->additional_information);
}

inline result owning_item_star(call const &c)
{
    auto const self = self_of<cbor::owning<cbor::item>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return node_view_of(*c.self, ***self);
}

inline result item_pointer_star(call const &c)
{
    auto const self = self_of<cbor::item const *>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return node_view_of(*c.self, ***self);
}

inline result nodes_index(call const &c)
{
    auto const self = self_of<std::span<cbor::item const>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    auto const i = unsigned_argument(c, 0);
    if (!i) [[unlikely]]
        return std::unexpected(i.error());
    if (*i >= (*self)->size()) [[unlikely]]
        return std::unexpected("the index is outside the span");
    return node_view_of(*c.self, (**self)[static_cast<std::size_t>(*i)]);
}

inline result reference_get(call const &c)
{
    auto const self = self_of<std::reference_wrapper<cbor::item const>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return node_view_of(*c.self, (*self)->get());
}

inline result tag_number_of(call const &c)
{
    auto const self = self_of<std::pair<std::uint64_t, cbor::item const *>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of((*self)->first);
}

inline result tag_content_of(call const &c)
{
    auto const self = self_of<std::pair<std::uint64_t, cbor::item const *>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (*self)->second);
}

inline result decode_made(call const &c)
{
    auto const encoded = text_argument(c, 0);
    if (!encoded || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::decode takes the bytes of one item");
    return value_of(cbor::decode(*encoded));
}

inline result encoded_cbor_data_item_found(call const &c)
{
    if (auto const ok = arguments_counted(c, 2); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const tree = argument_of<cbor::owning<cbor::item>>(c, 0, "a cbor::owning<cbor::item>");
    if (!tree) [[unlikely]]
        return std::unexpected(tree.error());
    auto const tagged = argument_of<cbor::item>(c, 1, "a cbor::item");
    if (!tagged) [[unlikely]]
        return std::unexpected(tagged.error());
    return expected_view_of(c.arguments.front(), cbor::encoded_cbor_data_item(**tree, **tagged));
}

template <bool Cycles>
result encode_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 2); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const root = argument_of<cbor::item>(c, 0, "a cbor::item");
    if (!root) [[unlikely]]
        return std::unexpected(root.error());
    auto const write = [&](auto &target) {
        if constexpr (Cycles)
            return cbor::encode_cycles(**root, target);
        else
            return cbor::encode(**root, target);
    };
    if (auto *const target = changed_if<std::string>(c.arguments[1]))
        return value_of(write(*target));
    if (auto const *const target = held_if<std::span<char>>(c.arguments[1])) {
        std::span<char> span = *target;
        return value_of(write(span));
    }
    return std::unexpected("the argument 2 is not a std::string or a std::span<char>");
}

template <class T>
result span_star(call const &c)
{
    auto const self = self_of<cbor::ref<T>>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, ***self);
}

inline result string_made(call const &c)
{
    if (c.arguments.empty())
        return value_of(std::string());
    auto const text = text_argument(c, 0);
    if (!text || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("std::string takes no argument or one text here");
    return value_of(std::string(*text));
}

inline result chars_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(std::array<char, 16>{});
}

template <class T>
std::expected<T, fault> fitting_argument(call const &c, std::size_t const i)
{
    auto const n = integer_argument(c, i);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    if (!std::in_range<T>(*n)) [[unlikely]]
        return std::unexpected("the argument " + number_text(i + 1) + " does not fit, so the braces do not compile");
    return static_cast<T>(*n);
}

inline result octets_made(call const &c)
{
    if (c.arguments.size() > 2) [[unlikely]]
        return std::unexpected("std::array<std::uint8_t, 2> takes at most 2 values");
    std::array<std::uint8_t, 2> out{};
    for (std::size_t i = 0; i < c.arguments.size(); ++i) {
        auto const n = fitting_argument<std::uint8_t>(c, i);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        out.at(i) = *n;
    }
    return value_of(out);
}

template <class T>
result unsigned_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = fitting_argument<T>(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return value_of(*n);
}

inline result span_made(call const &c)
{
    if (c.template_arguments.size() == 1 && c.template_arguments.front() == "char") {
        if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
            return std::unexpected(ok.error());
        auto *const chars = changed_if<std::array<char, 16>>(c.arguments.front());
        if (chars == nullptr) [[unlikely]]
            return std::unexpected("std::span<char> takes a std::array<char, 16> here");
        return view_of(c.arguments.front(), std::span<char>(*chars));
    }
    if (!c.template_arguments.empty()) [[unlikely]]
        return std::unexpected("std::span takes <char> or no template argument here");
    if (c.arguments.size() == 1) {
        auto *const octets = changed_if<std::array<std::uint8_t, 2>>(c.arguments.front());
        if (octets == nullptr) [[unlikely]]
            return std::unexpected("std::span takes a std::array<std::uint8_t, 2> here");
        return view_of(c.arguments.front(), std::span(*octets));
    }
    return std::unexpected("std::span takes one std::array here");
}

inline result as_bytes_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    value const &a = c.arguments.front();
    if (auto const *const s = held_if<std::span<char const>>(a))
        return view_of(a, std::as_bytes(*s));
    if (auto const *const s = held_if<std::span<char>>(a))
        return view_of(a, std::as_bytes(*s));
    if (auto const *const s = held_if<std::span<std::uint8_t, 2>>(a))
        return view_of(a, std::as_bytes(*s));
    return std::unexpected("std::as_bytes takes a std::span here");
}

inline result typed_array_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 2); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const tag = unsigned_argument(c, 0);
    if (!tag) [[unlikely]]
        return std::unexpected(tag.error());
    value const &a = c.arguments[1];
    if (auto const *const s = held_if<std::span<std::byte const>>(a))
        return view_of(a, cbor::typed_array{*tag, *s});
    if (auto const *const s = held_if<std::span<std::byte const, 2>>(a))
        return view_of(a, cbor::typed_array{*tag, *s});
    return std::unexpected("the argument 2 is not a std::span<std::byte const>");
}

template <cbor::simple_value V>
result simple_value_named(call const &)
{
    return value_of(V);
}

inline result simple_value_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = fitting_argument<std::uint8_t>(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return value_of(cbor::simple_value{*n});
}

inline constexpr entry decode_functions[] = {
    {"cbor::decode()", decode_made},
    {"cbor::encoded_cbor_data_item()", encoded_cbor_data_item_found},
    {"cbor::encode()", encode_made<false>},
    {"cbor::encode_cycles()", encode_made<true>},
    {.name = "cbor::owning<cbor::item>.operator*()", .run = owning_item_star, .runnable = true},
    {.name = "cbor::item const *.operator*()", .run = item_pointer_star, .runnable = true},
    {.name = "cbor::item const *.get()", .run = item_get, .candidates = item_get_candidates, .runnable = true},
    {.name = "cbor::item.get()", .run = item_get, .candidates = item_get_candidates, .runnable = true},
    {.name = "cbor::item.type", .run = item_type_of, .runnable = true},
    {.name = "cbor::item const *.additional_information", .run = item_additional_information, .runnable = true},
    {.name = "cbor::item.additional_information", .run = item_additional_information, .runnable = true},
    {.name = "std::span<cbor::item const>.size()", .run = view_size<std::span<cbor::item const>>, .runnable = true},
    {.name = "std::span<cbor::item const>.operator[]()", .run = nodes_index, .candidates = indexes_candidates<std::span<cbor::item const>>, .runnable = true},
    {.name = "std::reference_wrapper<cbor::item const>.get()", .run = reference_get, .runnable = true},
    {.name = "std::pair<std::uint64_t, cbor::item const *>.first", .run = tag_number_of, .runnable = true},
    {.name = "std::pair<std::uint64_t, cbor::item const *>.second", .run = tag_content_of, .runnable = true},
    {"std::string{}", string_made},
    {"std::array<char, 16>{}", chars_made},
    {"std::array<std::uint8_t, 2>{}", octets_made},
    {"std::uint8_t{}", unsigned_made<std::uint8_t>},
    {"std::uint16_t{}", unsigned_made<std::uint16_t>},
    {"std::uint32_t{}", unsigned_made<std::uint32_t>},
    {"std::uint64_t{}", unsigned_made<std::uint64_t>},
    {"std::span()", span_made},
    {"std::as_bytes()", as_bytes_made},
    {"cbor::typed_array{}", typed_array_made},
    {"cbor::simple_value::false_value", simple_value_named<cbor::simple_value::false_value>},
    {"cbor::simple_value::true_value", simple_value_named<cbor::simple_value::true_value>},
    {"cbor::simple_value::null", simple_value_named<cbor::simple_value::null>},
    {"cbor::simple_value::undefined", simple_value_named<cbor::simple_value::undefined>},
    {"cbor::simple_value{}", simple_value_made},
};

}

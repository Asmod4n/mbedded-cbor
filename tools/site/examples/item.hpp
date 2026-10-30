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

template <class T>
value expected_view_of(value const &owner, std::expected<T, cbor::error> const &v)
{
    value out = v ? view_of(owner, *v) : value_of(v.error());
    out.expected = true;
    return out;
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

inline result encode_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 2); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const root = argument_of<cbor::item>(c, 0, "a cbor::item");
    if (!root) [[unlikely]]
        return std::unexpected(root.error());
    if (auto *const target = changed_if<std::string>(c.arguments[1]))
        return value_of(cbor::encode(**root, *target));
    if (auto const *const target = held_if<std::span<char>>(c.arguments[1]))
        return value_of(cbor::encode(**root, *target));
    return std::unexpected("the argument 2 is not a std::string or a std::span<char>");
}

inline result encode_iov_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const root = argument_of<cbor::item>(c, 0, "a cbor::item");
    if (!root) [[unlikely]]
        return std::unexpected(root.error());
    return value_of(cbor::encode_iov(**root));
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
    if (auto const ok = arguments_counted(c, 2); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const text = argument_of<std::string>(c, 0, "a text");
    if (!text) [[unlikely]]
        return std::unexpected(text.error());
    auto const count = unsigned_argument(c, 1);
    if (!count) [[unlikely]]
        return std::unexpected(count.error());
    if (*count > (*text)->size()) [[unlikely]]
        return std::unexpected("the count is larger than the text");
    return view_of(c.arguments.front(), std::span((*text)->data(), static_cast<std::size_t>(*count)));
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

inline result iov_writer_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(cbor::iov_writer{});
}

template <class Target, class Argument>
value encoder_value(std::shared_ptr<void> owner, Argument &&argument)
{
    return {kind<encoder_over<Target>>::name, type_of<encoder_over<Target>>(),
            std::make_shared<encoder_over<Target>>(std::move(owner), std::forward<Argument>(argument)),
            text_of<encoder_over<Target>>, false};
}

inline result encoder_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    value const &a = c.arguments.front();
    if (auto *const target = changed_if<std::string>(a))
        return encoder_value<std::string>(a.held, *target);
    if (auto const *const target = held_if<std::span<char>>(a))
        return encoder_value<std::span<char>>(a.held, *target);
    if (auto *const target = changed_if<cbor::iov_writer>(a))
        return encoder_value<cbor::iov_writer>(a.held, *target);
    return std::unexpected("cbor::encoder takes a std::string, a std::span<char> or a cbor::iov_writer here");
}

template <class Target>
std::expected<cbor::encoder<Target> *, fault> encoder_of(call const &c)
{
    if (c.self == nullptr || changed_if<encoder_over<Target>>(*c.self) == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    return &changed_if<encoder_over<Target>>(*c.self)->encoder;
}

template <class F>
result unsigned_typed(call const &c, F const f)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    value const &a = c.arguments.front();
    if (auto const *const n = held_if<std::uint8_t>(a))
        return f(*n);
    if (auto const *const n = held_if<std::uint16_t>(a))
        return f(*n);
    if (auto const *const n = held_if<std::uint32_t>(a))
        return f(*n);
    if (auto const *const n = held_if<std::uint64_t>(a))
        return f(*n);
    if (auto const *const n = held_if<std::size_t>(a))
        return f(*n);
    return std::unexpected("the argument is " + std::string(a.kind) +
                           ", and the call compiles only for an unsigned integer type: std::uint8_t{5}");
}

template <class Target>
result encoder_unsigned_integer(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    return unsigned_typed(c, [&](auto const n) { return value_of((*e)->unsigned_integer(n)); });
}

template <class Target>
result encoder_negative_integer(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    return unsigned_typed(c, [&](auto const n) { return value_of((*e)->negative_integer(n)); });
}

template <class Target>
result encoder_byte_string(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    value const &a = c.arguments.front();
    if (auto const *const bytes = held_if<std::span<std::byte const>>(a))
        return value_of((*e)->byte_string(*bytes));
    if (auto const *const bytes = held_if<std::span<std::byte const, 2>>(a))
        return value_of((*e)->byte_string(*bytes));
    if (auto const *const bytes = held_if<cbor::ref<std::span<std::byte const>>>(a))
        return value_of((*e)->byte_string(*bytes));
    return std::unexpected("byte_string takes a std::span<std::byte const> or a cbor::ref of it here");
}

template <class Target>
result encoder_text_string(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    value const &a = c.arguments.front();
    if (auto const *const text = held_if<std::string>(a))
        return value_of((*e)->text_string(std::string_view(*text)));
    if (auto const *const text = held_if<std::string_view>(a))
        return value_of((*e)->text_string(*text));
    if (auto const *const text = held_if<std::shared_ptr<std::string const>>(a))
        return value_of((*e)->text_string(*text));
    if (auto const *const text = held_if<cbor::ref<std::string_view>>(a))
        return value_of((*e)->text_string(*text));
    return std::unexpected("text_string takes a text, a std::shared_ptr<std::string const> or a cbor::ref here");
}

template <class Target, auto Member>
result encoder_head(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const n = unsigned_argument(c, 0);
    if (!n) [[unlikely]]
        return std::unexpected(n.error());
    return value_of(((*e)->*Member)(*n));
}

template <class Target>
result encoder_simple_value(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    auto const v = argument_of<cbor::simple_value>(c, 0, "a cbor::simple_value");
    if (!v || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("simple_value takes one cbor::simple_value");
    return value_of((*e)->simple_value(**v));
}

template <class Target>
result encoder_floating_point(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    if (auto const *const f = held_if<float>(c.arguments.front()))
        return value_of((*e)->floating_point(*f));
    if (auto const *const d = held_if<double>(c.arguments.front()))
        return value_of((*e)->floating_point(*d));
    return std::unexpected("floating_point takes a float or a double");
}

template <class Target>
result encoder_typed_array(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    auto const v = argument_of<cbor::typed_array>(c, 0, "a cbor::typed_array");
    if (!v || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("typed_array takes one cbor::typed_array");
    return value_of((*e)->typed_array(**v));
}

template <class Target>
result encoder_finish(call const &c)
{
    auto const e = encoder_of<Target>(c);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of((*e)->finish());
}

using string_encoder = cbor::encoder<std::string>;
using chars_encoder = cbor::encoder<std::span<char>>;
using pieces_encoder = cbor::encoder<cbor::iov_writer>;

inline constexpr entry item_functions[] = {
    {"cbor::decode()", decode_made},
    {"cbor::encoded_cbor_data_item()", encoded_cbor_data_item_found},
    {"cbor::encode()", encode_made},
    {"cbor::encode_iov()", encode_iov_made},
    {"cbor::owning<cbor::item>.operator*()", owning_item_star},
    {"cbor::item const *.operator*()", item_pointer_star},
    {"cbor::item const *.get()", item_get},
    {"cbor::item.get()", item_get},
    {"cbor::item.type", item_type_of},
    {"cbor::item const *.additional_information", item_additional_information},
    {"cbor::item.additional_information", item_additional_information},
    {"std::span<cbor::item const>.size()", view_size<std::span<cbor::item const>>},
    {"std::span<cbor::item const>.operator[]()", nodes_index},
    {"std::reference_wrapper<cbor::item const>.get()", reference_get},
    {"std::pair<std::uint64_t, cbor::item const *>.first", tag_number_of},
    {"std::pair<std::uint64_t, cbor::item const *>.second", tag_content_of},
    {"cbor::ref<std::span<std::string_view const>>.operator*()", span_star<std::span<std::string_view const>>},
    {"cbor::ref<std::span<std::string_view const>>.operator->()", span_star<std::span<std::string_view const>>},
    {"std::span<std::string_view const>.size()", view_size<std::span<std::string_view const>>},
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
    {"cbor::iov_writer{}", iov_writer_made},
    {"cbor::encoder{}", encoder_made},
    {"cbor::encoder<std::string>.unsigned_integer()", encoder_unsigned_integer<std::string>, true},
    {"cbor::encoder<std::string>.negative_integer()", encoder_negative_integer<std::string>, true},
    {"cbor::encoder<std::string>.byte_string()", encoder_byte_string<std::string>, true},
    {"cbor::encoder<std::string>.text_string()", encoder_text_string<std::string>, true},
    {"cbor::encoder<std::string>.array()", encoder_head<std::string, &string_encoder::array>, true},
    {"cbor::encoder<std::string>.map()", encoder_head<std::string, &string_encoder::map>, true},
    {"cbor::encoder<std::string>.tag()", encoder_head<std::string, &string_encoder::tag>, true},
    {"cbor::encoder<std::string>.simple_value()", encoder_simple_value<std::string>, true},
    {"cbor::encoder<std::string>.floating_point()", encoder_floating_point<std::string>, true},
    {"cbor::encoder<std::string>.typed_array()", encoder_typed_array<std::string>, true},
    {"cbor::encoder<std::string>.finish()", encoder_finish<std::string>, true},
    {"cbor::encoder<std::span<char>>.unsigned_integer()", encoder_unsigned_integer<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.negative_integer()", encoder_negative_integer<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.byte_string()", encoder_byte_string<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.text_string()", encoder_text_string<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.array()", encoder_head<std::span<char>, &chars_encoder::array>, true},
    {"cbor::encoder<std::span<char>>.map()", encoder_head<std::span<char>, &chars_encoder::map>, true},
    {"cbor::encoder<std::span<char>>.tag()", encoder_head<std::span<char>, &chars_encoder::tag>, true},
    {"cbor::encoder<std::span<char>>.simple_value()", encoder_simple_value<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.floating_point()", encoder_floating_point<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.typed_array()", encoder_typed_array<std::span<char>>, true},
    {"cbor::encoder<std::span<char>>.finish()", encoder_finish<std::span<char>>, true},
    {"cbor::encoder<cbor::iov_writer>.unsigned_integer()", encoder_unsigned_integer<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.negative_integer()", encoder_negative_integer<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.byte_string()", encoder_byte_string<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.text_string()", encoder_text_string<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.array()", encoder_head<cbor::iov_writer, &pieces_encoder::array>, true},
    {"cbor::encoder<cbor::iov_writer>.map()", encoder_head<cbor::iov_writer, &pieces_encoder::map>, true},
    {"cbor::encoder<cbor::iov_writer>.tag()", encoder_head<cbor::iov_writer, &pieces_encoder::tag>, true},
    {"cbor::encoder<cbor::iov_writer>.simple_value()", encoder_simple_value<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.floating_point()", encoder_floating_point<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.typed_array()", encoder_typed_array<cbor::iov_writer>, true},
    {"cbor::encoder<cbor::iov_writer>.finish()", encoder_finish<cbor::iov_writer>, true},
};

}

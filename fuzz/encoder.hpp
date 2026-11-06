#pragma once

#include "common.hpp"

#include <cbor/encode.hpp>
#include <cbor/item_size.hpp>
#include <cbor/lazy.hpp>
#include <cbor/rfc8746.hpp>

#include <bit>
#include <cmath>
#include <memory>
#include <cstdint>
#include <optional>
#include <span>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif
#include <string>
#include <vector>

namespace fuzz
{

struct written {
    test::value value;
    std::optional<std::size_t> size;
};

// RFC 8949 3.3 Table 3: a binary64 float is the initial byte 0xfb and 8 bytes. The encoder writes a double at
// this width and keeps its bits.
inline constexpr std::size_t binary64_size = 9;

// A long string grows by this many bytes for each value of one input byte, so that it reaches over several
// blocks.
inline constexpr std::size_t long_string_step = 256;

// RFC 8746 2: the typed array tags are 64 to 87; tag 76 is reserved, and the encoder refuses it and a length that
// is not whole elements.
inline constexpr std::uint64_t typed_array_tag_first = 64;
inline constexpr std::uint64_t typed_array_tags = 24;

// A byte string reaches the encoder as a span, as a std::vector<std::byte> moved in, as a std::shared_ptr to one,
// or as the ref of a lazy that reads a byte string; each writes the same item.
inline void byte_string_put(cbor::encoder<std::string> &out, std::string const &b, std::uint8_t const form)
{
    std::span<std::byte const> const bytes = std::as_bytes(std::span(b));
    switch (form % 4) {
    case 0:
        require(out.byte_string(bytes).has_value());
        break;
    case 1:
        require(out.byte_string(std::vector<std::byte>(bytes.begin(), bytes.end())).has_value());
        break;
    case 2:
        require(out.byte_string(std::make_shared<std::vector<std::byte> const>(bytes.begin(), bytes.end())).has_value());
        break;
    default: {
        std::string head;
        cbor::encoder<std::string> h(head);
        require(h.byte_string(bytes).has_value() && h.finish().has_value());
        auto const node = cbor::lazy::take_ownership(std::move(head));
        require(node.has_value());
        auto const r = node->get<std::span<std::byte const>>();
        require(r.has_value());
        require(out.byte_string(*r).has_value());
    }
    }
}

// A text reaches the encoder as a string_view, as a std::string moved in, as a std::shared_ptr to one, or as the
// ref of a lazy that reads a text; each writes the same item.
inline void text_string_put(cbor::encoder<std::string> &out, std::string const &t, std::uint8_t const form)
{
    switch (form % 4) {
    case 0:
        require(out.text_string(std::string_view(t)).has_value());
        break;
    case 1:
        require(out.text_string(std::string(t)).has_value());
        break;
    case 2:
        require(out.text_string(std::make_shared<std::string const>(t)).has_value());
        break;
    default: {
        std::string head;
        cbor::encoder<std::string> h(head);
        require(h.text_string(std::string_view(t)).has_value() && h.finish().has_value());
        auto const node = cbor::lazy::take_ownership(std::move(head));
        require(node.has_value());
        auto const r = node->get<std::string_view>();
        require(r.has_value());
        require(out.text_string(*r).has_value());
    }
    }
}

// The floats of <stdfloat> keep their width: std::float16_t is 3 bytes, std::float32_t 5 and std::float64_t 9
// (RFC 8949 3.3). A binary16 NaN is left out, because the model reads its payload only through the path harness.
inline void stdfloat_put([[maybe_unused]] cbor::encoder<std::string> &out, [[maybe_unused]] std::uint64_t const bits,
                         [[maybe_unused]] std::vector<written> &items)
{
#if defined(__STDCPP_FLOAT16_T__) && defined(__STDCPP_FLOAT32_T__) && defined(__STDCPP_FLOAT64_T__)
    switch (bits % 3) {
    case 0: {
        auto const h = std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(bits >> 8));
        if (std::isnan(static_cast<double>(h)))
            return;
        require(out.floating_point(h).has_value());
        items.push_back({{static_cast<double>(h)}, 3});
        break;
    }
    case 1: {
        auto const f = std::bit_cast<std::float32_t>(static_cast<std::uint32_t>(bits >> 8));
        require(out.floating_point(f).has_value());
        items.push_back({{widened(static_cast<float>(f))}, 5});
        break;
    }
    default: {
        auto const d = std::bit_cast<std::float64_t>(bits);
        require(out.floating_point(d).has_value());
        items.push_back({{static_cast<double>(d)}, 9});
    }
    }
#endif
}

// A sequence of calls of cbor::encoder made from the bytes of the input. Each item is read back alone: it
// must hold the value that went in, and an item of a fixed width or a float must have the size RFC 8949 gives
// it. A refusal ends an encoder, so a simple value starts an encoder of its own after the bytes before it are
// finished, and a refused value takes a fresh encoder for the items after it.
inline void encoder_target(std::string_view const input)
{
    source in{input};
    std::string wire;
    std::vector<written> items;
    {
        std::optional<cbor::encoder<std::string>> out;
        out.emplace(wire);
        for (std::size_t n = in.byte() % 24; n > 0; --n) {
            switch (in.byte() % 11) {
            case 0: {
                std::uint64_t const a = in.number();
                require(out->unsigned_integer(a).has_value());
                items.push_back({{a}, 9});
                break;
            }
            case 1: {
                std::uint64_t const a = in.number();
                require(out->negative_integer(a).has_value());
                items.push_back({{test::negative{a}}, 9});
                break;
            }
            case 2: {
                std::string b = in.string();
                if (in.byte() % 8 == 0)
                    b.append(in.byte() * long_string_step, 'x');
                byte_string_put(*out, b, in.byte());
                items.push_back({{test::bytes{b}}, std::nullopt});
                break;
            }
            case 3: {
                std::string t = in.text();
                if (in.byte() % 8 == 0)
                    t.append(in.byte() * long_string_step, 'y');
                text_string_put(*out, t, in.byte());
                items.push_back({{t}, std::nullopt});
                break;
            }
            case 4: {
                double const d = std::bit_cast<double>(in.number());
                require(out->floating_point(d).has_value());
                items.push_back({{d}, binary64_size});
                break;
            }
            case 5: {
                std::uint8_t const s = in.byte() % 32;
                require(out->finish().has_value());
                out.emplace(wire);
                auto const r = out->simple_value(static_cast<cbor::simple_value>(s));
                require(r.has_value() == (s < 24));
                if (r)
                    items.push_back({{test::simple{s}}, 1});
                else
                    out.emplace(wire);
                break;
            }
            case 6: {
                std::uint8_t const width = in.byte() % 4;
                std::uint64_t const a = in.number();
                if (width == 0) {
                    require(out->unsigned_integer(static_cast<std::uint8_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint8_t>(a)}}, 2});
                } else if (width == 1) {
                    require(out->unsigned_integer(static_cast<std::uint16_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint16_t>(a)}}, 3});
                } else if (width == 2) {
                    require(out->unsigned_integer(static_cast<std::uint32_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint32_t>(a)}}, 5});
                } else {
                    require(out->unsigned_integer(a).has_value());
                    items.push_back({{a}, 9});
                }
                break;
            }
            case 7: {
                auto const a = static_cast<std::int64_t>(in.number());
                std::uint8_t const width = in.byte() % 2;
                if (width == 0) {
                    auto const v = static_cast<std::int16_t>(a);
                    if (v < 0) {
                        auto const argument = static_cast<std::uint16_t>(-1 - v);
                        require(out->negative_integer(argument).has_value());
                        items.push_back({{test::negative{argument}}, 3});
                    } else {
                        auto const argument = static_cast<std::uint16_t>(v);
                        require(out->unsigned_integer(argument).has_value());
                        items.push_back({{std::uint64_t{argument}}, 3});
                    }
                } else if (a < 0) {
                    auto const argument = static_cast<std::uint64_t>(-1 - a);
                    require(out->negative_integer(argument).has_value());
                    items.push_back({{test::negative{argument}}, 9});
                } else {
                    auto const argument = static_cast<std::uint64_t>(a);
                    require(out->unsigned_integer(argument).has_value());
                    items.push_back({{argument}, 9});
                }
                break;
            }
            case 9: {
                std::uint8_t const width = in.byte() % 3;
                std::uint64_t const a = in.number();
                if (width == 0) {
                    require(out->negative_integer(static_cast<std::uint8_t>(a)).has_value());
                    items.push_back({{test::negative{static_cast<std::uint8_t>(a)}}, 2});
                } else if (width == 1) {
                    require(out->negative_integer(static_cast<std::uint32_t>(a)).has_value());
                    items.push_back({{test::negative{static_cast<std::uint32_t>(a)}}, 5});
                } else {
                    stdfloat_put(*out, a, items);
                }
                break;
            }
            case 10: {
                std::uint64_t const tag = typed_array_tag_first + in.byte() % typed_array_tags;
                std::string const content = in.string();
                require(out->finish().has_value());
                out.emplace(wire);
                auto const r = out->typed_array(cbor::typed_array{tag, std::as_bytes(std::span(content))});
                if (r)
                    items.push_back({{test::tagged{tag, test::bytes{content}}}, std::nullopt});
                else
                    out.emplace(wire);
                break;
            }
            default: {
                std::uint64_t const bits = in.number();
                if (in.byte() & 1) {
                    auto const f = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
                    require(out->floating_point(f).has_value());
                    items.push_back({{widened(f)}, 5});
                } else {
                    auto const d = std::bit_cast<double>(bits);
                    require(out->floating_point(d).has_value());
                    items.push_back({{d}, 9});
                }
                break;
            }
            }
        }
        require(out->finish().has_value());
    }
    std::string_view rest = wire;
    for (auto const &item : items) {
        auto const end = cbor::item_size(rest);
        require(end.has_value());
        if (item.size)
            require(*end == *item.size);
        auto const back = test::decoded(rest.substr(0, *end));
        require(back.has_value() && same(*back, item.value));
        rest.remove_prefix(*end);
    }
    require(rest.empty());
}

} // namespace fuzz

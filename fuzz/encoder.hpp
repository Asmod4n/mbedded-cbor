#pragma once

#include "common.hpp"

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
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
            switch (in.byte() % 9) {
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
                require(out->byte_string(std::as_bytes(std::span(b))).has_value());
                items.push_back({{test::bytes{b}}, std::nullopt});
                break;
            }
            case 3: {
                std::string t = in.text();
                if (in.byte() % 8 == 0)
                    t.append(in.byte() * long_string_step, 'y');
                require(out->text_string(std::string_view(t)).has_value());
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

#pragma once

#include "common.hpp"

#include <bit>
#include <optional>
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

// A sequence of calls of cbor::encoder made from the bytes of the input. Each item is read back alone: it must
// hold the value that went in, and an item of a fixed width or a float must have the size RFC 8949 gives it.
inline void encoder_target(std::string_view const input)
{
    source in{input};
    string_writer w;
    std::vector<written> items;
    {
        cbor::encoding::encoder<string_writer> out{w};
        for (std::size_t n = in.byte() % 24; n > 0; --n) {
            switch (in.byte() % 9) {
            case 0: {
                std::uint64_t const a = in.number();
                require(out.head_encode(cbor::major_type::unsigned_integer, a).has_value());
                items.push_back({{a}, std::nullopt});
                break;
            }
            case 1: {
                std::uint64_t const a = in.number();
                require(out.head_encode(cbor::major_type::negative_integer, a).has_value());
                items.push_back({{test::negative{a}}, std::nullopt});
                break;
            }
            case 2: {
                std::string b = in.string();
                if (in.byte() % 8 == 0)
                    b.append(16384uz + in.byte() * 64uz, 'x');
                require(out.byte_string_encode(b).has_value());
                items.push_back({{test::bytes{b}}, std::nullopt});
                break;
            }
            case 3: {
                std::string t = in.text();
                if (in.byte() % 8 == 0)
                    t.append(16384uz + in.byte() * 64uz, 'y');
                require(out.text_string_encode(t).has_value());
                items.push_back({{t}, std::nullopt});
                break;
            }
            case 4: {
                double const d = std::bit_cast<double>(in.number());
                require(out.float_encode(d).has_value());
                items.push_back({{d}, binary64_size});
                break;
            }
            case 5: {
                std::uint8_t const s = in.byte() % 32;
                auto const r = out.simple_value_encode(static_cast<cbor::simple_value>(s));
                require(r.has_value() == (s < 24));
                if (r)
                    items.push_back({{test::simple{s}}, 1});
                break;
            }
            case 6: {
                std::uint8_t const width = in.byte() % 4;
                std::uint64_t const a = in.number();
                if (width == 0) {
                    require(out.fixed_width_unsigned_encode(static_cast<std::uint8_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint8_t>(a)}}, 2});
                } else if (width == 1) {
                    require(out.fixed_width_unsigned_encode(static_cast<std::uint16_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint16_t>(a)}}, 3});
                } else if (width == 2) {
                    require(out.fixed_width_unsigned_encode(static_cast<std::uint32_t>(a)).has_value());
                    items.push_back({{std::uint64_t{static_cast<std::uint32_t>(a)}}, 5});
                } else {
                    require(out.fixed_width_unsigned_encode(a).has_value());
                    items.push_back({{a}, 9});
                }
                break;
            }
            case 7: {
                auto const a = static_cast<std::int64_t>(in.number());
                std::uint8_t const width = in.byte() % 2;
                if (width == 0) {
                    auto const v = static_cast<std::int16_t>(a);
                    require(out.fixed_width_signed_encode(v).has_value());
                    items.push_back({v < 0 ? test::value{test::negative{static_cast<std::uint64_t>(-1 - v)}}
                                           : test::value{static_cast<std::uint64_t>(v)},
                                     3});
                } else {
                    require(out.fixed_width_signed_encode(a).has_value());
                    items.push_back({a < 0 ? test::value{test::negative{static_cast<std::uint64_t>(-1 - a)}}
                                           : test::value{static_cast<std::uint64_t>(a)},
                                     9});
                }
                break;
            }
            default: {
                std::uint64_t const bits = in.number();
                if (in.byte() & 1) {
                    auto const f = std::bit_cast<float>(static_cast<std::uint32_t>(bits));
                    require(out.fixed_width_float_encode(f).has_value());
                    items.push_back({{static_cast<double>(f)}, 5});
                } else {
                    auto const d = std::bit_cast<double>(bits);
                    require(out.fixed_width_float_encode(d).has_value());
                    items.push_back({{d}, 9});
                }
                break;
            }
            }
        }
        require(out.flush().has_value());
    }
    std::string_view rest = w.encoded;
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

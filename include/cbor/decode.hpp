#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "item_end.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "shared.hpp"

namespace cbor
{

class decoding
{
    template <class Binding>
    using marks = std::vector<std::optional<typename Binding::value>>;

    struct prefix {
        value_sharing::top_level_item &top_level;
        std::vector<bool> evaluating;
    };

    template <class Binding>
    struct value_decoder {
        heads::decoder d;
        Binding &binding;
        marks<Binding> shared;
        prefix *before;

        template <std::size_t DepthMax>
        std::expected<typename Binding::value, error> value_decode(std::size_t const depth, std::optional<std::size_t> const mark)
        {
            if (auto const r = validity::check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            switch (h->major) {
            case major_type::unsigned_integer:
                return binding.unsigned_integer_decode(h->argument);
            case major_type::negative_integer:
                return binding.negative_integer_decode(h->argument);
            case major_type::byte_string: {
                auto const s = d.byte_string_decode(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                return binding.byte_string_decode(*s);
            }
            case major_type::text_string: {
                auto const s = d.byte_string_decode(h->argument);
                if (!s) [[unlikely]]
                    return std::unexpected(s.error());
                return binding.text_string_decode(*s);
            }
            case major_type::array: {
                auto array = binding.array_decode(std::min<std::uint64_t>(h->argument, d.encoded.size()));
                if constexpr (requires { binding.cyclic_data_structures(); })
                    if (mark && binding.cyclic_data_structures())
                        shared[*mark] = array;
                for (std::uint64_t i = 0; i < h->argument; ++i) {
                    auto element = value_decode<DepthMax>(depth + 1, std::nullopt);
                    if (!element) [[unlikely]]
                        return element;
                    array = binding.array_append(std::move(array), std::move(*element));
                }
                return array;
            }
            case major_type::map: {
                auto map = binding.map_decode(std::min<std::uint64_t>(h->argument, d.encoded.size() / 2));
                if constexpr (requires { binding.cyclic_data_structures(); })
                    if (mark && binding.cyclic_data_structures())
                        shared[*mark] = map;
                for (std::uint64_t i = 0; i < h->argument; ++i) {
                    if constexpr (requires(std::string_view const t) { binding.map_key_decode(t); }) {
                        heads::decoder probe = d;
                        auto const k = probe.head_decode();
                        if (k && k->major == major_type::text_string) {
                            auto const t = probe.byte_string_decode(k->argument);
                            if (!t) [[unlikely]]
                                return std::unexpected(t.error());
                            d = probe;
                            auto key = binding.map_key_decode(*t);
                            auto value = value_decode<DepthMax>(depth + 1, std::nullopt);
                            if (!value) [[unlikely]]
                                return value;
                            map = binding.map_insert(std::move(map), std::move(key), std::move(*value));
                            continue;
                        }
                    }
                    auto key = value_decode<DepthMax>(depth + 1, std::nullopt);
                    if (!key) [[unlikely]]
                        return key;
                    auto value = value_decode<DepthMax>(depth + 1, std::nullopt);
                    if (!value) [[unlikely]]
                        return value;
                    map = binding.map_insert(std::move(map), std::move(*key), std::move(*value));
                }
                return map;
            }
            case major_type::tag: {
                if (auto const r = validity::check_nesting_depth(depth + 1, DepthMax); !r) [[unlikely]]
                    return std::unexpected(r.error());
                if (h->argument == std::to_underlying(rfc8949::tag_number::shareable)) {
                    if (!before) {
                        std::size_t const index = shared.size();
                        shared.emplace_back();
                        auto content = value_decode<DepthMax>(depth + 1, index);
                        if (!content) [[unlikely]]
                            return content;
                        shared[index] = *content;
                        return content;
                    }
                    std::size_t const index = before->top_level.mark(d);
                    if (index >= shared.size()) {
                        shared.resize(index + 1);
                        before->evaluating.resize(index + 1);
                    }
                    if (shared[index]) {
                        if (auto const r = well_formedness::item_skip<DepthMax>(d, before->top_level, depth + 1); !r) [[unlikely]]
                            return std::unexpected(r.error());
                        return *shared[index];
                    }
                    before->evaluating[index] = true;
                    auto content = value_decode<DepthMax>(depth + 1, index);
                    before->evaluating[index] = false;
                    if (!content) [[unlikely]]
                        return content;
                    shared[index] = *content;
                    return content;
                }
                if (h->argument == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
                    h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)) {
                    bool const negative =
                        h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                    auto const r = d.head_decode();
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    if (error const c = validity::check_tag_content(h->argument, r->major).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    auto const bytes = d.byte_string_decode(r->argument);
                    if (!bytes) [[unlikely]]
                        return std::unexpected(bytes.error());
                    std::string_view const magnitude = heads::magnitude_without_leading_zeros(*bytes);
                    if (magnitude.size() <= sizeof(std::uint64_t)) {
                        if (negative)
                            return binding.negative_integer_decode(heads::magnitude_value(magnitude));
                        return binding.unsigned_integer_decode(heads::magnitude_value(magnitude));
                    }
                    if (negative)
                        return binding.negative_bignum_decode(std::string_view(heads::magnitude_plus_one(magnitude)));
                    return binding.unsigned_bignum_decode(magnitude);
                }
                if (h->argument == std::to_underlying(rfc8949::tag_number::sharedref)) {
                    auto const r = d.head_decode();
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    if (error const c = validity::check_tag_content(h->argument, r->major).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    auto const checked = validity::check_sharedref_index(r->argument, shared.size());
                    if (!checked) [[unlikely]]
                        return std::unexpected(checked.error());
                    std::size_t const index = *checked;
                    if (!shared[index] && before && !before->evaluating[index] &&
                        before->top_level.sharedrefs[index].offset < before->top_level.encoded.size() - d.encoded.size()) {
                        std::string_view const rest = d.encoded;
                        d.encoded = std::string_view(std::span(before->top_level.encoded).subspan(before->top_level.sharedrefs[index].offset));
                        before->evaluating[index] = true;
                        auto content = value_decode<DepthMax>(depth + 1, index);
                        before->evaluating[index] = false;
                        d.encoded = rest;
                        if (!content) [[unlikely]]
                            return content;
                        shared[index] = *content;
                    }
                    if (!shared[index]) [[unlikely]]
                        return std::unexpected(error::sharedref_not_complete);
                    return *shared[index];
                }
                if constexpr (requires { binding.tag_begin(h->argument); }) {
                    std::optional<typename Binding::value> object = binding.tag_begin(h->argument);
                    if (object) {
                        if constexpr (requires { binding.cyclic_data_structures(); })
                            if (mark && binding.cyclic_data_structures())
                                shared[*mark] = *object;
                        auto content = value_decode<DepthMax>(depth + 1, std::nullopt);
                        if (!content) [[unlikely]]
                            return content;
                        return binding.after_decode(binding.registered_decode(std::move(*object), std::move(*content)));
                    }
                }
                auto content = value_decode<DepthMax>(depth + 1, std::nullopt);
                if (!content) [[unlikely]]
                    return content;
                return binding.tag_decode(h->argument, std::move(*content));
            }
            default:
                switch (static_cast<rfc8949::simple_float_information>(h->info)) {
                case rfc8949::simple_float_information::simple_value_follows:
                    if (error const r = validity::check_simple_value(h->info, h->argument).error_or(error{});
                        r != error{}) [[unlikely]]
                        return std::unexpected(r);
                    return binding.simple_value_decode(static_cast<std::uint8_t>(h->argument));
                case rfc8949::simple_float_information::half_precision_float:
                case rfc8949::simple_float_information::single_precision_float:
                case rfc8949::simple_float_information::double_precision_float:
                    return binding.float_decode(heads::float_decode(h->info, h->argument));
                default:
                    return binding.simple_value_decode(h->info);
                }
            }
        }
    };

    template <std::size_t DepthMax, class Binding>
        requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
    friend std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l);
};

}

#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "binding.hpp"
#include "decode.hpp"
#include "item_end.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "owning_ref.hpp"
#include "shared.hpp"

namespace cbor
{

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

template <std::size_t DepthMax>
struct lazy_elements {
    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<value_sharing::top_level_item> top_level;
        std::size_t offset;
        std::uint64_t left;
        error failure;

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return lazy{top_level, offset};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(offset))};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *top_level, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            offset = top_level->encoded.size() - d.encoded.size();
            --left;
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, error{}};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <std::size_t DepthMax>
struct lazy_entries {
    std::shared_ptr<value_sharing::top_level_item> top_level;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<value_sharing::top_level_item> top_level;
        std::size_t key;
        std::size_t value;
        std::uint64_t left;
        error failure;

        void value_find()
        {
            if (left == 0)
                return;
            heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(key))};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *top_level, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            value = top_level->encoded.size() - d.encoded.size();
        }

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return std::pair{lazy{top_level, key}, lazy{top_level, value}};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            heads::decoder d{std::string_view(std::span(top_level->encoded).subspan(value))};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *top_level, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            key = top_level->encoded.size() - d.encoded.size();
            --left;
            value_find();
            return *this;
        }

        void operator++(int)
        {
            ++*this;
        }

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }
    };

    iterator begin() const
    {
        iterator first{top_level, offset, offset, count, error{}};
        first.value_find();
        return first;
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded)
{
    validity::throw_logic_error_if_null(encoded, "cbor::decode: the encoded data item is null");
    validity::throw_logic_error_if_empty(encoded,
                                         "cbor::decode: the owner of the encoded data item is empty");
    return lazy{std::make_shared<value_sharing::top_level_item>(encoded, *encoded, std::vector<lazy>{}, 0), 0};
}

template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy, error> decode(std::string_view const encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax, std::same_as<std::string> Encoded>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy, error> decode(Encoded &&encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(std::move(encoded)));
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    validity::throw_logic_error_if_empty(owner,
                                         "cbor::lazy::from: the owner of the encoded data item is empty");
    return lazy{std::make_shared<value_sharing::top_level_item>(std::move(owner), encoded, std::vector<lazy>{}, 0), 0};
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<std::string const> encoded)
{
    validity::throw_logic_error_if_null(encoded, "cbor::lazy::from: the encoded data item is null");
    std::string_view const view = *encoded;
    return from(std::move(encoded), view);
}

template <std::same_as<std::string> Encoded>
std::expected<lazy, error> lazy::from(Encoded &&encoded)
{
    return from(std::make_shared<std::string const>(std::move(encoded)));
}

inline std::expected<lazy, error> lazy::from(std::string_view const encoded)
{
    return from(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax, class Match>
std::expected<lazy, error> value_sharing::key_find(resolved const &found, Match const &match)
{
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    top_level_item &source = *found.source;
    heads::decoder d = found.d;
    for (std::uint64_t i = 0; i < found.h.argument; ++i) {
        auto const key_at = shared_resolve(source, source.encoded.size() - d.encoded.size());
        if (!key_at) [[unlikely]]
            return std::unexpected(key_at.error());
        heads::decoder probe{std::string_view(std::span(source.encoded).subspan(*key_at))};
        auto const k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        std::expected<bool, error> const matched = match(*k, probe);
        if (!matched) [[unlikely]]
            return std::unexpected(matched.error());
        if (auto const r = well_formedness::item_skip<DepthMax>(d, source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
        if (*matched)
            return lazy{found.source, source.encoded.size() - d.encoded.size()};
        if (auto const r = well_formedness::item_skip<DepthMax>(d, source, 1); !r) [[unlikely]]
            return std::unexpected(r.error());
    }
    return std::unexpected(error::key_not_found);
}

template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy, error> lazy::at(std::string_view const key) const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return value_sharing::key_find<DepthMax>(
        *found, [key](heads::head const &k, heads::decoder const probe) -> std::expected<bool, error> {
            if (k.major != major_type::text_string)
                return false;
            heads::decoder text = probe;
            auto const content = text.byte_string_decode(k.argument);
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            return *content == key;
        });
}

template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy, error> lazy::at(std::int64_t const index) const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if (h.major == major_type::array) {
        std::int64_t const size =
            h.argument > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(h.argument);
        std::int64_t const position = index < 0 ? index + size : index;
        if (position < 0 || position >= size) [[unlikely]]
            return std::unexpected(error::index_out_of_bounds);
        for (std::int64_t i = 0; i < position; ++i)
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *source, 1); !r) [[unlikely]]
                return std::unexpected(r.error());
        std::size_t const element = source->encoded.size() - d.encoded.size();
        return lazy{source, element};
    }
    return value_sharing::key_find<DepthMax>(
        *found, [index](heads::head const &k, heads::decoder) -> std::expected<bool, error> {
            return (k.major == major_type::unsigned_integer && index >= 0 && k.argument == static_cast<std::uint64_t>(index)) ||
                   (k.major == major_type::negative_integer && index < 0 && k.argument == static_cast<std::uint64_t>(-1 - index));
        });
}

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           owning_ref<T>, T>, error> lazy::get() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto [source, h, d] = *found;
    if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
        bool negative = h.major == major_type::negative_integer;
        std::uint64_t argument = h.argument;
        if (h.major == major_type::tag &&
            (h.argument == std::to_underlying(heads::tag_number::unsigned_bignum) ||
             h.argument == std::to_underlying(heads::tag_number::negative_bignum))) {
            negative = h.argument == std::to_underlying(heads::tag_number::negative_bignum);
            auto const content = value_sharing::shared_resolve(*source, source->encoded.size() - d.encoded.size());
            if (!content) [[unlikely]]
                return std::unexpected(content.error());
            d = heads::decoder{std::string_view(std::span(source->encoded).subspan(*content))};
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->major != major_type::byte_string) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            auto const bytes = d.byte_string_decode(r->argument);
            if (!bytes) [[unlikely]]
                return std::unexpected(bytes.error());
            std::string_view const magnitude = heads::magnitude_without_leading_zeros(*bytes);
            if (magnitude.size() > sizeof(std::uint64_t)) [[unlikely]]
                return std::unexpected(error::number_out_of_range);
            argument = heads::magnitude_value(magnitude);
        } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
            return std::unexpected(error::incorrect_type);
        }
        if (!std::in_range<T>(argument) || (std::is_unsigned_v<T> && negative)) [[unlikely]]
            return std::unexpected(error::number_out_of_range);
        T const magnitude = static_cast<T>(argument);
        return static_cast<T>(negative ? ~magnitude : magnitude);
    } else if constexpr (std::is_same_v<T, double>) {
        if (h.major != major_type::simple_float) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        switch (static_cast<heads::simple_float_information>(h.info)) {
        case heads::simple_float_information::half_precision_float:
        case heads::simple_float_information::single_precision_float:
        case heads::simple_float_information::double_precision_float:
            return heads::float_decode(h.info, h.argument);
        [[unlikely]] default:
            return std::unexpected(error::incorrect_type);
        }
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!heads::is_boolean(h)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return h.info == std::to_underlying(simple_value::true_value);
    } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
        if (!heads::is_null(h)) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        return nullptr;
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        if (h.major != major_type::text_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const text = d.byte_string_decode(h.argument);
        if (!text) [[unlikely]]
            return std::unexpected(text.error());
        return owning_ref<T>(source->owner, *text);
    } else if constexpr (std::is_same_v<T, typed_array>) {
        if (h.major != major_type::tag) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        if (auto const r = validity::typed_array_check(h.argument, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const content = value_sharing::shared_resolve(*source, source->encoded.size() - d.encoded.size());
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        d = heads::decoder{std::string_view(std::span(source->encoded).subspan(*content))};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (r->major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        auto const bytes = d.byte_string_decode(r->argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        if (auto const c = validity::typed_array_check(h.argument, bytes->size()); !c) [[unlikely]]
            return std::unexpected(c.error());
        return owning_ref<T>(source->owner, typed_array{h.argument, std::as_bytes(std::span(*bytes))});
    } else {
        if (h.major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::incorrect_type);
        auto const bytes = d.byte_string_decode(h.argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        return owning_ref<T>(source->owner, std::as_bytes(std::span(*bytes)));
    }
}
template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy_elements<DepthMax>, error> lazy::elements() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_elements<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<lazy_entries<DepthMax>, error> lazy::entries() const
{
    auto const found = value_sharing::container_resolve(top_level, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_entries<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax>
std::expected<std::pair<item *, std::size_t>, error> value_sharing::item_decode(top_level_item &top_level, std::size_t const at,
                                                                               std::size_t const depth)
{
    if (auto const r = validity::check_nesting_depth(depth, DepthMax); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const h = heads::raw_head_read(top_level.encoded, at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    if (h->info == std::to_underlying(heads::additional_information::indefinite_length)) [[unlikely]]
        return std::unexpected(h->major >= major_type::byte_string && h->major <= major_type::map ? error::indefinite_length
                                                                                                 : error::syntax_error);
    if (h->major == major_type::tag && h->argument == std::to_underlying(heads::tag_number::shareable)) {
        top_level.mark(heads::decoder{std::string_view(std::span(top_level.encoded).subspan(h->at))});
        return item_decode<DepthMax>(top_level, h->at, depth + 1);
    }
    if (h->major == major_type::tag && h->argument == std::to_underlying(heads::tag_number::sharedref)) {
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(h->at))};
        auto const found = top_level.sharedref_decode(d, at);
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        auto const target = item_resolve(top_level, found->offset);
        if (!target) [[unlikely]]
            return std::unexpected(target.error());
        if (std::holds_alternative<lazy>((*target)->content)) {
            auto const built = item_decode<DepthMax>(top_level, found->offset, depth);
            if (!built) [[unlikely]]
                return std::unexpected(built.error());
        }
        return std::pair{*target, top_level.encoded.size() - d.encoded.size()};
    }
    auto const e = top_level.entry(at);
    if (!e) [[unlikely]]
        return std::unexpected(e.error());
    item *const node = *e;
    if (!std::holds_alternative<lazy>(node->content)) {
        if (h->major == major_type::array || h->major == major_type::map)
            return std::pair{node, h->at + std::get<std::span<std::byte const>>(node->content).size()};
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(at))};
        if (auto const r = well_formedness::item_skip<DepthMax>(d, top_level, depth); !r) [[unlikely]]
            return std::unexpected(r.error());
        return std::pair{node, top_level.encoded.size() - d.encoded.size()};
    }
    std::size_t const left = top_level.encoded.size() - h->at;
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        node->content = std::monostate{};
        return std::pair{node, h->at};
    case major_type::byte_string:
    case major_type::text_string: {
        if (h->argument > left) [[unlikely]]
            return std::unexpected(error::too_little_data);
        std::string_view const string{std::span(top_level.encoded).subspan(h->at, h->argument)};
        if (h->major == major_type::text_string)
            node->content = string;
        else
            node->content = std::as_bytes(std::span(string));
        return std::pair{node, h->at + string.size()};
    }
    case major_type::array:
    case major_type::map: {
        heads::decoder d{std::string_view(std::span(top_level.encoded).subspan(at))};
        if (auto const r = well_formedness::item_skip<DepthMax>(d, top_level, depth); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::size_t const end = top_level.encoded.size() - d.encoded.size();
        node->content = std::as_bytes(std::span(top_level.encoded).subspan(h->at, end - h->at));
        return std::pair{node, end};
    }
    case major_type::tag: {
        node->content = static_cast<item const *>(nullptr);
        auto const content = item_decode<DepthMax>(top_level, h->at, depth + 1);
        if (!content) [[unlikely]] {
            node->content = lazy{{}, at};
            return std::unexpected(content.error());
        }
        node->content = static_cast<item const *>(content->first);
        return std::pair{node, content->second};
    }
    case major_type::simple_float:
        switch (static_cast<heads::simple_float_information>(h->info)) {
        case heads::simple_float_information::half_precision_float:
#if defined(__STDCPP_FLOAT16_T__)
            node->content = std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(h->argument));
#else
            node->content = heads::float_decode_binary16(static_cast<std::uint16_t>(h->argument));
#endif
            break;
        case heads::simple_float_information::single_precision_float:
            node->content = std::bit_cast<float>(static_cast<std::uint32_t>(h->argument));
            break;
        case heads::simple_float_information::double_precision_float:
            node->content = std::bit_cast<double>(h->argument);
            break;
        default:
            if (auto const r = validity::check_simple_value(h->info, h->argument); !r) [[unlikely]]
                return std::unexpected(r.error());
            node->content = std::monostate{};
            break;
        }
        return std::pair{node, h->at};
    }
    std::unreachable();
}

template <std::size_t DepthMax, class Self>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value() &&
             std::is_lvalue_reference_v<Self>)
std::expected<std::reference_wrapper<item const>, error> lazy::decode(this Self &&self)
{
    auto const built = value_sharing::item_decode<DepthMax>(*self.top_level, self.offset, 0);
    if (!built) [[unlikely]]
        return std::unexpected(built.error());
    return std::cref(*built->first);
}

template <std::size_t DepthMax, class Binding>
    requires(validity::check_nesting_depth(DepthMax, validity::nesting_depth_limit).has_value())
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l)
{
    decoding::prefix before{*l.top_level, std::vector<bool>(l.top_level->sharedrefs.size())};
    decoding::value_decoder<Binding> v{
        {std::string_view(std::span(l.top_level->encoded).subspan(l.offset))}, binding, decoding::marks<Binding>(l.top_level->sharedrefs.size()), &before};
    return v.template value_decode<DepthMax>(0, std::nullopt);
}

}

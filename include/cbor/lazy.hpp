#pragma once

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "binding.hpp"
#include "decode.hpp"
#include "doc_end.hpp"
#include "error.hpp"
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
    std::shared_ptr<value_sharing::document> document;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<value_sharing::document> document;
        std::size_t offset;
        std::uint64_t left;
        error failure;

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return lazy{document, offset};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            heads::decoder d{document->encoded.substr(offset)};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            offset = document->encoded.size() - d.encoded.size();
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
        return iterator{document, offset, count, error{}};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <std::size_t DepthMax>
struct lazy_entries {
    std::shared_ptr<value_sharing::document> document;
    std::size_t offset;
    std::uint64_t count;

    struct iterator {
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        std::shared_ptr<value_sharing::document> document;
        std::size_t key;
        std::size_t value;
        std::uint64_t left;
        error failure;

        void value_find()
        {
            if (left == 0)
                return;
            heads::decoder d{document->encoded.substr(key)};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            value = document->encoded.size() - d.encoded.size();
        }

        value_type operator*() const
        {
            if (failure != error{}) [[unlikely]]
                return std::unexpected(failure);
            return std::pair{lazy{document, key}, lazy{document, value}};
        }

        iterator &operator++()
        {
            if (failure != error{}) [[unlikely]] {
                left = 0;
                return *this;
            }
            heads::decoder d{document->encoded.substr(value)};
            if (auto const r = well_formedness::item_skip<DepthMax>(d, *document, 1); !r) [[unlikely]] {
                failure = r.error();
                return *this;
            }
            key = document->encoded.size() - d.encoded.size();
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
        iterator first{document, offset, offset, count, error{}};
        first.value_find();
        return first;
    }

    std::default_sentinel_t end() const
    {
        return {};
    }
};

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded)
{
    if (!encoded) [[unlikely]]
        throw std::logic_error("cbor::decode: the encoded data item is empty");
    return lazy{std::make_shared<value_sharing::document>(encoded, *encoded, std::vector<lazy>{}, 0), 0};
}

template <std::size_t DepthMax>
std::expected<lazy, error> decode(std::string_view const encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax, std::same_as<std::string> Encoded>
std::expected<lazy, error> decode(Encoded &&encoded)
{
    return decode<DepthMax>(std::make_shared<std::string const>(std::move(encoded)));
}

template <>
struct result<lazy, error> : std::expected<lazy, error> {
    using std::expected<lazy, cbor::error>::expected;

    template <std::size_t DepthMax = 64>
    result at(std::string_view const key) const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).at<DepthMax>(key);
    }

    template <std::size_t DepthMax = 64>
    result at(std::int64_t const index) const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).at<DepthMax>(index);
    }

    template <class T>
    auto get() const -> decltype((**this).template get<T>())
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).get<T>();
    }

    template <std::size_t DepthMax = 64>
    cbor::result<lazy_elements<DepthMax>> elements() const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).elements<DepthMax>();
    }

    template <std::size_t DepthMax = 64>
    cbor::result<lazy_entries<DepthMax>> entries() const
    {
        if (!has_value()) [[unlikely]]
            return std::unexpected(error());
        return (**this).entries<DepthMax>();
    }
};

inline result<lazy> lazy::from(std::shared_ptr<void const> owner, std::string_view const encoded)
{
    if (!owner) [[unlikely]]
        throw std::logic_error("cbor::lazy::from: the owner of the encoded data item is empty");
    return lazy{std::make_shared<value_sharing::document>(std::move(owner), encoded, std::vector<lazy>{}, 0), 0};
}

inline result<lazy> lazy::from(std::shared_ptr<std::string const> encoded)
{
    std::string_view const view = *encoded;
    return from(std::move(encoded), view);
}

template <std::same_as<std::string> Encoded>
result<lazy> lazy::from(Encoded &&encoded)
{
    return from(std::make_shared<std::string const>(std::move(encoded)));
}

inline result<lazy> lazy::from(std::string_view const encoded)
{
    return from(std::make_shared<std::string const>(encoded));
}

template <std::size_t DepthMax, class Match>
result<lazy> value_sharing::key_find(resolved const &found, Match const &match)
{
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    document &source = *found.source;
    heads::decoder d = found.d;
    for (std::uint64_t i = 0; i < found.h.argument; ++i) {
        auto const key_at = shared_resolve(source, source.encoded.size() - d.encoded.size());
        if (!key_at) [[unlikely]]
            return std::unexpected(key_at.error());
        heads::decoder probe{source.encoded.substr(*key_at)};
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
result<lazy> lazy::at(std::string_view const key) const
{
    auto const found = value_sharing::container_resolve(document, offset);
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
result<lazy> lazy::at(std::int64_t const index) const
{
    auto const found = value_sharing::container_resolve(document, offset);
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
result<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           owning_ref<T>, T>> lazy::get() const
{
    auto const found = value_sharing::container_resolve(document, offset);
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
            d = heads::decoder{source->encoded.substr(*content)};
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
        return static_cast<T>(negative ? -1 - magnitude : magnitude);
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
        if (auto const r = heads::typed_array_check(h.argument, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        auto const content = value_sharing::shared_resolve(*source, source->encoded.size() - d.encoded.size());
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        d = heads::decoder{source->encoded.substr(*content)};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (r->major != major_type::byte_string) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        auto const bytes = d.byte_string_decode(r->argument);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        if (auto const c = heads::typed_array_check(h.argument, bytes->size()); !c) [[unlikely]]
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
result<lazy_elements<DepthMax>> lazy::elements() const
{
    auto const found = value_sharing::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_elements<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax>
result<lazy_entries<DepthMax>> lazy::entries() const
{
    auto const found = value_sharing::container_resolve(document, offset);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    return lazy_entries<DepthMax>{source, source->encoded.size() - d.encoded.size(), h.argument};
}
template <std::size_t DepthMax, class Binding>
std::expected<typename Binding::value, error> lazy_decode(Binding &binding, lazy const &l)
{
    decoding::prefix before{*l.document, std::vector<bool>(l.document->sharedrefs.size())};
    decoding::marks<Binding> shared(l.document->sharedrefs.size());
    heads::decoder d{l.document->encoded.substr(l.offset)};
    return decoding::value_decode<DepthMax>(d, binding, shared, &before, 0, std::nullopt);
}

}

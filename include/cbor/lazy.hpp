#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <forward_list>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#ifdef __STDCPP_FLOAT128_T__
#include <stdfloat>
#endif

#include "rfc8746.hpp"
#include "rfc9535.hpp"
#include "item_size.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "ref.hpp"
#include "item.hpp"
#include "decode.hpp"

namespace cbor
{

struct lazy_elements;

struct lazy_entries;

struct lazy;

using nodelist = std::vector<lazy>;

template <class T>
concept scalar_result = (std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                        std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
                        || std::is_same_v<T, std::float128_t>
#endif
    ;

template <class T>
concept view_result =
    std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>;

template <class T>
concept path_result = scalar_result<T> || view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>;

template <class E>
inline constexpr bool is_path_query_v = false;

struct lazy {
private:
    struct top_level_item {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
    };

public:
    top_level_item top_level;
    validity::offset offset;
    std::uint64_t typed_array_tag{};

    template <borrowed_bytes Encoded>
    static std::expected<lazy, error> view(std::shared_ptr<void const> owner, Encoded const &encoded);
    template <owning_bytes Encoded>
    static std::expected<lazy, error> view(std::shared_ptr<Encoded const> owner);
    template <owned_bytes Encoded>
    static std::expected<lazy, error> take(Encoded &&encoded);

    std::expected<lazy, error> at(std::string_view key) const;

    std::expected<lazy, error> at(key k) const;

    std::expected<lazy, error> at(std::size_t index) const;

    std::expected<lazy, error> operator[](std::string_view key) const;

    std::expected<lazy, error> operator[](key k) const;

    std::expected<lazy, error> operator[](std::size_t index) const;

    template <fixed_string Path>
        requires is_valid_path_v<Path>
    std::expected<lazy, error> at_path() const;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> &&
                 ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                  std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
                  || std::is_same_v<T, std::float128_t>
#endif
                  ))
    std::expected<T, error> at_path() const;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> &&
                 (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                  std::is_same_v<T, typed_array>))
    std::expected<ref<T>, error> at_path() const;

    template <fixed_string Path, std::same_as<nodelist> T>
        requires is_valid_path_v<Path>
    std::expected<nodelist, error> at_path() const;

    template <class E>
        requires is_path_query_v<E>
    std::expected<typename E::result_type, error> at_path(E const &e) const;

    std::expected<std::uint64_t, error> size() const;

    std::expected<bool, error> empty() const;

    std::expected<cbor::type, error> type() const;

    template <class T>
        requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                 std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
                 std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
#ifdef __STDCPP_FLOAT128_T__
                 || std::is_same_v<T, std::float128_t>
#endif
    std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               ref<T>, T>, error> get() const;

    std::expected<lazy_elements, error> elements() const;

    std::expected<lazy_entries, error> entries() const;

    std::expected<std::shared_ptr<item const>, error> decode() const;

private:
    static std::expected<lazy, error> from(std::shared_ptr<void const> owner, std::string_view encoded,
                                           limit_values const &loaded);

    template <class Entries = lazy_entries>
    std::expected<typename Entries::iterator, error> find(std::string_view key, limit_values const &loaded) const;

    template <class Entries = lazy_entries>
    std::expected<typename Entries::iterator, error> find(std::int64_t key, limit_values const &loaded) const;

    template <class T>
    auto get(limit_values const &loaded) const;

    std::expected<lazy_elements, error> elements(limit_values const &loaded) const;

    std::expected<lazy_entries, error> entries(limit_values const &loaded) const;

    template <class F>
    decltype(auto) limits_apply(limit_values const &loaded, F &&f) const;

    template <bool Checked>
    struct resolved;

    template <bool Checked>
    static std::expected<resolved<Checked>, error> container_resolve(top_level_item source,
                                                                     validity::offset offset,
                                                                     validity::limit_checks<Checked> checks);

    template <bool Checked>
    static std::expected<resolved<Checked>, error> tag_content_resolve(top_level_item source,
                                                                       validity::offset offset,
                                                                       validity::limit_checks<Checked> checks);

    struct tag_content {
        validity::offset offset;
        std::string_view bytes;
    };

    template <bool Checked>
    static std::expected<tag_content, error> typed_array_bytes_read(resolved<Checked> const &found);

    static std::expected<std::string_view, error> typed_array_element_read(lazy const &node);

    template <bool Checked>
    static std::expected<resolved<Checked>, error> resolve(lazy const &node, validity::limit_checks<Checked> checks);

    template <bool Checked, std::integral Index>
    static std::expected<lazy, error> index_select(resolved<Checked> const &found, Index i);

    template <class Key, class Entries = lazy_entries, bool Checked>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<typename Entries::iterator, error> key_find(resolved<Checked> found, Key key,
                                                                     limit_values const &loaded);

    template <class Iterator, class Last, class Key, bool Checked>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<std::pair<Iterator, bool>, error> key_find(Iterator first, Last last, Key key,
                                                                    validity::limit_checks<Checked> checks);

    template <class Iterator>
    static std::expected<lazy, error> value_of(std::expected<Iterator, error> found);

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class jsonpath;
};


struct lazy_elements : std::ranges::view_interface<lazy_elements> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(lazy::top_level_item t, validity::offset const o, std::uint64_t const l,
                 limit_values const &v, std::uint64_t const tag)
            : top_level(std::move(t)), offset(o), left(l), loaded(v), typed_array_tag(tag)
        {
        }

        lazy::top_level_item top_level;
        validity::offset offset{};
        std::uint64_t left{};
        error failure{};
        limit_values loaded{};
        std::uint64_t typed_array_tag{};

        friend struct lazy_elements;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, loaded, typed_array_tag};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_elements(lazy::top_level_item t, validity::offset const o,
                  std::uint64_t const c, limit_values const &v, std::uint64_t const tag)
        : top_level(std::move(t)), offset(o), count(c), loaded(v), typed_array_tag(tag)
    {
    }

    lazy::top_level_item top_level;
    validity::offset offset;
    std::uint64_t count;
    limit_values loaded;
    std::uint64_t typed_array_tag;

    friend struct lazy;
};

struct lazy_entries : std::ranges::view_interface<lazy_entries> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(lazy::top_level_item t, validity::offset const k, std::uint64_t const l,
                 limit_values const &v)
            : top_level(std::move(t)), key(k), value(k), left(l), loaded(v)
        {
            value_find();
        }

        void value_find();

        lazy::top_level_item top_level;
        validity::offset key{};
        validity::offset value{};
        std::uint64_t left{};
        error failure{};
        limit_values loaded{};

        friend struct lazy_entries;

        friend struct lazy;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, loaded};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_entries(lazy::top_level_item t, validity::offset const o, std::uint64_t const c,
                 limit_values const &v)
        : top_level(std::move(t)), offset(o), count(c), loaded(v)
    {
    }

    lazy::top_level_item top_level;
    validity::offset offset;
    std::uint64_t count;
    limit_values loaded;

    friend struct lazy;
};

}

template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_elements> = true;

template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_entries> = true;

namespace cbor
{

template <class First, class Second, bool Checked>
std::expected<bool, error> validity::keys_equivalent(First &first, offset const first_at, Second &second,
                                                     offset const second_at, std::size_t const depth,
                                                     std::size_t const depth_max,
                                                     limit_checks<Checked> const checks)
{
    if (auto const r = check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const encoded_of = []<class Message>(Message &message) -> std::string_view {
        if constexpr (std::same_as<Message, std::string_view const>)
            return message;
        else
            return message.encoded;
    };
    auto const skip = [](heads::decoder<Checked> &d) {
        well_formedness::no_marks none;
        return well_formedness::item_skip(d, none);
    };
    auto const resolve = []<class Message>(Message &message, offset const at) -> offset {
        if constexpr (std::same_as<Message, std::string_view const>)
            return at;
        else
            return heads::self_described_cbor_content_at(message.encoded, at);
    };
    std::string_view const a = encoded_of(first);
    std::string_view const b = encoded_of(second);
    auto const h = heads::raw_head_read(a, resolve(first, first_at), checks);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const k = heads::raw_head_read(b, resolve(second, second_at), checks);
    if (!k) [[unlikely]]
        return std::unexpected(k.error());
    if (error const c = check_definite_length(h->major, h->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (error const c = check_definite_length(k->major, k->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (h->major != k->major || heads::is_bignum(h->major, h->argument)) [[unlikely]] {
        auto const integer_of = [&]<class Message>(Message &message, std::string_view const encoded,
                                                   heads::raw_head const n)
            -> std::expected<std::optional<std::pair<bool, std::uint64_t>>, error> {
            if (n.major == major_type::unsigned_integer || n.major == major_type::negative_integer)
                return std::pair{n.major == major_type::negative_integer, n.argument};
            if (!heads::is_bignum(n.major, n.argument))
                return std::nullopt;
            heads::decoder<Checked> d{subspan(encoded, resolve(message, n.at)), checks};
            auto const read = d.bignum_decode(n.argument);
            if (!read) [[unlikely]]
                return std::unexpected(read.error());
            if (!*read)
                return std::nullopt;
            return std::pair{n.argument == std::to_underlying(rfc8949::tag_number::negative_bignum), **read};
        };
        auto const x_value = integer_of(first, a, *h);
        if (!x_value) [[unlikely]]
            return std::unexpected(x_value.error());
        auto const y_value = integer_of(second, b, *k);
        if (!y_value) [[unlikely]]
            return std::unexpected(y_value.error());
        if (*x_value && *y_value)
            return **x_value == **y_value;
        if (h->major != k->major)
            return false;
    }
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == k->argument;
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder<Checked> d{subspan(a, h->at), checks};
        auto const s = d.byte_string_decode(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        heads::decoder<Checked> e{subspan(b, k->at), checks};
        auto const t = e.byte_string_decode(k->argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
    }
    case major_type::array: {
        if (h->argument != k->argument)
            return false;
        heads::decoder<Checked> d{subspan(a, h->at), checks};
        heads::decoder<Checked> e{subspan(b, k->at), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = keys_equivalent(first, distance(a, d.encoded), second,
                                               distance(b, e.encoded), depth + 1, depth_max, checks);
            if (!equal || !*equal)
                return equal;
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = skip(e); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != k->argument)
            return false;
        auto const pairs_counted = [&]<class From, class In>(From &from, offset const key,
                                                             offset const value, In &in,
                                                             std::string_view const in_encoded,
                                                             offset const in_at)
            -> std::expected<std::uint64_t, error> {
            std::uint64_t count = 0;
            heads::decoder<Checked> e{validity::subspan(in_encoded, in_at), checks};
            for (std::uint64_t j = 0; j < h->argument; ++j) {
                offset const other = distance(in_encoded, e.encoded);
                if (auto const r = skip(e); !r) [[unlikely]]
                    return std::unexpected(r.error());
                offset const other_value = distance(in_encoded, e.encoded);
                if (auto const r = skip(e); !r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const key_same = keys_equivalent(from, key, in, other, depth + 1, depth_max, checks);
                if (!key_same) [[unlikely]]
                    return std::unexpected(key_same.error());
                if (!*key_same)
                    continue;
                auto const value_same =
                    keys_equivalent(from, value, in, other_value, depth + 1, depth_max, checks);
                if (!value_same) [[unlikely]]
                    return std::unexpected(value_same.error());
                count += *value_same;
            }
            return count;
        };
        heads::decoder<Checked> d{validity::subspan(a, h->at), checks};
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            offset const key = distance(a, d.encoded);
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            offset const value = distance(a, d.encoded);
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const own = pairs_counted(first, key, value, first, a, h->at);
            if (!own) [[unlikely]]
                return std::unexpected(own.error());
            auto const other = pairs_counted(first, key, value, second, b, k->at);
            if (!other) [[unlikely]]
                return std::unexpected(other.error());
            if (*own != *other)
                return false;
        }
        return true;
    }
    case major_type::tag:
        if (h->argument != k->argument)
            return false;
        return keys_equivalent(first, h->at, second, k->at, depth + 1, depth_max, checks);
    case major_type::simple_float:
        break;
    }
    constexpr std::uint8_t half = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
    constexpr std::uint8_t twice =
        std::to_underlying(rfc8949::simple_float_information::double_precision_float);
    bool const h_float = h->info >= half && h->info <= twice;
    bool const k_float = k->info >= half && k->info <= twice;
    if (h_float != k_float)
        return false;
    if (!h_float)
        return h->argument == k->argument;
    heads::float_key const p = heads::float_key_of(h->info, h->argument);
    heads::float_key const q = heads::float_key_of(k->info, k->argument);
    if (p.nan || q.nan) {
        constexpr std::uint64_t significand =
            (std::uint64_t{1} << heads::double_precision.significand_bits) - 1u;
        return p.nan && q.nan && (p.widened & significand) == (q.widened & significand);
    }
    return p.value == q.value;
}

template <bool Checked>
struct lazy::resolved {
    lazy::top_level_item source;
    heads::head h;
    heads::decoder<Checked> d;
    validity::offset item_at;
};

template <bool Checked>
std::expected<lazy::resolved<Checked>, error>
lazy::container_resolve(lazy::top_level_item source, validity::offset offset,
                        validity::limit_checks<Checked> const checks)
{
    validity::throw_logic_error_if(source.encoded.data() == nullptr, "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        validity::offset const at = heads::self_described_cbor_content_at(source.encoded, offset);
        heads::decoder<Checked> d{validity::subspan(source.encoded, at), checks};
        auto const h = d.head_decode();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved<Checked>{source, *h, d, at};
        auto const r = d.head_decode();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = validity::check_tag_content(h->argument, r->major).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const embedded = d.byte_string_decode(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = lazy::top_level_item{source.owner, *embedded};
        offset = validity::offset{};
    }
}

template <bool Checked>
std::expected<lazy::resolved<Checked>, error>
lazy::tag_content_resolve(lazy::top_level_item source, validity::offset offset,
                          validity::limit_checks<Checked> const checks)
{
    for (;;) {
        auto found = container_resolve(std::move(source), offset, checks);
        if (!found) [[unlikely]]
            return found;
        if (found->h.major != major_type::tag || !heads::is_tag_passed(found->h.argument)) [[likely]] {
            if (auto const r = validity::check_pending_items(found->h.major, found->h.info, found->h.argument,
                                                             found->d.encoded.size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return found;
        }
        source = std::move(found->source);
        offset = validity::distance(source.encoded, found->d.encoded);
    }
}

template <bool Checked>
std::expected<lazy::tag_content, error> lazy::typed_array_bytes_read(resolved<Checked> const &found)
{
    if (found.h.major != major_type::tag) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    if (error const r = validity::typed_array_check(found.h.argument, 0).error_or(error{}); r != error{})
        [[unlikely]]
        return std::unexpected(r);
    validity::offset const content =
        heads::self_described_cbor_content_at(found.source.encoded, validity::distance(found.source.encoded, found.d.encoded));
    heads::decoder<Checked> d{validity::subspan(found.source.encoded, content), found.d.checks};
    auto const r = d.head_decode();
    if (!r) [[unlikely]]
        return std::unexpected(r.error());
    if (error const c = validity::check_tag_content(found.h.argument, r->major).error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    auto const offset = validity::offset{validity::distance(found.source.encoded, d.encoded)};
    auto const bytes = d.byte_string_decode(r->argument);
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    if (error const c = validity::typed_array_check(found.h.argument, bytes->size()).error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    return tag_content{offset, *bytes};
}

inline lazy_elements::iterator::value_type lazy_elements::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return lazy{top_level, offset, typed_array_tag};
}

inline bool lazy_elements::iterator::operator==(iterator const &other) const
{
    return offset == other.offset && typed_array_tag == other.typed_array_tag && left == other.left &&
           failure == other.failure &&
           top_level.encoded.data() == other.top_level.encoded.data();
}

inline lazy_elements::iterator &lazy_elements::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    if (typed_array_tag != 0) {
        offset = validity::next(offset, validity::typed_array_element_size(typed_array_tag));
        --left;
        return *this;
    }
    return validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) -> iterator & {
            heads::decoder<Checked> d{validity::subspan(top_level.encoded, offset),
                                      checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                --left;
                return *this;
            }
            offset = validity::distance(top_level.encoded, d.encoded);
            --left;
            if (left != 0 && d.encoded.empty()) [[unlikely]]
                failure = error::too_little_data;
            return *this;
        });
}

inline void lazy_entries::iterator::value_find()
{
    if (left == 0)
        return;
    validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            heads::decoder<Checked> d{validity::subspan(top_level.encoded, key), checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                return;
            }
            if (d.encoded.empty()) [[unlikely]] {
                failure = error::too_little_data;
                return;
            }
            value = validity::distance(top_level.encoded, d.encoded);
        });
}

inline lazy_entries::iterator::value_type lazy_entries::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return std::pair{lazy{top_level, key, {}}, lazy{top_level, value, {}}};
}

inline bool lazy_entries::iterator::operator==(iterator const &other) const
{
    return key == other.key && value == other.value && left == other.left && failure == other.failure &&
           top_level.encoded.data() == other.top_level.encoded.data();
}

inline lazy_entries::iterator &lazy_entries::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    validity::limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
            heads::decoder<Checked> d{validity::subspan(top_level.encoded, value), checks};
            if (auto const r = well_formedness::items_skip(d, 1); !r) [[unlikely]] {
                failure = r.error();
                --left;
                return;
            }
            key = validity::distance(top_level.encoded, d.encoded);
            --left;
        });
    if (failure == error{})
        value_find();
    return *this;
}

inline std::expected<lazy, error> lazy::from(std::shared_ptr<void const> owner,
                                             std::string_view const encoded, limit_values const &loaded)
{
    validity::throw_logic_error_if_empty(owner,
                                         "cbor::lazy::from: the owner of the encoded data item is empty");
    if (auto const r = validity::limits_apply(
            loaded,
            [encoded]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) {
                return validity::check_input_bytes(encoded.size(), checks);
            });
        !r) [[unlikely]]
        return std::unexpected(r.error());
    std::string_view const content = heads::self_described_cbor_content(encoded);
    return lazy{
        lazy::top_level_item{std::move(owner), content}, validity::offset{}, {}};
}

template <class F>
decltype(auto) lazy::limits_apply(limit_values const &loaded, F &&f) const
{
    validity::throw_logic_error_if(top_level.encoded.data() == nullptr, "cbor::lazy: the lazy holds no top-level item");
    return validity::limits_apply(loaded, std::forward<F>(f));
}

template <borrowed_bytes Encoded>
std::expected<lazy, error> lazy::view(std::shared_ptr<void const> owner, Encoded const &encoded)
{
    return from(std::move(owner), std::string_view(std::ranges::data(encoded), std::ranges::size(encoded)),
                limits.load());
}

template <owning_bytes Encoded>
std::expected<lazy, error> lazy::view(std::shared_ptr<Encoded const> owner)
{
    validity::throw_logic_error_if_null(owner, "cbor::lazy::view: the encoded data item is null");
    std::string_view const bytes(std::ranges::data(*owner), std::ranges::size(*owner));
    return from(std::move(owner), bytes, limits.load());
}

template <owned_bytes Encoded>
std::expected<lazy, error> lazy::take(Encoded &&encoded)
{
    return view(std::make_shared<Encoded const>(std::move(encoded)));
}

template <class Key, class Entries, bool Checked>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<typename Entries::iterator, error>
lazy::key_find(resolved<Checked> found, Key const key, limit_values const &loaded)
{
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    validity::offset const first = validity::distance(found.source.encoded, found.d.encoded);
    std::uint64_t const readable = std::min<std::uint64_t>(found.h.argument, found.d.encoded.size());
    typename Entries::iterator it(std::move(found.source), first, readable, loaded);
    auto found_at = key_find(std::move(it), std::default_sentinel, key, found.d.checks);
    if (!found_at) [[unlikely]]
        return std::unexpected(found_at.error());
    if (found_at->first == std::default_sentinel)
        if (auto const complete = validity::check_entries_all_read(found.h.argument, readable, found_at->first.failure);
            !complete) [[unlikely]]
            return std::unexpected(complete.error());
    return std::move(found_at->first);
}

template <class Iterator, class Last, class Key, bool Checked>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<std::pair<Iterator, bool>, error>
lazy::key_find(Iterator it, Last const last, Key const key,
                        validity::limit_checks<Checked> const checks)
{
    for (; it != last; ++it) {
        if (it == std::default_sentinel) [[unlikely]] {
            if constexpr (!std::same_as<Last, std::default_sentinel_t>)
                validity::throw_logic_error("cbor::lazy: the last iterator is not a later pair of the same map");
            break;
        }
        if (it.failure != error{}) [[unlikely]]
            return std::unexpected(it.failure);
        validity::offset const key_at = heads::self_described_cbor_content_at(it.top_level.encoded, it.key);
        heads::decoder<Checked> probe{validity::subspan(it.top_level.encoded, key_at),
                                      checks};
        auto const k = probe.head_decode();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        bool equal = false;
        if constexpr (std::same_as<Key, std::string_view>) {
            if (k->major == major_type::text_string) {
                auto const content = probe.byte_string_decode(k->argument);
                if (!content) [[unlikely]]
                    return std::unexpected(content.error());
                equal = *content == key;
            }
        } else {
            equal = (k->major == major_type::unsigned_integer && key >= 0 && k->argument == static_cast<std::uint64_t>(key)) ||
                    (k->major == major_type::negative_integer && key < 0 && k->argument == static_cast<std::uint64_t>(-1 - key));
            if (heads::is_bignum(k->major, k->argument)) [[unlikely]] {
                validity::offset const content =
                    heads::self_described_cbor_content_at(it.top_level.encoded, validity::distance(it.top_level.encoded, probe.encoded));
                heads::decoder<Checked> d{validity::subspan(it.top_level.encoded, content), checks};
                auto const read = d.bignum_decode(k->argument);
                if (!read) [[unlikely]]
                    return std::unexpected(read.error());
                bool const negative = k->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                equal = *read && negative == (key < 0) &&
                        **read == (key < 0 ? static_cast<std::uint64_t>(-1 - key) : static_cast<std::uint64_t>(key));
            }
        }
        if (equal)
            return std::pair{std::move(it), true};
    }
    return std::pair{std::move(it), false};
}

template <class Entries>
std::expected<typename Entries::iterator, error> lazy::find(std::string_view const key,
                                                                         limit_values const &loaded) const
{
    return limits_apply(loaded,
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy_entries::iterator, error> {
                            auto found = resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return key_find(std::move(*found), key, loaded);
                        });
}

template <class Entries>
std::expected<typename Entries::iterator, error> lazy::find(std::int64_t const key,
                                                                         limit_values const &loaded) const
{
    return limits_apply(loaded,
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy_entries::iterator, error> {
                            auto found = resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return key_find(std::move(*found), key, loaded);
                        });
}

inline std::expected<std::string_view, error> lazy::typed_array_element_read(lazy const &node)
{
    validity::throw_logic_error_if(node.top_level.encoded.data() == nullptr, "cbor::lazy: the lazy holds no top-level item");
    return validity::substr(node.top_level.encoded, node.offset,
                            validity::typed_array_element_size(node.typed_array_tag));
}

template <bool Checked>
std::expected<lazy::resolved<Checked>, error>
lazy::resolve(lazy const &node, validity::limit_checks<Checked> const checks)
{
    if (node.typed_array_tag == 0) [[likely]]
        return tag_content_resolve(node.top_level, node.offset, checks);
    auto const element = typed_array_element_read(node);
    if (!element) [[unlikely]]
        return std::unexpected(element.error());
    auto const h = heads::typed_array_element_decode(node.typed_array_tag, *element, validity::index{});
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return resolved<Checked>{node.top_level, *h, heads::decoder<Checked>{std::string_view{}, checks}, node.offset};
}

template <bool Checked, std::integral Index>
std::expected<lazy, error> lazy::index_select(resolved<Checked> const &found, Index const i)
{
    if (heads::is_typed_array(found.h)) {
        auto const bytes = typed_array_bytes_read(found);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        std::size_t const size = validity::typed_array_element_size(found.h.argument);
        auto const position = validity::check_index(i, bytes->bytes.size() / size);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        return lazy{found.source, validity::element_offset(bytes->offset, size, *position), found.h.argument};
    }
    if (found.h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    auto const position = validity::check_index(i, found.h.argument);
    if (!position) [[unlikely]]
        return std::unexpected(position.error());
    heads::decoder<Checked> d = found.d;
    if (auto const r = well_formedness::items_skip(d, std::to_underlying(*position)); !r) [[unlikely]]
        return std::unexpected(r.error());
    validity::offset const element = validity::distance(found.source.encoded, d.encoded);
    if (auto const target = heads::decoder<Checked>(d).head_decode(); !target) [[unlikely]]
        return std::unexpected(target.error());
    return lazy{found.source, element, {}};
}

inline std::expected<std::uint64_t, error> lazy::size() const
{
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<std::uint64_t, error> {
                            auto const found = resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            if (heads::is_typed_array(found->h))
                                return typed_array_bytes_read(*found).transform(
                                    [&](tag_content const &bytes) -> std::uint64_t {
                                        return bytes.bytes.size() / validity::typed_array_element_size(found->h.argument);
                                    });
                            auto const &[source, h, d, item_at] = *found;
                            if (h.major != major_type::array && h.major != major_type::map) [[unlikely]]
                                return std::unexpected(error::not_indexable);
                            auto const items = validity::checked_mul(
                                h.argument, h.major == major_type::map ? rfc8949::data_items_per_pair : 1);
                            if (!items) [[unlikely]]
                                return std::unexpected(error::too_little_data);
                            if (auto const r = validity::check_pending_items(*items, d.encoded.size()); !r)
                                [[unlikely]]
                                return std::unexpected(r.error());
                            return h.argument;
                        });
}

inline std::expected<bool, error> lazy::empty() const
{
    return size().transform([](std::uint64_t const n) { return n == 0; });
}

inline std::expected<cbor::type, error> lazy::type() const
{
    validity::throw_logic_error_if(top_level.encoded.data() == nullptr, "cbor::lazy::type: the lazy holds no top-level item");
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<cbor::type, error> {
                            if (typed_array_tag != 0) [[unlikely]] {
                                auto const found = resolve(*this, checks);
                                if (!found) [[unlikely]]
                                    return std::unexpected(found.error());
                                return heads::type_of(found->h.major, found->h.info, found->h.argument);
                            }
                            validity::offset const at = heads::self_described_cbor_content_at(top_level.encoded, offset);
                            auto const h = heads::raw_head_read(top_level.encoded, at, checks);
                            if (!h) [[unlikely]]
                                return std::unexpected(h.error());
                            if (!heads::is_bignum(h->major, h->argument)) [[likely]]
                                return heads::type_of(h->major, h->info, h->argument);
                            validity::offset const content = heads::self_described_cbor_content_at(top_level.encoded, h->at);
                            auto const c = heads::raw_head_read(top_level.encoded, content, checks);
                            if (!c) [[unlikely]]
                                return std::unexpected(c.error());
                            if (c->major != major_type::byte_string ||
                                !validity::check_definite_length(c->major, c->info).has_value())
                                return heads::type_of(h->major, h->info, h->argument);
                            heads::decoder<Checked> d{validity::subspan(top_level.encoded, content), checks};
                            auto const read = d.bignum_decode(h->argument);
                            if (!read) [[unlikely]]
                                return std::unexpected(read.error());
                            if (!*read)
                                return heads::type_of(h->major, h->info, h->argument);
                            return h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                       ? cbor::type::negative_integer
                                       : cbor::type::unsigned_integer;
                        });
}

template <class Iterator>
std::expected<lazy, error> lazy::value_of(std::expected<Iterator, error> found)
{
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (*found == std::default_sentinel) [[unlikely]]
        return std::unexpected(error::key_not_found);
    return lazy{std::move(found->top_level), found->value, {}};
}

inline std::expected<lazy, error> lazy::at(std::string_view const key) const
{
    return value_of(find(key, limits.load()));
}

inline std::expected<lazy, error> lazy::at(key const k) const
{
    if (k.text != nullptr)
        return at(std::string_view(k.text));
    return value_of(find(k.number, limits.load()));
}

inline std::expected<lazy, error> lazy::at(std::size_t const index) const
{
    return limits_apply(limits.load(),
                        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                            -> std::expected<lazy, error> {
                            auto const found = resolve(*this, checks);
                            if (!found) [[unlikely]]
                                return std::unexpected(found.error());
                            return index_select(*found, index);
                        });
}

template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
                 std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
#ifdef __STDCPP_FLOAT128_T__
             || std::is_same_v<T, std::float128_t>
#endif
std::expected<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                               std::is_same_v<T, typed_array>,
                           ref<T>, T>, error> lazy::get() const
{
    return get<T>(limits.load());
}

template <class T>
auto lazy::get(limit_values const &loaded) const
{
    using result = std::expected<std::conditional_t<std::is_same_v<T, std::string_view> ||
                                                        std::is_same_v<T, std::span<std::byte const>> ||
                                                        std::is_same_v<T, typed_array>,
                                                    ref<T>, T>,
                                 error>;
    return limits_apply(
        loaded, [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks) -> result {
            if constexpr (std::is_same_v<T, std::span<std::byte const>>) {
                if (typed_array_tag != 0) {
                    auto const element = typed_array_element_read(*this);
                    if (!element) [[unlikely]]
                        return std::unexpected(element.error());
                    return ref<T>(top_level.owner, std::as_bytes(std::span(*element)));
                }
            }
#ifdef __STDCPP_FLOAT128_T__
            if constexpr (std::is_same_v<T, std::float128_t>) {
                if (typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_big_endian) &&
                    typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_little_endian)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const element = typed_array_element_read(*this);
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                std::array<std::byte, sizeof(std::float128_t)> bits{};
                std::ranges::copy(std::as_bytes(std::span(*element)), bits.begin());
                bool const little_endian =
                    typed_array_tag == std::to_underlying(rfc8746::tag_number::float128_little_endian);
                if (little_endian != (std::endian::native == std::endian::little))
                    std::ranges::reverse(bits);
                return std::bit_cast<std::float128_t>(bits);
            } else {
#endif
            auto const found = resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            auto [source, h, d, item_at] = *found;
            if constexpr (std::integral<T> && !std::is_same_v<T, bool>) {
                bool negative = h.major == major_type::negative_integer;
                std::uint64_t argument = h.argument;
                if (heads::is_bignum(h.major, h.argument)) {
                    negative = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
                    validity::offset const content =
                        heads::self_described_cbor_content_at(source.encoded, validity::distance(source.encoded, d.encoded));
                    d = heads::decoder<Checked>{
                        validity::subspan(source.encoded, content), checks};
                    auto const read = d.bignum_decode(h.argument);
                    if (!read) [[unlikely]]
                        return std::unexpected(read.error());
                    if (!*read) [[unlikely]]
                        return std::unexpected(error::number_out_of_range);
                    argument = **read;
                } else if (h.major != major_type::unsigned_integer && !negative) [[unlikely]] {
                    return std::unexpected(error::incorrect_type);
                }
                if (error const c = validity::check_number_range<T>(negative, argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                T const magnitude = static_cast<T>(argument);
                return static_cast<T>(negative ? ~magnitude : magnitude);
            } else if constexpr (std::is_same_v<T, double>) {
                if (heads::is_bignum(h.major, h.argument)) [[unlikely]] {
                    validity::offset const content =
                        heads::self_described_cbor_content_at(source.encoded, validity::distance(source.encoded, d.encoded));
                    d = heads::decoder<Checked>{
                        validity::subspan(source.encoded, content), checks};
                    auto const read = d.bignum_decode(h.argument);
                    if (!read) [[unlikely]]
                        return std::unexpected(read.error());
                    if (!*read) [[unlikely]]
                        return std::unexpected(error::number_out_of_range);
                    h.major = h.argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                  ? major_type::negative_integer
                                  : major_type::unsigned_integer;
                    h.argument = **read;
                }
                if (h.major == major_type::unsigned_integer || h.major == major_type::negative_integer) {
                    bool const negative = h.major == major_type::negative_integer;
                    if (error const c = validity::check_number_range<double>(negative, h.argument).error_or(error{});
                        c != error{}) [[unlikely]]
                        return std::unexpected(c);
                    return negative ? -1.0 - static_cast<double>(h.argument) : static_cast<double>(h.argument);
                }
                if (h.major != major_type::simple_float) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                switch (static_cast<rfc8949::simple_float_information>(h.info)) {
                case rfc8949::simple_float_information::half_precision_float:
                case rfc8949::simple_float_information::single_precision_float:
                case rfc8949::simple_float_information::double_precision_float:
                    return heads::float_decode(h.info, h.argument);
                [[unlikely]] default:
                    return std::unexpected(error::incorrect_type);
                }
            } else if constexpr (std::is_same_v<T, bool>) {
                if (!heads::is_boolean(h)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                return h.info == std::to_underlying(simple_value::true_value);
            } else if constexpr (std::is_same_v<T, simple_value>) {
                if (!heads::is_simple_value(h)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                if (error const c = validity::check_simple_value(h.info, h.argument).error_or(error{});
                    c != error{}) [[unlikely]]
                    return std::unexpected(c);
                return static_cast<simple_value>(h.argument);
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
                return ref<T>(source.owner, *text);
            } else if constexpr (std::is_same_v<T, typed_array>) {
                auto const bytes = typed_array_bytes_read(*found);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return ref<T>(source.owner, typed_array{h.argument, std::as_bytes(std::span(bytes->bytes))});
            } else {
                if (h.major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const bytes = d.byte_string_decode(h.argument);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return ref<T>(source.owner, std::as_bytes(std::span(*bytes)));
            }
#ifdef __STDCPP_FLOAT128_T__
            }
#endif
        });
}

inline std::expected<lazy_elements, error> lazy::elements() const
{
    return elements(limits.load());
}

inline std::expected<lazy_elements, error> lazy::elements(limit_values const &loaded) const
{
    return limits_apply(
        loaded,
        [&]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
            -> std::expected<lazy_elements, error> {
            auto const found = resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            if (heads::is_typed_array(found->h)) {
                auto const bytes = typed_array_bytes_read(*found);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return lazy_elements(found->source, bytes->offset,
                                     bytes->bytes.size() / validity::typed_array_element_size(found->h.argument), loaded,
                                     found->h.argument);
            }
            auto const &[source, h, d, item_at] = *found;
            if (h.major != major_type::array) [[unlikely]]
                return std::unexpected(error::not_indexable);
            if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = validity::check_pending_items(h.argument, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return lazy_elements(source, validity::distance(source.encoded, d.encoded), h.argument, loaded, {});
        });
}

inline std::expected<lazy_entries, error> lazy::entries() const
{
    return entries(limits.load());
}

inline std::expected<lazy_entries, error> lazy::entries(limit_values const &loaded) const
{
    return limits_apply(
        loaded,
        [&]<bool Checked>(
            std::size_t, validity::limit_checks<Checked> const checks) -> std::expected<lazy_entries, error> {
            auto const found = resolve(*this, checks);
            if (!found) [[unlikely]]
                return std::unexpected(found.error());
            auto const &[source, h, d, item_at] = *found;
            if (h.major != major_type::map) [[unlikely]]
                return std::unexpected(error::not_indexable);
            if (auto const r = validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
                return std::unexpected(r.error());
            auto const items = validity::checked_mul(h.argument, rfc8949::data_items_per_pair);
            if (!items) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (auto const r = validity::check_pending_items(*items, d.encoded.size()); !r) [[unlikely]]
                return std::unexpected(r.error());
            return lazy_entries(source, validity::distance(source.encoded, d.encoded), h.argument, loaded);
        });
}

inline std::expected<std::shared_ptr<item const>, error> lazy::decode() const
{
    validity::throw_logic_error_if(top_level.encoded.data() == nullptr, "cbor::lazy::decode: the lazy holds no top-level item");
    if (typed_array_tag != 0) [[unlikely]] {
        auto const h = validity::limits_apply(
            limits.load(),
            [this]<bool Checked>(std::size_t, validity::limit_checks<Checked> const checks)
                -> std::expected<heads::head, error> {
                return resolve(*this, checks).transform(
                    [](resolved<Checked> const &found) { return found.h; });
            });
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::string encoded;
        heads::head_append(encoded, h->major, h->info, h->argument);
        return cbor::decode(encoded);
    }
    if (offset == validity::offset{})
        return validity::limits_apply(
            limits.load(),
            [this]<bool Checked>(std::size_t const depth_max, validity::limit_checks<Checked> const checks)
                -> std::expected<std::shared_ptr<item const>, error> {
                return tree_decode(top_level.encoded, checks, depth_max)
                    .transform([](decoded_tree<Checked> const &d) { return d.tree; });
            });
    std::string_view const rest = validity::subspan(top_level.encoded, offset);
    auto const size = item_size(rest);
    if (!size) [[unlikely]]
        return std::unexpected(size.error());
    return cbor::decode(rest.substr(0, *size));
}

}

#include "query.hpp"

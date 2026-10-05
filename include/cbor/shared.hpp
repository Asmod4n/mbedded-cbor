#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"
#include "head.hpp"

namespace cbor
{

struct lazy;

template <std::size_t DepthMax>
struct lazy_elements;

template <std::size_t DepthMax>
struct lazy_entries;

class value_sharing
{
    struct sharing_decoder : heads::decoder {
        std::string_view message;
        std::vector<std::size_t> marks;
        std::size_t high_water_mark;

        void mark(heads::decoder const &at)
        {
            std::size_t const offset = message.size() - at.encoded.size();
            if (offset > high_water_mark) {
                marks.push_back(offset);
                high_water_mark = offset;
            }
        }

        std::expected<std::size_t, error> reference_follow(std::size_t const tag_at)
        {
            auto const n = head_decode();
            if (!n) [[unlikely]]
                return std::unexpected(n.error());
            if (n->major != major_type::unsigned_integer) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            if (n->argument >= marks.size()) [[unlikely]]
                return std::unexpected(error::sharedref_index_not_marked);
            std::size_t const marked = marks.at(static_cast<std::size_t>(n->argument));
            if (marked >= tag_at) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            return marked;
        }
    };

    struct document {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::vector<std::size_t> marks;
        std::size_t high_water_mark;

        void mark(heads::decoder const &d)
        {
            std::size_t const offset = encoded.size() - d.encoded.size();
            if (offset > high_water_mark) {
                marks.push_back(offset);
                high_water_mark = offset;
            }
        }
    };

    template <std::size_t DepthMax>
    static std::expected<std::size_t, error> shared_resolve(document const &doc, std::size_t at);

    struct resolved {
        std::shared_ptr<document> source;
        heads::head h;
        heads::decoder d;
    };

    static std::expected<resolved, error> container_resolve(std::shared_ptr<document> source, std::size_t offset)
    {
        std::vector<std::size_t> followed;
        for (;;) {
            heads::decoder d{source->encoded.substr(offset)};
            auto const h = d.head_decode();
            if (!h) [[unlikely]]
                return std::unexpected(h.error());
            if (h->major != major_type::tag)
                return resolved{source, *h, d};
            if (h->argument == std::to_underlying(heads::tag_number::shareable)) {
                source->mark(d);
                offset = source->encoded.size() - d.encoded.size();
                continue;
            }
            if (h->argument == std::to_underlying(heads::tag_number::encoded_cbor_data_item)) {
                auto const r = d.head_decode();
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (r->major != major_type::byte_string) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
                auto const embedded = d.byte_string_decode(r->argument);
                if (!embedded) [[unlikely]]
                    return std::unexpected(embedded.error());
                source = std::make_shared<document>(source->owner, *embedded, std::vector<std::size_t>{}, 0);
                offset = 0;
                followed.clear();
                continue;
            }
            if (h->argument != std::to_underlying(heads::tag_number::sharedref))
                return resolved{source, *h, d};
            if (std::ranges::find(followed, offset) != followed.end()) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            followed.push_back(offset);
            auto const r = d.head_decode();
            if (!r) [[unlikely]]
                return std::unexpected(r.error());
            if (r->major != major_type::unsigned_integer) [[unlikely]]
                return std::unexpected(error::inadmissible_type_for_tag_content);
            std::vector<std::size_t> const &offsets = source->marks;
            if (r->argument >= offsets.size()) [[unlikely]]
                return std::unexpected(error::sharedref_index_not_marked);
            std::size_t const marked = offsets.at(static_cast<std::size_t>(r->argument));
            if (marked >= offset) [[unlikely]]
                return std::unexpected(error::sharedref_not_complete);
            offset = marked;
        }
    }

    friend struct lazy;

    template <std::size_t>
    friend struct lazy_elements;

    template <std::size_t>
    friend struct lazy_entries;

    template <std::size_t DepthMax>
    friend std::expected<lazy, error> decode(std::shared_ptr<std::string const> const &encoded);

    friend class jsonpath;

#ifdef __cpp_impl_reflection
    friend class generic;

    template <class>
    friend class databind;
#endif
};

template <std::size_t DepthMax>
std::expected<std::size_t, error> value_sharing::shared_resolve(document const &doc, std::size_t at)
{
    for (;;) {
        auto const h = heads::raw_head_read(doc.encoded, at);
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag || h->info == std::to_underlying(heads::additional_information::indefinite_length))
            return at;
        if (h->argument == std::to_underlying(heads::tag_number::shareable)) {
            at = h->at;
            continue;
        }
        if (h->argument != std::to_underlying(heads::tag_number::sharedref))
            return at;
        auto const n = heads::raw_head_read(doc.encoded, h->at);
        if (!n) [[unlikely]]
            return std::unexpected(n.error());
        if (n->major != major_type::unsigned_integer) [[unlikely]]
            return std::unexpected(error::inadmissible_type_for_tag_content);
        if (n->argument >= doc.marks.size()) [[unlikely]]
            return std::unexpected(error::sharedref_index_not_marked);
        std::size_t const marked = doc.marks.at(static_cast<std::size_t>(n->argument));
        if (marked >= at) [[unlikely]]
            return std::unexpected(error::sharedref_not_complete);
        at = marked;
    }
}

}

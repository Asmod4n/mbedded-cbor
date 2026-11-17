/**
 * @file
 * Writes a tree of cbor::item as CBOR into a span, a byte container or a writer, with cbor::encode, and into a list
 * of `struct iovec` pieces, with cbor::encode_iov.
 * https://www.rfc-editor.org/rfc/rfc8949#section-3
 * https://www.rfc-editor.org/rfc/rfc8746#section-2
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <memory>
#include <limits>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if __has_include(<sys/uio.h>)
#include <sys/uio.h>
#endif

#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "item.hpp"
#include "ref.hpp"

namespace cbor
{

template <class T>
class schema;

#if __has_include(<sys/uio.h>)
/**
 * @brief Writes the tree at `root` into a list of `struct iovec` pieces, not into one buffer.
 *
 * The result gives the pieces in order and holds their storage and the owners of the pieces that refer to strings.
 * The pieces are for sending only: `writev`, `sendmsg`, `IORING_OP_WRITEV` and `IORING_OP_SENDMSG`.
 * The program holds the `ref` until the call returns or until the last completion of the request.
 * The bytes are those of cbor::encode().
 * The function exists where `<sys/uio.h>` exists.
 * The errors are those of cbor::encode().
 * A piece that refers to the string of an owner points into memory that the owner holds as const; a call that writes
 * into the pieces, such as `readv`, is a wrong use.
 *
 * @see cbor::encode()
 * @see cbor::encode_iov(owning<item> const &)
 *
 * @ingroup decode
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);

/**
 * @brief Writes the tree at `root` with tags 28 and 29 for each cycle into a list of `struct iovec` pieces.
 *
 * The bytes are those of cbor::encode_cycles(), and its errors.
 *
 * @see cbor::encode()
 *
 * @ingroup decode
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov_cycles(item const &root);

/**
 * @brief Writes the tree that `root` owns into a list of `struct iovec` pieces, and refers to its long strings.
 *
 * A byte or text string longer than `CBOR_ENCODER_COPYBREAK` bytes is a piece that points into the tree; a shorter
 * one is copied into a block. The result holds the whole tree alive. The bytes and the errors are those of
 * cbor::encode_iov(item const &).
 *
 * @code{.output}
 * bytes: 82 01 61 61
 * call: auto const pieces = cbor::encode_iov(*cbor::decode(bytes))
 * (*pieces)->size(): 1
 * sent with writev: [1, "a"]
 * @endcode
 *
 * @snippet doc_reference_item.cpp encode_iov
 * @see cbor::encode_iov(item const &)
 *
 * @ingroup decode
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov(owning<item> const &root);

/**
 * @brief Writes the tree that `root` owns with tags 28 and 29 for each cycle, and refers to its long strings.
 *
 * The bytes are those of cbor::encode_cycles(), and its errors. The pieces are those of
 * cbor::encode_iov(owning<item> const &).
 *
 * @ingroup decode
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov_cycles(owning<item> const &root);
#endif

namespace detail
{

class encoding
{
public:
    static constexpr std::size_t block_size = CBOR_ENCODER_BLOCK_SIZE;
    static constexpr std::size_t item_size_max = detail::heads::initial_byte_size + sizeof(std::uint64_t);
    static constexpr std::size_t copybreak = CBOR_ENCODER_COPYBREAK;
    static_assert(block_size >= item_size_max, "A block must hold one whole head or float.");
    static constexpr std::size_t inline_buffer_size = CBOR_ENCODER_INLINE_BUFFER_SIZE;
    static_assert(inline_buffer_size >= item_size_max, "The inline buffer must hold one whole head or float.");

private:
    template <class Writer>
    struct binary_writer;

#if __has_include(<sys/uio.h>)
    class iov_writer;
#endif

    template <class C>
    struct container_message {
        C &container;

        template <class E>
            requires std::same_as<E, char> || std::same_as<E, std::byte>
        std::expected<void, std::errc> append(std::span<E const> const part)
        {
            std::size_t const at = std::ranges::size(container);
            container.resize(at + part.size());
            std::ranges::copy(std::as_bytes(part), std::as_writable_bytes(std::span(container).subspan(at)).begin());
            return {};
        }

        std::expected<void, std::errc> append(std::string_view const part)
        {
            return append(std::span(part));
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }
    };

    template <class B>
    struct span_message {
        std::span<B> out;
        std::size_t used;

        template <class E>
            requires std::same_as<E, char> || std::same_as<E, std::byte>
        std::expected<void, std::errc> append(std::span<E const> const part)
        {
            if (auto const fits = detail::validity::check_space_left(part.size(), out.size() - used); !fits)
                [[unlikely]]
                return fits;
            std::ranges::copy(std::as_bytes(part), std::as_writable_bytes(out.subspan(used).first(part.size())).begin());
            used += part.size();
            return {};
        }

        std::expected<void, std::errc> append(std::string_view const part)
        {
            return append(std::span(part));
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }
    };

    template <class C>
    static constexpr bool byte_container = requires(C &c) {
        c.resize(std::size_t{});
        requires sizeof(std::ranges::range_value_t<C>) == 1;
        requires std::ranges::contiguous_range<C>;
    };

public:
    template <class Target>
    static constexpr bool is_target_v =
        requires(Target &target) {
            std::span(target);
            requires std::same_as<typename Target::element_type, char> ||
                         std::same_as<typename Target::element_type, std::byte>;
            requires !requires { target.resize(std::size_t{}); };
        } || byte_container<Target> ||
        requires(Target &target, std::size_t const hint) { target.allocate(hint).done(hint); };

    template <bool Cycles, class Target>
    static std::expected<std::size_t, error> encode_into(Target &&target, item const &root);

private:
    template <class Target>
        requires is_target_v<std::remove_reference_t<Target>>
    static decltype(auto) message_of(Target &&target, std::size_t const hint)
    {
        using U = std::remove_cvref_t<Target>;
        if constexpr (requires { typename U::element_type; } && requires { std::span(target); } &&
                      !requires { target.resize(std::size_t{}); })
            return span_message<typename U::element_type>{target, 0};
        else if constexpr (byte_container<U>)
            return container_message<U>{target};
        else
            return target.allocate(hint);
    }

    struct head_parts {
        major_type major;
        std::uint8_t info;
        std::uint64_t argument;
    };

    static head_parts head_of(item const &node);
    static std::span<item const> children_of(item const &node, major_type major);
    static item const *content_of(item const &node, major_type major);
    template <bool AppendRef, class Writer>
    static std::expected<void, error> encode_item(binary_writer<Writer> &out, item const &node);
    template <bool AppendRef, class Writer>
    static std::expected<void, std::errc> encode_head_and_payload(binary_writer<Writer> &out, item const &node,
                                                                  head_parts const &head)
        requires(!AppendRef || binary_writer<Writer>::blocks);
    template <class Writer>
    static std::expected<void, std::errc> encode_sharedref(binary_writer<Writer> &out, std::uint64_t index);
    static std::uint64_t size_of(item const &node, head_parts const &head);
    static std::uint64_t reference_size_of(item const &reference);

    class depth_first_search;

    template <bool Cycles, bool AppendRef, class Writer>
    static std::expected<void, error> encode_tree(binary_writer<Writer> &out, item const &root);

#if __has_include(<sys/uio.h>)
    template <bool Cycles>
    static std::expected<ref<std::span<iovec const>>, error> encode_iov_of(item const &root);
    template <bool Cycles>
    static std::expected<ref<std::span<iovec const>>, error> encode_iov_of(owning<item> const &root);
#endif

#if __has_include(<sys/uio.h>)
    friend std::expected<ref<std::span<iovec const>>, error> cbor::encode_iov(item const &root);
    friend std::expected<ref<std::span<iovec const>>, error> cbor::encode_iov_cycles(item const &root);
    friend std::expected<ref<std::span<iovec const>>, error> cbor::encode_iov(owning<item> const &root);
    friend std::expected<ref<std::span<iovec const>>, error> cbor::encode_iov_cycles(owning<item> const &root);
#endif

    template <class>
    friend class cbor::structs;

    template <class>
    friend class cbor::schema;
};

}

/**
 * @brief Writes the tree at `root` and gives the count of bytes written.
 *
 * Each integer, length and tag number keeps the width of its head, from `additional_information`.
 * A float keeps its width.
 * A tree without a `shared_reference` is written in one walk, and the call writes no tag 28 or 29 of its own.
 * A tag 28 and a tag 29 of a tree of cbor::lazy::to_item() are written as they are.
 * A `shared_reference` stands for the item it names.
 * A walk before the write finds each item that a depth-first walk reaches again while it is inside that item.
 * Such an item gets a tag 28, and each edge that reaches it again is a tag 29 with the index of that mark, in its
 * shortest head.
 * A value that two references reach without a cycle is written in full each time.
 * `out` is one of these:
 * - a container of bytes, for example `std::string`: the call appends;
 * - a `std::span` of bytes: the call writes into it and allocates nothing;
 * - an object with `allocate(std::size_t)`, which gives a writer with `append(std::string_view)` and
 *   `done(std::size_t)`.
 *
 * @code{.example}
 * message: [1, "a"]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, "a"]
 * call: std::string out;
 * result: ""
 * call: cbor::encode(out, **tree)
 * result: 4
 * call: out
 * result: 4 bytes: 82 01 61 61
 * call: bytes
 * result: 4 bytes: 82 01 61 61
 * @endcode
 *
 * @code{.example}
 * bytes: d8 1c 81 d8 1d 00
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of 28([29(0)])
 * call: std::string out;
 * result: ""
 * call: cbor::encode(out, **tree)
 * result: 6
 * call: out
 * result: 6 bytes: d8 1c 81 d8 1d 00
 * @endcode
 *
 * @retval no_buffer_space the span is too short
 * @retval nesting_depth_exceeded the tree, or the path through its references, is deeper than the limit
 * @retval amplification_limit_breach the output would be larger than `cbor::limits::amplification_activation_threshold`
 * and than `cbor::limits::maximum_amplification` times the tree; nothing is written
 * @retval value_too_large, not_enough_memory, io_error the error of a writer, from its `std::errc`
 * @snippet doc_reference_item.cpp encode
 * @see cbor::encode_iov()
 * @see cbor::decode()
 *
 * @ingroup decode
 */
template <class Target>
    requires detail::encoding::is_target_v<std::remove_reference_t<Target>>
std::expected<std::size_t, error> encode(Target &&out, item const &root);

/**
 * @brief Writes the tree at `root` with tags 28 and 29 for each cycle and gives the count of bytes written.
 *
 * A `shared_reference` stands for the item it names.
 * A walk before the write finds each item that a depth-first walk reaches again while it is inside that item.
 * Such an item gets a tag 28, and each edge that reaches it again is a tag 29 with the index of that mark, in its
 * shortest head.
 * A value that two references reach without a cycle is written in full each time.
 * Every other item is written as cbor::encode() writes it.
 *
 * @code{.example}
 * bytes: d8 1c 81 d8 1d 00
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of 28([29(0)])
 * call: std::string out;
 * result: ""
 * call: cbor::encode_cycles(out, **tree)
 * result: 6
 * call: out
 * result: 6 bytes: d8 1c 81 d8 1d 00
 * @endcode
 *
 * @retval no_buffer_space the span is too short
 * @retval nesting_depth_exceeded the tree, or the path through its references, is deeper than the limit
 * @retval amplification_limit_breach the output would be larger than `cbor::limits::amplification_activation_threshold`
 * and than `cbor::limits::maximum_amplification` times the tree; nothing is written
 * @retval value_too_large, not_enough_memory, io_error the error of a writer, from its `std::errc`
 * @see cbor::encode()
 *
 * @ingroup decode
 */
template <class Target>
    requires detail::encoding::is_target_v<std::remove_reference_t<Target>>
std::expected<std::size_t, error> encode_cycles(Target &&out, item const &root);

template <class Writer>
struct detail::encoding::binary_writer {
    static constexpr bool direct = std::same_as<Writer, detail::encoding::span_message<char>>;
#if __has_include(<sys/uio.h>)
    static constexpr bool blocks = std::same_as<Writer, detail::encoding::iov_writer>;
#else
    static constexpr bool blocks = false;
#endif

    Writer &writer;
    std::conditional_t<direct || blocks, std::span<char>, std::array<char, inline_buffer_size>> block;
    std::size_t used = 0;
    std::size_t closed = 0;
    std::size_t written = 0;

    explicit binary_writer(Writer &w) : writer(w)
    {
        if constexpr (direct) {
            block = w.out;
            used = w.used;
        } else if constexpr (blocks) {
            take_block(item_size_max);
        }
    }

    void take_block(std::size_t const needed)
        requires blocks
    {
        block = writer.preallocate(std::max(needed, block_size));
        used = 0;
        closed = 0;
    }

    std::expected<void, std::errc> refill(std::size_t const needed)
    {
        if constexpr (blocks) {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            take_block(needed);
            return {};
        } else {
            return flush();
        }
    }

    std::expected<void, std::errc> flush()
    {
        if constexpr (direct) {
            written += used - writer.used;
            writer.used = used;
            return {};
        } else if constexpr (blocks) {
            std::size_t const size = used - closed;
            std::span<char> const part = block.subspan(closed).first(size);
            closed = used;
            written += size;
            writer.postallocate(part);
            return {};
        } else {
            std::size_t const size = used;
            used = 0;
            written += size;
            return writer.append(std::string_view(std::span(block).first(size)));
        }
    }

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_head(major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        if constexpr (!direct) {
            if (!detail::validity::bytes_fit(detail::heads::head_size_max, block.size() - used)) [[unlikely]]
                if (auto const r = refill(item_size_max); !r) [[unlikely]]
                    return r;
            std::array<char, detail::heads::head_size_max> head;
            std::size_t const size = detail::heads::head_encode(head, major, info, argument);
            std::ranges::copy(head, std::span(block).subspan(used).begin());
            used += size;
            return {};
        } else {
            std::span<char> const out = block;
            std::size_t const at = used;
            std::size_t const size = detail::heads::initial_byte_size + detail::heads::argument_size(info);
            if (!detail::validity::bytes_fit(size, out.size() - at)) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
            used = at + detail::heads::write_head(out.first(at + size), at, major, info, argument);
            return {};
        }
    }

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_head(major_type const major, std::uint64_t const argument)
    {
        if (!direct && detail::validity::bytes_fit(detail::heads::head_size_max, block.size() - used)) [[likely]] {
            std::span<char> const out = block;
            used += detail::heads::head_encode(out.subspan(used).template first<detail::heads::head_size_max>(), major,
                                       detail::heads::preferred_argument_info(argument), argument);
            return {};
        }
        return encode_head(major, detail::heads::preferred_argument_info(argument), argument);
    }

    template <class E>
    static void copy_into(std::span<E const> const from, std::span<char> const to)
    {
        std::ranges::copy(std::as_bytes(from), std::as_writable_bytes(to.first(from.size())).begin());
    }

    template <class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_payload(std::span<E const> const bytes)
    {
        std::span<char> const out = block;
        std::size_t const at = used;
        if (bytes.size() <= out.size() - at) [[likely]] {
            copy_into(bytes, out.subspan(at));
            used = at + bytes.size();
            return {};
        }
        if constexpr (direct) {
            return std::unexpected(std::errc::no_buffer_space);
        } else if constexpr (blocks) {
            std::size_t const first = out.size() - at;
            copy_into(bytes.first(first), out.subspan(at));
            used = out.size();
            std::span<E const> const rest = bytes.subspan(first);
            if (auto const r = refill(rest.size()); !r) [[unlikely]]
                return r;
            copy_into(rest, block);
            used = rest.size();
            return {};
        } else if constexpr (std::same_as<E, char>) {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            written += bytes.size();
            return writer.append(std::string_view(bytes));
        } else if constexpr (requires { writer.append(bytes); }) {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            written += bytes.size();
            return writer.append(bytes);
        } else {
            std::span<E const> rest = bytes;
            while (!rest.empty()) {
                if (auto const r = flush(); !r) [[unlikely]]
                    return r;
                std::size_t const part = std::min(rest.size(), block.size());
                copy_into(rest.first(part), block);
                used = part;
                rest = rest.subspan(part);
            }
            return {};
        }
    }

    std::expected<void, std::errc> append_ref(std::span<std::byte const> const piece)
        requires blocks
    {
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        written += piece.size();
        writer.append_ref(piece);
        return {};
    }

    auto release()
        requires blocks
    {
        std::ignore = flush();
        block = {};
        used = 0;
        closed = 0;
        return writer.release();
    }
};

#if __has_include(<sys/uio.h>)
class detail::encoding::iov_writer
{
    struct storage {
        std::vector<std::vector<char>> blocks;
        std::vector<std::shared_ptr<void const>> owners;
        std::vector<iovec> pieces;
    };

    std::shared_ptr<storage> kept = std::make_shared<storage>();

    std::span<char> preallocate(std::size_t const size)
    {
        kept->blocks.emplace_back(size);
        return std::span(kept->blocks.back());
    }

    void append_ref(std::span<std::byte const> const piece)
    {
        iovec v{};
        v.iov_base = const_cast<std::byte *>(piece.data());
        v.iov_len = piece.size();
        kept->pieces.push_back(v);
    }

    void postallocate(std::span<char> const part)
    {
        if (part.empty())
            return;
        append_ref(std::as_bytes(part));
    }

    void push_owner(std::shared_ptr<void const> owner)
    {
        kept->owners.push_back(std::move(owner));
    }

    ref<std::span<iovec const>> release()
    {
        std::span<iovec const> const pieces = kept->pieces;
        return {std::move(kept), pieces};
    }

    friend class detail::encoding;
};
#endif

inline detail::encoding::head_parts detail::encoding::head_of(item const &node)
{
    head_parts head{detail::heads::major_type_of(node.m_type), node.additional_information, node.via.u64};
    switch (node.m_type) {
    case cbor::type::byte_string:
        head.argument = node.via.bin.size();
        break;
    case cbor::type::text_string:
        head.argument = node.via.str.size();
        break;
    case cbor::type::array:
        head.argument = node.via.array.size();
        break;
    case cbor::type::map:
        detail::validity::throw_logic_error_if(node.via.map.size() % rfc8949::data_items_per_pair != 0,
                                               "cbor::encode: a map holds a key and a value for each entry");
        head.argument = node.via.map.size() / rfc8949::data_items_per_pair;
        break;
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        if (node.via.tag.content->m_type == cbor::type::byte_string)
            if (auto const r = detail::heads::convert_bignum_to_integer(node.via.tag.number,
                                                                          node.via.tag.content->via.bin)) {
                head.major = r->major;
                head.info = r->info;
                head.argument = r->argument;
                break;
            }
        head.argument = node.via.tag.number;
        break;
    case cbor::type::tag:
    case cbor::type::typed_array:
        head.argument = node.via.tag.number;
        break;
    case cbor::type::shared_reference:
        head.argument = std::to_underlying(rfc8949::tag_number::sharedref);
        head.info = detail::heads::preferred_argument_info(head.argument);
        break;
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        head.argument = std::to_underlying(node.via.simple_value);
        break;
    case cbor::type::half_float:
        head.info = std::to_underlying(rfc8949::simple_information::half_precision_float);
        head.argument = detail::ieee754::encode_float(rfc8949::simple_information::half_precision_float, node.via.f64);
        break;
    case cbor::type::single_float:
        head.info = std::to_underlying(rfc8949::simple_information::single_precision_float);
        head.argument = detail::ieee754::encode_float(rfc8949::simple_information::single_precision_float, node.via.f64);
        break;
    case cbor::type::double_float:
        head.info = std::to_underlying(rfc8949::simple_information::double_precision_float);
        head.argument = detail::ieee754::encode_float(rfc8949::simple_information::double_precision_float, node.via.f64);
        break;
    case cbor::type::unsigned_integer:
    case cbor::type::negative_integer:
        break;
    }
    return head;
}

inline std::span<item const> detail::encoding::children_of(item const &node, major_type const major)
{
    switch (major) {
    case major_type::array:
        return node.via.array;
    case major_type::map:
        return node.via.map;
    case major_type::tag:
    case major_type::unsigned_integer:
    case major_type::negative_integer:
    case major_type::byte_string:
    case major_type::text_string:
    case major_type::simple:
        return {};
    }
    return {};
}

inline item const *detail::encoding::content_of(item const &node, major_type const major)
{
    if (major != major_type::tag || node.m_type == cbor::type::shared_reference)
        return nullptr;
    return node.via.tag.content;
}

template <bool AppendRef, class Writer>
CBOR_ALWAYS_INLINE inline std::expected<void, std::errc> detail::encoding::encode_head_and_payload(binary_writer<Writer> &out, item const &node,
                                                                 head_parts const &head)
    requires(!AppendRef || binary_writer<Writer>::blocks)
{
    std::expected<void, std::errc> written = out.encode_head(head.major, head.info, head.argument);
    if constexpr (AppendRef) {
        if (written && head.major == major_type::byte_string && node.via.bin.size() > copybreak)
            return out.append_ref(node.via.bin);
        if (written && head.major == major_type::text_string && node.via.str.size() > copybreak)
            return out.append_ref(std::as_bytes(std::span(node.via.str)));
    }
    if (written && head.major == major_type::byte_string)
        written = out.encode_payload(node.via.bin);
    else if (written && head.major == major_type::text_string)
        written = out.encode_payload(std::span(node.via.str));
    return written;
}

template <class Writer>
std::expected<void, std::errc> detail::encoding::encode_sharedref(binary_writer<Writer> &out, std::uint64_t const index)
{
    std::expected<void, std::errc> const written =
        out.encode_head(major_type::tag, std::to_underlying(rfc8949::tag_number::sharedref));
    if (!written) [[unlikely]]
        return written;
    return out.encode_head(major_type::unsigned_integer, index);
}

template <bool AppendRef, class Writer>
std::expected<void, error> detail::encoding::encode_item(binary_writer<Writer> &out, item const &node)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return r;
    detail::validity::throw_logic_error_if(node.m_type == cbor::type::shared_reference,
                                           "cbor::encode: a shared_reference stands below a node that does not hold it");
    head_parts const head = head_of(node);
    std::expected<void, std::errc> const written = encode_head_and_payload<AppendRef>(out, node, head);
    if (!written) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(written.error()));
    for (item const &child : children_of(node, head.major))
        if (auto const r = encode_item<AppendRef>(out, child); !r) [[unlikely]]
            return r;
    if (item const *const content = content_of(node, head.major))
        return encode_item<AppendRef>(out, *content);
    return {};
}

inline std::uint64_t detail::encoding::size_of(item const &node, head_parts const &head)
{
    std::uint64_t const size = detail::heads::initial_byte_size + detail::heads::argument_size(head.info);
    if (head.major == major_type::byte_string)
        return size + node.via.bin.size();
    if (head.major == major_type::text_string)
        return size + node.via.str.size();
    return size;
}

inline std::uint64_t detail::encoding::reference_size_of(item const &reference)
{
    return detail::heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) + detail::heads::initial_byte_size +
           detail::heads::argument_size(reference.additional_information);
}

class detail::encoding::depth_first_search
{
    struct frame {
        item const *node;
        std::uint64_t index;
        frame const *up;
    };

    static constexpr std::uint64_t back_edge_size =
        detail::heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) + detail::heads::initial_byte_size;

    std::vector<item const *> targets;
    std::unordered_map<item const *, std::uint64_t> open;
    bool path_in_map = false;
    std::uint64_t direct = 0;
    std::uint64_t tree = 0;

    void enter_path(frame const &here)
    {
        for (frame const *f = &here; f != nullptr; f = f->up)
            open.emplace(f->node, f->index);
        path_in_map = true;
    }

    std::expected<void, error> count(std::uint64_t const bytes, bool const below)
    {
        direct += bytes;
        if (!below)
            tree += bytes;
        return detail::validity::check_amplification(direct, tree);
    }

    std::expected<void, error> find_back_edges(item const &node, frame const *const up, bool const below)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return r;
        head_parts const head = head_of(node);
        if (auto const r = count(size_of(node, head), below); !r) [[unlikely]]
            return r;
        frame const here{&node, 0, up};
        if (path_in_map) [[unlikely]]
            open.emplace(&node, 0);
        for (item const &child : children_of(node, head.major))
            if (auto const r = follow_edge(child, here, below); !r) [[unlikely]]
                return r;
        if (item const *const content = content_of(node, head.major))
            if (auto const r = follow_edge(*content, here, below); !r) [[unlikely]]
                return r;
        if (path_in_map) [[unlikely]]
            open.erase(&node);
        return {};
    }

    std::expected<void, error> follow_edge(item const &child, frame const &here, bool const below)
    {
        bool const reference = child.m_type == cbor::type::shared_reference;
        item const &next = reference ? *child.via.tag.content : child;
        if (reference) [[unlikely]] {
            if (!below)
                tree += reference_size_of(child);
            if (!path_in_map)
                enter_path(here);
        }
        if (path_in_map && open.contains(&next)) [[unlikely]] {
            targets.push_back(&next);
            return count(back_edge_size, true);
        }
        return find_back_edges(next, &here, below || reference);
    }

    template <bool AppendRef, class Writer>
    std::expected<std::uint64_t, error> write(binary_writer<Writer> &out, item const &node, frame const *const up,
                                              std::uint64_t marks)
    {
        if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
            return std::unexpected(r.error());
        std::uint64_t const index = marks;
        if (!targets.empty() && std::ranges::binary_search(targets, &node)) [[unlikely]] {
            if (auto const r = out.encode_head(major_type::tag, std::to_underlying(rfc8949::tag_number::shareable));
                !r) [[unlikely]]
                return std::unexpected(detail::validity::writer_error(r.error()));
            ++marks;
        }
        head_parts const head = head_of(node);
        std::expected<void, std::errc> const written = detail::encoding::encode_head_and_payload<AppendRef>(out, node, head);
        if (!written) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(written.error()));
        frame const here{&node, index, up};
        if (path_in_map) [[unlikely]]
            open.emplace(&node, index);
        for (item const &child : children_of(node, head.major)) {
            auto const r = write_edge<AppendRef>(out, child, here, marks);
            if (!r) [[unlikely]]
                return r;
            marks = *r;
        }
        if (item const *const content = content_of(node, head.major)) {
            auto const r = write_edge<AppendRef>(out, *content, here, marks);
            if (!r) [[unlikely]]
                return r;
            marks = *r;
        }
        if (path_in_map) [[unlikely]]
            open.erase(&node);
        return marks;
    }

    template <bool AppendRef, class Writer>
    std::expected<std::uint64_t, error> write_edge(binary_writer<Writer> &out, item const &child, frame const &here,
                                                   std::uint64_t const marks)
    {
        bool const reference = child.m_type == cbor::type::shared_reference;
        item const &next = reference ? *child.via.tag.content : child;
        if (reference && !path_in_map) [[unlikely]]
            enter_path(here);
        if (path_in_map) [[unlikely]] {
            if (auto const at = open.find(&next); at != open.end()) {
                std::expected<void, std::errc> const written = detail::encoding::encode_sharedref(out, at->second);
                if (!written) [[unlikely]]
                    return std::unexpected(detail::validity::writer_error(written.error()));
                return marks;
            }
        }
        return write<AppendRef>(out, next, &here, marks);
    }

public:
    template <bool AppendRef, class Writer>
    std::expected<void, error> encode(binary_writer<Writer> &out, item const &root)
    {
        item const &start = root.m_type == cbor::type::shared_reference ? *root.via.tag.content : root;
        if (auto const r = find_back_edges(start, nullptr, false); !r) [[unlikely]]
            return r;
        std::ranges::sort(targets);
        auto const repeated = std::ranges::unique(targets);
        targets.erase(repeated.begin(), repeated.end());
        open.clear();
        path_in_map = false;
        if (auto const r = write<AppendRef>(out, start, nullptr, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        return {};
    }
};

template <bool Cycles, bool AppendRef, class Writer>
std::expected<void, error> detail::encoding::encode_tree(binary_writer<Writer> &out, item const &root)
{
    if (Cycles || root.m_holds_shared_reference) [[unlikely]] {
        depth_first_search walk;
        return walk.encode<AppendRef>(out, root);
    }
    return encode_item<AppendRef>(out, root);
}

template <bool Cycles, class Target>
std::expected<std::size_t, error> detail::encoding::encode_into(Target &&target, item const &root)
{
    decltype(auto) message = message_of(target, 0);
    binary_writer<std::remove_reference_t<decltype(message)>> out(message);
    if (auto const r = encode_tree<Cycles, false>(out, root); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = out.flush(); !r) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(r.error()));
    if (auto const r = message.done(out.written); !r) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(r.error()));
    return out.written;
}

template <class Target>
    requires detail::encoding::is_target_v<std::remove_reference_t<Target>>
std::expected<std::size_t, error> encode(Target &&out, item const &root)
{
    return detail::encoding::encode_into<false>(std::forward<Target>(out), root);
}

template <class Target>
    requires detail::encoding::is_target_v<std::remove_reference_t<Target>>
std::expected<std::size_t, error> encode_cycles(Target &&out, item const &root)
{
    return detail::encoding::encode_into<true>(std::forward<Target>(out), root);
}

#if __has_include(<sys/uio.h>)
template <bool Cycles>
std::expected<ref<std::span<iovec const>>, error> detail::encoding::encode_iov_of(item const &root)
{
    iov_writer writer;
    binary_writer<iov_writer> out(writer);
    if (auto const r = encode_tree<Cycles, false>(out, root); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out.release();
}

template <bool Cycles>
std::expected<ref<std::span<iovec const>>, error> detail::encoding::encode_iov_of(owning<item> const &root)
{
    iov_writer writer;
    writer.push_owner(root.held);
    binary_writer<iov_writer> out(writer);
    if (auto const r = encode_tree<Cycles, true>(out, *root.held); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out.release();
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root)
{
    return detail::encoding::encode_iov_of<false>(root);
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov_cycles(item const &root)
{
    return detail::encoding::encode_iov_of<true>(root);
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov(owning<item> const &root)
{
    return detail::encoding::encode_iov_of<false>(root);
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov_cycles(owning<item> const &root)
{
    return detail::encoding::encode_iov_of<true>(root);
}
#endif
}

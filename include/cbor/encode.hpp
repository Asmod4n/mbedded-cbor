/**
 * @file
 * Writes CBOR into a span, a byte container or a writer.
 * cbor::encoder writes one head or one value for each call.
 * cbor::encode and cbor::encode_iov write a tree of cbor::item.
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

template <class Target>
class encoder;

class iov_writer;

/**
 * @brief Selects the encode that writes each cycle of a tree with tags 28 and 29 (RFC 8949 3.4, IANA tags 28 and 29).
 *
 * cbor::encode() and cbor::encode_iov() without it write no tag 28 or 29 of their own, and a `shared_reference` gives
 * `unsupported_value`.
 *
 * @code{.callseq}
 * cbor::encode(**tree, out, cbor::cycles) → std::expected<std::size_t, error>
 * @endcode
 *
 * @see cbor::encode()
 *
 * @ingroup item
 */
struct cycles_t {
    explicit cycles_t() = default;
};

/** @brief The value of cbor::cycles_t. */
inline constexpr cycles_t cycles{};

/**
 * @brief Writes the tree at `root` and gives the count of bytes written.
 *
 * Each integer, length and tag number keeps the width of its head, from `additional_information`.
 * A float keeps its width.
 * The call walks the tree once and writes no tag 28 or 29 of its own.
 * A tag 28 and a tag 29 of a tree of cbor::lazy::clone() are written as they are.
 * A `shared_reference` gives `unsupported_value`; the target then holds the bytes written before it.
 * The form with cbor::cycles writes it as the overload below says.
 * The target is one of these:
 * - a container of bytes, for example `std::string`: the call appends; - a `std::span` of bytes: the call writes
 * into it and allocates nothing; - an object with `allocate(std::size_t)`, which gives a writer with
 * `append(std::string_view)` and `done(std::size_t)`.
 *
 * @code{.callseq}
 * cbor::encode(**tree, out) → std::expected<std::size_t, error>
 * @endcode
 * @code{.example}
 * message: [1, "a"]
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of [1, "a"]
 * call: std::string out;
 * result: ""
 * call: cbor::encode(**tree, out)
 * result: 4
 * call: out
 * result: 4 bytes: 82 01 61 61
 * call: bytes
 * result: 4 bytes: 82 01 61 61
 * @endcode
 *
 * @retval no_buffer_space the span is too short
 * @retval nesting_depth_exceeded the tree is deeper than the limit
 * @retval unsupported_value the tree holds a `shared_reference`
 * @retval value_too_large, not_enough_memory, io_error the error of a writer, from its `std::errc`
 * @snippet doc_reference_item.cpp encode
 * @see cbor::encode_iov()
 * @see cbor::decode()
 * @see cbor::encoder
 *
 * @ingroup item
 */
template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target);

/**
 * @brief Writes the tree at `root` with tags 28 and 29 for each cycle and gives the count of bytes written.
 *
 * A `shared_reference` stands for the item it names.
 * A walk before the write finds each item that a depth-first walk reaches again while it is inside that item.
 * Such an item gets a tag 28, and each edge that reaches it again is a tag 29 with the index of that mark, in its
 * shortest head.
 * A value that two references reach without a cycle is written in full each time.
 * Every other item is written as cbor::encode() without cbor::cycles writes it.
 *
 * @code{.callseq}
 * cbor::encode(**tree, out, cbor::cycles) → std::expected<std::size_t, error>
 * @endcode
 * @code{.example}
 * message: 28([29(0)])
 * call: auto const tree = cbor::decode(bytes)
 * result: owning item of an array that holds itself
 * call: std::string out;
 * result: ""
 * call: cbor::encode(**tree, out, cbor::cycles)
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
 * @see cbor::cycles_t
 *
 * @ingroup item
 */
template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target, cycles_t);

#if __has_include(<sys/uio.h>)
/**
 * @brief Writes the tree at `root` into a list of `struct iovec` pieces, not into one buffer.
 *
 * The result gives the pieces in order and holds their storage and the owners of the pieces that refer to strings.
 * The pieces are for sending only: `writev`, `sendmsg`, `IORING_OP_WRITEV` and `IORING_OP_SENDMSG`.
 * The program holds the `ref` until the call returns or until the last completion of the request.
 * The bytes are those of cbor::encode().
 * The function exists where `<sys/uio.h>` exists.
 * `nesting_depth_exceeded`: the tree is deeper than the limit.
 * `unsupported_value`: the tree holds a `shared_reference`.
 * The form with cbor::cycles writes tags 28 and 29 as cbor::encode() with cbor::cycles does.
 * A piece that refers to the string of an owner points into memory that the owner holds as const; a call that writes
 * into the pieces, such as `readv`, is a wrong use.
 *
 * @code{.callseq}
 * cbor::encode_iov(**tree) → std::expected<ref<std::span<iovec const>>, error>
 * @endcode
 *
 * @code{.output}
 * bytes: 82 01 61 61
 * call: auto const pieces = cbor::encode_iov(**cbor::decode(bytes))
 * (*pieces)->size(): 1
 * sent with writev: [1, "a"]
 * @endcode
 *
 * @snippet doc_reference_item.cpp encode_iov
 * @see cbor::encode()
 * @see cbor::iov_writer
 *
 * @ingroup item
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);

/**
 * @brief Writes the tree at `root` with tags 28 and 29 for each cycle into a list of `struct iovec` pieces.
 *
 * The bytes are those of cbor::encode() with cbor::cycles, and its errors.
 *
 * @code{.callseq}
 * cbor::encode_iov(**tree, cbor::cycles) → std::expected<ref<std::span<iovec const>>, error>
 * @endcode
 * @see cbor::cycles_t
 *
 * @ingroup item
 */
std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root, cycles_t);
#endif

class encoding
{
public:
    static constexpr std::size_t block_size = CBOR_ENCODER_BLOCK_SIZE;
    static constexpr std::size_t item_size_max = heads::initial_byte_size + sizeof(std::uint64_t);
    static constexpr std::size_t ref_size = 32;
    static_assert(block_size >= item_size_max, "A block must hold one whole head or float.");
    static constexpr std::size_t inline_buffer_size = CBOR_ENCODER_INLINE_BUFFER_SIZE;
    static_assert(inline_buffer_size >= item_size_max, "The inline buffer must hold one whole head or float.");

private:
    template <class Writer>
    struct binary_writer;

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

    template <class Target>
    static decltype(auto) message_of(Target &&target, std::size_t const hint)
    {
        using U = std::remove_cvref_t<Target>;
        if constexpr (requires { typename U::element_type; } && requires { std::span(target); } &&
                      !requires { target.resize(std::size_t{}); })
            return span_message<typename U::element_type>{target, 0};
        else if constexpr (byte_container<U>)
            return container_message<U>{target};
        else if constexpr (std::same_as<U, iov_writer>)
            return (target);
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
    template <class Writer>
    static std::expected<void, error> encode_item(binary_writer<Writer> &out, item const &node);
    template <class Writer>
    static std::expected<void, std::errc> encode_head_and_payload(binary_writer<Writer> &out, item const &node,
                                                                  head_parts const &head);
    template <class Writer>
    static std::expected<void, std::errc> encode_sharedref(binary_writer<Writer> &out, std::uint64_t index);
    static std::uint64_t size_of(item const &node, head_parts const &head);
    static std::uint64_t reference_size_of(item const &reference);

    class depth_first_search;

    template <bool Cycles, class Writer>
    static std::expected<void, error> encode_tree(binary_writer<Writer> &out, item const &root);

    template <bool Cycles, class Target>
    static std::expected<std::size_t, error> encode_into(item const &root, Target &&target);

#if __has_include(<sys/uio.h>)
    template <bool Cycles>
    static std::expected<ref<std::span<iovec const>>, error> encode_iov_of(item const &root);
#endif

    template <class Target>
    friend std::expected<std::size_t, error> encode(item const &root, Target &&target);

    template <class Target>
    friend std::expected<std::size_t, error> encode(item const &root, Target &&target, cycles_t);

#if __has_include(<sys/uio.h>)
    friend std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);
    friend std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root, cycles_t);
#endif

    template <class>
    friend class cbor::encoder;

    template <class>
    friend class structs;

    template <class>
    friend class schema;
};

template <class Writer>
struct encoding::binary_writer {
    static constexpr bool direct = std::same_as<Writer, encoding::span_message<char>>;
    static constexpr bool blocks = requires(Writer &w, std::size_t const n) {
        { w.preallocate(n) } -> std::same_as<std::span<char>>;
    };

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
        detail::validity::throw_logic_error_if(
            block.size() < needed, "cbor::encoder: the writer gave a block shorter than the item");
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

    std::expected<void, std::errc>
    write_item(std::array<char, heads::initial_byte_size + sizeof(std::uint64_t)> const &item,
               std::size_t const size)
    {
        if (!detail::validity::bytes_fit(item.size(), block.size() - used)) [[unlikely]] {
            if constexpr (direct) {
                if (!detail::validity::bytes_fit(size, block.size() - used)) [[unlikely]]
                    return std::unexpected(std::errc::no_buffer_space);
                std::ranges::copy(std::span(item).first(size), block.subspan(used).begin());
                used += size;
                return {};
            } else if (auto const r = refill(item.size()); !r) [[unlikely]] {
                return r;
            }
        }
        std::ranges::copy(item, std::span(block).subspan(used).begin());
        used += size;
        return {};
    }

    std::expected<void, std::errc> encode_head(major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        if constexpr (!direct) {
            if (!detail::validity::bytes_fit(heads::head_size_max, block.size() - used)) [[unlikely]]
                if (auto const r = refill(item_size_max); !r) [[unlikely]]
                    return r;
            std::array<char, heads::head_size_max> head;
            std::size_t const size = heads::head_encode(head, major, info, argument);
            std::ranges::copy(head, std::span(block).subspan(used).begin());
            used += size;
            return {};
        } else {
            std::span<char> const out = block;
            std::size_t const at = used;
            std::array<char, heads::head_size_max> tail;
            bool const near_end = !detail::validity::bytes_fit(tail.size(), out.size() - at);
            std::span<char, heads::head_size_max> const item =
                near_end ? std::span(tail) : out.subspan(at).template first<heads::head_size_max>();
            std::size_t const size = heads::head_encode(item, major, info, argument);
            if (near_end) [[unlikely]]
                return write_item(tail, size);
            used = at + size;
            return {};
        }
    }

    template <std::unsigned_integral T>
    CBOR_ALWAYS_INLINE void put_item(char const initial, T const argument)
    {
        std::span<char> const out = std::span<char>(block).subspan(used);
        out.front() = initial;
        std::ranges::copy(std::bit_cast<std::array<char, sizeof(T)>>(std::byteswap(argument)),
                          out.subspan(heads::initial_byte_size).begin());
        used += heads::initial_byte_size + sizeof(T);
    }

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_head(major_type const major, std::uint64_t const argument)
    {
        if (detail::validity::bytes_fit(heads::head_size_max, block.size() - used)) [[likely]] {
            std::span<char> const out = block;
            used += heads::head_encode(out.subspan(used).template first<heads::head_size_max>(), major,
                                       heads::preferred_argument_info(argument), argument);
            return {};
        }
        return encode_head(major, heads::preferred_argument_info(argument), argument);
    }

    template <class E>
    static void copy_into(std::span<E const> const from, std::span<char> const to)
    {
        std::ranges::copy(std::as_bytes(from), std::as_writable_bytes(to.first(from.size())).begin());
    }

    std::expected<void, std::errc> encode_byte_string(std::span<std::byte const> const bytes)
    {
        if (auto const r = encode_head(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        return encode_payload(bytes);
    }

    template <class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    std::expected<void, std::errc> encode_payload(std::span<E const> const bytes)
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

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_text_string(std::string_view text)
    {
        if (auto const r = encode_head(major_type::text_string, text.size()); !r) [[unlikely]]
            return r;
        std::span<char> const out = block;
        std::size_t const at = used;
        if (text.size() <= out.size() - at) [[likely]] {
            std::ranges::copy(text, out.subspan(at).begin());
            used = at + text.size();
            return {};
        }
        if constexpr (direct) {
            return std::unexpected(std::errc::no_buffer_space);
        } else if constexpr (blocks) {
            std::size_t const first = out.size() - at;
            std::ranges::copy(std::span(text).first(first), out.subspan(at).begin());
            used = out.size();
            std::string_view const rest{std::span(text).subspan(first)};
            if (auto const r = refill(rest.size()); !r) [[unlikely]]
                return r;
            std::ranges::copy(rest, block.begin());
            used = rest.size();
            return {};
        } else {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            written += text.size();
            return writer.append(text);
        }
    }

    template <class S>
        requires std::same_as<S, std::string>
    std::expected<void, std::errc> encode_text_string(S &&text)
    {
        if constexpr (blocks)
            if (text.size() >= ref_size) {
                auto const owner = std::make_shared<std::string const>(std::move(text));
                return encode_piece(major_type::text_string, std::as_bytes(std::span(*owner)), owner);
            }
        return encode_text_string(std::string_view(text));
    }

    std::expected<void, std::errc> encode_text_string(std::shared_ptr<std::string const> const text)
    {
        detail::validity::throw_logic_error_if_null(text,
                                                    "cbor::encoder::encode_text_string: the string is null");
        if constexpr (blocks)
            if (text->size() >= ref_size)
                return encode_piece(major_type::text_string, std::as_bytes(std::span(*text)), text);
        return encode_text_string(std::string_view(*text));
    }

    std::expected<void, std::errc> encode_text_string(ref<std::string_view> const text)
    {
        if constexpr (blocks)
            if ((*text).size() >= ref_size)
                return encode_piece(major_type::text_string, std::as_bytes(std::span(*text)), text.owner);
        return encode_text_string(*text);
    }

    template <class V>
        requires std::same_as<V, std::vector<std::byte>>
    std::expected<void, std::errc> encode_byte_string(V &&bytes)
    {
        if constexpr (blocks)
            if (bytes.size() >= ref_size) {
                auto const owner = std::make_shared<std::vector<std::byte> const>(std::move(bytes));
                return encode_piece(major_type::byte_string, std::span(*owner), owner);
            }
        return encode_byte_string(std::span<std::byte const>(bytes));
    }

    std::expected<void, std::errc> encode_byte_string(std::shared_ptr<std::vector<std::byte> const> const bytes)
    {
        detail::validity::throw_logic_error_if_null(bytes,
                                                    "cbor::encoder::encode_byte_string: the bytes are null");
        if constexpr (blocks)
            if (bytes->size() >= ref_size)
                return encode_piece(major_type::byte_string, std::span(*bytes), bytes);
        return encode_byte_string(std::span<std::byte const>(*bytes));
    }

    std::expected<void, std::errc> encode_byte_string(ref<std::span<std::byte const>> const bytes)
    {
        if constexpr (blocks)
            if ((*bytes).size() >= ref_size)
                return encode_piece(major_type::byte_string, *bytes, bytes.owner);
        return encode_byte_string(*bytes);
    }

    std::expected<void, std::errc> encode_piece(major_type const major, std::span<std::byte const> const piece,
                                                std::shared_ptr<void const> owner)
        requires blocks
    {
        if (auto const r = encode_head(major, piece.size()); !r) [[unlikely]]
            return r;
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        written += piece.size();
        writer.wrap_buffer(piece, std::move(owner));
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

    template <std::unsigned_integral T>
        requires(sizeof(T) <= sizeof(std::uint64_t))
    CBOR_ALWAYS_INLINE std::expected<void, std::errc> encode_fixed_width_head(major_type const major, T const argument)
    {
        constexpr std::uint8_t info = static_cast<std::uint8_t>(
            std::to_underlying(rfc8949::additional_information::one_byte_argument) + std::countr_zero(sizeof(T)));
        if (detail::validity::bytes_fit(item_size_max, block.size() - used)) [[likely]] {
            put_item(heads::initial_byte(major, info), argument);
            return {};
        }
        return encode_head(major, info, argument);
    }

    template <std::floating_point T>
        requires(sizeof(T) == sizeof(std::uint32_t) || sizeof(T) == sizeof(std::uint64_t))
    std::expected<void, std::errc> encode_fixed_width_float(T value)
    {
        return encode_fixed_width_head(
            major_type::simple_float,
            std::bit_cast<
                std::conditional_t<sizeof(T) == sizeof(std::uint32_t), std::uint32_t, std::uint64_t>>(value));
    }
};

#if __has_include(<sys/uio.h>)
/**
 * @brief A target of cbor::encoder that collects pieces, not one buffer.
 *
 * The encoder writes heads and short strings into blocks of CBOR_ENCODER_BLOCK_SIZE bytes.
 * A text or a byte string of 32 bytes or more that the program hands over with its owner becomes a piece of its own,
 * with no copy.
 * cbor::encoder::finish() gives the pieces as `struct iovec`, in a `ref` that holds the blocks and the owners.
 * The pieces are for sending only, as cbor::encode_iov() says.
 * The class exists where `<sys/uio.h>` exists.
 * The class has no public member function.
 * A write after `finish` throws `std::logic_error`.
 *
 * @code{.callseq}
 * cbor::iov_writer w; → iov_writer
 * @endcode
 * @code{.output}
 * call: cbor::iov_writer w; cbor::encoder e(w);
 * call: e.text_string(text), with text a std::shared_ptr<std::string const> of 40 times "x"
 * call: auto const pieces = e.finish()
 * (*pieces)->size(): 2, the head 78 28 and *text with no copy
 * sent with writev: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
 * @endcode
 *
 * @snippet doc_reference_item.cpp iov_writer
 * @see cbor::encode_iov()
 * @see cbor::encoder
 *
 * @ingroup item
 */
class iov_writer
{
    struct storage {
        std::vector<std::vector<char>> blocks;
        std::vector<std::shared_ptr<void const>> owners;
        std::vector<iovec> pieces;
    };

    std::shared_ptr<storage> kept = std::make_shared<storage>();
    bool released = false;

    std::span<char> preallocate(std::size_t const size)
    {
        detail::validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->blocks.emplace_back(size);
        return std::span(kept->blocks.back());
    }

    void push_piece(void *const base, std::size_t const size)
    {
        iovec piece{};
        piece.iov_base = base;
        piece.iov_len = size;
        kept->pieces.push_back(piece);
    }

    void postallocate(std::span<char> const part)
    {
        detail::validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        if (part.empty())
            return;
        push_piece(part.data(), part.size());
    }

    void wrap_buffer(std::span<std::byte const> const piece, std::shared_ptr<void const> owner)
    {
        detail::validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->owners.push_back(std::move(owner));
        push_piece(const_cast<std::byte *>(piece.data()), piece.size());
    }

    ref<std::span<iovec const>> release()
    {
        detail::validity::throw_logic_error_if(released, "cbor::iov_writer: a second release");
        released = true;
        std::span<iovec const> const pieces = kept->pieces;
        return {std::move(kept), pieces};
    }

    friend class encoding;
};
#endif

/**
 * @brief Writes CBOR one data item per call, in the order of the bytes.
 *
 * The encoder counts nothing: the counts of `array` and `map` come from the program.
 * The target is one of these:
 * - a container of bytes, for example `std::string`: the encoder appends; - a `std::span` of bytes: the encoder
 * writes into it and allocates nothing; - an cbor::iov_writer: the encoder gives pieces; - an object with
 * `allocate(std::size_t)`, which gives a writer with `append(std::string_view)` and `done(std::size_t)`.
 * The encoder does not write tags 28 and 29.
 * Each call gives `std::expected<void, error>`.
 * After an error, and after cbor::encoder::finish(), each call throws `std::logic_error`.
 * A copy does not compile.
 *
 * @code{.callseq}
 * cbor::encoder e(out)                     → encoder<std::string>
 * cbor::encoder e{std::span<char>(buffer)} → encoder<std::span<char>>
 * cbor::encoder e(w)                       → encoder<iov_writer>
 * @endcode
 * @code{.example}
 * bytes: a1 61 61 18 05
 * call: std::string out;
 * result: ""
 * call: cbor::encoder e(out);
 * result: encoder over a std::string
 * call: e.map(1)
 * result: void
 * call: e.text_string("a")
 * result: void
 * call: e.unsigned_integer(std::uint8_t{5})
 * result: void
 * call: e.finish()
 * result: 5
 * call: out
 * result: 5 bytes: a1 61 61 18 05
 * @endcode
 *
 * @snippet doc_reference_item.cpp encoder
 * @see cbor::encoder::finish()
 * @see cbor::encode()
 *
 * @ingroup item
 */
template <class Target>
class encoder
{
    static constexpr bool pieces = std::same_as<Target, iov_writer>;

    using message_type = decltype(encoding::message_of(std::declval<Target &>(), 0));

    message_type message;
    encoding::binary_writer<std::remove_reference_t<message_type>> out;
    bool usable = true;

    void throw_logic_error_if_unusable() const
    {
        detail::validity::throw_logic_error_if(!usable,
                                               "cbor::encoder: a call after an error or after finish");
    }

    std::expected<void, error> written(std::expected<void, std::errc> const r)
    {
        if (!r) [[unlikely]] {
            usable = false;
            return std::unexpected(detail::validity::writer_error(r.error()));
        }
        return {};
    }

    std::expected<void, error> refused(error const e)
    {
        usable = false;
        return std::unexpected(e);
    }

    template <class T>
    static constexpr bool integer_argument =
        std::unsigned_integral<T> && !std::same_as<T, bool> && !std::same_as<T, char> && !std::same_as<T, wchar_t> &&
        !std::same_as<T, char8_t> && !std::same_as<T, char16_t> && !std::same_as<T, char32_t>;

public:
    explicit encoder(Target &target)
        requires(!pieces && !requires { typename Target::element_type; })
        : message(encoding::message_of(target, 0)), out(message)
    {
    }

    explicit encoder(Target const target)
        requires requires { typename Target::element_type; }
        : message(encoding::message_of(target, 0)), out(message)
    {
    }

    explicit encoder(Target &target)
        requires pieces
        : message(encoding::message_of(target, 0)), out(message)
    {
    }

    encoder(encoder const &) = delete;
    encoder &operator=(encoder const &) = delete;

    /**
     * @brief Writes `value` as an unsigned integer (major type 0).
     *
     * The head takes the width of `T`: one byte of argument for `std::uint8_t`, eight for `std::uint64_t`, also when
     * a shorter head holds the value.
     * `T` is an unsigned integer type.
     * `bool` and the character types do not compile.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.unsigned_integer(value) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: 19 00 05
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.unsigned_integer(std::uint16_t{5})
     * result: void
     * call: e.finish()
     * result: 3
     * call: out
     * result: 3 bytes: 19 00 05
     * @endcode
     *
     * @snippet doc_reference_item.cpp unsigned_integer
     * @see cbor::encoder::negative_integer()
     * @see cbor::encoder
     */
    template <class T>
        requires integer_argument<T>
    std::expected<void, error> unsigned_integer(T const value)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_fixed_width_head(major_type::unsigned_integer, value));
    }

    /**
     * @brief Writes a negative integer (major type 1) with the argument `argument`.
     *
     * The value is -1 - `argument`.
     * The head takes the width of `T`.
     * `T` is an unsigned integer type.
     * `bool` and the character types do not compile.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.negative_integer(argument) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: 39 01 f3
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.negative_integer(std::uint16_t{499})
     * result: void
     * call: e.finish()
     * result: 3
     * call: out
     * result: 3 bytes: 39 01 f3
     * call: cbor::decode(out)
     * result: owning item of -500
     * @endcode
     *
     * @snippet doc_reference_item.cpp negative_integer
     * @see cbor::encoder::unsigned_integer()
     * @see cbor::encoder
     */
    template <class T>
        requires integer_argument<T>
    std::expected<void, error> negative_integer(T const argument)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_fixed_width_head(major_type::negative_integer, argument));
    }

    /**
     * @brief Writes `bytes` as a byte string, with the shortest head for the length.
     *
     * Over an cbor::iov_writer, the last three forms do not copy 32 bytes or more: the result keeps the moved vector
     * or the owner.
     * Over any other target, each form copies the bytes.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A null `std::shared_ptr` throws `std::logic_error`.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.byte_string(bytes) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: 42 61 62
     * call: std::array<std::uint8_t, 2> ab{97, 98};
     * result: {97, 98}
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.byte_string(std::as_bytes(std::span(ab)))
     * result: void
     * call: e.finish()
     * result: 3
     * call: out
     * result: "Bab"
     * call: cbor::decode(out)
     * result: owning item of h'6162'
     * @endcode
     *
     * @snippet doc_reference_item.cpp byte_string
     * @see cbor::encoder::text_string()
     * @see cbor::encoder
     */
    std::expected<void, error> byte_string(std::span<std::byte const> const bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_byte_string(bytes));
    }

    /**
     * @brief Writes a byte string that it takes by move.
     */
    template <class V>
        requires std::same_as<V, std::vector<std::byte>>
    std::expected<void, error> byte_string(V &&bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_byte_string(std::move(bytes)));
    }

    /**
     * @brief Writes a byte string that @p bytes owns.
     */
    std::expected<void, error> byte_string(std::shared_ptr<std::vector<std::byte> const> const &bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_byte_string(bytes));
    }

    /**
     * @brief Writes the byte string that a cbor::ref views.
     */
    std::expected<void, error> byte_string(ref<std::span<std::byte const>> const &bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_byte_string(bytes));
    }

    /**
     * @brief Writes `text` as a text string, with the shortest head for the length.
     *
     * The call does not check UTF-8.
     * Over an cbor::iov_writer, the last three forms do not copy a text of 32 bytes or more: the result keeps the
     * moved string or the owner.
     * Over any other target, each form copies the text.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A null `std::shared_ptr` throws `std::logic_error`.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.text_string(text) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: 63 61 62 63
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.text_string("abc")
     * result: void
     * call: e.finish()
     * result: 4
     * call: out
     * result: "cabc"
     * call: cbor::decode(out)
     * result: owning item of "abc"
     * @endcode
     *
     * @snippet doc_reference_item.cpp text_string
     * @see cbor::encoder::byte_string()
     * @see cbor::iov_writer
     */
    std::expected<void, error> text_string(std::string_view const text)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_text_string(text));
    }

    /**
     * @brief Writes a text string that it takes by move.
     */
    template <class S>
        requires std::same_as<S, std::string>
    std::expected<void, error> text_string(S &&text)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_text_string(std::move(text)));
    }

    /**
     * @brief Writes a text string that @p text owns.
     */
    std::expected<void, error> text_string(std::shared_ptr<std::string const> const &text)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_text_string(text));
    }

    /**
     * @brief Writes the text string that a cbor::ref views.
     */
    std::expected<void, error> text_string(ref<std::string_view> const &text)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_text_string(text));
    }

    /**
     * @brief Writes the head of an array (major type 4) with `count` elements, with the shortest head for `count`.
     *
     * The next calls write the elements.
     * The encoder does not count them: a wrong count gives bytes that are not one item.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.array(count) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: 82 18 01 18 02
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.array(2)
     * result: void
     * call: e.unsigned_integer(std::uint8_t{1})
     * result: void
     * call: e.unsigned_integer(std::uint8_t{2})
     * result: void
     * call: e.finish()
     * result: 5
     * call: out
     * result: 5 bytes: 82 18 01 18 02
     * @endcode
     *
     * @snippet doc_reference_item.cpp array
     * @see cbor::encoder::map()
     * @see cbor::encoder
     */
    std::expected<void, error> array(std::uint64_t const count)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_head(major_type::array, count));
    }

    /**
     * @brief Writes the head of a map (major type 5) with `count` pairs, with the shortest head for `count`.
     *
     * The next calls write each key and then its value.
     * The encoder does not count them: a wrong count gives bytes that are not one item.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.map(count) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: a1 61 61 18 05
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.map(1)
     * result: void
     * call: e.text_string("a")
     * result: void
     * call: e.unsigned_integer(std::uint8_t{5})
     * result: void
     * call: e.finish()
     * result: 5
     * call: out
     * result: 5 bytes: a1 61 61 18 05
     * @endcode
     *
     * @snippet doc_reference_item.cpp map
     * @see cbor::encoder::array()
     * @see cbor::encoder
     */
    std::expected<void, error> map(std::uint64_t const count)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_head(major_type::map, count));
    }

    /**
     * @brief Writes the head of a tag (major type 6) with the tag number `number`, with the shortest head.
     *
     * The next call writes the content.
     * The call does not check that the content fits the tag.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.tag(number) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: d9 d9 f7 18 01
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.tag(55799)
     * result: void
     * call: e.unsigned_integer(std::uint8_t{1})
     * result: void
     * call: e.finish()
     * result: 5
     * call: out
     * result: 5 bytes: d9 d9 f7 18 01
     * @endcode
     *
     * @snippet doc_reference_item.cpp tag
     * @see cbor::encoder::typed_array()
     * @see cbor::encoder
     */
    std::expected<void, error> tag(std::uint64_t const number)
    {
        throw_logic_error_if_unusable();
        return written(out.encode_head(major_type::tag, number));
    }

private:
    std::expected<void, error> shareable()
    {
        throw_logic_error_if_unusable();
        return written(out.encode_head(major_type::tag, std::to_underlying(rfc8949::tag_number::shareable)));
    }

    std::expected<void, error> sharedref(std::uint64_t const index)
    {
        throw_logic_error_if_unusable();
        return written(encoding::encode_sharedref(out, index));
    }

public:

    /**
     * @brief Writes a simple value (major type 7), for example `false_value`, `true_value`, `null` or `undefined`.
     *
     * A value from 0 to 23 takes one byte, and a value from 32 to 255 takes two.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     *
     * @code{.callseq}
     * e.simple_value(value) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: f6
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.simple_value(cbor::simple_value::null)
     * result: void
     * call: e.finish()
     * result: 1
     * call: out
     * result: 1 bytes: f6
     * call: std::string out2;
     * result: ""
     * call: cbor::encoder e2(out2);
     * result: encoder over a std::string
     * call: e2.simple_value(cbor::simple_value{16})
     * result: void
     * @endcode
     *
     * @retval reserved_simple_value `value` is from 24 to 31 (RFC 8949 3.3)
     * @snippet doc_reference_item.cpp simple_value
     * @see cbor::encoder::floating_point()
     * @see cbor::encoder
     */
    std::expected<void, error> simple_value(cbor::simple_value const value)
    {
        throw_logic_error_if_unusable();
        std::uint8_t const info = heads::preferred_argument_info(std::to_underlying(value));
        if (!detail::validity::check_simple_value(info, std::to_underlying(value))) [[unlikely]]
            return refused(error::reserved_simple_value);
        return written(out.encode_head(major_type::simple_float, info, std::to_underlying(value)));
    }

    /**
     * @brief Writes `value` as a float in the width of `T`: 16, 32 or 64 bits.
     *
     * It does not choose a shorter form.
     * A NaN keeps its bits.
     * `T` is an IEEE 754 type of 16, 32 or 64 bits.
     * Another float type does not compile.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     * A call after an error or after `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.floating_point(value) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: fa 3f c0 00 00
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.floating_point(1.5f)
     * result: void
     * call: e.finish()
     * result: 5
     * call: out
     * result: 5 bytes: fa 3f c0 00 00
     * @endcode
     *
     * @snippet doc_reference_item.cpp floating_point
     * @see cbor::encoder::simple_value()
     * @see cbor::encoder
     */
    template <std::floating_point T>
        requires(std::numeric_limits<T>::is_iec559 &&
                 (sizeof(T) == sizeof(std::uint32_t) || sizeof(T) == sizeof(std::uint64_t) ||
                  (sizeof(T) == sizeof(std::uint16_t) &&
                   std::numeric_limits<T>::digits == heads::half_precision.significand_bits + 1)))
    std::expected<void, error> floating_point(T const value)
    {
        throw_logic_error_if_unusable();
        if constexpr (sizeof(T) == sizeof(std::uint16_t))
            return written(out.encode_fixed_width_head(major_type::simple_float, std::bit_cast<std::uint16_t>(value)));
        else
            return written(out.encode_fixed_width_float(value));
    }

    /**
     * @brief Writes a typed array (RFC 8746): the tag `value.tag` and then `value.bytes` as a byte string.
     *
     * The bytes are copied as they are, in the byte order that the tag names.
     * `no_buffer_space` over a span that is too short.
     * The error of a writer.
     *
     * @code{.callseq}
     * e.typed_array(value) → std::expected<void, error>
     * @endcode
     * @code{.example}
     * bytes: d8 40 42 01 02
     * call: std::array<std::uint8_t, 2> two{1, 2};
     * result: {1, 2}
     * call: std::string out;
     * result: ""
     * call: cbor::encoder e(out);
     * result: encoder over a std::string
     * call: e.typed_array(cbor::typed_array{64, std::as_bytes(std::span(two))})
     * result: void
     * call: e.finish()
     * result: 5
     * call: out
     * result: 5 bytes: d8 40 42 01 02
     * call: std::string out2;
     * result: ""
     * call: cbor::encoder e2(out2);
     * result: encoder over a std::string
     * call: e2.typed_array(cbor::typed_array{68, std::as_bytes(std::span(two))})
     * result: void
     * @endcode
     *
     * @retval unsupported_value `value.tag` is not the tag of a typed array
     * @retval inadmissible_type_for_tag_content the count of bytes is not a multiple of the size of one element
     * @snippet doc_reference_item.cpp typed_array
     * @see cbor::encoder::tag()
     * @see cbor::encoder::byte_string()
     * @see cbor::encoder
     */
    std::expected<void, error> typed_array(cbor::typed_array const value)
    {
        throw_logic_error_if_unusable();
        if (auto const r = detail::validity::check_typed_array(value.tag, value.bytes.size()); !r)
            [[unlikely]]
            return refused(r.error() == error::incorrect_type ? error::unsupported_value : r.error());
        if (auto const r = written(out.encode_head(major_type::tag, value.tag)); !r) [[unlikely]]
            return r;
        return byte_string(value.bytes);
    }

    /**
     * @brief Ends the encoder.
     *
     * It writes the buffer of the encoder to the target and calls `done` of a writer.
     * It gives the count of bytes written, or the pieces over an cbor::iov_writer.
     * After the call, each call of the encoder throws `std::logic_error`.
     * `no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error`: the error of the target.
     * A second `finish` throws `std::logic_error`.
     *
     * @code{.callseq}
     * e.finish() → std::expected<std::size_t, error>
     * e.finish() → std::expected<ref<std::span<iovec const>>, error>
     * @endcode
     * @code{.example}
     * bytes: 63 61 62 63
     * call: std::array<char, 16> buffer{};
     * result: 16 bytes: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
     * call: cbor::encoder e{std::span<char>(buffer)};
     * result: encoder over a std::span<char>
     * call: e.text_string("abc")
     * result: void
     * call: e.finish()
     * result: 4
     * call: buffer
     * result: 16 bytes: 63 61 62 63 00 00 00 00 00 00 00 00 00 00 00 00
     * @endcode
     *
     * @snippet doc_reference_item.cpp finish
     * @see cbor::encoder
     * @see cbor::iov_writer
     */
    auto finish()
    {
        throw_logic_error_if_unusable();
        usable = false;
        if constexpr (pieces) {
            return std::expected<decltype(out.release()), error>(out.release());
        } else {
            if (auto const r = written(out.flush()); !r) [[unlikely]]
                return std::expected<std::size_t, error>(std::unexpected(r.error()));
            if (auto const r = written(message.done(out.written)); !r) [[unlikely]]
                return std::expected<std::size_t, error>(std::unexpected(r.error()));
            return std::expected<std::size_t, error>(out.written);
        }
    }
};

template <class Target>
encoder(Target &) -> encoder<Target>;

template <class Element>
encoder(std::span<Element>) -> encoder<std::span<Element>>;

inline encoding::head_parts encoding::head_of(item const &node)
{
    head_parts head{heads::major_type_of(node.type), node.additional_information, node.via.u64};
    switch (node.type) {
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
        if (node.via.tag.content->type == cbor::type::byte_string) {
            std::span<std::byte const> const magnitude =
                heads::magnitude_without_leading_zeros(node.via.tag.content->via.bin);
            if (detail::validity::check_magnitude_size(magnitude.size(), sizeof(std::uint64_t))) {
                head.major = node.type == cbor::type::negative_bignum ? major_type::negative_integer
                                                                      : major_type::unsigned_integer;
                head.argument = heads::magnitude_value(magnitude);
                head.info = heads::preferred_argument_info(head.argument);
                break;
            }
        }
        head.argument = node.via.tag.number;
        break;
    case cbor::type::tag:
    case cbor::type::typed_array:
        head.argument = node.via.tag.number;
        break;
    case cbor::type::shared_reference:
        head.argument = std::to_underlying(rfc8949::tag_number::sharedref);
        head.info = heads::preferred_argument_info(head.argument);
        break;
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        head.argument = std::to_underlying(node.via.simple_value);
        break;
    case cbor::type::half_float:
        head.info = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        head.argument = heads::encode_float(rfc8949::simple_float_information::half_precision_float, node.via.f64);
        break;
    case cbor::type::single_float:
        head.info = std::to_underlying(rfc8949::simple_float_information::single_precision_float);
        head.argument = heads::encode_float(rfc8949::simple_float_information::single_precision_float, node.via.f64);
        break;
    case cbor::type::double_float:
        head.info = std::to_underlying(rfc8949::simple_float_information::double_precision_float);
        head.argument = heads::encode_float(rfc8949::simple_float_information::double_precision_float, node.via.f64);
        break;
    case cbor::type::unsigned_integer:
    case cbor::type::negative_integer:
        break;
    }
    return head;
}

inline std::span<item const> encoding::children_of(item const &node, major_type const major)
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
    case major_type::simple_float:
        return {};
    }
    return {};
}

inline item const *encoding::content_of(item const &node, major_type const major)
{
    if (major != major_type::tag || node.type == cbor::type::shared_reference)
        return nullptr;
    return node.via.tag.content;
}

template <class Writer>
CBOR_ALWAYS_INLINE inline std::expected<void, std::errc> encoding::encode_head_and_payload(binary_writer<Writer> &out, item const &node,
                                                                 head_parts const &head)
{
    std::expected<void, std::errc> written = out.encode_head(head.major, head.info, head.argument);
    if (written && head.major == major_type::byte_string)
        written = out.encode_payload(node.via.bin);
    else if (written && head.major == major_type::text_string)
        written = out.encode_payload(std::span(node.via.str));
    return written;
}

template <class Writer>
std::expected<void, std::errc> encoding::encode_sharedref(binary_writer<Writer> &out, std::uint64_t const index)
{
    std::expected<void, std::errc> const written =
        out.encode_head(major_type::tag, std::to_underlying(rfc8949::tag_number::sharedref));
    if (!written) [[unlikely]]
        return written;
    return out.encode_head(major_type::unsigned_integer, index);
}

template <class Writer>
std::expected<void, error> encoding::encode_item(binary_writer<Writer> &out, item const &node)
{
    if (auto const r = detail::validity::check_recursion_limit(); !r) [[unlikely]]
        return r;
    if (node.type == cbor::type::shared_reference) [[unlikely]]
        return std::unexpected(error::unsupported_value);
    head_parts const head = head_of(node);
    std::expected<void, std::errc> const written = encode_head_and_payload(out, node, head);
    if (!written) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(written.error()));
    for (item const &child : children_of(node, head.major))
        if (auto const r = encode_item(out, child); !r) [[unlikely]]
            return r;
    if (item const *const content = content_of(node, head.major))
        return encode_item(out, *content);
    return {};
}

inline std::uint64_t encoding::size_of(item const &node, head_parts const &head)
{
    std::uint64_t const size = heads::initial_byte_size + heads::argument_size(head.info);
    if (head.major == major_type::byte_string)
        return size + node.via.bin.size();
    if (head.major == major_type::text_string)
        return size + node.via.str.size();
    return size;
}

inline std::uint64_t encoding::reference_size_of(item const &reference)
{
    return heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) + heads::initial_byte_size +
           heads::argument_size(reference.additional_information);
}

class encoding::depth_first_search
{
    struct frame {
        item const *node;
        std::uint64_t index;
        frame const *up;
    };

    static constexpr std::uint64_t back_edge_size =
        heads::head_size(std::to_underlying(rfc8949::tag_number::sharedref)) + heads::initial_byte_size;

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
        bool const reference = child.type == cbor::type::shared_reference;
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

    template <class Writer>
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
        std::expected<void, std::errc> const written = encoding::encode_head_and_payload(out, node, head);
        if (!written) [[unlikely]]
            return std::unexpected(detail::validity::writer_error(written.error()));
        frame const here{&node, index, up};
        if (path_in_map) [[unlikely]]
            open.emplace(&node, index);
        for (item const &child : children_of(node, head.major)) {
            auto const r = write_edge(out, child, here, marks);
            if (!r) [[unlikely]]
                return r;
            marks = *r;
        }
        if (item const *const content = content_of(node, head.major)) {
            auto const r = write_edge(out, *content, here, marks);
            if (!r) [[unlikely]]
                return r;
            marks = *r;
        }
        if (path_in_map) [[unlikely]]
            open.erase(&node);
        return marks;
    }

    template <class Writer>
    std::expected<std::uint64_t, error> write_edge(binary_writer<Writer> &out, item const &child, frame const &here,
                                                   std::uint64_t const marks)
    {
        bool const reference = child.type == cbor::type::shared_reference;
        item const &next = reference ? *child.via.tag.content : child;
        if (reference && !path_in_map) [[unlikely]]
            enter_path(here);
        if (path_in_map) [[unlikely]] {
            if (auto const at = open.find(&next); at != open.end()) {
                std::expected<void, std::errc> const written = encoding::encode_sharedref(out, at->second);
                if (!written) [[unlikely]]
                    return std::unexpected(detail::validity::writer_error(written.error()));
                return marks;
            }
        }
        return write(out, next, &here, marks);
    }

public:
    template <class Writer>
    std::expected<void, error> encode(binary_writer<Writer> &out, item const &root)
    {
        item const &start = root.type == cbor::type::shared_reference ? *root.via.tag.content : root;
        if (auto const r = find_back_edges(start, nullptr, false); !r) [[unlikely]]
            return r;
        std::ranges::sort(targets);
        auto const repeated = std::ranges::unique(targets);
        targets.erase(repeated.begin(), repeated.end());
        open.clear();
        path_in_map = false;
        if (auto const r = write(out, start, nullptr, 0); !r) [[unlikely]]
            return std::unexpected(r.error());
        return {};
    }
};

template <bool Cycles, class Writer>
std::expected<void, error> encoding::encode_tree(binary_writer<Writer> &out, item const &root)
{
    if constexpr (Cycles) {
        depth_first_search walk;
        return walk.encode(out, root);
    } else {
        return encode_item(out, root);
    }
}

template <bool Cycles, class Target>
std::expected<std::size_t, error> encoding::encode_into(item const &root, Target &&target)
{
    decltype(auto) message = message_of(target, 0);
    binary_writer<std::remove_reference_t<decltype(message)>> out(message);
    if (auto const r = encode_tree<Cycles>(out, root); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = out.flush(); !r) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(r.error()));
    if (auto const r = message.done(out.written); !r) [[unlikely]]
        return std::unexpected(detail::validity::writer_error(r.error()));
    return out.written;
}

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target)
{
    return encoding::encode_into<false>(root, std::forward<Target>(target));
}

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target, cycles_t)
{
    return encoding::encode_into<true>(root, std::forward<Target>(target));
}

#if __has_include(<sys/uio.h>)
template <bool Cycles>
std::expected<ref<std::span<iovec const>>, error> encoding::encode_iov_of(item const &root)
{
    iov_writer writer;
    binary_writer<iov_writer> out(writer);
    if (auto const r = encode_tree<Cycles>(out, root); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out.release();
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root)
{
    return encoding::encode_iov_of<false>(root);
}

inline std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root, cycles_t)
{
    return encoding::encode_iov_of<true>(root);
}
#endif
}

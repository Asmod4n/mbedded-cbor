// Writes CBOR into a span, a byte container or a writer.
// cbor::encoder writes one head or one value for each call.
// cbor::encode and cbor::encode_iov write a tree of cbor::item.
// https://www.rfc-editor.org/rfc/rfc8949#section-3
// https://www.rfc-editor.org/rfc/rfc8746#section-2
#pragma once

#ifndef CBOR_ENCODER_BLOCK_SIZE
#define CBOR_ENCODER_BLOCK_SIZE 16384
#endif

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

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target);

#if __has_include(<sys/uio.h>)
std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);
#endif

class encoding
{
public:
    static constexpr std::size_t block_size = CBOR_ENCODER_BLOCK_SIZE;
    static constexpr std::size_t item_size_max = heads::initial_byte_size + sizeof(std::uint64_t);
    static constexpr std::size_t ref_size = 32;
    static_assert(block_size >= item_size_max, "A block must hold one whole head or float.");

private:
    template <class Writer>
    struct encoder;

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
            if (part.size() > out.size() - used) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
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

    template <class Writer>
    static std::expected<void, error> item_encode(encoder<Writer> &out, item const &node, std::size_t depth,
                                                  std::size_t depth_max);

    template <class Target>
    friend std::expected<std::size_t, error> encode(item const &root, Target &&target);

#if __has_include(<sys/uio.h>)
    friend std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);
#endif

    template <class>
    friend class cbor::encoder;

    template <class>
    friend class structs;

    template <class>
    friend class schema;
};

template <class Writer>
struct encoding::encoder {
    static constexpr bool direct = std::same_as<Writer, encoding::span_message<char>>;
    static constexpr bool blocks = requires(Writer &w, std::size_t const n) {
        { w.block_next(n) } -> std::same_as<std::span<char>>;
    };

    Writer &writer;
    std::conditional_t<direct || blocks, std::span<char>, std::array<char, block_size>> block;
    std::size_t used = 0;
    std::size_t closed = 0;
    std::size_t written = 0;

    explicit encoder(Writer &w) : writer(w)
    {
        if constexpr (direct) {
            block = w.out;
            used = w.used;
        } else if constexpr (blocks) {
            block_take(item_size_max);
        }
    }

    void block_take(std::size_t const needed)
        requires blocks
    {
        block = writer.block_next(std::max(needed, block_size));
        validity::throw_logic_error_if(block.size() < needed,
                                       "cbor::encoder: the writer gave a block shorter than the item");
        used = 0;
        closed = 0;
    }

    std::expected<void, std::errc> refill(std::size_t const needed)
    {
        if constexpr (blocks) {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            block_take(needed);
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
            writer.block_close(part);
            return {};
        } else {
            std::size_t const size = used;
            used = 0;
            written += size;
            return writer.append(std::string_view(std::span(block).first(size)));
        }
    }

    std::expected<void, std::errc>
    item_write(std::array<char, heads::initial_byte_size + sizeof(std::uint64_t)> const &item,
               std::size_t const size)
    {
        if (block.size() - used < item.size()) [[unlikely]] {
            if constexpr (direct) {
                if (block.size() - used < size) [[unlikely]]
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

    std::expected<void, std::errc> head_encode(major_type const major, std::uint8_t const info, std::uint64_t const argument)
    {
        std::size_t const bytes = heads::argument_size(info);
        std::uint64_t const big =
            std::byteswap(argument << ((std::numeric_limits<std::uint64_t>::digits -
                                        std::numeric_limits<std::uint8_t>::digits * bytes) &
                                       (std::numeric_limits<std::uint64_t>::digits - 1)));
        if constexpr (!direct) {
            if (block.size() - used < heads::initial_byte_size + sizeof(std::uint64_t)) [[unlikely]]
                if (auto const r = refill(item_size_max); !r) [[unlikely]]
                    return r;
            std::array<char, heads::initial_byte_size + sizeof(std::uint64_t)> head;
            static_assert(heads::initial_byte_size + sizeof big <= std::tuple_size_v<decltype(head)>,
                          "A head must hold the initial byte and the eight argument bytes.");
            std::get<0>(head) = heads::initial_byte(major, info);
            std::ranges::copy(std::bit_cast<std::array<char, sizeof big>>(big),
                              std::span(head).template subspan<heads::initial_byte_size>().begin());
            std::ranges::copy(head, std::span(block).subspan(used).begin());
            used += heads::initial_byte_size + bytes;
            return {};
        } else {
            std::span<char> const out = block;
            std::size_t const at = used;
            std::array<char, heads::initial_byte_size + sizeof(std::uint64_t)> tail;
            static_assert(heads::initial_byte_size + sizeof big <= std::tuple_size_v<decltype(tail)>,
                          "A head must hold the initial byte and the eight argument bytes.");
            bool const near_end = out.size() - at < tail.size();
            std::span<char, heads::initial_byte_size + sizeof(std::uint64_t)> const item =
                near_end ? std::span(tail)
                         : out.subspan(at).template first<heads::initial_byte_size + sizeof(std::uint64_t)>();
            item.front() = heads::initial_byte(major, info);
            std::ranges::copy(std::bit_cast<std::array<char, sizeof big>>(big),
                              item.template subspan<heads::initial_byte_size>().begin());
            if (near_end) [[unlikely]]
                return item_write(tail, heads::initial_byte_size + bytes);
            used = at + heads::initial_byte_size + bytes;
            return {};
        }
    }

    template <std::unsigned_integral T>
    CBOR_ALWAYS_INLINE void item_put(char const initial, T const argument)
    {
        std::span<char> const out = std::span<char>(block).subspan(used);
        out.front() = initial;
        std::ranges::copy(std::bit_cast<std::array<char, sizeof(T)>>(std::byteswap(argument)),
                          out.subspan(heads::initial_byte_size).begin());
        used += heads::initial_byte_size + sizeof(T);
    }

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> head_encode(major_type const major, std::uint64_t const argument)
    {
        using info = rfc8949::additional_information;
        if (block.size() - used >= item_size_max) [[likely]] {
            if (argument < std::to_underlying(info::one_byte_argument)) {
                std::span<char> const out = block;
                out[used] = heads::initial_byte(major, static_cast<std::uint8_t>(argument));
                used += heads::initial_byte_size;
            } else if (argument <= std::numeric_limits<std::uint8_t>::max()) {
                item_put(heads::initial_byte(major, std::to_underlying(info::one_byte_argument)),
                         static_cast<std::uint8_t>(argument));
            } else if (argument <= std::numeric_limits<std::uint16_t>::max()) {
                item_put(heads::initial_byte(major, std::to_underlying(info::two_byte_argument)),
                         static_cast<std::uint16_t>(argument));
            } else if (argument <= std::numeric_limits<std::uint32_t>::max()) {
                item_put(heads::initial_byte(major, std::to_underlying(info::four_byte_argument)),
                         static_cast<std::uint32_t>(argument));
            } else {
                item_put(heads::initial_byte(major, std::to_underlying(info::eight_byte_argument)), argument);
            }
            return {};
        }
        return head_encode(major, heads::preferred_argument_info(argument), argument);
    }

    template <class E>
    static void copy_into(std::span<E const> const from, std::span<char> const to)
    {
        std::ranges::copy(std::as_bytes(from), std::as_writable_bytes(to.first(from.size())).begin());
    }

    std::expected<void, std::errc> byte_string_encode(std::span<std::byte const> const bytes)
    {
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        return payload_encode(bytes);
    }

    template <class E>
        requires std::same_as<E, char> || std::same_as<E, std::byte>
    std::expected<void, std::errc> payload_encode(std::span<E const> const bytes)
    {
        std::span<char> const out = block;
        std::size_t const at = used;
        if (bytes.size() <= out.size() - at) {
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

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> text_string_encode(std::string_view text)
    {
        if (auto const r = head_encode(major_type::text_string, text.size()); !r) [[unlikely]]
            return r;
        std::span<char> const out = block;
        std::size_t const at = used;
        if (text.size() <= out.size() - at) {
            std::ranges::copy(text, out.subspan(at).begin());
            used = at + text.size();
            return {};
        }
        if constexpr (direct) {
            return std::unexpected(std::errc::no_buffer_space);
        } else if constexpr (blocks) {
            std::size_t const first = out.size() - at;
            std::ranges::copy(text.substr(0, first), out.subspan(at).begin());
            used = out.size();
            std::string_view const rest = text.substr(first);
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
    std::expected<void, std::errc> text_string_encode(S &&text)
    {
        if constexpr (blocks)
            if (text.size() >= ref_size) {
                auto const owner = std::make_shared<std::string const>(std::move(text));
                return piece_encode(major_type::text_string, std::as_bytes(std::span(*owner)), owner);
            }
        return text_string_encode(std::string_view(text));
    }

    std::expected<void, std::errc> text_string_encode(std::shared_ptr<std::string const> const text)
    {
        validity::throw_logic_error_if_null(text, "cbor::encoder::text_string_encode: the string is null");
        if constexpr (blocks)
            if (text->size() >= ref_size)
                return piece_encode(major_type::text_string, std::as_bytes(std::span(*text)), text);
        return text_string_encode(std::string_view(*text));
    }

    std::expected<void, std::errc> text_string_encode(ref<std::string_view> const text)
    {
        if constexpr (blocks)
            if ((*text).size() >= ref_size)
                return piece_encode(major_type::text_string, std::as_bytes(std::span(*text)), text.owner);
        return text_string_encode(*text);
    }

    template <class V>
        requires std::same_as<V, std::vector<std::byte>>
    std::expected<void, std::errc> byte_string_encode(V &&bytes)
    {
        if constexpr (blocks)
            if (bytes.size() >= ref_size) {
                auto const owner = std::make_shared<std::vector<std::byte> const>(std::move(bytes));
                return piece_encode(major_type::byte_string, std::span(*owner), owner);
            }
        return byte_string_encode(std::span<std::byte const>(bytes));
    }

    std::expected<void, std::errc> byte_string_encode(std::shared_ptr<std::vector<std::byte> const> const bytes)
    {
        validity::throw_logic_error_if_null(bytes, "cbor::encoder::byte_string_encode: the bytes are null");
        if constexpr (blocks)
            if (bytes->size() >= ref_size)
                return piece_encode(major_type::byte_string, std::span(*bytes), bytes);
        return byte_string_encode(std::span<std::byte const>(*bytes));
    }

    std::expected<void, std::errc> byte_string_encode(ref<std::span<std::byte const>> const bytes)
    {
        if constexpr (blocks)
            if ((*bytes).size() >= ref_size)
                return piece_encode(major_type::byte_string, *bytes, bytes.owner);
        return byte_string_encode(*bytes);
    }

    std::expected<void, std::errc> piece_encode(major_type const major, std::span<std::byte const> const piece,
                                                std::shared_ptr<void const> owner)
        requires blocks
    {
        if (auto const r = head_encode(major, piece.size()); !r) [[unlikely]]
            return r;
        if (auto const r = flush(); !r) [[unlikely]]
            return r;
        written += piece.size();
        writer.piece_reference(piece, std::move(owner));
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
    CBOR_ALWAYS_INLINE std::expected<void, std::errc> fixed_width_head_encode(major_type const major, T const argument)
    {
        if (block.size() - used >= item_size_max) [[likely]] {
            item_put(heads::initial_byte(major, static_cast<std::uint8_t>(
                                                    std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                                    std::countr_zero(sizeof(T)))),
                     argument);
            return {};
        }
        return head_encode(major,
                           std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                               std::countr_zero(sizeof(T)),
                           argument);
    }

    template <std::floating_point T>
        requires(sizeof(T) == sizeof(std::uint32_t) || sizeof(T) == sizeof(std::uint64_t))
    std::expected<void, std::errc> fixed_width_float_encode(T value)
    {
        return fixed_width_head_encode(
            major_type::simple_float,
            std::bit_cast<
                std::conditional_t<sizeof(T) == sizeof(std::uint32_t), std::uint32_t, std::uint64_t>>(value));
    }
};

#if __has_include(<sys/uio.h>)
class iov_writer
{
    struct storage {
        std::vector<std::vector<char>> blocks;
        std::vector<std::shared_ptr<void const>> owners;
        std::vector<iovec> pieces;
    };

    std::shared_ptr<storage> kept = std::make_shared<storage>();
    bool released = false;

    std::span<char> block_next(std::size_t const size)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->blocks.emplace_back(size);
        return std::span(kept->blocks.back());
    }

    void piece_push(void *const base, std::size_t const size)
    {
        iovec piece{};
        piece.iov_base = base;
        piece.iov_len = size;
        kept->pieces.push_back(piece);
    }

    void block_close(std::span<char> const part)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        if (part.empty())
            return;
        piece_push(part.data(), part.size());
    }

    void piece_reference(std::span<std::byte const> const piece, std::shared_ptr<void const> owner)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->owners.push_back(std::move(owner));
        piece_push(const_cast<std::byte *>(piece.data()), piece.size());
    }

    ref<std::span<iovec const>> release()
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a second release");
        released = true;
        std::span<iovec const> const pieces = kept->pieces;
        return {std::move(kept), pieces};
    }

    friend class encoding;
};
#endif

template <class Target>
class encoder
{
    static constexpr bool pieces = std::same_as<Target, iov_writer>;

    using message_type = decltype(encoding::message_of(std::declval<Target &>(), 0));

    message_type message;
    encoding::encoder<std::remove_reference_t<message_type>> out;
    bool usable = true;

    void throw_logic_error_if_unusable() const
    {
        validity::throw_logic_error_if(!usable, "cbor::encoder: a call after an error or after finish");
    }

    std::expected<void, error> written(std::expected<void, std::errc> const r)
    {
        if (!r) [[unlikely]] {
            usable = false;
            return std::unexpected(validity::writer_error(r.error()));
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

    template <class T>
        requires integer_argument<T>
    std::expected<void, error> unsigned_integer(T const value)
    {
        throw_logic_error_if_unusable();
        return written(out.fixed_width_head_encode(major_type::unsigned_integer, value));
    }

    template <class T>
        requires integer_argument<T>
    std::expected<void, error> negative_integer(T const argument)
    {
        throw_logic_error_if_unusable();
        return written(out.fixed_width_head_encode(major_type::negative_integer, argument));
    }

    std::expected<void, error> byte_string(std::span<std::byte const> const bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.byte_string_encode(bytes));
    }

    template <class V>
        requires std::same_as<V, std::vector<std::byte>>
    std::expected<void, error> byte_string(V &&bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.byte_string_encode(std::move(bytes)));
    }

    std::expected<void, error> byte_string(std::shared_ptr<std::vector<std::byte> const> const &bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.byte_string_encode(bytes));
    }

    std::expected<void, error> byte_string(ref<std::span<std::byte const>> const &bytes)
    {
        throw_logic_error_if_unusable();
        return written(out.byte_string_encode(bytes));
    }

    std::expected<void, error> text_string(std::string_view const text)
    {
        throw_logic_error_if_unusable();
        return written(out.text_string_encode(text));
    }

    template <class S>
        requires std::same_as<S, std::string>
    std::expected<void, error> text_string(S &&text)
    {
        throw_logic_error_if_unusable();
        return written(out.text_string_encode(std::move(text)));
    }

    std::expected<void, error> text_string(std::shared_ptr<std::string const> const &text)
    {
        throw_logic_error_if_unusable();
        return written(out.text_string_encode(text));
    }

    std::expected<void, error> text_string(ref<std::string_view> const &text)
    {
        throw_logic_error_if_unusable();
        return written(out.text_string_encode(text));
    }

    std::expected<void, error> array(std::uint64_t const count)
    {
        throw_logic_error_if_unusable();
        return written(out.head_encode(major_type::array, count));
    }

    std::expected<void, error> map(std::uint64_t const count)
    {
        throw_logic_error_if_unusable();
        return written(out.head_encode(major_type::map, count));
    }

    std::expected<void, error> tag(std::uint64_t const number)
    {
        throw_logic_error_if_unusable();
        return written(out.head_encode(major_type::tag, number));
    }

private:
    std::expected<void, error> shareable()
    {
        throw_logic_error_if_unusable();
        return written(out.head_encode(major_type::tag, std::to_underlying(rfc8949::tag_number::shareable)));
    }

    std::expected<void, error> sharedref(std::uint64_t const index)
    {
        throw_logic_error_if_unusable();
        if (auto const r = written(out.head_encode(major_type::tag, std::to_underlying(rfc8949::tag_number::sharedref)));
            !r) [[unlikely]]
            return r;
        return written(out.head_encode(major_type::unsigned_integer, index));
    }

public:

    std::expected<void, error> simple_value(cbor::simple_value const value)
    {
        throw_logic_error_if_unusable();
        std::uint8_t const info = heads::preferred_argument_info(std::to_underlying(value));
        if (!validity::check_simple_value(info, std::to_underlying(value))) [[unlikely]]
            return refused(error::reserved_simple_value);
        return written(out.head_encode(major_type::simple_float, info, std::to_underlying(value)));
    }

    template <std::floating_point T>
        requires(std::numeric_limits<T>::is_iec559 &&
                 (sizeof(T) == sizeof(std::uint32_t) || sizeof(T) == sizeof(std::uint64_t) ||
                  (sizeof(T) == sizeof(std::uint16_t) &&
                   std::numeric_limits<T>::digits == heads::half_precision.significand_bits + 1)))
    std::expected<void, error> floating_point(T const value)
    {
        throw_logic_error_if_unusable();
        if constexpr (sizeof(T) == sizeof(std::uint16_t))
            return written(out.fixed_width_head_encode(major_type::simple_float, std::bit_cast<std::uint16_t>(value)));
        else
            return written(out.fixed_width_float_encode(value));
    }

    std::expected<void, error> typed_array(cbor::typed_array const value)
    {
        throw_logic_error_if_unusable();
        if (auto const r = validity::typed_array_check(value.tag, value.bytes.size()); !r) [[unlikely]]
            return refused(r.error() == error::incorrect_type ? error::unsupported_value : r.error());
        if (auto const r = written(out.head_encode(major_type::tag, value.tag)); !r) [[unlikely]]
            return r;
        return byte_string(value.bytes);
    }

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

template <class Writer>
std::expected<void, error> encoding::item_encode(encoder<Writer> &out, item const &node, std::size_t const depth,
                                                 std::size_t const depth_max)
{
    if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]]
        return r;
    major_type const major = heads::major_type_of(node.type);
    std::uint64_t argument = node.via.u64;
    std::uint8_t info = node.additional_information;
    switch (node.type) {
    case cbor::type::byte_string:
        argument = node.via.bin.size();
        break;
    case cbor::type::text_string:
        argument = node.via.str.size();
        break;
    case cbor::type::array:
        argument = node.via.array.size();
        break;
    case cbor::type::map:
        validity::throw_logic_error_if(node.via.map.size() % rfc8949::data_items_per_pair != 0,
                                       "cbor::encode: a map holds a key and a value for each entry");
        argument = node.via.map.size() / rfc8949::data_items_per_pair;
        break;
    case cbor::type::unsigned_bignum:
    case cbor::type::negative_bignum:
        if (node.via.tag.content->type == cbor::type::byte_string) {
            std::span<std::byte const> const magnitude =
                heads::magnitude_without_leading_zeros(node.via.tag.content->via.bin);
            if (validity::check_magnitude_size(magnitude.size(), sizeof(std::uint64_t))) {
                std::expected<void, std::errc> const fits =
                    out.head_encode(node.type == cbor::type::negative_bignum ? major_type::negative_integer
                                                                            : major_type::unsigned_integer,
                                    heads::magnitude_value(magnitude));
                if (!fits) [[unlikely]]
                    return std::unexpected(validity::writer_error(fits.error()));
                return {};
            }
        }
        argument = node.via.tag.number;
        break;
    case cbor::type::tag:
    case cbor::type::typed_array:
        argument = node.via.tag.number;
        break;
    case cbor::type::false_value:
    case cbor::type::true_value:
    case cbor::type::null:
    case cbor::type::undefined:
    case cbor::type::simple_value:
        argument = std::to_underlying(node.via.simple_value);
        break;
    case cbor::type::half_float:
        info = std::to_underlying(rfc8949::simple_float_information::half_precision_float);
        argument = heads::float_encode(rfc8949::simple_float_information::half_precision_float, node.via.f64);
        break;
    case cbor::type::single_float:
        info = std::to_underlying(rfc8949::simple_float_information::single_precision_float);
        argument = heads::float_encode(rfc8949::simple_float_information::single_precision_float, node.via.f64);
        break;
    case cbor::type::double_float:
        info = std::to_underlying(rfc8949::simple_float_information::double_precision_float);
        argument = heads::float_encode(rfc8949::simple_float_information::double_precision_float, node.via.f64);
        break;
    case cbor::type::unsigned_integer:
    case cbor::type::negative_integer:
        break;
    }
    std::expected<void, std::errc> written = out.head_encode(major, info, argument);
    if (!written) [[unlikely]]
        return std::unexpected(validity::writer_error(written.error()));
    switch (major) {
    case major_type::byte_string:
        written = out.payload_encode(node.via.bin);
        break;
    case major_type::text_string:
        written = out.payload_encode(std::span(node.via.str));
        break;
    case major_type::array:
    case major_type::map:
        for (item const &child : major == major_type::array ? node.via.array : node.via.map)
            if (auto const r = item_encode(out, child, depth + 1, depth_max); !r) [[unlikely]]
                return r;
        break;
    case major_type::tag:
        return item_encode(out, *node.via.tag.content, depth + 1, depth_max);
    case major_type::unsigned_integer:
    case major_type::negative_integer:
    case major_type::simple_float:
        break;
    }
    if (!written) [[unlikely]]
        return std::unexpected(validity::writer_error(written.error()));
    return {};
}

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target)
{
    decltype(auto) message = encoding::message_of(target, 0);
    encoding::encoder<std::remove_reference_t<decltype(message)>> out(message);
    if (auto const r = encoding::item_encode(out, root, 0, limits::nesting_depth); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = out.flush(); !r) [[unlikely]]
        return std::unexpected(validity::writer_error(r.error()));
    if (auto const r = message.done(out.written); !r) [[unlikely]]
        return std::unexpected(validity::writer_error(r.error()));
    return out.written;
}

#if __has_include(<sys/uio.h>)
inline std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root)
{
    iov_writer writer;
    encoding::encoder<iov_writer> out(writer);
    if (auto const r = encoding::item_encode(out, root, 0, limits::nesting_depth); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out.release();
}
#endif
}

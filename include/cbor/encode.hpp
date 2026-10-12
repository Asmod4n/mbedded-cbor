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

#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "item.hpp"
#include "owning_ref.hpp"

namespace cbor
{

template <class Target>
class encoder;

class iov_writer;

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target);

std::expected<owning_ref<std::span<std::string_view const>>, error> encode_iov(item const &root);

class encoding
{
public:
    template <class Writer>
    struct encoder;

private:
    struct string_sink {
        std::string encoded;

        std::expected<void, std::errc> append(std::string_view const part)
        {
            encoded.append(part);
            return {};
        }

        std::expected<void, std::errc> done(std::size_t)
        {
            return {};
        }

        template <class Op>
        std::expected<void, std::errc> resize_and_overwrite(std::size_t const size, Op op)
        {
            std::size_t const at = encoded.size();
            encoded.resize_and_overwrite(at + size, [&](char *const p, std::size_t const n) {
                return at + op(std::span<char>(p, n).subspan(at, size));
            });
            return {};
        }
    };

    template <class C>
    struct container_message {
        C &container;

        std::expected<void, std::errc> append(std::string_view const part)
        {
            auto const at = std::ranges::ssize(container);
            container.resize(std::ranges::size(container) + part.size());
            if constexpr (std::same_as<std::ranges::range_value_t<C>, char>)
                std::ranges::copy(part, std::ranges::next(std::ranges::begin(container), at));
            else
                std::ranges::transform(part, std::ranges::next(std::ranges::begin(container), at),
                                       [](char const c) { return static_cast<std::ranges::range_value_t<C>>(c); });
            return {};
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

        std::expected<void, std::errc> append(std::string_view const part)
        {
            if (part.size() > out.size() - used) [[unlikely]]
                return std::unexpected(std::errc::no_buffer_space);
            std::ranges::copy(std::as_bytes(std::span(part)), std::as_writable_bytes(out.subspan(used)).begin());
            used += part.size();
            return {};
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

    friend std::expected<owning_ref<std::span<std::string_view const>>, error> encode_iov(item const &root);

    template <class>
    friend class cbor::encoder;

#ifdef __cpp_impl_reflection
    template <class>
    friend class packed;

    template <class>
    friend class structs;
#endif
};

template <class Writer>
struct encoding::encoder {
    static constexpr bool direct = std::same_as<Writer, encoding::span_message<char>>;
    static constexpr bool blocks = requires(Writer &w, std::size_t const n) {
        { w.block_next(n) } -> std::same_as<std::span<char>>;
    };
    static constexpr std::size_t block_size = 16384;
    static constexpr std::size_t item_size_max = heads::initial_byte_size + sizeof(std::uint64_t);
    static constexpr std::size_t ref_size = 32;
    static_assert(block_size >= item_size_max, "A block must hold one whole head or float.");

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
            std::string_view const part(block.data() + closed, size);
            closed = used;
            written += size;
            writer.block_close(part);
            return {};
        } else {
            std::size_t const size = used;
            used = 0;
            written += size;
            return writer.append(std::string_view(block.data(), size));
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

    std::expected<void, std::errc> byte_string_encode(std::string_view const bytes)
    {
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
        return payload_encode(bytes);
    }

    std::expected<void, std::errc> payload_encode(std::string_view const bytes)
    {
        std::span<char> const out = block;
        std::size_t const at = used;
        if (bytes.size() <= out.size() - at) {
            std::ranges::copy(bytes, out.subspan(at).begin());
            used = at + bytes.size();
            return {};
        }
        if constexpr (direct) {
            return std::unexpected(std::errc::no_buffer_space);
        } else if constexpr (blocks) {
            std::size_t const first = out.size() - at;
            std::ranges::copy(bytes.substr(0, first), out.subspan(at).begin());
            used = out.size();
            std::string_view const rest = bytes.substr(first);
            if (auto const r = refill(rest.size()); !r) [[unlikely]]
                return r;
            std::ranges::copy(rest, block.begin());
            used = rest.size();
            return {};
        } else {
            if (auto const r = flush(); !r) [[unlikely]]
                return r;
            written += bytes.size();
            return writer.append(bytes);
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
                return piece_encode(major_type::text_string, *owner, owner);
            }
        return text_string_encode(std::string_view(text));
    }

    std::expected<void, std::errc> text_string_encode(std::shared_ptr<std::string const> const text)
    {
        validity::throw_logic_error_if_null(text, "cbor::encoder::text_string_encode: the string is null");
        if constexpr (blocks)
            if (text->size() >= ref_size)
                return piece_encode(major_type::text_string, *text, text);
        return text_string_encode(std::string_view(*text));
    }

    std::expected<void, std::errc> text_string_encode(owning_ref<std::string_view> const text)
    {
        if constexpr (blocks)
            if ((*text).size() >= ref_size)
                return piece_encode(major_type::text_string, *text, text.owner);
        return text_string_encode(*text);
    }

    template <class V>
        requires std::same_as<V, std::vector<std::byte>>
    std::expected<void, std::errc> byte_string_encode(V &&bytes)
    {
        if constexpr (blocks)
            if (bytes.size() >= ref_size) {
                auto const owner = std::make_shared<std::vector<std::byte> const>(std::move(bytes));
                return piece_encode(major_type::byte_string, chars_of(*owner), owner);
            }
        return byte_string_encode(chars_of(bytes));
    }

    std::expected<void, std::errc> byte_string_encode(std::shared_ptr<std::vector<std::byte> const> const bytes)
    {
        validity::throw_logic_error_if_null(bytes, "cbor::encoder::byte_string_encode: the bytes are null");
        if constexpr (blocks)
            if (bytes->size() >= ref_size)
                return piece_encode(major_type::byte_string, chars_of(*bytes), bytes);
        return byte_string_encode(chars_of(*bytes));
    }

    std::expected<void, std::errc> byte_string_encode(owning_ref<std::span<std::byte const>> const bytes)
    {
        if constexpr (blocks)
            if ((*bytes).size() >= ref_size)
                return piece_encode(major_type::byte_string, chars_of(*bytes), bytes.owner);
        return byte_string_encode(chars_of(*bytes));
    }

    static std::string_view chars_of(std::span<std::byte const> const bytes)
    {
        return {reinterpret_cast<char const *>(bytes.data()), bytes.size()};
    }

    std::expected<void, std::errc> piece_encode(major_type const major, std::string_view const piece,
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

    owning_ref<std::span<std::string_view const>> release()
        requires blocks
    {
        std::ignore = flush();
        block = {};
        used = 0;
        closed = 0;
        return writer.release();
    }

    CBOR_ALWAYS_INLINE std::expected<void, std::errc> float_encode(double const value)
    {
        if (block.size() - used >= item_size_max) [[likely]] {
            item_put(heads::initial_byte(major_type::simple_float,
                                         std::to_underlying(rfc8949::simple_float_information::double_precision_float)),
                     std::bit_cast<std::uint64_t>(value));
            return {};
        }
        return fixed_width_float_encode(value);
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

    std::expected<void, std::errc> simple_value_encode(simple_value const value)
    {
        std::uint8_t const info = heads::preferred_argument_info(std::to_underlying(value));
        if (!validity::check_simple_value(info, std::to_underlying(value))) [[unlikely]]
            return std::unexpected(std::errc::invalid_argument);
        return head_encode(major_type::simple_float, info, std::to_underlying(value));
    }

    template <std::unsigned_integral T>
        requires(!std::is_same_v<T, bool>)
    std::expected<void, std::errc> fixed_width_unsigned_encode(T value)
    {
        return fixed_width_head_encode(major_type::unsigned_integer, value);
    }

    template <std::signed_integral T>
    std::expected<void, std::errc> fixed_width_signed_encode(T value)
    {
        using U = std::make_unsigned_t<T>;
        U const sign = static_cast<U>(value >> std::numeric_limits<T>::digits);
        return fixed_width_head_encode(static_cast<major_type>(sign & 1), static_cast<U>(static_cast<U>(value) ^ sign));
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

class iov_writer
{
    struct storage {
        std::vector<std::unique_ptr<char[]>> blocks;
        std::vector<std::shared_ptr<void const>> owners;
        std::vector<std::string_view> pieces;
    };

    std::shared_ptr<storage> kept = std::make_shared<storage>();
    bool piece_open = false;
    bool released = false;

    std::span<char> block_next(std::size_t const size)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->blocks.push_back(std::make_unique_for_overwrite<char[]>(size));
        piece_open = false;
        return {kept->blocks.back().get(), size};
    }

    void block_close(std::string_view const part)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        if (part.empty())
            return;
        if (piece_open)
            kept->pieces.back() = std::string_view(kept->pieces.back().data(), kept->pieces.back().size() + part.size());
        else
            kept->pieces.push_back(part);
        piece_open = true;
    }

    void piece_reference(std::string_view const piece, std::shared_ptr<void const> owner)
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a write after release");
        kept->owners.push_back(std::move(owner));
        kept->pieces.push_back(piece);
        piece_open = false;
    }

    owning_ref<std::span<std::string_view const>> release()
    {
        validity::throw_logic_error_if(released, "cbor::iov_writer: a second release");
        released = true;
        std::span<std::string_view const> const pieces = kept->pieces;
        return {std::move(kept), pieces};
    }

    template <class>
    friend struct encoding::encoder;
};

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
        return written(out.byte_string_encode(encoding::encoder<std::remove_reference_t<message_type>>::chars_of(bytes)));
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

    std::expected<void, error> byte_string(owning_ref<std::span<std::byte const>> const &bytes)
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

    std::expected<void, error> text_string(owning_ref<std::string_view> const &text)
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
        -> std::expected<std::conditional_t<pieces, owning_ref<std::span<std::string_view const>>, std::size_t>, error>
    {
        throw_logic_error_if_unusable();
        usable = false;
        if constexpr (pieces) {
            return out.release();
        } else {
            if (auto const r = written(out.flush()); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = written(message.done(out.written)); !r) [[unlikely]]
                return std::unexpected(r.error());
            return out.written;
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
    std::expected<void, std::errc> written = out.head_encode(node.major_type, node.additional_information, node.argument);
    if (auto const *const text = std::get_if<std::string_view>(&node.content); written && text != nullptr) {
        written = out.payload_encode(*text);
    } else if (auto const *const bytes = std::get_if<std::span<std::byte const>>(&node.content);
               written && bytes != nullptr) {
        written = out.payload_encode(encoder<Writer>::chars_of(*bytes));
    } else if (auto const *const children = std::get_if<data_items>(&node.content); written && children != nullptr) {
        for (item const *const child : children->span())
            if (auto const r = item_encode(out, *child, depth + 1, depth_max); !r) [[unlikely]]
                return r;
    } else if (auto const *const content = std::get_if<item const *>(&node.content); written && content != nullptr) {
        return item_encode(out, **content, depth + 1, depth_max);
    } else if (auto const *const embedded = std::get_if<encoded_cbor_data_item>(&node.content);
               written && embedded != nullptr) {
        return item_encode(out, *embedded->content, depth + 1, depth_max);
    } else if (auto const *const reference = std::get_if<sharedref>(&node.content); written && reference != nullptr) {
        written = out.head_encode(major_type::unsigned_integer, reference->additional_information, reference->argument);
    }
    if (!written) [[unlikely]]
        return std::unexpected(validity::writer_error(written.error()));
    return {};
}

template <class Target>
std::expected<std::size_t, error> encode(item const &root, Target &&target)
{
    std::size_t const depth_max = limits.load().nesting_depth;
    decltype(auto) message = encoding::message_of(target, 0);
    encoding::encoder<std::remove_reference_t<decltype(message)>> out(message);
    if (auto const r = encoding::item_encode(out, root, 0, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    if (auto const r = out.flush(); !r) [[unlikely]]
        return std::unexpected(validity::writer_error(r.error()));
    if (auto const r = message.done(out.written); !r) [[unlikely]]
        return std::unexpected(validity::writer_error(r.error()));
    return out.written;
}

inline std::expected<owning_ref<std::span<std::string_view const>>, error> encode_iov(item const &root)
{
    std::size_t const depth_max = limits.load().nesting_depth;
    iov_writer writer;
    encoding::encoder<iov_writer> out(writer);
    if (auto const r = encoding::item_encode(out, root, 0, depth_max); !r) [[unlikely]]
        return std::unexpected(r.error());
    return out.release();
}

}

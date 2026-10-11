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

#include "binding.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "owning_ref.hpp"

namespace cbor
{

enum class sharedrefs { off, on };

template <sharedrefs Sharing = sharedrefs::off, class Binding, class Writer>
std::expected<void, error> encode(Binding &binding, Writer &&target, typename Binding::value const &value);

template <sharedrefs Sharing = sharedrefs::off, class Binding>
std::expected<owning_ref<std::span<std::string_view const>>, error> encode_iov(Binding &binding,
                                                                               typename Binding::value const &value);

class encoding
{
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
        else
            return target.allocate(hint);
    }

    template <sharedrefs Sharing, class Binding, class Writer>
    static std::expected<std::size_t, error> encode_from(Binding &binding, Writer &writer,
                                                         typename Binding::value const &value, std::size_t depth,
                                                         bool embedded, std::size_t depth_max);

    template <class Writer>
    friend struct encoder;

    template <class, class, pass>
    friend class walker;

    template <sharedrefs Sharing, class Binding>
    friend std::expected<owning_ref<std::span<std::string_view const>>, error>
    encode_iov(Binding &binding, typename Binding::value const &value);

    template <sharedrefs Sharing, class Binding, class Writer>
    friend std::expected<void, error> encode(Binding &binding, Writer &&target,
                                                       typename Binding::value const &value);

#ifdef __cpp_impl_reflection
    template <class>
    friend class schema;

    template <class>
    friend class databind;
#endif
};

template <class Writer>
struct encoder {
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

    std::expected<void, std::errc> byte_string_encode(std::string_view bytes)
    {
        if (auto const r = head_encode(major_type::byte_string, bytes.size()); !r) [[unlikely]]
            return r;
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
    friend struct encoder;

    template <sharedrefs Sharing, class Binding>
    friend std::expected<owning_ref<std::span<std::string_view const>>, error>
    encode_iov(Binding &binding, typename Binding::value const &value);
};

enum class pass { plain, count, write };


struct discarding_writer {
    std::expected<void, std::errc> append(std::string_view)
    {
        return {};
    }

    std::expected<void, std::errc> done(std::size_t)
    {
        return {};
    }
};

template <class Binding>
struct sharing {
    std::unordered_map<typename Binding::identity, std::uint64_t> seen;
    std::unordered_map<typename Binding::identity, std::uint64_t> numbers;
    std::uint64_t next = 0;
    std::unordered_map<typename Binding::identity, typename Binding::value> replaced;
};

template <class Binding, class Writer, pass Pass>
class walker
{
    Binding &binding;
    encoder<Writer> out;
    sharing<Binding> *shared;
    std::size_t depth;
    bool embedded;
    std::size_t depth_max;
    error failure{};

    friend class encoding;

    walker(Binding &h, Writer &w, sharing<Binding> *s, std::size_t const d, bool const e, std::size_t const m)
        : binding(h), out{w}, shared(s), depth(d), embedded(e), depth_max(m)
    {
    }

    void keep(std::expected<void, std::errc> const r)
    {
        if (!r && failure == decltype(failure){}) [[unlikely]]
            failure = validity::writer_error(r.error());
    }

    void keep_error(error const e)
    {
        if (failure == decltype(failure){})
            failure = e;
    }

    void head(major_type const major, std::uint64_t const argument)
    {
        keep(out.head_encode(major, argument));
    }

    void key(typename Binding::value const &item)
    {
        if (leaf(item)) [[likely]]
            return;
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, binding.key_identity(item));
    }

    CBOR_ALWAYS_INLINE bool leaf(typename Binding::value const &item)
    {
        if constexpr (Pass == pass::plain && !requires { binding.embed_of(item); }) {
            kind const k = binding.kind_of(item);
            switch (k) {
            case kind::floating_point:
                if constexpr (requires { binding.float_of(item); }) {
                    if (!validity::check_nesting_depth(depth, depth_max)) [[unlikely]]
                        return false;
                    keep(out.float_encode(binding.float_of(item)));
                    return true;
                }
                return false;
            case kind::unsigned_integer:
                if constexpr (requires { binding.unsigned_of(item); }) {
                    if (!validity::check_nesting_depth(depth, depth_max)) [[unlikely]]
                        return false;
                    keep(out.fixed_width_head_encode(major_type::unsigned_integer, std::uint64_t{binding.unsigned_of(item)}));
                    return true;
                }
                return false;
            case kind::negative_integer:
                if constexpr (requires { binding.unsigned_of(item); }) {
                    if (!validity::check_nesting_depth(depth, depth_max)) [[unlikely]]
                        return false;
                    keep(out.fixed_width_head_encode(major_type::negative_integer, std::uint64_t{binding.unsigned_of(item) - 1}));
                    return true;
                }
                return false;
            case kind::text_string:
                if constexpr (requires { binding.text_of(item); }) {
                    if (!validity::check_nesting_depth(depth, depth_max)) [[unlikely]]
                        return false;
                    keep(out.text_string_encode(binding.text_of(item)));
                    return true;
                }
                return false;
            default:
                return false;
            }
        } else {
            return false;
        }
    }

    void value(typename Binding::value const &item)
    {
        if (leaf(item)) [[likely]]
            return;
        if constexpr (Pass == pass::plain)
            child(item, std::false_type{});
        else
            child(item, binding.value_identity(item));
    }

    template <class Identity>
    void child(typename Binding::value const &item, Identity const &identity)
    {
        if (failure != decltype(failure){}) [[unlikely]]
            return;
        if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]] {
            keep_error(r.error());
            return;
        }
        if constexpr (requires { binding.embed_of(item); }) {
            bool const outer = embedded;
            embedded = false;
            if (!outer && binding.embed_of(item)) {
                if constexpr (Pass != pass::count) {
                    encoding::string_sink inner;
                    auto const r = encoding::encode_from<Pass == pass::plain ? sharedrefs::off : sharedrefs::on>(
                        binding, inner, item, depth, true, depth_max);
                    if (!r) [[unlikely]] {
                        if (failure == decltype(failure){})
                            failure = r.error();
                        return;
                    }
                    head(major_type::tag, std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item));
                    keep(out.byte_string_encode(inner.encoded));
                }
                return;
            }
        }
        if constexpr (Pass == pass::count) {
            if (identity && ++shared->seen.try_emplace(*identity, 0).first->second > 1)
                return;
        }
        if constexpr (Pass == pass::write) {
            if (identity && !shared->numbers.empty()) {
                auto const number = shared->numbers.find(*identity);
                if (number != shared->numbers.end()) {
                    if (number->second < shared->next) {
                        head(major_type::tag, std::to_underlying(rfc8949::tag_number::sharedref));
                        head(major_type::unsigned_integer, number->second);
                        return;
                    }
                    number->second = shared->next++;
                    head(major_type::tag, std::to_underlying(rfc8949::tag_number::shareable));
                }
            }
        }
        ++depth;
        describe(item, identity);
        --depth;
    }

    template <class Identity>
    typename Binding::value content_of(typename Binding::value const &item, Identity const &identity)
    {
        if constexpr (Pass == pass::plain) {
            return binding.before_encode(item);
        } else {
            if (!identity)
                return binding.before_encode(item);
            if constexpr (Pass == pass::count)
                return shared->replaced.emplace(*identity, binding.before_encode(item)).first->second;
            else if (auto const found = shared->replaced.find(*identity); found != shared->replaced.end()) [[likely]]
                return found->second;
            else
                return binding.before_encode(item);
        }
    }

    template <class Identity>
    void describe(typename Binding::value const &item, Identity const &identity)
    {
        kind const k = binding.kind_of(item);
        if constexpr (Pass == pass::count)
            if (k != kind::array && k != kind::map && k != kind::registered)
                return;
        switch (k) {
        case kind::unsigned_integer:
            if constexpr (requires { binding.unsigned_of(item); }) {
                keep(out.fixed_width_head_encode(major_type::unsigned_integer, std::uint64_t{binding.unsigned_of(item)}));
                return;
            }
            break;
        case kind::negative_integer:
            if constexpr (requires { binding.unsigned_of(item); }) {
                keep(out.fixed_width_head_encode(major_type::negative_integer, std::uint64_t{binding.unsigned_of(item) - 1}));
                return;
            }
            break;
        case kind::unsigned_bignum:
            if constexpr (requires { binding.magnitude_of(item); }) {
                bignum(false, binding.magnitude_of(item));
                return;
            }
            break;
        case kind::negative_bignum:
            if constexpr (requires { binding.magnitude_of(item); }) {
                bignum(true, binding.magnitude_of(item));
                return;
            }
            break;
        case kind::byte_string:
            if constexpr (requires { binding.bytes_of(item); }) {
                keep(out.byte_string_encode(binding.bytes_of(item)));
                return;
            }
            break;
        case kind::text_string:
            if constexpr (requires { binding.text_of(item); }) {
                keep(out.text_string_encode(binding.text_of(item)));
                return;
            }
            break;
        case kind::floating_point:
            if constexpr (requires { binding.float_of(item); }) {
                keep(out.float_encode(binding.float_of(item)));
                return;
            }
            break;
        case kind::simple_value:
            if constexpr (requires { binding.simple_of(item); }) {
                simple(binding.simple_of(item));
                return;
            }
            break;
        case kind::array:
            if constexpr (requires { binding.array_size(item); }) {
                std::uint64_t const size = binding.array_size(item);
                head(major_type::array, size);
                for (std::uint64_t i = 0; i < size; ++i)
                    value(binding.array_at(item, i));
                return;
            }
            break;
        case kind::map:
            if constexpr (requires { binding.map_size(item); }) {
                std::uint64_t const size = binding.map_size(item);
                head(major_type::map, size);
                std::uint64_t written = 0;
                binding.map_for_each(item,
                             [this, &written](typename Binding::value const &map_key, typename Binding::value const &v) {
                                 key(map_key);
                                 value(v);
                                 ++written;
                             });
                if (written != size) [[unlikely]]
                    validity::throw_logic_error(
                        "cbor::encode: the binding gave a map size that differs from the entries of map_for_each");
                return;
            }
            break;
        case kind::typed_array:
            if constexpr (requires { binding.typed_array_of(item); }) {
                if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]] {
                    keep_error(r.error());
                    return;
                }
                cbor::typed_array const a = binding.typed_array_of(item);
                if (auto const r = validity::typed_array_check(a.tag, a.bytes.size()); !r) [[unlikely]] {
                    keep_error(r.error() == error::incorrect_type ? error::unsupported_value : r.error());
                    return;
                }
                head(major_type::tag, a.tag);
                keep(out.byte_string_encode(
                    std::string_view(reinterpret_cast<char const *>(a.bytes.data()), a.bytes.size())));
                return;
            }
            break;
        case kind::registered:
            if constexpr (requires { binding.registered_tag(item); }) {
                head(major_type::tag, binding.registered_tag(item));
                if constexpr (Pass == pass::plain)
                    value(binding.before_encode(item));
                else
                    value(content_of(item, identity));
                return;
            }
            break;
        case kind::unsupported:
            break;
        }
        keep_error(error::unsupported_value);
    }

    void bignum(bool const negative, std::string_view const absolute)
    {
        std::string_view const m = heads::magnitude_without_leading_zeros(absolute);
        if (!negative) {
            if (m.size() <= sizeof(std::uint64_t)) {
                head(major_type::unsigned_integer, heads::magnitude_value(m));
                return;
            }
            if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]] {
                keep_error(r.error());
                return;
            }
            head(major_type::tag, std::to_underlying(rfc8949::tag_number::unsigned_bignum));
            keep(out.byte_string_encode(m));
            return;
        }
        if (m.empty()) [[unlikely]] {
            keep_error(error::unsupported_value);
            return;
        }
        std::string const n = heads::magnitude_minus_one(m);
        if (n.size() <= sizeof(std::uint64_t)) {
            head(major_type::negative_integer, heads::magnitude_value(n));
            return;
        }
        if (auto const r = validity::check_nesting_depth(depth, depth_max); !r) [[unlikely]] {
            keep_error(r.error());
            return;
        }
        head(major_type::tag, std::to_underlying(rfc8949::tag_number::negative_bignum));
        keep(out.byte_string_encode(n));
    }

    void simple(std::uint8_t const v)
    {
        if (!validity::check_simple_value(heads::preferred_argument_info(v), v)) [[unlikely]] {
            keep_error(error::reserved_simple_value);
            return;
        }
        head(major_type::simple_float, v);
    }

public:
    walker(walker const &) = delete;
    walker &operator=(walker const &) = delete;
};

template <class Binding>
bool cycle_find(Binding &binding, typename Binding::value const &item,
                std::vector<typename Binding::identity> &path, std::size_t const depth_max)
{
    auto const identity = binding.value_identity(item);
    if (identity && std::ranges::find(path, *identity) != path.end())
        return true;
    if (!validity::check_nesting_depth(path.size(), depth_max)) [[unlikely]]
        return false;
    if (identity)
        path.push_back(*identity);
    bool found = false;
    switch (binding.kind_of(item)) {
    case kind::array:
        if constexpr (requires { binding.array_size(item); })
            for (std::uint64_t i = 0; !found && i < binding.array_size(item); ++i)
                found = cycle_find(binding, binding.array_at(item, i), path, depth_max);
        break;
    case kind::map:
        if constexpr (requires { binding.map_size(item); })
            binding.map_for_each(item,
                                 [&](typename Binding::value const &k, typename Binding::value const &v) {
                                     found = found || cycle_find(binding, k, path, depth_max) ||
                                             cycle_find(binding, v, path, depth_max);
                                 });
        break;
    default:
        break;
    }
    if (identity)
        path.pop_back();
    return found;
}

template <sharedrefs Sharing, class Binding, class Writer>
std::expected<std::size_t, error> encoding::encode_from(Binding &binding, Writer &writer, typename Binding::value const &value,
                                                        std::size_t const depth, bool const embedded,
                                                        std::size_t const depth_max)
{
    if constexpr (Sharing == sharedrefs::off) {
        walker<Binding, Writer, pass::plain> walk{binding, writer, nullptr, depth, embedded, depth_max};
        walk.value(value);
        walk.keep(walk.out.flush());
        if (walk.failure != decltype(walk.failure){}) [[unlikely]] {
            if constexpr (requires { binding.value_identity(value); }) {
                std::vector<typename Binding::identity> path;
                if (walk.failure == error{error::nesting_depth_exceeded} &&
                    cycle_find(binding, value, path, depth_max))
                    return std::unexpected(error{error::cyclic_data_structure});
            }
            return std::unexpected(walk.failure);
        }
        return walk.out.written;
    } else {
        sharing<Binding> shared;
        discarding_writer nothing;
        walker<Binding, discarding_writer, pass::count> count{binding, nothing, &shared, depth, embedded, depth_max};
        count.value(value);
        if (count.failure != decltype(count.failure){}) [[unlikely]]
            return std::unexpected(count.failure);
        for (auto const &[identity, times] : shared.seen)
            if (times > 1)
                shared.numbers.emplace(identity, std::numeric_limits<std::uint64_t>::max());
        walker<Binding, Writer, pass::write> write{binding, writer, &shared, depth, embedded, depth_max};
        write.value(value);
        write.keep(write.out.flush());
        if (write.failure != decltype(write.failure){}) [[unlikely]]
            return std::unexpected(write.failure);
        return write.out.written;
    }
}

template <sharedrefs Sharing, class Binding, class Writer>
std::expected<void, error> encode(Binding &binding, Writer &&target, typename Binding::value const &value)
{
    decltype(auto) message = encoding::message_of(target, 0);
    auto const size = encoding::encode_from<Sharing>(binding, message, value, 0, false, limits.nesting_depth);
    if (!size) [[unlikely]]
        return std::unexpected(size.error());
    if (auto const r = message.done(*size); !r) [[unlikely]]
        return std::unexpected(validity::writer_error(r.error()));
    return {};
}

template <sharedrefs Sharing, class Binding>
std::expected<owning_ref<std::span<std::string_view const>>, error> encode_iov(Binding &binding,
                                                                               typename Binding::value const &value)
{
    iov_writer writer;
    if (auto const size = encoding::encode_from<Sharing>(binding, writer, value, 0, false, limits.nesting_depth); !size)
        [[unlikely]]
        return std::unexpected(size.error());
    return writer.release();
}

}

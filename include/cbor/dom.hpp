// Reads JSON text into a tree of cbor::item in the form of the yyjson reader: one padded copy of the text,
// strings unescaped in place and viewed in the copy, and each item written once at its place. Gives
// cbor::parse_edn, which reads EDN that is not JSON through cbor::diag2cbor and cbor::decode.
// https://www.rfc-editor.org/rfc/rfc8259
// https://www.rfc-editor.org/rfc/rfc8949
// https://github.com/ibireme/yyjson
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "decode.hpp"
#include "diagnostic_notation.hpp"
#include "error.hpp"
#include "head.hpp"
#include "ieee754.hpp"
#include "padded_string.hpp"
#include "rfc8949.hpp"
#include "validity.hpp"

namespace cbor
{

namespace detail
{

namespace dom
{

inline constexpr std::size_t utf8_seq_size = 4;

constexpr std::uint32_t utf8_seq(std::uint8_t const b0, std::uint8_t const b1, std::uint8_t const b2,
                                 std::uint8_t const b3)
{
    return std::bit_cast<std::uint32_t>(std::array<std::uint8_t, utf8_seq_size>{b0, b1, b2, b3});
}

class reader
{
    using heads = detail::heads;
    using validity = detail::validity;
    using decoder = dom::decoder;

    using edn = extended_diagnostic_notation;

    static constexpr std::size_t estimated_minify_ratio = 6;
    static constexpr std::size_t estimated_pretty_ratio = 16;
    static constexpr std::size_t estimated_extra = 4;
    static constexpr std::size_t pair_values = 2;
    static constexpr std::size_t pretty_probe = 2;
    static constexpr std::string_view pretty_indent = "  ";
    static constexpr std::size_t indent_repeat = 16;
    static constexpr std::string_view pretty_comma = ",\n";
    static constexpr std::string_view pretty_colon = ": ";
    static constexpr std::string_view literal_true = "true";
    static constexpr std::string_view literal_false = "false";
    static constexpr std::string_view literal_null = "null";
    static constexpr std::size_t failed = 0;
    static constexpr std::size_t two_octets = 2;
    static constexpr std::size_t three_octets = 3;
    static constexpr std::size_t four_octets = 4;

    static constexpr std::uint32_t b1_mask = utf8_seq(0x80, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b1_patt = utf8_seq(0x00, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b2_mask = utf8_seq(0xE0, 0xC0, 0x00, 0x00);
    static constexpr std::uint32_t b2_patt = utf8_seq(0xC0, 0x80, 0x00, 0x00);
    static constexpr std::uint32_t b2_requ = utf8_seq(0x1E, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b3_mask = utf8_seq(0xF0, 0xC0, 0xC0, 0x00);
    static constexpr std::uint32_t b3_patt = utf8_seq(0xE0, 0x80, 0x80, 0x00);
    static constexpr std::uint32_t b3_requ = utf8_seq(0x0F, 0x20, 0x00, 0x00);
    static constexpr std::uint32_t b3_erro = utf8_seq(0x0D, 0x20, 0x00, 0x00);
    static constexpr std::uint32_t b4_mask = utf8_seq(0xF8, 0xC0, 0xC0, 0xC0);
    static constexpr std::uint32_t b4_patt = utf8_seq(0xF0, 0x80, 0x80, 0x80);
    static constexpr std::uint32_t b4_requ = utf8_seq(0x07, 0x30, 0x00, 0x00);
    static constexpr std::uint32_t b4_req1 = utf8_seq(0x04, 0x00, 0x00, 0x00);
    static constexpr std::uint32_t b4_req2 = utf8_seq(0x03, 0x30, 0x00, 0x00);

    CBOR_ALWAYS_INLINE static bool is_utf8_seq1(std::uint32_t const uni)
    {
        return (uni & b1_mask) == b1_patt;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq2(std::uint32_t const uni)
    {
        return (uni & b2_mask) == b2_patt && (uni & b2_requ) != 0;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq3(std::uint32_t const uni)
    {
        std::uint32_t const requ = uni & b3_requ;
        return (uni & b3_mask) == b3_patt && requ != 0 && requ != b3_erro;
    }

    CBOR_ALWAYS_INLINE static bool is_utf8_seq4(std::uint32_t const uni)
    {
        std::uint32_t const requ = uni & b4_requ;
        return (uni & b4_mask) == b4_patt && requ != 0 && ((requ & b4_req1) == 0 || (requ & b4_req2) == 0);
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static std::array<char, N> bytes_at(std::span<char const> const buf,
                                                                             std::size_t const at)
    {
        std::array<char, N> bytes;
        std::ranges::copy(buf.subspan(at).first<N>(), bytes.begin());
        return bytes;
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static void byte_move(std::span<char> const buf,
                                                               std::size_t const dst, std::size_t const src)
    {
        std::ranges::copy(bytes_at<N>(buf, src), buf.subspan(dst).begin());
    }

    CBOR_ALWAYS_INLINE static void byte_move_forward(std::span<char> const buf, std::size_t dst,
                                                     std::size_t src, std::size_t n)
    {
        constexpr std::size_t eight = 8;
        constexpr std::size_t four = 4;
        constexpr std::size_t two = 2;
        n += n % two;
        if (n == edn::repeat16) {
            byte_move<edn::repeat16>(buf, dst, src);
            return;
        }
        if (n >= eight) {
            byte_move<eight>(buf, dst, src);
            n -= eight;
            dst += eight;
            src += eight;
        }
        if (n >= four) {
            byte_move<four>(buf, dst, src);
            n -= four;
            dst += four;
            src += four;
        }
        if (n >= two)
            byte_move<two>(buf, dst, src);
    }

    [[gnu::always_inline, gnu::flatten]] static std::uint32_t byte_load_4(std::span<char const> const buf,
                                                                          std::size_t const at)
    {
        return std::bit_cast<std::uint32_t>(bytes_at<utf8_seq_size>(buf, at));
    }

    [[gnu::always_inline, gnu::flatten]] static void
    byte_store(std::span<char> const buf, std::size_t const dst, std::uint32_t const uni, std::size_t const n)
    {
        std::ranges::copy_n(std::bit_cast<std::array<char, utf8_seq_size>>(uni).begin(),
                            static_cast<std::ptrdiff_t>(n), buf.subspan(dst).begin());
    }

    template <std::size_t N>
    [[gnu::always_inline, gnu::flatten]] static bool
    byte_match(std::span<char const> const buf, std::size_t const at, std::string_view const pattern)
    {
        return std::ranges::equal(bytes_at<N>(buf, at), pattern);
    }

    CBOR_ALWAYS_INLINE static unsigned char byte(std::span<char const> const buf, std::size_t const at)
    {
        return static_cast<unsigned char>(buf[at]);
    }

    struct in_place_writer {
        std::span<char> buf;
        std::size_t &dst;

        void push_back(char const c)
        {
            buf[dst] = c;
            ++dst;
        }
    };

    [[gnu::always_inline, gnu::flatten]] static bool hex_load_4(std::span<char const> const buf,
                                                                std::size_t const at, std::uint32_t &value)
    {
        std::uint8_t invalid = 0;
        value = 0;
        for (char const c : buf.subspan(at).first<edn::hex4_digits>()) {
            std::uint8_t const v = edn::hex_conv_table[static_cast<unsigned char>(c)];
            invalid |= v;
            value = value << rfc4648::base16_group_bits | v;
        }
        return (invalid & edn::hex_conv_invalid) == 0;
    }

    [[gnu::always_inline, gnu::flatten]] static bool read_uni_esc(std::span<char> const buf, std::size_t &src,
                                                                  std::size_t &dst)
    {
        std::uint32_t hi;
        std::uint32_t lo;
        src += edn::escape_prefix.size();
        if (!hex_load_4(buf, src, hi)) [[unlikely]]
            return false;
        src += edn::hex4_digits;
        in_place_writer out{buf, dst};
        if (hi < rfc2781::high_surrogate_first || hi > rfc2781::low_surrogate_last) [[likely]] {
            edn::append_utf8(out, hi);
            return true;
        }
        if (hi >= rfc2781::low_surrogate_first) [[unlikely]]
            return false;
        if (!byte_match<edn::escape_prefix.size()>(buf, src, edn::escape_prefix)) [[unlikely]]
            return false;
        if (!hex_load_4(buf, src + edn::escape_prefix.size(), lo)) [[unlikely]]
            return false;
        if (lo < rfc2781::low_surrogate_first || lo > rfc2781::low_surrogate_last) [[unlikely]]
            return false;
        edn::append_utf8(out, rfc2781::surrogate_pair_first +
                                  ((hi - rfc2781::high_surrogate_first) << rfc2781::surrogate_bits) +
                                  (lo - rfc2781::low_surrogate_first));
        src += edn::escape_prefix.size() + edn::hex4_digits;
        return true;
    }

    CBOR_ALWAYS_INLINE static void set_text(node &x, std::size_t const first, std::size_t const size)
    {
        x.m_tag = kind::word(size, heads::preferred_argument_info(size), kind::type_text);
        x.m_uni.ofs = first;
    }

    CBOR_ALWAYS_INLINE static bool is_escape_or_end(unsigned char const c)
    {
        return c <= rfc3629::one_octet_last;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_str(std::span<char> const buf, std::size_t const at, node &x)
    {
        std::size_t const hdr = at + 1;
        std::size_t src = hdr;
        std::size_t dst = 0;
        std::size_t pos = 0;
        std::uint32_t uni = 0;

    skip_ascii:
        for (;;) {
#pragma GCC unroll edn::repeat16
            for (std::size_t i = 0; i < edn::repeat16; ++i) {
                if (!edn::char_is_ascii_skip(byte(buf, src + i))) [[unlikely]] {
                    src += i;
                    goto skip_ascii_end;
                }
            }
            src += edn::repeat16;
        }

    skip_ascii_end:
        if (buf[src] == '"') [[likely]] {
            set_text(x, hdr, src - hdr);
            return src + 1;
        }

        if (!is_escape_or_end(byte(buf, src))) {
            pos = src;
            uni = byte_load_4(buf, src);
            while (is_utf8_seq3(uni)) {
                src += three_octets;
                uni = byte_load_4(buf, src);
            }
            if (is_utf8_seq1(uni))
                goto skip_ascii;
            while (is_utf8_seq2(uni)) {
                src += two_octets;
                uni = byte_load_4(buf, src);
            }
            while (is_utf8_seq4(uni)) {
                src += four_octets;
                uni = byte_load_4(buf, src);
            }
            if (pos == src) [[unlikely]]
                return failed;
            goto skip_ascii;
        }

        dst = src;
    copy_escape:
        if (buf[src] == '\\') [[likely]] {
            switch (buf[++src]) {
            case '"':
                buf[dst++] = '"';
                ++src;
                break;
            case '\\':
                buf[dst++] = '\\';
                ++src;
                break;
            case '/':
                buf[dst++] = '/';
                ++src;
                break;
            case 'b':
                buf[dst++] = '\b';
                ++src;
                break;
            case 'f':
                buf[dst++] = '\f';
                ++src;
                break;
            case 'n':
                buf[dst++] = '\n';
                ++src;
                break;
            case 'r':
                buf[dst++] = '\r';
                ++src;
                break;
            case 't':
                buf[dst++] = '\t';
                ++src;
                break;
            case 'u':
                --src;
                if (!read_uni_esc(buf, src, dst)) [[unlikely]]
                    return failed;
                break;
            [[unlikely]] default:
                return failed;
            }
        } else if (buf[src] == '"') [[likely]] {
            set_text(x, hdr, dst - hdr);
            return src + 1;
        } else [[unlikely]] {
            return failed;
        }

    copy_ascii:
        for (;;) {
#pragma GCC unroll edn::repeat16
            for (std::size_t i = 0; i < edn::repeat16; ++i) {
                if (!edn::char_is_ascii_skip(byte(buf, src + i))) [[unlikely]] {
                    byte_move_forward(buf, dst, src, i);
                    dst += i;
                    src += i;
                    goto copy_utf8;
                }
            }
            byte_move<edn::repeat16>(buf, dst, src);
            dst += edn::repeat16;
            src += edn::repeat16;
        }

    copy_utf8:
        if (!is_escape_or_end(byte(buf, src))) {
            pos = src;
            uni = byte_load_4(buf, src);
            while (is_utf8_seq3(uni)) {
                byte_store(buf, dst, uni, utf8_seq_size);
                dst += three_octets;
                src += three_octets;
                uni = byte_load_4(buf, src);
            }
            if (is_utf8_seq1(uni))
                goto copy_ascii;
            while (is_utf8_seq2(uni)) {
                byte_store(buf, dst, uni, two_octets);
                dst += two_octets;
                src += two_octets;
                uni = byte_load_4(buf, src);
            }
            while (is_utf8_seq4(uni)) {
                byte_store(buf, dst, uni, four_octets);
                dst += four_octets;
                src += four_octets;
                uni = byte_load_4(buf, src);
            }
            if (pos == src) [[unlikely]]
                return failed;
            goto copy_ascii;
        }
        goto copy_escape;
    }

    CBOR_ALWAYS_INLINE static std::size_t
    read_num(std::span<char const> const buf, std::string_view const text, std::size_t const hdr, node &x)
    {
        enum class digi { intg_more, frac, frac_more, frac_end, exp_more, exp_finish, finish };
        auto const ch = [buf](std::size_t const at) { return static_cast<unsigned char>(buf[at]); };
        bool const sign = ch(hdr) == '-';
        std::size_t cur = hdr + (sign ? 1 : 0);
        auto const return_i64 = [&x, &cur, sign](std::uint64_t const v) {
            if (sign && v != 0)
                decoder::set_integer(x, major_type::negative_integer, heads::preferred_argument_info(v - 1), v - 1);
            else
                decoder::set_integer(x, major_type::unsigned_integer, heads::preferred_argument_info(v), v);
            return cur;
        };
        auto const return_f64 = [&x, &cur, sign](double const v) {
            double const d = sign ? -v : v;
            decoder::set_float(x, std::to_underlying(ieee754::preferred_float_info(d)), d);
            return cur;
        };
        auto const return_f64_bin = [&return_f64](std::uint64_t const raw) {
            return return_f64(std::bit_cast<double>(raw));
        };
        auto const digit_value = [&ch](std::size_t const at) {
            return static_cast<std::uint64_t>(ch(at) - '0');
        };

        std::size_t sig_cut = std::string_view::npos;
        std::size_t sig_end = std::string_view::npos;
        std::size_t dot_pos = std::string_view::npos;
        std::uint64_t sig = 0;
        std::int32_t exp = 0;
        std::int64_t exp_sig = 0;
        std::int64_t exp_lit = 0;
        std::size_t i = 1;
        digi next = digi::finish;

        if (!edn::char_is_nonzero(ch(cur))) {
            if (ch(cur) != '0') [[unlikely]]
                return failed;
            ++cur;
            if (!edn::char_is_digit_or_fp(ch(cur))) [[likely]] {
                return return_i64(0);
            }
            if (ch(cur) == '.') {
                dot_pos = cur++;
                if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                while (ch(cur) == '0')
                    ++cur;
                if (edn::char_is_digit(ch(cur))) {
                    sig = digit_value(cur);
                    --cur;
                    next = digi::frac;
                }
            }
            if (next != digi::frac) {
                if (edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                if (edn::char_is_exp(ch(cur))) {
                    cur += 1 + (edn::char_is_sign(ch(cur + 1)) ? std::size_t{1} : 0);
                    if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                        return failed;
                    for (; edn::char_is_digit(ch(cur)); ++cur) {
                        if (exp_lit >= edn::exponent_beyond_double) [[unlikely]]
                            return failed;
                        exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * edn::decimal;
                    }
                }
                return return_f64_bin(0);
            }
        } else {
            sig = digit_value(cur);
#pragma GCC unroll edn::u64_safe_dig - 1
            for (; i < edn::u64_safe_dig; ++i) {
                std::uint64_t const num = digit_value(cur + i);
                if (num >= edn::decimal)
                    break;
                sig = num + sig * edn::decimal;
            }
            if (i == edn::u64_safe_dig) {
                cur += edn::u64_safe_dig;
                if (!edn::char_is_digit_or_fp(ch(cur))) {
                    return return_i64(sig);
                }
                next = digi::intg_more;
            } else {
                if (!edn::char_is_fp(ch(cur + i))) [[likely]] {
                    cur += i;
                    return return_i64(sig);
                }
                dot_pos = cur + i;
                if (ch(cur + i) == '.') {
                    next = digi::frac;
                } else {
                    cur += i;
                    sig_end = cur;
                    next = digi::exp_more;
                }
            }
        }

        while (next != digi::finish) {
            switch (next) {
            case digi::frac:
#pragma GCC unroll edn::u64_safe_dig - 1
                for (; i < edn::u64_safe_dig; ++i) {
                    std::uint64_t const num = digit_value(cur + i + 1);
                    if (num >= edn::decimal)
                        break;
                    sig = num + sig * edn::decimal;
                }
                cur += i + 1;
                next = edn::char_is_digit(ch(cur)) ? digi::frac_more : digi::frac_end;
                break;
            case digi::intg_more: {
                unsigned char const c = ch(cur);
                if (edn::char_is_digit(c) && !edn::char_is_digit_or_fp(ch(cur + 1))) {
                    std::uint64_t const num = digit_value(cur);
                    constexpr std::uint64_t u64_max = std::numeric_limits<std::uint64_t>::max();
                    if (sig < u64_max / edn::decimal ||
                        (sig == u64_max / edn::decimal && num <= u64_max % edn::decimal)) {
                        sig = num + sig * edn::decimal;
                        ++cur;
                        return return_i64(sig);
                    }
                }
                if (edn::char_is_exp(c)) {
                    dot_pos = cur;
                    next = digi::exp_more;
                    break;
                }
                if (c == '.') {
                    dot_pos = cur++;
                    if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                        return failed;
                }
                next = digi::frac_more;
                break;
            }
            case digi::frac_more: {
                sig_cut = cur;
                sig += ch(cur) >= '5' ? std::uint64_t{1} : 0;
                do
                    ++cur;
                while (edn::char_is_digit(ch(cur)));
                if (dot_pos == std::string_view::npos) {
                    if (!edn::char_is_fp(ch(cur))) [[unlikely]]
                        return failed;
                    dot_pos = cur;
                    if (ch(cur) == '.') {
                        ++cur;
                        if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                            return failed;
                        while (edn::char_is_digit(ch(cur)))
                            ++cur;
                    }
                }
                exp_sig = static_cast<std::int64_t>(dot_pos) - static_cast<std::int64_t>(sig_cut) +
                          (dot_pos < sig_cut ? 1 : 0);
                std::size_t tmp = cur - 1;
                while ((ch(tmp) == '0' || ch(tmp) == '.') && tmp > hdr)
                    --tmp;
                if (tmp < sig_cut)
                    sig_cut = std::string_view::npos;
                else
                    sig_end = cur;
                next = edn::char_is_exp(ch(cur)) ? digi::exp_more : digi::exp_finish;
                break;
            }
            case digi::frac_end:
                if (dot_pos + 1 == cur) [[unlikely]]
                    return failed;
                sig_end = cur;
                exp_sig = -static_cast<std::int64_t>(cur - dot_pos - 1);
                if (edn::char_is_exp(ch(cur))) {
                    next = digi::exp_more;
                    break;
                }
                if (exp_sig < edn::f64_min_dec_exp - edn::u64_safe_dig) [[unlikely]]
                    return failed;
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::exp_more: {
                ++cur;
                bool const exp_sign = ch(cur) == '-';
                cur += edn::char_is_sign(ch(cur)) ? std::size_t{1} : 0;
                if (!edn::char_is_digit(ch(cur))) [[unlikely]]
                    return failed;
                while (ch(cur) == '0')
                    ++cur;
                for (; edn::char_is_digit(ch(cur)); ++cur) {
                    if (exp_lit >= edn::exponent_beyond_double) [[unlikely]]
                        return failed;
                    exp_lit = static_cast<std::int64_t>(digit_value(cur)) + exp_lit * edn::decimal;
                }
                exp_sig += exp_sign ? -exp_lit : exp_lit;
                next = digi::exp_finish;
                break;
            }
            case digi::exp_finish:
                if (exp_sig < edn::f64_min_dec_exp - edn::u64_safe_dig || exp_sig > edn::f64_max_dec_exp)
                    [[unlikely]]
                    return failed;
                exp = static_cast<std::int32_t>(exp_sig);
                next = digi::finish;
                break;
            case digi::finish:
                break;
            }
        }

        if constexpr (edn::double_math_correct) {
            if (sig < (std::uint64_t{1} << edn::f64_sig_full_bits) && exp >= -edn::f64_pow10_max_exact_exp &&
                exp <= edn::f64_pow10_max_exact_exp) {
                double dbl = static_cast<double>(sig);
                if (exp < 0)
                    dbl /= edn::f64_pow10_table[static_cast<std::size_t>(-exp)];
                else
                    dbl *= edn::f64_pow10_table[static_cast<std::size_t>(exp)];
                return return_f64(dbl);
            }
        }
        if (sig == 0) [[unlikely]] {
            return return_f64_bin(0);
        }

        if (sig_cut == std::string_view::npos && exp > -edn::f64_max_dec_exp + 1 &&
            exp < edn::f64_max_dec_exp - (edn::u64_safe_dig + 1)) [[likely]] {
            constexpr int low_bits = edn::f64_bits - (edn::f64_sig_full_bits + 1) - 1;
            constexpr std::uint64_t low_mask = (std::uint64_t{1} << low_bits) - 1;
            constexpr std::uint64_t round_bit = std::uint64_t{1}
                                                << (edn::f64_bits - (edn::f64_sig_full_bits + 1));
            constexpr std::uint64_t top_bit = std::uint64_t{1} << (edn::f64_bits - 1);
            edn::uint128 const sig2 = edn::pow10_table_get_sig(exp);
            int const lz = std::countl_zero(sig);
            std::uint64_t const sig1 = sig << lz;
            std::int32_t exp2 = edn::pow10_table_get_exp(exp) - lz;
            edn::uint128 const m = edn::u128_mul(sig1, sig2.hi);
            std::uint64_t hi = m.hi;
            bool exact = false;
            std::uint64_t const bits = hi & low_mask;
            if (bits - 1 < low_mask - 1) {
                exact = true;
            } else {
                edn::uint128 const m2 = edn::u128_mul(sig1, sig2.lo);
                std::uint64_t const add = m.lo + m2.hi;
                if (add + 1 > 1) {
                    hi += add < m.lo || add < m2.hi ? 1 : 0;
                    exact = true;
                }
            }
            if (exact) {
                int const shift = hi < top_bit ? 1 : 0;
                hi <<= shift;
                exp2 += edn::f64_bits - shift;
                hi += (hi & round_bit) != 0 ? round_bit : 0;
                if (hi < round_bit) {
                    hi = top_bit;
                    exp2 += 1;
                }
                hi >>= edn::f64_bits - edn::f64_sig_full_bits;
                exp2 += edn::f64_bits - edn::f64_sig_full_bits + edn::f64_sig_bits + edn::f64_exp_bias;
                return return_f64_bin(static_cast<std::uint64_t>(exp2) << edn::f64_sig_bits |
                                      (hi & edn::f64_sig_mask));
            }
        }

        constexpr std::int32_t err_ulp_log = 3;
        constexpr std::int32_t err_ulp = 1 << err_ulp_log;
        constexpr std::int32_t err_cached_pow = err_ulp / 2;
        constexpr std::int32_t err_mul_fixed = err_ulp / 2;
        constexpr std::int32_t diy_sig_bits = std::numeric_limits<std::uint64_t>::digits;
        constexpr std::int32_t exp_bias = edn::f64_exp_bias + edn::f64_sig_bits;
        constexpr std::int32_t exp_subnormal = -exp_bias + 1;

        edn::diy_fp fp{sig, 0};
        std::uint64_t fp_err = sig_cut != std::string_view::npos ? std::uint64_t{err_ulp / 2} : 0;
        int bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;
        fp = edn::diy_fp_mul(fp, edn::diy_fp_get_cached_pow10(exp));
        fp_err += std::uint64_t{err_cached_pow} + (fp_err != 0 ? 1 : 0) + std::uint64_t{err_mul_fixed};
        bits = std::countl_zero(fp.sig);
        fp.sig <<= bits;
        fp.exp -= bits;
        fp_err <<= bits;

        std::int32_t const order_of_magnitude = diy_sig_bits + fp.exp;
        std::int32_t effective_significand_size = order_of_magnitude - exp_subnormal;
        if (order_of_magnitude >= exp_subnormal + edn::f64_sig_full_bits) [[likely]]
            effective_significand_size = edn::f64_sig_full_bits;
        else if (order_of_magnitude <= exp_subnormal)
            effective_significand_size = 0;
        std::int32_t precision_digits_count = diy_sig_bits - effective_significand_size;
        if (precision_digits_count + err_ulp_log >= diy_sig_bits) [[unlikely]] {
            std::int32_t const shr = precision_digits_count + err_ulp_log - diy_sig_bits + 1;
            fp.sig >>= shr;
            fp.exp += shr;
            fp_err = (fp_err >> shr) + 1 + std::uint64_t{err_ulp};
            precision_digits_count -= shr;
        }
        std::uint64_t const precision_bits =
            (fp.sig & ((std::uint64_t{1} << precision_digits_count) - 1)) * std::uint64_t{err_ulp};
        std::uint64_t const half_way =
            (std::uint64_t{1} << (precision_digits_count - 1)) * std::uint64_t{err_ulp};
        fp.sig >>= precision_digits_count;
        fp.sig += precision_bits >= half_way + fp_err ? 1 : 0;
        fp.exp += precision_digits_count;

        std::uint64_t raw = edn::diy_fp_to_ieee_raw(fp);
        if (raw == edn::f64_bits_inf) [[unlikely]]
            return failed;
        if (precision_bits > half_way - fp_err && precision_bits < half_way + fp_err) [[unlikely]] {
            edn::diy_fp fp_upper{
                (raw & edn::f64_sig_mask) +
                    ((raw & edn::f64_exp_mask) != 0 ? std::uint64_t{1} << edn::f64_sig_bits : 0),
                (raw & edn::f64_exp_mask) != 0
                    ? static_cast<std::int32_t>((raw & edn::f64_exp_mask) >> edn::f64_sig_bits)
                    : 1};
            fp_upper.exp -= edn::f64_exp_bias + edn::f64_sig_bits;
            fp_upper.sig <<= 1;
            fp_upper.exp -= 1;
            fp_upper.sig += 1;
            std::strong_ordering const cmp =
                edn::bigcomp(text, sig, exp, sig_cut, sig_end, dot_pos, fp_upper);
            if (cmp != 0) [[likely]]
                raw += cmp > 0 ? std::uint64_t{1} : 0;
            else
                raw += raw & 1;
            if (raw == edn::f64_bits_inf) [[unlikely]]
                return failed;
        }
        return return_f64_bin(raw);
    }

    CBOR_ALWAYS_INLINE static std::size_t read_true(std::span<char const> const buf, std::size_t const cur,
                                                    node &x)
    {
        if (byte_match<literal_true.size()>(buf, cur, literal_true)) [[likely]] {
            x.m_tag = kind::word(0, std::to_underlying(simple_value::true_value),
                               kind::type_bool | kind::subtype_true);
            return cur + literal_true.size();
        }
        return failed;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_false(std::span<char const> const buf, std::size_t const cur,
                                                     node &x)
    {
        static constexpr std::string_view rest = validity::subspan(literal_false, validity::offset{1});
        if (byte_match<rest.size()>(buf, cur + 1, rest)) [[likely]] {
            x.m_tag = kind::word(0, std::to_underlying(simple_value::false_value),
                               kind::type_bool | kind::subtype_false);
            return cur + literal_false.size();
        }
        return failed;
    }

    CBOR_ALWAYS_INLINE static std::size_t read_null(std::span<char const> const buf, std::size_t const cur,
                                                    node &x)
    {
        if (byte_match<literal_null.size()>(buf, cur, literal_null)) [[likely]] {
            x.m_tag = kind::word(0, std::to_underlying(simple_value::null), kind::type_null);
            return cur + literal_null.size();
        }
        return failed;
    }

    static std::expected<void, error> check_string_length(node const &x)
    {
        return validity::check_argument<true>(major_type::text_string, kind::high_of(x.m_tag));
    }

    [[gnu::flatten]] static std::size_t space_end(padded_string const &text, std::size_t cur)
    {
        while (edn::char_is_space(text[cur]))
            ++cur;
        return cur;
    }

    template <bool Pretty>
    [[gnu::noinline]] static std::expected<std::unique_ptr<tree>, error> read_root(padded_string text, std::size_t cur)
    {
        std::span<char> const buf = text.padded();
        std::string_view const chars(text);
        std::size_t const len = text.size();
        value_block block = value_block::allocated(
            len / (Pretty ? estimated_pretty_ratio : estimated_minify_ratio) + estimated_extra);
        std::size_t val = 0;
        std::size_t ctn = 0;
        std::size_t ctn_len = 0;
        std::size_t ctn_parent = 0;
        std::size_t depth = 1;
        std::size_t r = failed;

        auto const val_incr = [&block, &val] {
            ++val;
            if (val >= block.val_end) [[unlikely]]
                block.grow(val);
        };
        auto const skip_indent = [&buf, &cur] {
            for (;;) {
#pragma GCC unroll indent_repeat
                for (std::size_t k = 0; k < indent_repeat; ++k) {
                    if (!byte_match<pretty_indent.size()>(buf, cur, pretty_indent))
                        return;
                    cur += pretty_indent.size();
                }
            }
        };
        auto const space = [&buf, &cur] {
            while (edn::char_is_space(byte(buf, ++cur)))
                ;
        };
        auto const newline = [&] {
            if constexpr (Pretty)
                cur += buf[cur] == '\n' ? std::size_t{1} : 0;
        };
        auto const container_begin = [&](std::uint8_t const type) -> std::expected<void, error> {
            if (auto const c = decoder::check_nesting_depth(depth); !c) [[unlikely]]
                return c;
            ++depth;
            block.items[ctn].m_tag =
                kind::word(ctn_len + 1, 0, static_cast<std::uint8_t>(block.items[ctn].m_tag & kind::type_mask));
            val_incr();
            block.items[val].m_tag = kind::word(0, 0, type);
            block.items[val].m_uni.ofs = val - ctn;
            ctn = val;
            ctn_len = 0;
            newline();
            return {};
        };
        auto const container_end = [&](major_type const major) -> std::expected<void, error> {
            node &c = block.items[ctn];
            std::size_t const count =
                major == major_type::map ? ctn_len / rfc8949::data_items_per_pair : ctn_len;
            if (auto const a = validity::check_argument<true>(major, count); !a) [[unlikely]]
                return a;
            ctn_parent = ctn - c.m_uni.ofs;
            c.m_tag = kind::word(count, heads::preferred_argument_info(count),
                               major == major_type::map ? kind::type_map : kind::type_array);
            c.m_uni.ofs = val - ctn + 1;
            return {};
        };

        block.items[ctn].m_uni.ofs = 0;
        if (buf[cur++] == '{') {
            block.items[ctn].m_tag = kind::word(0, 0, kind::type_map);
            newline();
            goto obj_key_begin;
        }
        block.items[ctn].m_tag = kind::word(0, 0, kind::type_array);
        newline();
        goto arr_val_begin;

    arr_begin:
        if (auto const o = container_begin(kind::type_array); !o) [[unlikely]]
            return std::unexpected(o.error());

    arr_val_begin:
        if constexpr (Pretty)
            skip_indent();
        if (buf[cur] == '{') {
            ++cur;
            goto obj_begin;
        }
        if (buf[cur] == '[') {
            ++cur;
            goto arr_begin;
        }
        if (edn::char_is_num(byte(buf, cur))) {
            val_incr();
            ++ctn_len;
            r = read_num(buf, chars, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == '"') {
            val_incr();
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 't') {
            val_incr();
            ++ctn_len;
            r = read_true(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'f') {
            val_incr();
            ++ctn_len;
            r = read_false(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'n') {
            val_incr();
            ++ctn_len;
            r = read_null(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto arr_val_end;
            }
            goto fail;
        }
        if (buf[cur] == ']') {
            ++cur;
            if (ctn_len == 0) [[likely]]
                goto arr_end;
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto arr_val_begin;
        }
        goto fail;

    arr_val_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_comma.size()>(buf, cur, pretty_comma)) {
                cur += pretty_comma.size();
                goto arr_val_begin;
            }
        }
        if (buf[cur] == ',') {
            ++cur;
            goto arr_val_begin;
        }
        if (buf[cur] == ']') {
            ++cur;
            goto arr_end;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto arr_val_end;
        }
        goto fail;

    arr_end:
        if (auto const e = container_end(major_type::array); !e) [[unlikely]]
            return std::unexpected(e.error());
        goto ctn_end;

    obj_begin:
        if (auto const o = container_begin(kind::type_map); !o) [[unlikely]]
            return std::unexpected(o.error());

    obj_key_begin:
        if constexpr (Pretty)
            skip_indent();
        if (buf[cur] == '"') [[likely]] {
            val_incr();
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto obj_key_end;
            }
            goto fail;
        }
        if (buf[cur] == '}') [[likely]] {
            ++cur;
            if (ctn_len == 0) [[likely]]
                goto obj_end;
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_key_begin;
        }
        goto fail;

    obj_key_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_colon.size()>(buf, cur, pretty_colon)) {
                cur += pretty_colon.size();
                goto obj_val_begin;
            }
        }
        if (buf[cur] == ':') {
            ++cur;
            goto obj_val_begin;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_key_end;
        }
        goto fail;

    obj_val_begin:
        if (buf[cur] == '"') {
            ++val;
            ++ctn_len;
            r = read_str(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                if (auto const a = check_string_length(block.items[val]); !a) [[unlikely]]
                    return std::unexpected(a.error());
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (edn::char_is_num(byte(buf, cur))) {
            ++val;
            ++ctn_len;
            r = read_num(buf, chars, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == '{') {
            ++cur;
            goto obj_begin;
        }
        if (buf[cur] == '[') {
            ++cur;
            goto arr_begin;
        }
        if (buf[cur] == 't') {
            ++val;
            ++ctn_len;
            r = read_true(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'f') {
            ++val;
            ++ctn_len;
            r = read_false(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (buf[cur] == 'n') {
            ++val;
            ++ctn_len;
            r = read_null(buf, cur, block.items[val]);
            if (r != failed) [[likely]] {
                cur = r;
                goto obj_val_end;
            }
            goto fail;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_val_begin;
        }
        goto fail;

    obj_val_end:
        if constexpr (Pretty) {
            if (byte_match<pretty_comma.size()>(buf, cur, pretty_comma)) {
                cur += pretty_comma.size();
                goto obj_key_begin;
            }
        }
        if (buf[cur] == ',') [[likely]] {
            ++cur;
            goto obj_key_begin;
        }
        if (buf[cur] == '}') [[likely]] {
            ++cur;
            goto obj_end;
        }
        if (edn::char_is_space(byte(buf, cur))) {
            space();
            goto obj_val_end;
        }
        goto fail;

    obj_end:
        if (auto const e = container_end(major_type::map); !e) [[unlikely]]
            return std::unexpected(e.error());

    ctn_end:
        if (ctn == ctn_parent) [[unlikely]]
            goto doc_end;
        --depth;
        ctn = ctn_parent;
        ctn_len = static_cast<std::size_t>(kind::high_of(block.items[ctn].m_tag));
        newline();
        if ((block.items[ctn].m_tag & kind::type_mask) == kind::type_map)
            goto obj_val_end;
        goto arr_val_end;

    doc_end:
        if (space_end(text, cur) != len) [[unlikely]]
            goto fail;
        block.items.resize(val + 1);
        return std::make_unique<tree>(std::move(block), std::move(text), std::vector<tree::link>{});

    fail:
        return std::unexpected(error::syntax_error);
    }

    static std::expected<std::unique_ptr<tree>, error> read_root_single(padded_string text, std::size_t const cur)
    {
        value_block block = value_block::allocated(1);
        std::span<char> const buf = text.padded();
        node &x = block.items.front();
        std::size_t r = failed;
        unsigned char const c = byte(buf, cur);
        if (edn::char_is_num(c))
            r = read_num(buf, std::string_view(text), cur, x);
        else if (c == '"')
            r = read_str(buf, cur, x);
        else if (c == 't')
            r = read_true(buf, cur, x);
        else if (c == 'f')
            r = read_false(buf, cur, x);
        else if (c == 'n')
            r = read_null(buf, cur, x);
        if (r == failed) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (c == '"')
            if (auto const a = check_string_length(x); !a) [[unlikely]]
                return std::unexpected(a.error());
        if (space_end(text, r) != text.size()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        return std::make_unique<tree>(std::move(block), std::move(text), std::vector<tree::link>{});
    }

public:
    static std::expected<std::unique_ptr<tree>, error> read(std::string_view const json)
    {
        if (auto const r = decoder::check_input_size(json.size()); !r) [[unlikely]]
            return std::unexpected(r.error());
        padded_string text(json);
        std::size_t const cur = space_end(text, 0);
        if (cur >= text.size()) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if (text[cur] == '[' || text[cur] == '{') [[likely]] {
            if (std::ranges::all_of(
                    std::views::iota(std::size_t{1}, pretty_probe + 1),
                    [&text, cur](std::size_t const i) { return edn::char_is_space(text[cur + i]); }))
                return read_root<true>(std::move(text), cur);
            return read_root<false>(std::move(text), cur);
        }
        return read_root_single(std::move(text), cur);
    }
};

}

}

inline std::expected<owning<item>, error> parse_edn(std::string_view const text)
{
    if (auto read = detail::dom::reader::read(text)) [[likely]]
        return detail::tree_owner::tree_owned(std::move(*read));
    return diag2cbor(text).and_then([](std::string const &bytes) { return decode(std::string_view(bytes)); });
}

}

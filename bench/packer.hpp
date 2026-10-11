#pragma once

#include <bit>
#include <cstdint>
#include <cstring>
#include <stdfloat>
#include <string>
#include <string_view>
#include <vector>

namespace proto
{

#if defined(__STDCPP_FLOAT16_T__)
using half = std::float16_t;
#else
using half = _Float16;
#endif

inline std::size_t head_size(std::uint64_t const a)
{
    return a < 24 ? 1 : a <= 0xff ? 2 : a <= 0xffff ? 3 : a <= 0xffffffff ? 5 : 9;
}

inline char *head_put(char *p, std::uint8_t const major, std::uint64_t const a)
{
    std::uint8_t const m = static_cast<std::uint8_t>(major << 5);
    if (a < 24) {
        *p = static_cast<char>(m | a);
        return p + 1;
    }
    std::size_t const n = head_size(a) - 1;
    *p++ = static_cast<char>(m | (24 + std::countr_zero(n)));
    std::uint64_t const be = std::byteswap(a) >> (8 * (8 - n));
    std::memcpy(p, &be, n);
    return p + n;
}

inline std::size_t float_size(double const d)
{
    if (static_cast<double>(static_cast<half>(d)) == d || d != d)
        return 3;
    if (static_cast<double>(static_cast<float>(d)) == d)
        return 5;
    return 9;
}

inline char *float_put(char *p, double const d)
{
    switch (float_size(d)) {
    case 3: {
        *p++ = static_cast<char>(0xf9);
        auto const v = std::byteswap(std::bit_cast<std::uint16_t>(static_cast<half>(d)));
        std::memcpy(p, &v, 2);
        return p + 2;
    }
    case 5: {
        *p++ = static_cast<char>(0xfa);
        auto const v = std::byteswap(std::bit_cast<std::uint32_t>(static_cast<float>(d)));
        std::memcpy(p, &v, 4);
        return p + 4;
    }
    default: {
        *p++ = static_cast<char>(0xfb);
        auto const v = std::byteswap(std::bit_cast<std::uint64_t>(d));
        std::memcpy(p, &v, 8);
        return p + 8;
    }
    }
}

// A: direct, count given, append to a std::string.
struct direct {
    std::string out;
    void put(std::uint8_t const major, std::uint64_t const a)
    {
        char h[9];
        out.append(h, static_cast<std::size_t>(head_put(h, major, a) - h));
    }
    void unsigned_integer(std::uint64_t const v) { put(0, v); }
    void negative_integer(std::uint64_t const arg) { put(1, arg); }
    void byte_string(std::string_view const s) { put(2, s.size()); out.append(s); }
    void text_string(std::string_view const s) { put(3, s.size()); out.append(s); }
    void array(std::uint64_t const n) { put(4, n); }
    void map(std::uint64_t const n) { put(5, n); }
    void tag(std::uint64_t const n) { put(6, n); }
    void simple_value(std::uint8_t const v) { put(7, v); }
    void floating_point(double const d)
    {
        char h[9];
        out.append(h, static_cast<std::size_t>(float_put(h, d) - h));
    }
};

// C, first run: only sizes.
struct sizer {
    std::size_t n = 0;
    void unsigned_integer(std::uint64_t const v) { n += head_size(v); }
    void negative_integer(std::uint64_t const a) { n += head_size(a); }
    void byte_string(std::string_view const s) { n += head_size(s.size()) + s.size(); }
    void text_string(std::string_view const s) { n += head_size(s.size()) + s.size(); }
    void array(std::uint64_t const c) { n += head_size(c); }
    void map(std::uint64_t const c) { n += head_size(c); }
    void tag(std::uint64_t const t) { n += head_size(t); }
    void simple_value(std::uint8_t const v) { n += head_size(v); }
    void floating_point(double const d) { n += float_size(d); }
};

// C, second run: raw writes into memory of the exact size.
struct writer {
    char *p;
    void unsigned_integer(std::uint64_t const v) { p = head_put(p, 0, v); }
    void negative_integer(std::uint64_t const a) { p = head_put(p, 1, a); }
    void byte_string(std::string_view const s) { p = head_put(p, 2, s.size()); std::memcpy(p, s.data(), s.size()); p += s.size(); }
    void text_string(std::string_view const s) { p = head_put(p, 3, s.size()); std::memcpy(p, s.data(), s.size()); p += s.size(); }
    void array(std::uint64_t const c) { p = head_put(p, 4, c); }
    void map(std::uint64_t const c) { p = head_put(p, 5, c); }
    void tag(std::uint64_t const t) { p = head_put(p, 6, t); }
    void simple_value(std::uint8_t const v) { p = head_put(p, 7, v); }
    void floating_point(double const d) { p = float_put(p, d); }
};

template <class F>
std::string count_then_write(F const &f)
{
    sizer s;
    f(s);
    std::string out;
    out.resize_and_overwrite(s.n, [&](char *const b, std::size_t const n) {
        writer w{b};
        f(w);
        return n;
    });
    return out;
}

// B: batch; record, then size and write once. Containers by start and end.
struct batch {
    struct op {
        std::uint8_t major;
        bool is_float;
        std::uint64_t a;
        double d;
        std::string_view s;
    };
    std::vector<op> ops;
    std::vector<std::size_t> open;
    bool tagged_pending = false;
    void slot()
    {
        if (tagged_pending)
            tagged_pending = false;
        else if (!open.empty())
            ++ops[open.back()].a;
    }
    void rec(std::uint8_t const m, std::uint64_t const a, std::string_view const s = {})
    {
        slot();
        ops.push_back({m, false, a, 0, s});
    }
    void unsigned_integer(std::uint64_t const v) { rec(0, v); }
    void negative_integer(std::uint64_t const a) { rec(1, a); }
    void byte_string(std::string_view const s) { rec(2, s.size(), s); }
    void text_string(std::string_view const s) { rec(3, s.size(), s); }
    void simple_value(std::uint8_t const v) { rec(7, v); }
    void tag(std::uint64_t const t)
    {
        rec(6, t);
        tagged_pending = true;
    }
    void start(std::uint8_t const m)
    {
        rec(m, 0);
        open.push_back(ops.size() - 1);
    }
    void start_array() { start(4); }
    void start_map() { start(5); }
    void end_array() { open.pop_back(); }
    void end_map()
    {
        ops[open.back()].a /= 2;
        open.pop_back();
    }
    void floating_point(double const d)
    {
        slot();
        ops.push_back({7, true, 0, d, {}});
    }
    std::string finish() const
    {
        std::size_t n = 0;
        for (op const &o : ops)
            n += o.is_float ? float_size(o.d) : head_size(o.a) + o.s.size();
        std::string out;
        out.resize_and_overwrite(n, [&](char *p, std::size_t const size) {
            for (op const &o : ops) {
                if (o.is_float) {
                    p = float_put(p, o.d);
                } else {
                    p = head_put(p, o.major, o.a);
                    std::memcpy(p, o.s.data(), o.s.size());
                    p += o.s.size();
                }
            }
            return size;
        });
        return out;
    }
};

} // namespace proto

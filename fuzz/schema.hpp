#pragma once

#include "common.hpp"

#ifdef __cpp_impl_reflection

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <meta>
#include <optional>
#include <span>
#include <stdfloat>
#include <string>
#include <string_view>
#include <vector>

namespace fuzz::schema
{

enum class shade : std::uint8_t { dark, light };

struct [[=cbor::tag(1500)]] tire {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

struct [[=cbor::tag(1501)]] engine {
    std::uint16_t horsepower;
    std::int32_t torque;
    bool usesGas;
};

struct [[=cbor::tag(1502)]] vehicle {
    std::string make;
    std::int32_t balance;
    std::array<tire, 2> spare;
    std::vector<tire> wheels;
    engine motor;
    char code[4];
    std::array<std::byte, 4> mac;
    shade tone;
    std::optional<std::uint8_t> owner;
    std::vector<std::string> names;
    std::int64_t big;
    double d;
    bool flag;
};

struct [[=cbor::tag(1503)]] numbers {
    std::int8_t i8;
    std::uint8_t u8;
    std::int16_t i16;
    std::uint16_t u16;
    std::int32_t i32;
    std::uint32_t u32;
    std::int64_t i64;
    std::uint64_t u64;
    cbor::int128 i128;
    cbor::uint128 u128;
    std::float16_t f16;
    float f32;
    double f64;
    std::array<std::int16_t, 3> fixed;
    std::vector<std::uint32_t> list;
    std::vector<std::vector<std::int8_t>> nested;
    std::optional<std::int64_t> maybe;
};

struct [[=cbor::tag(1504)]] garage {
    std::vector<vehicle> cars;
    std::map<std::uint16_t, std::string> owners;
    std::map<std::int32_t, tire> stock;
    std::vector<std::uint8_t> blob;
    std::string note;
};

consteval bool character_p(std::meta::info const type)
{
    std::meta::info const t = std::meta::dealias(std::meta::remove_cv(type));
    return t == ^^char || t == ^^unsigned char || t == ^^std::byte || t == ^^char8_t;
}

consteval std::string decimal(std::size_t n)
{
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('0' + n % 10));
        n /= 10;
    } while (n != 0);
    return s;
}

// Every leaf path into a type, as an accessor reads it: each member, each element of a fixed array, the first
// elements of a list, down to a number, a bool, a float, a text or an optional of one. A path to a struct, a list or
// a map gives an accessor and is not listed here, and a map has no element path. A fixed string is one leaf; an
// index into it does not compile.
consteval void paths_of(std::meta::info type, std::string const &prefix, std::vector<std::string> &out, int depth)
{
    type = std::meta::remove_cv(type);
    if (depth > 6)
        return;
    if (std::meta::is_array_type(type)) {
        std::meta::info const element = std::meta::remove_all_extents(type);
        if (character_p(element)) {
            out.push_back(prefix);
            return;
        }
        std::size_t const n = std::meta::extent(type);
        for (std::size_t i = 0; i < n && i < 4; ++i)
            paths_of(element, prefix + "[" + decimal(i) + "]", out, depth + 1);
        return;
    }
    if (std::meta::has_template_arguments(type)) {
        std::meta::info const kind = std::meta::template_of(type);
        auto const arguments = std::meta::template_arguments_of(type);
        if (kind == ^^std::array) {
            std::meta::info const element = arguments.at(0);
            if (character_p(element)) {
                out.push_back(prefix);
                return;
            }
            std::size_t const n = std::meta::extract<std::size_t>(arguments.at(1));
            for (std::size_t i = 0; i < n && i < 4; ++i)
                paths_of(element, prefix + "[" + decimal(i) + "]", out, depth + 1);
            return;
        }
        if (kind == ^^std::vector) {
            std::meta::info const element = arguments.at(0);
            if (character_p(element)) {
                out.push_back(prefix);
                return;
            }
            for (std::size_t const i : {0uz, 1uz, 2uz, 7uz})
                paths_of(element, prefix + "[" + decimal(i) + "]", out, depth + 1);
            return;
        }
        if (kind == ^^std::map)
            return;
        out.push_back(prefix);
        return;
    }
    if (std::meta::is_class_type(type) && std::meta::is_aggregate_type(type)) {
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked()))
            paths_of(std::meta::type_of(m), prefix + "." + std::string(std::meta::identifier_of(m)), out, depth + 1);
        return;
    }
    out.push_back(prefix);
}

template <class T>
consteval std::vector<std::meta::info> path_texts()
{
    std::vector<std::string> paths;
    paths_of(^^T, "$", paths, 0);
    std::vector<std::meta::info> texts;
    for (std::string const &p : paths)
        texts.push_back(std::meta::reflect_constant_string(p));
    return texts;
}

template <std::meta::info Text>
consteval std::string_view text_of()
{
    return {[:Text:], std::meta::extent(std::meta::type_of(Text)) - 1};
}

template <std::meta::info Text>
inline constexpr cbor::fixed_string<std::meta::extent(std::meta::type_of(Text))> path_text{[:Text:]};

consteval std::size_t step_end(std::string_view const path, std::size_t const from)
{
    for (std::size_t i = from; i < path.size(); ++i)
        if (path.at(i) == '.' || path.at(i) == '[')
            return i;
    return path.size();
}

consteval std::size_t close_of(std::string_view const path, std::size_t const from)
{
    std::size_t i = from;
    while (path.at(i) != ']')
        ++i;
    return i;
}

consteval std::size_t index_of(std::string_view const path, std::size_t const from, std::size_t const close)
{
    std::size_t n = 0;
    for (std::size_t i = from; i < close; ++i)
        n = n * 10 + static_cast<std::size_t>(path.at(i) - '0');
    return n;
}

consteval std::meta::info member_named(std::meta::info const type, std::string_view const name)
{
    for (std::meta::info const m : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked()))
        if (std::meta::identifier_of(m) == name)
            return m;
    return std::meta::info{};
}

// The native value at a path, read with the same grammar: a member by its name, an element by its index.
// An index past the end of a list gives nothing, as the reader gives index_out_of_bounds.
template <class T, std::meta::info Text, std::size_t At>
auto const *native_at(T const &v)
{
    constexpr std::string_view path = text_of<Text>();
    if constexpr (At == path.size()) {
        return &v;
    } else if constexpr (path.at(At) == '.') {
        constexpr std::size_t end = step_end(path, At + 1);
        constexpr std::meta::info member = member_named(^^T, path.substr(At + 1, end - At - 1));
        return native_at<std::remove_cvref_t<decltype(v.[:member:])>, Text, end>(v.[:member:]);
    } else {
        constexpr std::size_t close = close_of(path, At);
        constexpr std::size_t index = index_of(path, At + 1, close);
        auto const elements = std::span(v);
        using E = std::remove_cvref_t<decltype(elements.front())>;
        using R = decltype(native_at<E, Text, close + 1>(elements.front()));
        if (index >= elements.size())
            return R{nullptr};
        return native_at<E, Text, close + 1>(elements.subspan(index).front());
    }
}

template <class T>
concept expected_type = requires(T const &t) {
    t.has_value();
    t.error();
};

template <class T>
concept optional_type = requires(T const &t) {
    t.has_value();
    t.value();
    typename T::value_type;
} && !expected_type<T>;

template <class T>
bool same_scalar(T const a, T const b)
{
    if constexpr (std::floating_point<T>) {
        if (a != a || b != b)
            return a != a && b != b;
        return std::bit_cast<std::array<std::byte, sizeof(T)>>(a) == std::bit_cast<std::array<std::byte, sizeof(T)>>(b);
    } else {
        return a == b;
    }
}

template <class N>
std::string_view native_bytes(N const &n)
{
    auto const s = std::as_bytes(std::span(n));
    return {reinterpret_cast<char const *>(s.data()), s.size()};
}

// The reader's answer at a path against the native value at the same path.
template <class N, class R>
void compare(N const *native, R const &read)
{
    if constexpr (expected_type<R>) {
        if (native == nullptr) {
            require(!read.has_value());
            return;
        }
        require(read.has_value());
        compare(native, *read);
    } else {
        require(native != nullptr);
        if constexpr (std::is_same_v<R, std::string_view>) {
            require(read == native_bytes(*native));
        } else if constexpr (std::is_same_v<R, std::optional<std::string_view>>) {
            require(read.has_value() == native->has_value());
            if (read)
                require(*read == native_bytes(**native));
        } else if constexpr (optional_type<R>) {
            require(read.has_value() == native->has_value());
            if (read)
                require(same_scalar(*read, **native));
        } else {
            require(same_scalar(read, *native));
        }
    }
}

// A text must lie inside the message.
template <class R>
void consume(R const &read, std::string_view const message)
{
    if constexpr (expected_type<R> || optional_type<R>) {
        if (read)
            consume(*read, message);
    } else if constexpr (std::is_same_v<R, std::string_view>) {
        std::string_view const part = read;
        std::less<char const *> const before;
        require(part.empty() || (!before(part.data(), message.data()) &&
                                 !before(message.data() + message.size(), part.data() + part.size())));
    }
}

// Every leaf path of the type is read from the message through one accessor.
template <class T>
void read_all(std::string_view const message)
{
    static constexpr auto texts = std::define_static_array(path_texts<T>());
    auto const owner = std::make_shared<int const>(0);
    auto lot = cbor::schema<T>::path(owner, message);
    if (!lot)
        return;
    template for (constexpr std::meta::info text : texts)
        consume(lot->template at<path_text<text>>(), message);
}

template <class T>
void compare_all(T const &native, std::string_view message);

template <class T>
void read_target(std::string_view const message)
{
    read_all<T>(message);
    auto const value = cbor::schema<T>::decode(message);
    if (value) {
        auto const again = cbor::schema<T>::encode(**value);
        require(again.has_value());
        compare_all(**value, *again);
    }
}

// A native value from the bytes of the input, member by member. Text is ASCII, because the encoder writes
// it as it is and a reader of the databind form expects UTF-8.
template <class T>
T filled(source &in, int depth);

template <class T>
void fill(T &v, source &in, int const depth)
{
    if constexpr (std::is_same_v<T, bool>) {
        v = in.byte() & 1;
    } else if constexpr (std::is_enum_v<T>) {
        v = static_cast<T>(in.byte());
    } else if constexpr (std::is_same_v<T, std::byte>) {
        v = std::byte{in.byte()};
    } else if constexpr (std::is_same_v<T, char>) {
        v = static_cast<char>(in.byte() & 0x7f);
    } else if constexpr (std::is_integral_v<T> || std::is_same_v<T, cbor::int128> ||
                         std::is_same_v<T, cbor::uint128>) {
        cbor::uint128 bits = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i)
            bits = bits << 8 | in.byte();
        v = static_cast<T>(bits);
    } else if constexpr (std::is_floating_point_v<T> || std::is_same_v<T, std::float16_t>) {
        std::array<std::byte, sizeof(T)> raw;
        for (std::byte &b : raw)
            b = std::byte{in.byte()};
        v = std::bit_cast<T>(raw);
    } else if constexpr (std::is_same_v<T, std::string>) {
        v = in.text();
    } else if constexpr (std::is_array_v<T>) {
        for (auto &e : v)
            fill(e, in, depth + 1);
    } else if constexpr (requires { std::tuple_size<T>::value; }) {
        for (auto &e : v)
            fill(e, in, depth + 1);
    } else if constexpr (requires { typename T::key_type; typename T::mapped_type; }) {
        for (std::size_t n = depth > 3 ? 0 : in.byte() % 4; n > 0; --n) {
            typename T::key_type k{};
            fill(k, in, depth + 1);
            fill(v.try_emplace(k).first->second, in, depth + 1);
        }
    } else if constexpr (requires { typename T::value_type; v.has_value(); }) {
        if (in.byte() & 1) {
            typename T::value_type e{};
            fill(e, in, depth + 1);
            v = e;
        }
    } else if constexpr (requires { v.push_back(typename T::value_type{}); }) {
        for (std::size_t n = depth > 3 ? 0 : in.byte() % 5; n > 0; --n) {
            typename T::value_type e{};
            fill(e, in, depth + 1);
            v.push_back(std::move(e));
        }
    } else {
        template for (constexpr std::meta::info m :
                      std::define_static_array(std::meta::nonstatic_data_members_of(
                          ^^T, std::meta::access_context::unchecked())))
            fill(v.[:m:], in, depth + 1);
    }
}

template <class T>
void compare_all(T const &native, std::string_view const message)
{
    static constexpr auto texts = std::define_static_array(path_texts<T>());
    auto lot = cbor::schema<T>::path(message);
    require(lot.has_value());
    template for (constexpr std::meta::info text : texts)
        compare(native_at<T, text, 1>(native), lot->template at<path_text<text>>());
}

// A struct from the bytes of the input, written and read back through every path of its type. The message
// must also be one well-formed CBOR item.
template <class T>
void round_trip(source &in)
{
    T native{};
    fill(native, in, 0);
    string_writer w;
    auto const written = cbor::schema<T>::encode(native, w);
    require(written.has_value());
    auto const whole = cbor::schema<T>::encode(native);
    require(whole.has_value() && *whole == w.encoded && *written == w.encoded.size());
    std::string prefixed = in.string();
    std::size_t const before = prefixed.size();
    require(cbor::schema<T>::encode(native, prefixed).has_value() && std::string_view(prefixed).substr(before) == w.encoded);
    std::vector<char> chars(in.byte() % 4);
    std::size_t const used = chars.size();
    require(cbor::schema<T>::encode(native, chars).has_value() &&
            std::string_view(chars.data(), chars.size()).substr(used) == w.encoded);
    std::vector<char> room(in.number() % (w.encoded.size() + 16));
    auto const placed = cbor::schema<T>::encode(native, std::span(room));
    require(placed.has_value() == (room.size() >= w.encoded.size()));
    if (placed)
        require(std::string_view(room.data(), *placed) == w.encoded);
    else
        require(placed.error() == std::errc::no_buffer_space);
    auto const back = cbor::schema<T>::decode(w.encoded);
    require(back.has_value());
    auto const again = cbor::schema<T>::encode(**back);
    require(again.has_value() && *again == w.encoded);
    auto const end = cbor::item_end<64>(w.encoded);
    require(end.has_value() && *end == w.encoded.size());
    compare_all(native, w.encoded);
}

inline void decode_target(std::string_view const input)
{
    std::string_view const message = input.size() > 16 ? input.substr(16) : std::string_view{};
    read_target<vehicle>(message);
    read_target<numbers>(message);
    read_target<garage>(message);
}

inline void encode_target(std::string_view const input)
{
    source in{input};
    switch (in.byte() % 3) {
    case 0:
        round_trip<vehicle>(in);
        break;
    case 1:
        round_trip<numbers>(in);
        break;
    default:
        round_trip<garage>(in);
        break;
    }
}

} // namespace fuzz::schema

#endif

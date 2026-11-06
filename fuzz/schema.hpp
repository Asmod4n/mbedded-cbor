#pragma once

#include "common.hpp"

#include <cbor/describe.hpp>
#include <cbor/error.hpp>
#include <cbor/item_size.hpp>
#include <cbor/schema.hpp>

#ifdef __cpp_impl_reflection

#include <algorithm>
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
#include <variant>
#include <vector>

namespace fuzz::schema
{

enum class shade : std::uint8_t { dark, light };

struct tire {
    std::uint16_t diameter;
    float airPressure;
    bool snowTires;
};

struct engine {
    std::uint16_t horsepower;
    std::int32_t torque;
    bool usesGas;
};

struct vehicle {
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

struct numbers {
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

struct machine {
    std::string maker;
};

struct sedan : machine {
    std::uint8_t doors;
};

struct truck : machine {
    std::vector<tire> axles;
};

struct garage {
    std::vector<vehicle> cars;
    std::map<std::uint16_t, std::string> owners;
    std::map<std::int32_t, tire> stock;
    std::vector<std::uint8_t> blob;
    std::string note;
    std::variant<sedan, truck> parked;
    std::variant<bool, std::int32_t, double, std::string> label;
};

} // namespace fuzz::schema

template <>
struct cbor::describe_members<fuzz::schema::tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1500};
    static constexpr std::tuple members{
        CBOR_MEMBER(fuzz::schema::tire, diameter),
        CBOR_MEMBER(fuzz::schema::tire, airPressure),
        CBOR_MEMBER(fuzz::schema::tire, snowTires),
    };
};

template <>
struct cbor::describe_members<fuzz::schema::engine> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1501};
    static constexpr std::tuple members{
        CBOR_MEMBER(fuzz::schema::engine, horsepower),
        CBOR_MEMBER(fuzz::schema::engine, torque),
        CBOR_MEMBER(fuzz::schema::engine, usesGas),
    };
};

template <>
struct cbor::describe_members<fuzz::schema::vehicle> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1502};
    static constexpr std::tuple members{
        CBOR_MEMBER(fuzz::schema::vehicle, make),
        CBOR_MEMBER(fuzz::schema::vehicle, balance),
        CBOR_MEMBER(fuzz::schema::vehicle, spare),
        CBOR_MEMBER(fuzz::schema::vehicle, wheels),
        CBOR_MEMBER(fuzz::schema::vehicle, motor),
        CBOR_MEMBER(fuzz::schema::vehicle, code),
        CBOR_MEMBER(fuzz::schema::vehicle, mac),
        CBOR_MEMBER(fuzz::schema::vehicle, tone),
        CBOR_MEMBER(fuzz::schema::vehicle, owner),
        CBOR_MEMBER(fuzz::schema::vehicle, names),
        CBOR_MEMBER(fuzz::schema::vehicle, big),
        CBOR_MEMBER(fuzz::schema::vehicle, d),
        CBOR_MEMBER(fuzz::schema::vehicle, flag),
    };
};

template <>
struct cbor::describe_members<fuzz::schema::numbers> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1503};
    static constexpr std::tuple members{
        CBOR_MEMBER(fuzz::schema::numbers, i8),
        CBOR_MEMBER(fuzz::schema::numbers, u8),
        CBOR_MEMBER(fuzz::schema::numbers, i16),
        CBOR_MEMBER(fuzz::schema::numbers, u16),
        CBOR_MEMBER(fuzz::schema::numbers, i32),
        CBOR_MEMBER(fuzz::schema::numbers, u32),
        CBOR_MEMBER(fuzz::schema::numbers, i64),
        CBOR_MEMBER(fuzz::schema::numbers, u64),
        CBOR_MEMBER(fuzz::schema::numbers, i128),
        CBOR_MEMBER(fuzz::schema::numbers, u128),
        CBOR_MEMBER(fuzz::schema::numbers, f16),
        CBOR_MEMBER(fuzz::schema::numbers, f32),
        CBOR_MEMBER(fuzz::schema::numbers, f64),
        CBOR_MEMBER(fuzz::schema::numbers, fixed),
        CBOR_MEMBER(fuzz::schema::numbers, list),
        CBOR_MEMBER(fuzz::schema::numbers, nested),
        CBOR_MEMBER(fuzz::schema::numbers, maybe),
    };
};

template <>
struct cbor::describe_members<fuzz::schema::garage> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1504};
    static constexpr std::tuple members{
        CBOR_MEMBER(fuzz::schema::garage, cars),
        CBOR_MEMBER(fuzz::schema::garage, owners),
        CBOR_MEMBER(fuzz::schema::garage, stock),
        CBOR_MEMBER(fuzz::schema::garage, blob),
        CBOR_MEMBER(fuzz::schema::garage, note),
        CBOR_MEMBER(fuzz::schema::garage, parked),
        CBOR_MEMBER(fuzz::schema::garage, label),
    };
};

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(fuzz::schema::sedan, fuzz::schema::machine, cbor::tag{1505}, maker, doors);
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(fuzz::schema::truck, fuzz::schema::machine, cbor::tag{1506}, maker, axles);

namespace fuzz::schema
{

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
        if (kind == ^^std::map || kind == ^^std::variant)
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
            require(std::ranges::equal(std::as_bytes(std::span(read)), std::as_bytes(std::span(*native))));
        } else if constexpr (std::is_same_v<R, std::optional<std::string_view>>) {
            require(read.has_value() == native->has_value());
            if (read)
                require(std::ranges::equal(std::as_bytes(std::span(*read)), std::as_bytes(std::span(**native))));
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
                                 !before(std::to_address(message.end()), std::to_address(part.end()))));
    }
}

// Every leaf path of the type is read from the message through one accessor.
template <class T>
void read_all(std::string_view const message)
{
    static constexpr auto texts = std::define_static_array(path_texts<T>());
    auto const owner = std::make_shared<std::string const>(message);
    auto lot = cbor::schema<T>::path(owner);
    if (!lot)
        return;
    template for (constexpr std::meta::info text : texts)
        consume(lot->template at_path<path_text<text>>(), *owner);
    if constexpr (std::is_same_v<T, garage>)
        parked_raw(*lot, *owner);
}

template <class T>
void compare_all(T const &native, std::string_view message);

// The errors that an unverified form may give on raw input (doc/reference/schema/unverified.md): it skips the
// checks of the data and keeps every check of a bound, so it never raises and never reads outside the input. The
// head of a variant item chooses the alternative, so a type with a variant member also gives incorrect_type.
template <class T>
bool unverified_error(cbor::error const e)
{
    using enum cbor::error;
    return e == too_little_data || e == index_out_of_bounds || e == unpopulated_table_index || e == syntax_error ||
           e == nesting_depth_exceeded || e == not_enough_memory || e == string_length_exceeded ||
           e == container_elements_exceeded || (std::is_same_v<T, garage> && e == incorrect_type);
}

// The unverified forms on raw input give a value or one of their errors, and every leaf path reads inside the
// message through an unverified accessor.
template <class T>
void unverified_raw(std::string_view const message)
{
    auto const decoded = cbor::schema<T>::unverified_decode(std::string(message));
    if (!decoded)
        require(unverified_error<T>(decoded.error()));
    static constexpr auto texts = std::define_static_array(path_texts<T>());
    auto const owner = std::make_shared<std::string const>(message);
    auto lot = cbor::schema<T>::unverified_path(owner);
    if (!lot) {
        require(unverified_error<T>(lot.error()));
        return;
    }
    template for (constexpr std::meta::info text : texts)
        consume(lot->template at_path<path_text<text>>(), *owner);
    if constexpr (std::is_same_v<T, garage>)
        parked_raw(*lot, *owner);
}

// The methods of an accessor read what the root reads: a list accessor counts as many elements through begin and
// end as size says, an element read through the iterator and through '@' with a run-time index is the same, and a
// '$' path from inside reads from the root.
inline void garage_accessors(std::string const &written)
{
    auto const root = cbor::schema<garage>::path(std::string(written));
    require(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    require(cars.has_value());
    std::size_t n = 0;
    for (auto it = cars->begin(); it != cars->end(); ++it) {
        auto const car = *it;
        auto const through_element = car.at_path<"@.balance">();
        auto const through_index = cars->at_path<"@[].balance">(n);
        require(through_element.has_value() && through_index.has_value() && *through_element == *through_index);
        auto const indexed = cars->at_path<"@[]">(n);
        require(indexed.has_value());
        auto const again = indexed->at_path<"@.balance">();
        require(again.has_value() && *again == *through_element);
        ++n;
    }
    require(n == cars->size());
    auto const note_from_inside = cars->at_path<"$.note">();
    auto const note = root->at_path<"$.note">();
    require(note_from_inside.has_value() && note.has_value() && *note_from_inside == *note);
}

// The accessor of the variant member gives the index of the alternative that decode reads, and the accessor of
// that alternative reads its members as decode does; the other alternative is incorrect_type.
inline void garage_parked(std::string const &written)
{
    auto const root = cbor::schema<garage>::path(std::string(written));
    auto const decoded = cbor::schema<garage>::decode(std::string(written));
    require(root.has_value() && decoded.has_value());
    auto const &native = (*decoded)->parked;
    auto const parked = root->at_path<"$.parked">();
    require(parked.has_value() && parked->index() == native.index());
    auto const s = parked->get<sedan>();
    auto const t = parked->get<truck>();
    require(s.has_value() != t.has_value());
    if (s) {
        auto const doors = s->at_path<"@.doors">();
        auto const maker = s->at_path<"@.maker">();
        require(doors.has_value() && *doors == std::get<sedan>(native).doors);
        require(maker.has_value() && *maker == std::get<sedan>(native).maker);
        require(t.error() == cbor::error::incorrect_type);
    } else {
        auto const axles = t->at_path<"@.axles">();
        auto const maker = t->at_path<"@.maker">();
        require(axles.has_value() && axles->size() == std::get<truck>(native).axles.size());
        require(maker.has_value() && *maker == std::get<truck>(native).maker);
        require(s.error() == cbor::error::incorrect_type);
    }
    auto const label = root->at_path<"$.label">();
    auto const &value = (*decoded)->label;
    require(label.has_value() && label->index() == value.index());
    auto const flag = label->get<bool>();
    auto const number = label->get<std::int32_t>();
    auto const real = label->get<double>();
    auto const text = label->get<std::string>();
    require(int{flag.has_value()} + int{number.has_value()} + int{real.has_value()} + int{text.has_value()} == 1);
    if (flag)
        require(*flag == std::get<bool>(value));
    if (number)
        require(*number == std::get<std::int32_t>(value));
    if (real)
        require(same_scalar(*real, std::get<double>(value)));
    if (text)
        require(*text == std::get<std::string>(value));
}

// The variant members of raw input read inside the message through the verified and the unverified accessor.
template <class Lot>
void parked_raw(Lot const &lot, std::string_view const message)
{
    if (auto const parked = lot.template at_path<"$.parked">()) {
        require(parked->index() < 2);
        if (auto const s = parked->template get<sedan>()) {
            consume(s->template at_path<"@.maker">(), message);
            consume(s->template at_path<"@.doors">(), message);
        }
        if (auto const t = parked->template get<truck>()) {
            consume(t->template at_path<"@.maker">(), message);
            consume(t->template at_path<"@.axles[0].diameter">(), message);
        }
    }
    if (auto const label = lot.template at_path<"$.label">()) {
        require(label->index() < 4);
        consume(label->template get<std::string>(), message);
        (void)label->template get<bool>();
        (void)label->template get<std::int32_t>();
        (void)label->template get<double>();
    }
}

// A typed array view gives each element through [], at, the iterator, front, back, first and last; an index or a
// count past the end is an error.
inline void numbers_view(std::string const &written)
{
    auto const root = cbor::schema<numbers>::path(std::string(written));
    require(root.has_value());
    auto const blob = root->view<"$.list">();
    require(blob.has_value());
    std::size_t i = 0;
    for (std::uint32_t const e : *blob) {
        auto const at = blob->at(i);
        require(at.has_value() && *at == e && (*blob)[i] == e);
        ++i;
    }
    require(i == blob->size());
    if (blob->size() != 0) {
        require(blob->front() == (*blob)[0] && blob->back() == (*blob)[blob->size() - 1]);
        auto const head = blob->first(1);
        auto const tail = blob->last(1);
        require(head.has_value() && tail.has_value() && head->size() == 1 && tail->size() == 1);
        require(head->front() == blob->front() && tail->back() == blob->back());
    }
    require(!blob->at(blob->size()).has_value());
    require(!blob->first(blob->size() + 1).has_value());
}

// Every form of decode and path reads the written message to the value of the std::string form, and the
// unverified forms read it as the verified ones do: the value is compared by its encoding.
template <class T>
void read_forms(std::string const &written)
{
    auto const bytes = std::make_shared<std::vector<char> const>(written.begin(), written.end());
    auto const encoded_as = [&](auto const &decoded) {
        require(decoded.has_value());
        auto const again = cbor::schema<T>::encode(**decoded);
        require(again.has_value() && *again == written);
    };
    encoded_as(cbor::schema<T>::decode(bytes));
    encoded_as(cbor::schema<T>::decode(bytes, std::span<char const>(*bytes)));
    encoded_as(cbor::schema<T>::unverified_decode(std::string(written)));
    encoded_as(cbor::schema<T>::unverified_decode(bytes));
    encoded_as(cbor::schema<T>::unverified_decode(bytes, std::span<char const>(*bytes)));
    require(cbor::schema<T>::path(std::string(written)).has_value());
    require(cbor::schema<T>::path(bytes).has_value());
    require(cbor::schema<T>::path(bytes, std::span<char const>(*bytes)).has_value());
    require(cbor::schema<T>::unverified_path(bytes, std::span<char const>(*bytes)).has_value());
    if constexpr (std::is_same_v<T, garage>) {
        garage_accessors(written);
        garage_parked(written);
    }
    if constexpr (std::is_same_v<T, numbers>)
        numbers_view(written);
}

template <class T>
void read_target(std::string_view const message)
{
    read_all<T>(message);
    unverified_raw<T>(message);
    auto const value = cbor::schema<T>::decode(std::string(message));
    if (value) {
        auto const again = cbor::schema<T>::encode(**value);
        require(again.has_value());
        compare_all(**value, *again);
        read_forms<T>(*again);
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
        for (std::size_t n = depth > 3 ? 0uz : in.byte() % 4uz; n > 0; --n) {
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
    } else if constexpr (requires { std::variant_size<T>::value; }) {
        std::size_t const k = in.byte() % std::variant_size_v<T>;
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            ((k == K ? fill(v.template emplace<K>(), in, depth + 1) : void()), ...);
        }(std::make_index_sequence<std::variant_size_v<T>>{});
    } else if constexpr (requires { v.push_back(typename T::value_type{}); }) {
        for (std::size_t n = depth > 3 ? 0uz : in.byte() % 5uz; n > 0; --n) {
            typename T::value_type e{};
            fill(e, in, depth + 1);
            v.push_back(std::move(e));
        }
    } else {
        template for (constexpr std::meta::info b :
                      std::define_static_array(std::meta::bases_of(^^T, std::meta::access_context::unchecked())))
            fill(static_cast<typename[:std::meta::type_of(b):] &>(v), in, depth + 1);
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
    auto lot = cbor::schema<T>::path(std::string(message));
    require(lot.has_value());
    template for (constexpr std::meta::info text : texts)
        compare(native_at<T, text, 1>(native), lot->template at_path<path_text<text>>());
}

// With the description apart, the data of a message reads back through decode, unverified_decode and path with
// the same description place, and writes the same bytes again.
template <class T>
void apart_round_trip(T const &native)
{
    constexpr auto apart = cbor::description_place::apart;
    auto const data = cbor::schema<T>::template encode<apart>(native);
    require(data.has_value());
    auto const bytes = std::make_shared<std::vector<char> const>(data->begin(), data->end());
    auto const encoded_as = [&](auto const &decoded) {
        require(decoded.has_value());
        auto const again = cbor::schema<T>::template encode<apart>(**decoded);
        require(again.has_value() && *again == *data);
    };
    encoded_as(cbor::schema<T>::template decode<apart>(std::string(*data)));
    encoded_as(cbor::schema<T>::template decode<apart>(bytes, std::span<char const>(*bytes)));
    encoded_as(cbor::schema<T>::template unverified_decode<apart>(std::string(*data)));
    require(cbor::schema<T>::template path<apart>(std::string(*data)).has_value());
    require(cbor::schema<T>::template unverified_path<apart>(bytes, std::span<char const>(*bytes)).has_value());
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
            std::string_view(std::span<char const>(chars)).substr(used) == w.encoded);
    std::vector<char> room(in.number() % (w.encoded.size() + 16));
    auto const placed = cbor::schema<T>::encode(native, std::span(room));
    require(placed.has_value() == (room.size() >= w.encoded.size()));
    if (placed)
        require(std::string_view(std::span<char const>(room).first(*placed)) == w.encoded);
    else
        require(placed.error() == cbor::error::no_buffer_space);
    auto const back = cbor::schema<T>::decode(std::string(w.encoded));
    require(back.has_value());
    auto const again = cbor::schema<T>::encode(**back);
    require(again.has_value() && *again == w.encoded);
    auto const end = cbor::item_size(w.encoded);
    require(end.has_value() && *end == w.encoded.size());
    compare_all(native, w.encoded);
    apart_round_trip(native);
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

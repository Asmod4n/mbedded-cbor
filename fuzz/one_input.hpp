#pragma once

#include "../test/host.hpp"
#include "../test/ref_host.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace fuzz
{

inline constexpr std::array<std::string_view, 12> paths{
    "$",       "$.a",         "$.a.b.c",          "$[0]",          "$[-1]", "$[*]", "$.a[*]", "$.items[*].id",
    "$[*][*]", "$.a[0][*].b", "$[\"weird key\"]", "$.a[*].b[*].c",
};

// A finding of the fuzzer is a wrong answer as much as a crash, so every check that fails ends the run.
inline void require(bool const holds, std::source_location const where = std::source_location::current())
{
    if (!holds) {
        std::fprintf(stderr, "fuzz: check failed at line %u\n", static_cast<unsigned>(where.line()));
        std::abort();
    }
}

// RFC 8949 3.4.3: a bignum whose magnitude fits 64 bits is the same number as the integer, and the encoder
// writes the integer. The comparison therefore reads both forms as one.
inline std::string magnitude_without_leading_zeros(std::string_view m)
{
    while (!m.empty() && m.front() == '\0')
        m.remove_prefix(1);
    return std::string(m);
}

inline std::uint64_t magnitude_value(std::string_view const m)
{
    std::uint64_t v = 0;
    for (char const c : m)
        v = v << 8 | static_cast<unsigned char>(c);
    return v;
}

inline std::string magnitude_minus_one(std::string m)
{
    for (auto it = m.rbegin(); it != m.rend(); ++it) {
        if (*it != '\0') {
            *it = static_cast<char>(static_cast<unsigned char>(*it) - 1);
            break;
        }
        *it = '\xff';
    }
    return magnitude_without_leading_zeros(m);
}

inline test::value canonical(test::value const &v)
{
    if (auto const *b = std::get_if<test::bignum>(&v.kind)) {
        std::string const m = magnitude_without_leading_zeros(b->magnitude);
        if (!b->negative)
            return m.size() <= 8 ? test::value{magnitude_value(m)} : test::value{test::bignum{false, m}};
        if (m.empty())
            return v;
        std::string const n = magnitude_minus_one(m);
        return n.size() <= 8 ? test::value{test::negative{magnitude_value(n)}} : test::value{test::bignum{true, m}};
    }
    if (auto const *a = std::get_if<test::array>(&v.kind)) {
        test::array out;
        for (auto const &e : *a)
            out.push_back(canonical(e));
        return {std::move(out)};
    }
    if (auto const *m = std::get_if<test::map>(&v.kind)) {
        test::map out;
        for (auto const &[k, x] : *m)
            out.push_back(test::entry{canonical(k), canonical(x)});
        return {std::move(out)};
    }
    if (auto const *t = std::get_if<test::tagged>(&v.kind)) {
        test::array content;
        for (auto const &e : t->content)
            content.push_back(canonical(e));
        return {test::tagged{t->tag, std::move(content)}};
    }
    return v;
}

// A float compares by its bits, so -0.0 and 0.0 differ; two NaN are the same, as the encoder may give a NaN
// its shortest form (RFC 8949 4.2.2).
inline bool same(test::value const &a, test::value const &b)
{
    if (a.kind.index() != b.kind.index())
        return false;
    if (auto const *x = std::get_if<double>(&a.kind)) {
        double const y = std::get<double>(b.kind);
        if (std::isnan(*x) || std::isnan(y))
            return std::isnan(*x) && std::isnan(y);
        return std::bit_cast<std::uint64_t>(*x) == std::bit_cast<std::uint64_t>(y);
    }
    if (auto const *x = std::get_if<test::array>(&a.kind)) {
        auto const &y = std::get<test::array>(b.kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same(x->at(i), y.at(i)))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::map>(&a.kind)) {
        auto const &y = std::get<test::map>(b.kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same(x->at(i).key, y.at(i).key) || !same(x->at(i).val, y.at(i).val))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<test::tagged>(&a.kind)) {
        auto const &y = std::get<test::tagged>(b.kind);
        return x->tag == y.tag && same(test::value{x->content}, test::value{y.content});
    }
    return a == b;
}

inline bool same_number(test::value const &a, test::value const &b)
{
    return same(canonical(a), canonical(b));
}

// The bytes of the input, taken from the front. When they run out every read gives zero.
struct source {
    std::string_view bytes;

    std::uint8_t byte()
    {
        if (bytes.empty())
            return 0;
        auto const b = static_cast<std::uint8_t>(bytes.front());
        bytes.remove_prefix(1);
        return b;
    }

    std::uint64_t number()
    {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v = v << 8 | byte();
        return v;
    }

    std::string string()
    {
        std::size_t const n = byte() % 40;
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
            s.push_back(static_cast<char>(byte()));
        return s;
    }

    std::string text()
    {
        std::string s = string();
        for (char &c : s)
            c = static_cast<char>(c & 0x7f);
        return s;
    }
};

// What the encoder refuses by RFC 8949: a simple value from 24 to 31 (3.3), and a negative bignum of
// magnitude 0, which no CBOR item holds (3.4.3).
struct refusal {
    bool reserved_simple = false;
    bool zero_negative = false;
};

inline bool tag_has_meaning(std::uint64_t const tag)
{
    return tag == 2 || tag == 3 || tag == 24 || tag == 28 || tag == 29 || (tag >= 64 && tag <= 87);
}

// A value of the test host from the bytes of the input, with every kind the encoder knows. A text is ASCII,
// because the host promises the encoder valid UTF-8.
inline test::value value_from(source &in, refusal &r, int const depth)
{
    std::uint8_t const k = in.byte() % (depth > 6 ? 7 : 10);
    switch (k) {
    case 0:
        return {in.number()};
    case 1:
        return {test::negative{in.number()}};
    case 2: {
        bool const negative = in.byte() & 1;
        std::string m = in.string();
        if (negative && magnitude_without_leading_zeros(m).empty())
            r.zero_negative = true;
        return {test::bignum{negative, std::move(m)}};
    }
    case 3:
        return {test::bytes{in.string()}};
    case 4:
        return {in.text()};
    case 5:
        return {std::bit_cast<double>(in.number())};
    case 6: {
        std::uint8_t const s = in.byte();
        if (s >= 24 && s < 32)
            r.reserved_simple = true;
        return {test::simple{s}};
    }
    case 7: {
        test::array a;
        for (std::size_t n = in.byte() % 5; n > 0; --n)
            a.push_back(value_from(in, r, depth + 1));
        return {std::move(a)};
    }
    case 8: {
        test::map m;
        for (std::size_t n = in.byte() % 4; n > 0; --n) {
            test::value key = value_from(in, r, depth + 1);
            m.push_back(test::entry{std::move(key), value_from(in, r, depth + 1)});
        }
        return {std::move(m)};
    }
    default: {
        std::uint64_t tag = in.number();
        if (tag_has_meaning(tag))
            tag += std::uint64_t{1} << 40;
        return {test::tagged{tag, test::array{value_from(in, r, depth + 1)}}};
    }
    }
}

// A graph of the reference host is compared node by node. The map pairs the nodes already seen, so two
// graphs are the same only when their sharing is the same.
inline bool same_graph(shared_test::handle const &a, shared_test::handle const &b,
                       std::map<shared_test::node const *, shared_test::node const *> &pairs)
{
    auto const [at, fresh] = pairs.try_emplace(a.get(), b.get());
    if (!fresh)
        return at->second == b.get();
    if (a->kind.index() != b->kind.index())
        return false;
    if (auto const *x = std::get_if<std::vector<shared_test::handle>>(&a->kind)) {
        auto const &y = std::get<std::vector<shared_test::handle>>(b->kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same_graph(x->at(i), y.at(i), pairs))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(&a->kind)) {
        auto const &y = std::get<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(b->kind);
        if (x->size() != y.size())
            return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!same_graph(x->at(i).first, y.at(i).first, pairs) ||
                !same_graph(x->at(i).second, y.at(i).second, pairs))
                return false;
        return true;
    }
    if (auto const *x = std::get_if<shared_test::object>(&a->kind)) {
        auto const &y = std::get<shared_test::object>(b->kind);
        return x->tag == y.tag && same_graph(x->content, y.content, pairs);
    }
    if (auto const *x = std::get_if<std::uint64_t>(&a->kind))
        return *x == std::get<std::uint64_t>(b->kind);
    return std::get<std::string>(a->kind) == std::get<std::string>(b->kind);
}

// Tag 28 can make a node hold itself. The nodes are emptied after the run, so the shared pointers let go.
inline void release(shared_test::handle const &root)
{
    std::vector<shared_test::handle> todo{root};
    std::set<shared_test::node const *> seen;
    std::vector<shared_test::handle> all;
    while (!todo.empty()) {
        shared_test::handle h = todo.back();
        todo.pop_back();
        if (!h || !seen.insert(h.get()).second)
            continue;
        all.push_back(h);
        if (auto const *a = std::get_if<std::vector<shared_test::handle>>(&h->kind))
            todo.insert(todo.end(), a->begin(), a->end());
        else if (auto const *m = std::get_if<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(&h->kind))
            for (auto const &[k, v] : *m) {
                todo.push_back(k);
                todo.push_back(v);
            }
        else if (auto const *o = std::get_if<shared_test::object>(&h->kind))
            todo.push_back(o->content);
    }
    for (auto const &h : all)
        h->kind = std::uint64_t{0};
}

// A host with values in place of references cannot hold a cycle: the eager decoder gives a copy of the
// container as it stood at the reference, the lazy one decodes it whole. Only a graph without a cycle has
// one answer.
inline bool has_cycle(shared_test::handle const &h, std::set<shared_test::node const *> &open,
                      std::set<shared_test::node const *> &done)
{
    if (!h || done.contains(h.get()))
        return false;
    if (!open.insert(h.get()).second)
        return true;
    bool found = false;
    if (auto const *a = std::get_if<std::vector<shared_test::handle>>(&h->kind))
        for (auto const &e : *a)
            found = found || has_cycle(e, open, done);
    else if (auto const *m = std::get_if<std::vector<std::pair<shared_test::handle, shared_test::handle>>>(&h->kind))
        for (auto const &[k, v] : *m)
            found = found || has_cycle(k, open, done) || has_cycle(v, open, done);
    else if (auto const *o = std::get_if<shared_test::object>(&h->kind))
        found = has_cycle(o->content, open, done);
    open.erase(h.get());
    done.insert(h.get());
    return found;
}

inline bool acyclic(std::string_view const input)
{
    shared_test::ref_host host;
    auto const value = cbor::decode<16>(host, input);
    if (!value)
        return false;
    std::set<shared_test::node const *> open;
    std::set<shared_test::node const *> done;
    bool const cycle = has_cycle(*value, open, done);
    release(*value);
    return !cycle;
}

inline void decode_encode_decode(std::string_view const input)
{
    test_host host;
    auto const value = cbor::decode<16>(host, input);
    if (!value)
        return;
    string_writer w;
    require(cbor::encode<16>(host, w, *value).has_value());
    auto const again = cbor::decode<16>(host, w.bytes);
    require(again.has_value() && same_number(*value, *again));
    string_writer twice;
    require(cbor::encode<16>(host, twice, *again).has_value() && twice.bytes == w.bytes);
    auto const end = cbor::doc_end<16>(input);
    require(end.has_value() && *end <= input.size());
}

inline void lazy_channels(std::string_view const input)
{
    test_host host;
    auto const eager = cbor::decode<16>(host, input);
    (void)cbor::doc_end<16>(input);
    auto const decoded = cbor::decode<16>(std::string{input});
    if (!decoded)
        return;
    cbor::lazy const &root = *decoded;
    auto const whole = cbor::lazy_decode<16>(host, root);
    if (eager && whole)
        require(same(*eager, *whole));
    if (whole) {
        string_writer w;
        if (cbor::encode<16>(host, w, *whole)) {
            auto const again = cbor::decode<16>(host, w.bytes);
            require(again.has_value() && same_number(*whole, *again));
        }
    }
    if (eager && acyclic(input)) {
        if (auto const *a = std::get_if<test::array>(&eager->kind)) {
            if (auto const elements = cbor::lazy_elements_of<16>(root)) {
                std::size_t i = 0;
                for (auto const e : *elements) {
                    if (!e || i >= a->size())
                        break;
                    if (auto const d = cbor::lazy_decode<16>(host, *e))
                        require(same(*d, a->at(i)));
                    if (auto const at = cbor::lazy_at<16>(root, static_cast<std::int64_t>(i)))
                        if (auto const d = cbor::lazy_decode<16>(host, *at))
                            require(same(*d, a->at(i)));
                    ++i;
                }
            }
        }
        if (auto const *m = std::get_if<test::map>(&eager->kind)) {
            if (auto const entries = cbor::lazy_entries_of<16>(root)) {
                std::size_t i = 0;
                for (auto const e : *entries) {
                    if (!e || i >= m->size())
                        break;
                    if (auto const k = cbor::lazy_decode<16>(host, e->first))
                        require(same(*k, m->at(i).key));
                    if (auto const v = cbor::lazy_decode<16>(host, e->second))
                        require(same(*v, m->at(i).val));
                    ++i;
                }
            }
        }
    }
    std::string_view const key = input.substr(0, 4);
    (void)cbor::lazy_at<16>(root, std::int64_t{0});
    (void)cbor::lazy_at<16>(root, std::int64_t{-1});
    (void)cbor::lazy_at<16>(root, "a");
    if (auto const a = cbor::lazy_at<16>(root, key))
        if (auto const b = cbor::lazy_at<16>(*a, std::int64_t{0}))
            (void)cbor::lazy_at<16>(*b, key);
    for (std::string_view const p : paths) {
        auto const steps = cbor::path_compile(p);
        require(steps.has_value());
        auto const found = cbor::path_decode<16>(host, *steps, root);
        if (p == "$" && whole)
            require(found.has_value() && same(*found, *whole));
    }
    if (auto const steps = cbor::path_compile(input))
        (void)cbor::path_decode<16>(host, *steps, root);
}

inline void shared_references(std::string_view const input)
{
    shared_test::ref_host host;
    auto const value = cbor::decode<16>(host, input);
    if (!value)
        return;
    string_writer w;
    if (cbor::encode<16, cbor::sharedrefs::on>(host, w, *value)) {
        shared_test::ref_host back;
        auto const again = cbor::decode<16>(back, w.bytes);
        if (!again) {
            require(again.error() == cbor::error::invalid_utf8_string);
            release(*value);
            return;
        }
        std::map<shared_test::node const *, shared_test::node const *> pairs;
        require(same_graph(*value, *again, pairs));
        release(*again);
    }
    release(*value);
}

inline void encode_from_input(std::string_view const input)
{
    source in{input};
    refusal r;
    test::value const v = value_from(in, r, 0);
    test_host host;
    string_writer w;
    auto const written = cbor::encode<16>(host, w, v);
    if (!written) {
        require(r.reserved_simple || r.zero_negative);
        return;
    }
    require(!r.reserved_simple && !r.zero_negative);
    auto const back = cbor::decode<16>(host, w.bytes);
    require(back.has_value() && same_number(v, *back));
    auto const end = cbor::doc_end<16>(w.bytes);
    require(end.has_value() && *end == w.bytes.size());
}

#if __cpp_impl_reflection

enum class shade : std::uint8_t { dark, light };

struct tire {
    std::uint16_t diameter;
    float airPressure;
};

struct engine {
    std::uint16_t horsepower;
    std::uint32_t cc;
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

inline bool same_float(float const a, float const b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// Every path of the schema read over bytes nobody checked. A fixed field reads whatever the bytes hold; a
// part of variable size reads an offset and a length from them, so those must stay inside the message.
inline void schema_read(std::string_view const input)
{
    auto const doc = cbor::decode<vehicle>(input);
    if (!doc)
        return;
    (void)cbor::at_path_compiled<vehicle, ".balance">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".spare[1].diameter">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".motor.cc">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".code">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".mac">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".tone">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".big">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".d">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".flag">(*doc);
    if (auto const make = cbor::at_path_compiled<vehicle, ".make">(*doc))
        require(make->data() >= input.data() && make->data() + make->size() <= input.data() + input.size());
    (void)cbor::at_path_compiled<vehicle, ".wheels[0].diameter">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".wheels[3].airPressure">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".owner">(*doc);
    (void)cbor::at_path_compiled<vehicle, ".names[1]">(*doc);
}

// A struct from the bytes of the input, encoded and read back: every field must come back as it went in,
// and the message must be two well-formed CBOR items.
inline void schema_round_trip(std::string_view const input)
{
    source in{input};
    vehicle v{};
    v.make = in.text();
    v.balance = static_cast<std::int32_t>(in.number());
    for (auto &t : v.spare)
        t = tire{static_cast<std::uint16_t>(in.number()), std::bit_cast<float>(static_cast<std::uint32_t>(in.number()))};
    for (std::size_t n = in.byte() % 5; n > 0; --n)
        v.wheels.push_back(
            tire{static_cast<std::uint16_t>(in.number()), std::bit_cast<float>(static_cast<std::uint32_t>(in.number()))});
    v.motor = engine{static_cast<std::uint16_t>(in.number()), static_cast<std::uint32_t>(in.number())};
    for (char &c : v.code)
        c = static_cast<char>(in.byte() & 0x7f);
    for (std::byte &b : v.mac)
        b = std::byte{in.byte()};
    v.tone = shade{in.byte()};
    if (in.byte() & 1)
        v.owner = in.byte();
    for (std::size_t n = in.byte() % 4; n > 0; --n)
        v.names.push_back(in.text());
    v.big = static_cast<std::int64_t>(in.number());
    v.d = std::bit_cast<double>(in.number());
    v.flag = in.byte() & 1;

    string_writer w;
    require(cbor::encode(w, v).has_value());
    auto const first = cbor::doc_end<16>(w.bytes);
    require(first.has_value());
    auto const second = cbor::doc_end<16>(std::string_view(w.bytes).substr(*first));
    require(second.has_value() && *first + *second == w.bytes.size());

    auto const doc = cbor::decode<vehicle>(w.bytes);
    require(doc.has_value());
    require(*cbor::at_path_compiled<vehicle, ".make">(*doc) == v.make);
    require(cbor::at_path_compiled<vehicle, ".balance">(*doc) == v.balance);
    require(cbor::at_path_compiled<vehicle, ".spare[1].diameter">(*doc) == v.spare[1].diameter);
    require(same_float(cbor::at_path_compiled<vehicle, ".spare[0].airPressure">(*doc), v.spare[0].airPressure));
    require(cbor::at_path_compiled<vehicle, ".motor.horsepower">(*doc) == v.motor.horsepower);
    require(cbor::at_path_compiled<vehicle, ".motor.cc">(*doc) == v.motor.cc);
    require(cbor::at_path_compiled<vehicle, ".code">(*doc) == std::string_view(v.code, 4));
    require(cbor::at_path_compiled<vehicle, ".mac">(*doc) ==
            std::string_view(reinterpret_cast<char const *>(v.mac.data()), 4));
    require(cbor::at_path_compiled<vehicle, ".tone">(*doc) == v.tone);
    require(*cbor::at_path_compiled<vehicle, ".owner">(*doc) == v.owner);
    require(cbor::at_path_compiled<vehicle, ".big">(*doc) == v.big);
    require(std::bit_cast<std::uint64_t>(cbor::at_path_compiled<vehicle, ".d">(*doc)) ==
            std::bit_cast<std::uint64_t>(v.d));
    require(cbor::at_path_compiled<vehicle, ".flag">(*doc) == v.flag);
    if (!v.wheels.empty()) {
        require(*cbor::at_path_compiled<vehicle, ".wheels[0].diameter">(*doc) == v.wheels[0].diameter);
        require(same_float(*cbor::at_path_compiled<vehicle, ".wheels[0].airPressure">(*doc),
                           v.wheels[0].airPressure));
    }
    if (v.wheels.size() < 4)
        require(!cbor::at_path_compiled<vehicle, ".wheels[3].diameter">(*doc).has_value());
    if (v.names.size() > 1)
        require(*cbor::at_path_compiled<vehicle, ".names[1]">(*doc) == v.names[1]);

    test_host host;
    auto const generic = cbor::decode<16>(host, std::string_view(w.bytes).substr(0, *first));
    require(generic.has_value());
}

#endif

// The channels of the fuzzer of mruby-cbor, and the ones it lacks: every round trip must give the same
// value back, the lazy reader must agree with the eager one, shared references must keep their graph, the
// encoder runs on values the decoder never makes, and the schema reads bytes and writes structs.
inline void one_input(std::string_view const input)
{
    decode_encode_decode(input);
    lazy_channels(input);
    shared_references(input);
    encode_from_input(input);
#if __cpp_impl_reflection
    schema_read(input);
    schema_round_trip(input);
#endif
}

} // namespace fuzz

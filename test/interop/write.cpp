// Writes every interop case as one line "name TAB section TAB written TAB expected" to stdout. written and
// expected are hex. written comes from a public writer of the library: cbor::encoder, cbor::encode(item),
// structs<T>::encode or packed<T>::encode. expected holds the same value in bytes that do not come from the
// writer under test: the input of a re-encode, or bytes written by hand from RFC 8949 and RFC 8746. A dash
// stands for a case with no expected bytes; the decoders must then only accept the written bytes. A writer
// that refuses a value gives "!" and the error in place of written, so the driver reports it.
#include <cbor/cbor.hpp>

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdfloat>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

std::string hex_of(std::string_view const bytes)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    std::string out;
    for (char const c : bytes) {
        auto const b = static_cast<unsigned char>(c);
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0xf]);
    }
    return out;
}

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.data() + i, hex.data() + i + 2, value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

std::string file_read(std::filesystem::path const &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string message_of(cbor::error const e)
{
    return "!" + std::error_code{e}.message();
}

void case_print(std::string_view const name, std::string_view const section, std::string_view const written,
                std::string_view const expected)
{
    std::cout << name << '\t' << section << '\t' << written << '\t' << expected << '\n';
}

void result_print(std::string_view const name, std::string_view const section,
                  std::expected<std::string, cbor::error> const &written, std::string_view const expected)
{
    case_print(name, section, written ? hex_of(*written) : message_of(written.error()), expected);
}

std::span<std::byte const> span_of(std::string_view const s)
{
    return std::as_bytes(std::span(s));
}

// Writes the item again through one call of cbor::encoder per head. Each integer, simple value and float
// takes the C++ type of the width that it had on the wire, so every native width of the encoder runs.
template <class Target>
std::expected<void, cbor::error> item_walk(cbor::encoder<Target> &e, cbor::item const &node)
{
    using cbor::type;
    std::uint8_t const info = node.additional_information;
    auto const integer = [&](std::uint64_t const arg, auto const call) {
        if (info <= 24)
            return call(static_cast<std::uint8_t>(arg));
        if (info == 25)
            return call(static_cast<std::uint16_t>(arg));
        if (info == 26)
            return call(static_cast<std::uint32_t>(arg));
        return call(arg);
    };
    auto const container = [&](std::span<cbor::item const> const children, std::uint64_t const count, bool const array) {
        auto const r = array ? e.array(count) : e.map(count);
        if (!r)
            return r;
        for (cbor::item const &child : children)
            if (auto const c = item_walk(e, child); !c)
                return c;
        return std::expected<void, cbor::error>{};
    };
    // A float goes through the encoder at the width and with the bits of the wire, so a NaN keeps its payload.
    auto const bits = [&] {
        std::string head;
        (void)cbor::encode(node, head);
        std::uint64_t v = 0;
        for (char const c : std::string_view(head).substr(1))
            v = v << 8 | static_cast<std::uint8_t>(c);
        return v;
    };
    switch (node.type) {
    case type::unsigned_integer:
        return integer(node.via.u64, [&](auto const v) { return e.unsigned_integer(v); });
    case type::negative_integer:
        return integer(node.via.u64, [&](auto const v) { return e.negative_integer(v); });
    case type::byte_string:
        return e.byte_string(node.via.bin);
    case type::text_string:
        return e.text_string(node.via.str);
    case type::array:
        return container(node.via.array, node.via.array.size(), true);
    case type::map:
        return container(node.via.map, node.via.map.size() / 2, false);
    case type::typed_array:
        return e.typed_array({node.via.tag.number, node.via.tag.content->via.bin});
    case type::tag:
    case type::unsigned_bignum:
    case type::negative_bignum:
        if (auto const r = e.tag(node.via.tag.number); !r)
            return r;
        return item_walk(e, *node.via.tag.content);
    case type::half_float:
        return e.floating_point(std::bit_cast<std::float16_t>(static_cast<std::uint16_t>(bits())));
    case type::single_float:
        return e.floating_point(std::bit_cast<float>(static_cast<std::uint32_t>(bits())));
    case type::double_float:
        return e.floating_point(std::bit_cast<double>(bits()));
    default:
        return e.simple_value(node.via.simple_value);
    }
}

// One input item goes through both generic writers: cbor::encode of the decoded item, and cbor::encoder
// with one call per head. The input bytes are the expectation of both.
void reencode(std::string_view const name, std::string_view const section, std::string const &input)
{
    auto const doc = cbor::lazy::copy(std::string_view(input));
    auto const item = doc.and_then([](cbor::lazy const &d) { return d.decode(); });
    if (!item) {
        case_print(std::string("decode/") + std::string(name), section, message_of(item.error()), hex_of(input));
        return;
    }
    std::string a;
    auto const size = cbor::encode(**item, a);
    case_print(std::string("encode/") + std::string(name), section, size ? hex_of(a) : message_of(size.error()),
               hex_of(input));
    std::string b;
    cbor::encoder e(b);
    auto const walked = item_walk(e, **item);
    auto const done = walked ? e.finish().transform([](auto) { return 0; }) : std::unexpected(walked.error());
    case_print(std::string("encoder/") + std::string(name), section, done ? hex_of(b) : message_of(done.error()),
               hex_of(input));
}

// test-vectors/appendix_a.json is an array of flat objects; a search for "hex" finds each example.
void appendix_a(std::filesystem::path const &file)
{
    std::string const text = file_read(file);
    std::size_t at = 0;
    for (int n = 0; (at = text.find("\"hex\"", at)) != std::string::npos; ++n) {
        std::size_t const open = text.find('"', text.find(':', at)) + 1;
        std::size_t const close = text.find('"', open);
        reencode("appendix_a/" + std::to_string(n), "RFC 8949 Appendix A",
                 bytes_of_hex(std::string_view(text).substr(open, close - open)));
        at = close;
    }
}

// The cbor-wg files are CBOR: {"tests": [{"encoded": h'..', "fail": bool, ...}]}. A vector that must fail
// has no value to write, so only the others are written.
void cbor_wg(std::filesystem::path const &root)
{
    std::vector<std::filesystem::path> files;
    for (auto const &entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.path().extension() == ".cbor")
            files.push_back(entry.path());
    std::ranges::sort(files);
    for (auto const &path : files) {
        std::string const file = file_read(path);
        auto const top = cbor::lazy::copy(std::string_view(file));
        if (!top)
            continue;
        auto const file_fail = top->at("fail").and_then([](cbor::lazy const &l) { return l.get<bool>(); });
        auto const tests = top->at("tests").and_then([](cbor::lazy const &l) { return l.elements(); });
        if (!tests)
            continue;
        int n = 0;
        for (auto const t : *tests) {
            ++n;
            if (!t)
                break;
            auto const fail = t->at("fail").and_then([](cbor::lazy const &l) { return l.get<bool>(); });
            if (fail ? *fail : (file_fail && *file_fail))
                continue;
            auto const encoded = t->at("encoded").and_then(
                [](cbor::lazy const &l) { return l.get<std::span<std::byte const>>(); });
            if (!encoded)
                continue;
            auto const &view = *encoded;
            std::string const input(reinterpret_cast<char const *>((*view).data()), (*view).size());
            reencode("cbor_wg/" + std::filesystem::relative(path, root).string() + "/" + std::to_string(n),
                     "cbor-wg vector", input);
        }
    }
}

void corpora(std::filesystem::path const &dir)
{
    std::vector<std::filesystem::path> files;
    for (auto const &entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".cbor")
            files.push_back(entry.path());
    std::ranges::sort(files);
    for (auto const &path : files)
        reencode("corpus/" + path.filename().string(), "bench corpus", file_read(path));
}

template <class T>
std::string one_item(T const value)
{
    std::string out;
    cbor::encoder e(out);
    if constexpr (std::floating_point<T>)
        (void)e.floating_point(value);
    else if constexpr (std::signed_integral<T>)
        (void)(value < 0 ? e.negative_integer(static_cast<std::make_unsigned_t<T>>(-(value + 1)))
                         : e.unsigned_integer(static_cast<std::make_unsigned_t<T>>(value)));
    else
        (void)e.unsigned_integer(value);
    (void)e.finish();
    return out;
}

// Every C++ integer type at its limits and every float width at its special values. The expected bytes are
// the preferred serialization of RFC 8949 4.2.1 and 4.2.2, written by hand.
void native_widths()
{
    char const *const s = "RFC 8949 3.1, 3.3";
    case_print("native/uint8_max", s, hex_of(one_item(std::uint8_t{255})), "18ff");
    case_print("native/uint8_zero", s, hex_of(one_item(std::uint8_t{0})), "00");
    case_print("native/uint16_max", s, hex_of(one_item(std::uint16_t{65535})), "19ffff");
    case_print("native/uint16_one", s, hex_of(one_item(std::uint16_t{1})), "01");
    case_print("native/uint32_max", s, hex_of(one_item(std::uint32_t{4294967295})), "1affffffff");
    case_print("native/uint32_zero", s, hex_of(one_item(std::uint32_t{0})), "00");
    case_print("native/uint64_max", s, hex_of(one_item(std::numeric_limits<std::uint64_t>::max())),
               "1bffffffffffffffff");
    case_print("native/uint64_zero", s, hex_of(one_item(std::uint64_t{0})), "00");
    case_print("native/int8_min", s, hex_of(one_item(std::int8_t{-128})), "387f");
    case_print("native/int8_max", s, hex_of(one_item(std::int8_t{127})), "187f");
    case_print("native/int8_minus_one", s, hex_of(one_item(std::int8_t{-1})), "20");
    case_print("native/int16_min", s, hex_of(one_item(std::int16_t{-32768})), "397fff");
    case_print("native/int32_min", s, hex_of(one_item(std::numeric_limits<std::int32_t>::min())), "3a7fffffff");
    case_print("native/int64_min", s, hex_of(one_item(std::numeric_limits<std::int64_t>::min())),
               "3b7fffffffffffffff");
    case_print("native/int64_max", s, hex_of(one_item(std::numeric_limits<std::int64_t>::max())),
               "1b7fffffffffffffff");
    {
        std::string out;
        cbor::encoder e(out);
        (void)e.negative_integer(std::numeric_limits<std::uint64_t>::max());
        (void)e.finish();
        case_print("native/nint_minus_2_64", s, hex_of(out), "3bffffffffffffffff");
    }
    char const *const f = "RFC 8949 3.3, IEEE 754";
    struct float_case {
        char const *name;
        std::uint64_t binary64;
        char const *expected;
    };
    static constexpr float_case doubles[] = {
        {"one", 0x3ff0000000000000, "f93c00"},
        {"minus_zero", 0x8000000000000000, "f98000"},
        {"zero", 0, "f90000"},
        {"infinity", 0x7ff0000000000000, "f97c00"},
        {"minus_infinity", 0xfff0000000000000, "f9fc00"},
        {"nan", 0x7ff8000000000000, "f97e00"},
        {"max", 0x7fefffffffffffff, "fb7fefffffffffffff"},
        {"min_subnormal", 1, "fb0000000000000001"},
        {"one_third", 0x3fd5555555555555, "fb3fd5555555555555"},
        {"1e300", 0x7e37e43c8800759c, "fb7e37e43c8800759c"},
    };
    for (auto const &c : doubles)
        case_print(std::string("native/double_") + c.name, f, hex_of(one_item(std::bit_cast<double>(c.binary64))),
                   c.expected);
    struct single_case {
        char const *name;
        std::uint32_t binary32;
        char const *expected;
    };
    static constexpr single_case singles[] = {
        {"one", 0x3f800000, "f93c00"},
        {"minus_zero", 0x80000000, "f98000"},
        {"infinity", 0x7f800000, "f97c00"},
        {"nan", 0x7fc00000, "f97e00"},
        {"max", 0x7f7fffff, "fa7f7fffff"},
        {"min_subnormal", 1, "fa00000001"},
        {"one_tenth", 0x3dcccccd, "fa3dcccccd"},
    };
    for (auto const &c : singles)
        case_print(std::string("native/float_") + c.name, f, hex_of(one_item(std::bit_cast<float>(c.binary32))),
                   c.expected);
    struct half_case {
        char const *name;
        std::uint16_t binary16;
        char const *expected;
    };
    static constexpr half_case halves[] = {
        {"one", 0x3c00, "f93c00"},          {"minus_zero", 0x8000, "f98000"},
        {"infinity", 0x7c00, "f97c00"},     {"nan", 0x7e00, "f97e00"},
        {"max", 0x7bff, "f97bff"},          {"min_subnormal", 1, "f90001"},
        {"min_normal", 0x0400, "f90400"},   {"minus_two", 0xc000, "f9c000"},
    };
    for (auto const &c : halves)
        case_print(std::string("native/float16_") + c.name, f,
                   hex_of(one_item(std::bit_cast<std::float16_t>(c.binary16))), c.expected);
    struct simple_case {
        char const *name;
        std::uint8_t value;
        char const *expected;
    };
    static constexpr simple_case simples[] = {
        {"false", 20, "f4"}, {"true", 21, "f5"},   {"null", 22, "f6"},
        {"undefined", 23, "f7"}, {"zero", 0, "e0"}, {"32", 32, "f820"}, {"255", 255, "f8ff"},
    };
    for (auto const &c : simples) {
        std::string out;
        cbor::encoder e(out);
        auto const r = e.simple_value(static_cast<cbor::simple_value>(c.value));
        if (r)
            (void)e.finish();
        case_print(std::string("native/simple_") + c.name, "RFC 8949 3.3", r ? hex_of(out) : message_of(r.error()),
                   c.expected);
    }
}

// Tags 28 and 29 of the value-sharing registration. Each expected item is the same bytes written by hand.
void shared_values()
{
    char const *const s = "value-sharing (tags 28, 29), RFC 8949 3.4";
    {
        std::string out;
        cbor::encoder e(out);
        (void)e.array(2);
        (void)e.tag(28);
        (void)e.text_string("x");
        (void)e.tag(29);
        (void)e.unsigned_integer(std::uint8_t{0});
        (void)e.finish();
        case_print("shared/array_text", s, hex_of(out), "82d81c6178d81d00");
    }
    {
        std::string out;
        cbor::encoder e(out);
        (void)e.map(2);
        (void)e.text_string("a");
        (void)e.tag(28);
        (void)e.array(2);
        (void)e.unsigned_integer(std::uint8_t{1});
        (void)e.unsigned_integer(std::uint8_t{2});
        (void)e.text_string("b");
        (void)e.tag(29);
        (void)e.unsigned_integer(std::uint8_t{0});
        (void)e.finish();
        case_print("shared/map_array", s, hex_of(out), "a26161d81c8201026162d81d00");
    }
    {
        std::string out;
        cbor::encoder e(out);
        (void)e.array(4);
        (void)e.tag(28);
        (void)e.text_string("p");
        (void)e.tag(28);
        (void)e.byte_string(span_of("q"));
        (void)e.tag(29);
        (void)e.unsigned_integer(std::uint8_t{1});
        (void)e.tag(29);
        (void)e.unsigned_integer(std::uint8_t{0});
        (void)e.finish();
        case_print("shared/two_marks", s, hex_of(out), "84d81c6170d81c4171d81d01d81d00");
    }
}

// Every tag of RFC 8746 Table 1 except the reserved 76, written by encoder::typed_array. 16 bytes are a
// whole number of elements for every element size.
void typed_arrays()
{
    std::string const payload = bytes_of_hex("000102030405060708090a0b0c0d0e0f");
    for (std::uint64_t tag = 64; tag <= 87; ++tag) {
        if (tag == 76)
            continue;
        std::string out;
        cbor::encoder e(out);
        auto const r = e.typed_array({tag, span_of(payload)});
        if (r)
            (void)e.finish();
        case_print("typed_array/" + std::to_string(tag), "RFC 8746 2", r ? hex_of(out) : message_of(r.error()),
                   "d8" + hex_of(std::string(1, static_cast<char>(tag))) + "50" + hex_of(payload));
    }
}

enum class color : std::uint8_t { red, green };

struct inner {
    std::string city;
    std::optional<std::string> zip;
};

struct everything {
    bool flag;
    std::nullptr_t nothing;
    std::int8_t i8;
    std::int16_t i16;
    std::int32_t i32;
    std::int64_t i64;
    std::uint8_t u8;
    std::uint16_t u16;
    std::uint32_t u32;
    std::uint64_t u64;
    float f32;
    double f64;
    double third;
    std::string text;
    std::vector<std::byte> bytes;
    std::optional<int> absent;
    std::vector<std::int32_t> list;
    std::vector<double> reals;
    std::map<std::string, int> table;
    cbor::tagged<1, std::int64_t> time;
    __int128 big;
    unsigned __int128 ubig;
    color hue;
    inner home;
};

struct senml {
    std::string n;
    double v;
    static constexpr std::array<std::int64_t, 2> keys{0, 2};
};

// structs<T>::encode. The expected bytes are written by hand in the preferred serialization; the decoders
// compare values, so a float that the library writes in another width still compares equal.
void structs_cases()
{
    char const *const s = "RFC 8949 3, 3.4.3";
    everything const v{true,
                       nullptr,
                       -128,
                       -32768,
                       std::numeric_limits<std::int32_t>::min(),
                       std::numeric_limits<std::int64_t>::min(),
                       255,
                       65535,
                       4294967295,
                       std::numeric_limits<std::uint64_t>::max(),
                       1.5f,
                       -0.0,
                       1.0 / 3.0,
                       "ann",
                       {std::byte{1}, std::byte{2}},
                       std::nullopt,
                       {1, -1, 1000},
                       {1.5, 2.5},
                       {{"a", 1}, {"b", 2}},
                       {1700000000},
                       -(static_cast<__int128>(1) << 100),
                       static_cast<unsigned __int128>(1) << 64,
                       color::green,
                       {"Graz", "8010"}};
    std::string const expected =
        "b7"                                         // map of 23 pairs: absent is left out
        "64666c6167f5"                               // "flag": true
        "676e6f7468696e67f6"                         // "nothing": null
        "626938387f"                                 // "i8": -128
        "63693136397fff"                             // "i16": -32768
        "636933323a7fffffff"                         // "i32"
        "636936343b7fffffffffffffff"                 // "i64"
        "62753818ff"                                 // "u8": 255
        "6375313619ffff"                             // "u16"
        "637533321affffffff"                         // "u32"
        "637536341bffffffffffffffff"                 // "u64"
        "63663332f93e00"                             // "f32": 1.5
        "63663634f98000"                             // "f64": -0.0
        "657468697264fb3fd5555555555555"             // "third"
        "647465787463616e6e"                         // "text": "ann"
        "656279746573d840420102"                     // "bytes": 64(h'0102'), RFC 8746 uint8
        "646c697374d84e4c01000000ffffffffe8030000"   // "list": 78(sint32 LE [1, -1, 1000])
        "657265616c73d85650000000000000f83f0000000000000440" // "reals": 86(binary64 LE [1.5, 2.5])
        "657461626c65a2616101616202"                 // "table": {"a": 1, "b": 2}
        "6474696d65c11a6553f100"                     // "time": 1(1700000000)
        "63626967c34d0fffffffffffffffffffffffff"     // "big": -2^100 = -1 - 0x0fff...ff (13 bytes)
        "6475626967c249010000000000000000"           // "ubig": 2^64
        "6368756501"                                 // "hue": 1
        "64686f6d65a26463697479644772617a637a69706438303130";                                         // "home": {"city": "Graz", "zip": "8010"}
    result_print("structs/everything", s, cbor::structs<everything>::encode(v), expected);
    result_print("structs/senml", "RFC 8428 6", cbor::structs<senml>::encode(senml{"t", 1.5}), "a200617402f93e00");
}

struct [[= cbor::tag(1700)]] tire {
    std::uint16_t diameter;
    float pressure;
};

struct [[= cbor::tag(1701)]] car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

struct [[= cbor::tag(1702)]] sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
    std::vector<std::byte> raw;
    std::int64_t offset;
};

// packed<T>::encode. A generic decoder does not know Packed CBOR, so it gives the tags 113 and 114 with their
// content (RFC 8949 3.4); the written bytes have no expectation and must only be accepted.
void packed_cases()
{
    char const *const s = "draft-ietf-cbor-packed-19";
    result_print("packed/car", s,
               cbor::packed<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt}),
               "-");
    result_print("packed/sensor", s,
               cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}, {std::byte{9}}, -5}), "-");
}

} // namespace

int main(int const argc, char const *const *const argv)
{
    if (argc != 4) {
        std::cerr << "usage: write <appendix_a.json> <cbor-test-vectors/tests> <bench/docs>\n";
        return 2;
    }
    appendix_a(argv[1]);
    cbor_wg(argv[2]);
    corpora(argv[3]);
    native_widths();
    shared_values();
    typed_arrays();
    structs_cases();
    packed_cases();
}

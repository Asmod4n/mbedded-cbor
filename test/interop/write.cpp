// Writes every interop case as one line to stdout. The line is "name TAB section TAB written TAB expected". Written and
// expected are hex. Written comes from a public writer of the library. The writers are cbor::encode(item),
// cbor::encode of a value and schema<T>::encode. Expected holds the same value in bytes that do not come
// from the writer under test. These bytes are the input of a re-encode, or bytes written by hand from RFC 8949 and RFC
// 8746. A dash stands for a case with no expected bytes; the decoders must then only accept the written bytes. A writer
// that refuses a value gives "!" and the error in place of written, so the driver reports it.
#include <cbor/cbor.hpp>
#include "../rfc8746.hpp"
#include "../hex.hpp"
#include "../owned_copy.hpp"

#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <chrono>
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
    case_print(name, section, written ? test::hexlify(*written) : message_of(written.error()), expected);
}

// One input item goes through the generic writer: cbor::encode of the decoded item. The input bytes are the
// expectation.
void reencode(std::string_view const name, std::string_view const section, std::string const &input)
{
    auto const doc = test::make_lazy_from_copy(std::string_view(input));
    auto const item = doc.and_then([](cbor::lazy const &d) { return d.to_item(); });
    if (!item) {
        case_print(std::string("decode/") + std::string(name), section, message_of(item.error()), test::hexlify(input));
        return;
    }
    std::string a;
    auto const size = cbor::encode(a, **item);
    case_print(std::string("encode/") + std::string(name), section, size ? test::hexlify(a) : message_of(size.error()),
               test::hexlify(input));
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
                 test::unhexlify(std::string_view(text).substr(open, close - open)));
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
        auto const top = test::make_lazy_from_copy(std::string_view(file));
        if (!top)
            continue;
        auto const file_fail_at = top->at("fail");
        auto const file_fail = file_fail_at.and_then([](cbor::lazy const &l) { return l.get<bool>(); });
        auto const tests_at = top->at("tests");
        if (!tests_at)
            continue;
        auto const tests = tests_at->elements();
        if (!tests)
            continue;
        int n = 0;
        for (auto const t : *tests) {
            ++n;
            if (!t)
                break;
            auto const fail_at = t->at("fail");
            auto const fail = fail_at.and_then([](cbor::lazy const &l) { return l.get<bool>(); });
            if (fail ? *fail : (file_fail && *file_fail))
                continue;
            auto const encoded_at = t->at("encoded");
            auto const encoded =
                encoded_at.and_then([](cbor::lazy const &l) { return l.get<std::span<std::byte const>>(); });
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
std::expected<std::string, cbor::error> one_item(T const value)
{
    return cbor::encode<T>(value);
}

// Every C++ integer type at its limits and every float width at its special values, through cbor::encode. The
// expected bytes are the preferred serialization of RFC 8949 4.2.1 and 4.2.2, written by hand. -2^64, a half float and a
// simple value have no C++ type that structs writes, so their expected bytes are the input of a re-encode.
void native_widths()
{
    char const *const s = "RFC 8949 3.1, 3.3";
    result_print("native/uint8_max", s, one_item(std::uint8_t{255}), "18ff");
    result_print("native/uint8_zero", s, one_item(std::uint8_t{0}), "00");
    result_print("native/uint16_max", s, one_item(std::uint16_t{65535}), "19ffff");
    result_print("native/uint16_one", s, one_item(std::uint16_t{1}), "01");
    result_print("native/uint32_max", s, one_item(std::uint32_t{4294967295}), "1affffffff");
    result_print("native/uint32_zero", s, one_item(std::uint32_t{0}), "00");
    result_print("native/uint64_max", s, one_item(std::numeric_limits<std::uint64_t>::max()), "1bffffffffffffffff");
    result_print("native/uint64_zero", s, one_item(std::uint64_t{0}), "00");
    result_print("native/int8_min", s, one_item(std::int8_t{-128}), "387f");
    result_print("native/int8_max", s, one_item(std::int8_t{127}), "187f");
    result_print("native/int8_minus_one", s, one_item(std::int8_t{-1}), "20");
    result_print("native/int16_min", s, one_item(std::int16_t{-32768}), "397fff");
    result_print("native/int32_min", s, one_item(std::numeric_limits<std::int32_t>::min()), "3a7fffffff");
    result_print("native/int64_min", s, one_item(std::numeric_limits<std::int64_t>::min()), "3b7fffffffffffffff");
    result_print("native/int64_max", s, one_item(std::numeric_limits<std::int64_t>::max()), "1b7fffffffffffffff");
    reencode("native/nint_minus_2_64", s, test::unhexlify("3bffffffffffffffff"));
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
        result_print(std::string("native/double_") + c.name, f, one_item(std::bit_cast<double>(c.binary64)), c.expected);
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
        result_print(std::string("native/float_") + c.name, f, one_item(std::bit_cast<float>(c.binary32)), c.expected);
    struct written_case {
        char const *name;
        char const *expected;
    };
    static constexpr written_case halves[] = {
        {"one", "f93c00"}, {"minus_zero", "f98000"}, {"infinity", "f97c00"},   {"nan", "f97e00"},
        {"max", "f97bff"}, {"min_subnormal", "f90001"}, {"min_normal", "f90400"}, {"minus_two", "f9c000"},
    };
    for (auto const &c : halves)
        reencode(std::string("native/float16_") + c.name, f, test::unhexlify(c.expected));
    static constexpr written_case simples[] = {
        {"false", "f4"}, {"true", "f5"}, {"null", "f6"}, {"undefined", "f7"}, {"zero", "e0"}, {"32", "f820"}, {"255", "f8ff"},
    };
    for (auto const &c : simples)
        reencode(std::string("native/simple_") + c.name, "RFC 8949 3.3", test::unhexlify(c.expected));
}

// Tags 28 and 29 of the value-sharing registration, written by hand. cbor::encode writes the decoded tree with the
// tags as they stand.
void shared_values()
{
    char const *const s = "value-sharing (tags 28, 29), RFC 8949 3.4";
    reencode("shared/array_text", s, test::unhexlify("82d81c6178d81d00"));
    reencode("shared/map_array", s, test::unhexlify("a26161d81c8201026162d81d00"));
    reencode("shared/two_marks", s, test::unhexlify("84d81c6170d81c4171d81d01d81d00"));
}

// Every tag of RFC 8746 Table 1 over 16 bytes, written by hand. The reserved tag 76 is the exception. 16 bytes are a
// whole number of elements for every element size.
void typed_arrays()
{
    std::string const payload = "000102030405060708090a0b0c0d0e0f";
    for (std::uint64_t tag = test::rfc8746::first_typed_array_tag; tag <= test::rfc8746::last_typed_array_tag; ++tag) {
        if (tag == test::rfc8746::reserved_typed_array_tag)
            continue;
        reencode("typed_array/" + std::to_string(tag), "RFC 8746 2",
                 test::unhexlify("d8" + test::hexlify(std::string(1, static_cast<char>(tag))) + "50" + payload));
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
    std::chrono::sys_seconds time;
    __int128 big;
    unsigned __int128 ubig;
    color hue;
    inner home;
};

struct senml {
    std::string n;
    double v;
};

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(inner, cbor::untagged, city, zip);
CBOR_DEFINE_TYPE_MEMBERS(everything, cbor::untagged, flag, nothing, i8, i16, i32, i64, u8, u16, u32, u64, f32, f64,
                         third, text, bytes, absent, list, reals, table, time, big, ubig, hue, home);

template <>
struct cbor::describe_members<senml> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &senml::n},
        cbor::member{2, &senml::v},
    };
};

namespace
{

// cbor::encode. The expected bytes are written by hand in the preferred serialization; the decoders compare
// values. So a float that the library writes in another width still compares equal.
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
                       std::chrono::sys_seconds{std::chrono::seconds{1700000000}},
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
    result_print("structs/everything", s, cbor::encode<everything>(v), expected);
    result_print("structs/senml", "RFC 8428 6", cbor::encode<senml>(senml{"t", 1.5}), "a200617402f93e00");
}

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

struct sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
    std::vector<std::byte> raw;
    std::int64_t offset;
};

} // namespace

template <>
struct cbor::describe_members<tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700};
    static constexpr std::tuple members{
        CBOR_MEMBER(tire, diameter),
        CBOR_MEMBER(tire, pressure),
    };
};

template <>
struct cbor::describe_members<car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1701};
    static constexpr std::tuple members{
        CBOR_MEMBER(car, make),
        CBOR_MEMBER(car, seats),
        CBOR_MEMBER(car, tires),
        CBOR_MEMBER(car, track),
        CBOR_MEMBER(car, note),
    };
};

template <>
struct cbor::describe_members<sensor> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1702};
    static constexpr std::tuple members{
        CBOR_MEMBER(sensor, name),
        CBOR_MEMBER(sensor, id),
        CBOR_MEMBER(sensor, readings),
        CBOR_MEMBER(sensor, raw),
        CBOR_MEMBER(sensor, offset),
    };
};

namespace
{

// schema<T>::encode. A generic decoder does not know Packed CBOR, so it gives the tags 113 and 114 with their content
// (RFC 8949 3.4). The written bytes have no expectation and must only be accepted.
void packed_cases()
{
    char const *const s = "draft-ietf-cbor-packed-19";
    result_print("packed/car", s,
               cbor::schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt}),
               "-");
    result_print("packed/sensor", s,
               cbor::schema<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}, {std::byte{9}}, -5}), "-");
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

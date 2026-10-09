#include "binding.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#if defined(__STDCPP_FLOAT16_T__)
#include <stdfloat>
#endif

using namespace std::string_view_literals;
using cbor::error;

namespace
{

struct vector_case {
    std::string hex;
    bool roundtrip;
};

// Reads the "hex" and "roundtrip" members of each object of a test-vectors file. The files are
// plain arrays of flat objects, so a search for the two keys is enough.
std::vector<vector_case> cases_of(std::string const &file)
{
    std::ifstream in(file);
    REQUIRE(in.good());
    std::string const text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::vector<vector_case> cases;
    std::size_t at = 0;
    while ((at = text.find("\"hex\"", at)) != std::string::npos) {
        std::size_t const open = text.find('"', text.find(':', at)) + 1;
        std::size_t const close = text.find('"', open);
        std::size_t const object_end = text.find('}', close);
        std::size_t const roundtrip = text.find("\"roundtrip\": true", at);
        cases.push_back({text.substr(open, close - open), roundtrip < object_end});
        at = close;
    }
    return cases;
}

std::string bytes_of_hex(std::string_view const hex)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        unsigned value = 0;
        std::from_chars(hex.substr(i, 2).data(), std::to_address(hex.substr(i, 2).end()), value, 16);
        bytes.push_back(static_cast<char>(value));
    }
    return bytes;
}

} // namespace

// Ported from test-vectors.rb: 'passes the official test vectors', the examples of RFC 8949
// Appendix A. Indefinite lengths are refused, as in mruby-cbor. Every other example decodes, and
// one that a generic encoder writes back identically comes back byte for byte. The skip list of
// mruby-cbor is not needed: the test binding keeps bytes, text, simple values and tags apart.
TEST_CASE("test vectors: RFC 8949 Appendix A")
{
    auto const cases = cases_of(TEST_VECTORS "/appendix_a.json");
    REQUIRE_EQ(cases.size(), 82);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const v = decoded(wire);
        if (!v) {
            CHECK_EQ(v.error(), error::indefinite_length);
            continue;
        }
        if (c.roundtrip)
            CHECK_EQ(encoded(*v), wire);
    }
}

// fail.json holds data items that are not well-formed. Read as exactly one data item, none may
// succeed: decode fails, or bytes remain after the item, which RFC 8949 Appendix F calls too much
// data. cbor::decode itself leaves trailing bytes alone, as CBOR.decode of mruby-cbor does.
TEST_CASE("test vectors: not well-formed items fail")
{
    auto const cases = cases_of(TEST_VECTORS "/fail.json");
    REQUIRE_GT(cases.size(), 600);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const end = cbor::item_size(wire);
        bool const one_item = end.has_value() && *end == wire.size();
        CHECK_FALSE((one_item && decoded(wire).has_value()));
    }
}

// RFC 8949 section 3: the head of every example gives the major type in the high 3 bits, the additional information
// in the low 5 bits and the argument after it. The item of each example keeps these fields from the wire, and its
// content follows section 3.1: a string is the bytes after the head, an array and a map hold as many members as the
// argument says, a tag points to its content, a float holds the bits of the argument in the width of the additional
// information. Indefinite lengths are refused, as cbor::decode refuses them.
TEST_CASE("test vectors: RFC 8949 Appendix A decodes to items with the head fields of the wire")
{
    auto const cases = cases_of(TEST_VECTORS "/appendix_a.json");
    REQUIRE_EQ(cases.size(), 82);
    for (vector_case const &c : cases) {
        CAPTURE(c.hex);
        std::string const wire = bytes_of_hex(c.hex);
        auto const top_level = cbor::lazy::from(wire);
        REQUIRE(top_level.has_value());
        auto const decoded_item = top_level->decode();
        auto const initial = static_cast<std::uint8_t>(wire.front());
        if ((initial & 0x1f) == 31) {
            CHECK_EQ(decoded_item.error(), error::indefinite_length);
            continue;
        }
        if (!decoded_item) {
            CHECK_EQ(decoded_item.error(), error::indefinite_length);
            continue;
        }
        cbor::item const &it = decoded_item->get();
        std::uint8_t const info = initial & 0x1f;
        std::size_t const argument_bytes = info < 24 ? 0 : std::size_t{1} << (info - 24);
        std::uint64_t argument = info < 24 ? info : 0;
        for (std::size_t i = 1; i <= argument_bytes; ++i)
            argument = argument << 8 | static_cast<std::uint8_t>(wire[i]);
        CHECK_EQ(std::to_underlying(it.major_type), initial >> 5);
        CHECK_EQ(it.additional_information, info);
        CHECK_EQ(it.argument, argument);
        std::string_view const after_head = std::string_view(wire).substr(1 + argument_bytes);
        switch (it.major_type) {
        case cbor::major_type::unsigned_integer:
        case cbor::major_type::negative_integer:
            CHECK(std::holds_alternative<std::monostate>(it.content));
            break;
        case cbor::major_type::byte_string:
            REQUIRE(std::holds_alternative<std::span<std::byte const>>(it.content));
            CHECK(std::ranges::equal(std::get<std::span<std::byte const>>(it.content), std::as_bytes(std::span(after_head))));
            break;
        case cbor::major_type::text_string:
            REQUIRE(std::holds_alternative<std::string_view>(it.content));
            CHECK_EQ(std::get<std::string_view>(it.content), after_head);
            break;
        case cbor::major_type::array:
        case cbor::major_type::map:
            REQUIRE(std::holds_alternative<std::span<std::byte const>>(it.content));
            CHECK(std::ranges::equal(std::get<std::span<std::byte const>>(it.content), std::as_bytes(std::span(after_head))));
            break;
        case cbor::major_type::tag:
            REQUIRE(std::holds_alternative<cbor::item const *>(it.content));
            CHECK_EQ(std::to_underlying(std::get<cbor::item const *>(it.content)->major_type),
                     static_cast<std::uint8_t>(after_head.front()) >> 5);
            break;
        case cbor::major_type::simple_float:
            if (info == 25) {
#if defined(__STDCPP_FLOAT16_T__)
                REQUIRE(std::holds_alternative<std::float16_t>(it.content));
                CHECK_EQ(std::bit_cast<std::uint16_t>(std::get<std::float16_t>(it.content)), argument);
#else
                REQUIRE(std::holds_alternative<float>(it.content));
#endif
            } else if (info == 26) {
                REQUIRE(std::holds_alternative<float>(it.content));
                CHECK_EQ(std::bit_cast<std::uint32_t>(std::get<float>(it.content)), argument);
            } else if (info == 27) {
                REQUIRE(std::holds_alternative<double>(it.content));
                CHECK_EQ(std::bit_cast<std::uint64_t>(std::get<double>(it.content)), argument);
            } else {
                REQUIRE(std::holds_alternative<cbor::simple_value>(it.content));
                CHECK_EQ(std::to_underlying(std::get<cbor::simple_value>(it.content)), argument);
            }
            break;
        }
    }
}

namespace
{

constexpr std::size_t depth_limit = cbor::validity::nesting_depth_limit;

// One test of a file of the CBOR WG test vectors. decoded holds the bytes of the "decoded" item, empty when
// the test has none.
struct wg_vector {
    std::string description;
    std::string encoded;
    std::string decoded;
    bool roundtrip;
    bool fail;
};

std::string file_read(std::string const &path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The value of an optional boolean member of a test. An absent member gives the default of the README.
bool member_bool(cbor::lazy const &object, std::string_view const key, bool const absent)
{
    auto const member = object.at(key);
    if (!member) {
        REQUIRE_EQ(member.error(), error::key_not_found);
        return absent;
    }
    auto const b = member->get<bool>();
    REQUIRE(b.has_value());
    return *b;
}

std::string member_bytes(cbor::lazy const &object, std::string_view const key)
{
    auto const member = object.at(key);
    REQUIRE(member.has_value());
    auto const bytes = member->get<std::span<std::byte const>>();
    REQUIRE(bytes.has_value());
    auto const &view = *bytes;
    std::string out;
    for (std::byte const b : *view)
        out.push_back(static_cast<char>(b));
    return out;
}

// The files are CBOR. They are read with the library under test: lazy finds the members, and item_size gives
// the bytes of each "decoded" item, which the tests below decode as the expectation.
std::vector<wg_vector> wg_vectors_of(std::string const &path)
{
    std::string const file = file_read(path);
    auto const top = cbor::lazy::from(std::string_view(file));
    REQUIRE(top.has_value());
    bool const file_fail = member_bool(*top, "fail", false);
    auto const tests = top->at("tests");
    REQUIRE(tests.has_value());
    auto const elements = tests->elements();
    REQUIRE(elements.has_value());
    std::vector<wg_vector> vectors;
    for (auto const t : *elements) {
        REQUIRE(t.has_value());
        auto const description = t->at("description");
        REQUIRE(description.has_value());
        auto const text = description->get<std::string_view>();
        REQUIRE(text.has_value());
        auto const &text_view = *text;
        wg_vector v{std::string(*text_view),
                    member_bytes(*t, "encoded"),
                    {},
                    member_bool(*t, "roundtrip", true),
                    member_bool(*t, "fail", file_fail)};
        if (auto const decoded = t->at("decoded")) {
            std::string_view const rest = std::string_view(file).substr(decoded->offset);
            auto const end = cbor::item_size(rest);
            REQUIRE(end.has_value());
            v.decoded = std::string(rest.substr(0, *end));
        }
        vectors.push_back(std::move(v));
    }
    return vectors;
}

// The absolute value of an integer, most significant byte first and with no leading zero.
struct integer_value {
    bool negative;
    std::string magnitude;
    bool operator==(integer_value const &) const = default;
};

std::string magnitude_of_unsigned(std::uint64_t const u)
{
    std::string out;
    for (int shift = 56; shift >= 0; shift -= 8)
        if (!out.empty() || (u >> shift & 0xff) != 0)
            out.push_back(static_cast<char>(u >> shift & 0xff));
    return out;
}

std::string magnitude_without_leading_zeros(std::string_view const m)
{
    std::size_t const first = m.find_first_not_of('\0');
    return first == std::string_view::npos ? std::string() : std::string(m.substr(first));
}

std::string magnitude_plus_one(std::string m)
{
    for (auto it = m.rbegin(); it != m.rend(); ++it) {
        *it = static_cast<char>(static_cast<unsigned char>(*it) + 1u);
        if (*it != '\0')
            return m;
    }
    return '\x01' + m;
}

// RFC 8949 3.4.3: the extended generic data model makes a bignum equal to the integer of the same numeric
// value. Tag 2 holds n, tag 3 holds -1 - n; a negative integer with argument a is -1 - a.
std::optional<integer_value> integer_of(value const &v)
{
    if (auto const *u = std::get_if<std::uint64_t>(&v.kind))
        return integer_value{false, magnitude_of_unsigned(*u)};
    if (auto const *n = std::get_if<negative>(&v.kind))
        return integer_value{true, magnitude_plus_one(magnitude_of_unsigned(n->argument))};
    if (auto const *b = get_if<bignum>(v))
        return integer_value{b->negative, magnitude_without_leading_zeros(b->magnitude)};
    if (auto const *t = get_if<tagged>(v); t != nullptr && (t->tag == 2 || t->tag == 3)) {
        auto const *content = get_if<bytes>(t->content);
        if (content == nullptr)
            return std::nullopt;
        std::string const n = magnitude_without_leading_zeros(content->b);
        return integer_value{t->tag == 3, t->tag == 3 ? magnitude_plus_one(n) : n};
    }
    return std::nullopt;
}

// Equality of RFC 8949 2.2 and 5.6.1 in the extended generic data model of 3.4.3. A float compares by its
// bits: a NaN keeps its sign and payload when it is decoded.
bool equivalent(value const &a, value const &b)
{
    auto const ia = integer_of(a);
    auto const ib = integer_of(b);
    if (ia || ib)
        return ia && ib && *ia == *ib;
    if (auto const *d = std::get_if<double>(&a.kind)) {
        auto const *e = std::get_if<double>(&b.kind);
        return e != nullptr && std::bit_cast<std::uint64_t>(*d) == std::bit_cast<std::uint64_t>(*e);
    }
    if (auto const *x = get_if<array>(a)) {
        auto const *y = get_if<array>(b);
        return y != nullptr && x->size() == y->size() &&
               std::ranges::equal(*x, *y, [](value const &l, value const &r) { return equivalent(l, r); });
    }
    if (auto const *x = get_if<map>(a)) {
        auto const *y = get_if<map>(b);
        return y != nullptr && x->size() == y->size() && std::ranges::all_of(*x, [y](entry const &e) {
                   return std::ranges::any_of(*y, [&e](entry const &f) {
                       return equivalent(e.key, f.key) && equivalent(e.val, f.val);
                   });
               });
    }
    if (auto const *x = get_if<tagged>(a)) {
        auto const *y = get_if<tagged>(b);
        return y != nullptr && x->tag == y->tag && equivalent(x->content, y->content);
    }
    return a == b;
}

// The walk of lazy: lazy::decode gives the item of each node, elements and entries give the members,
// get<double> gives a float. A tag stays a tag, so a bignum compares through integer_of.
std::expected<value, error> lazy_value(cbor::lazy const &l)
{
    auto const decoded_item = l.decode();
    if (!decoded_item)
        return std::unexpected(decoded_item.error());
    cbor::item const &it = decoded_item->get();
    switch (it.major_type) {
    case cbor::major_type::unsigned_integer:
        return value{it.argument};
    case cbor::major_type::negative_integer:
        return value{negative{it.argument}};
    case cbor::major_type::byte_string: {
        std::string b;
        for (std::byte const x : std::get<std::span<std::byte const>>(it.content))
            b.push_back(static_cast<char>(x));
        return value{bytes{std::move(b)}};
    }
    case cbor::major_type::text_string:
        return value{std::string(std::get<std::string_view>(it.content))};
    case cbor::major_type::array: {
        auto const elements = l.elements();
        if (!elements)
            return std::unexpected(elements.error());
        array a;
        for (auto const e : *elements) {
            if (!e)
                return std::unexpected(e.error());
            auto v = lazy_value(*e);
            if (!v)
                return v;
            a.push_back(std::move(*v));
        }
        return value{std::move(a)};
    }
    case cbor::major_type::map: {
        auto const entries = l.entries();
        if (!entries)
            return std::unexpected(entries.error());
        map m;
        for (auto const e : *entries) {
            if (!e)
                return std::unexpected(e.error());
            auto k = lazy_value(e->first);
            if (!k)
                return k;
            auto v = lazy_value(e->second);
            if (!v)
                return v;
            m.push_back(entry{std::move(*k), std::move(*v)});
        }
        return value{std::move(m)};
    }
    case cbor::major_type::tag: {
        // RFC 8949 3: the content follows the head, whose argument takes 1 << (ai - 24) bytes from ai 24 on.
        std::size_t const argument_bytes =
            it.additional_information < 24 ? 0 : std::size_t{1} << (it.additional_information - 24);
        auto content = lazy_value(cbor::lazy{l.top_level, l.offset + 1 + argument_bytes});
        if (!content)
            return content;
        return value{tagged{it.argument, std::move(*content)}};
    }
    case cbor::major_type::simple_float:
        if (it.additional_information >= 25 && it.additional_information <= 27) {
            auto const d = l.get<double>();
            if (!d)
                return std::unexpected(d.error());
            return value{*d};
        }
        return value{simple{static_cast<std::uint8_t>(it.argument)}};
    }
    return std::unexpected(error::syntax_error);
}

std::expected<value, error> lazy_value_of(std::string_view const wire)
{
    auto const top = cbor::lazy::from(wire);
    REQUIRE(top.has_value());
    return lazy_value(*top);
}

std::string wire_encoded(value const &v)
{
    test_binding binding;
    string_writer w;
    REQUIRE(cbor::encode(binding, w, v).has_value());
    return w.encoded;
}

// draft-ietf-cbor-edn-literals-28 4.1 and 4.2: an encoding indicator _0 to _3 follows a head that is not the
// preferred one. Outside a string the indicator and the blank after an opening bracket are removed.
std::string without_encoding_indicators(std::string_view const edn)
{
    std::string out;
    char quote = '\0';
    for (std::size_t i = 0; i < edn.size(); ++i) {
        char const c = edn[i];
        if (quote != '\0') {
            out.push_back(c);
            if (c == '\\' && i + 1 < edn.size())
                out.push_back(edn[++i]);
            else if (c == quote)
                quote = '\0';
            continue;
        }
        if (c == '"' || c == '\'') {
            quote = c;
            out.push_back(c);
            continue;
        }
        if (c == '_' && i + 1 < edn.size() && edn[i + 1] >= '0' && edn[i + 1] <= '3') {
            ++i;
            if (!out.empty() && (out.back() == '[' || out.back() == '{') && i + 1 < edn.size() &&
                edn[i + 1] == ' ')
                ++i;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

struct wg_count {
    std::size_t vectors;
    std::size_t nesting_depth_exceeded;
};

// The steps of the README of the vectors, through decode, lazy and diagnostic_notation, for a test that does not fail.
void wg_good_check(wg_vector const &v, wg_count &count)
{
    CAPTURE(v.description);
    CAPTURE(v.encoded);
    REQUIRE_FALSE(v.decoded.empty());
    auto const expected = decoded(v.decoded);
    REQUIRE(expected.has_value());

    auto const d = decoded(v.encoded);
    REQUIRE(d.has_value());
    CHECK(equivalent(*d, *expected));
    if (v.roundtrip) {
        // Every NaN is written as 0xf97e00 (owner, 2026-10-01 and 2026-10-05). A vector that keeps the sign
        // or the payload of a NaN in its preferred form (RFC 8949 4.1) comes back as 0xf97e00.
        auto const *f = std::get_if<double>(&d->kind);
        CHECK_EQ(wire_encoded(*d),
                 f != nullptr && std::isnan(*f) ? std::string("\xf9\x7e\x00"sv) : v.encoded);
    }

    auto const l = lazy_value_of(v.encoded);
    REQUIRE(l.has_value());
    CHECK(equivalent(*l, *expected));

    auto const text = cbor::diagnostic_notation(v.encoded);
    REQUIRE(text.has_value());
    auto const expected_text = cbor::diagnostic_notation(v.decoded);
    REQUIRE(expected_text.has_value());
    // diagnostic_notation writes the basic generic data model. A bignum is its tag and the byte string of the wire, so a
    // vector that makes it an integer or drops its leading zeros (RFC 8949 3.4.3) has another text. A NaN
    // with a payload is float'' of its bits on the wire (EDN draft 3.7), so its shorter form is another text.
    bool const bignum_tag = text->starts_with("2(") || text->starts_with("3(");
    if (!bignum_tag && !text->starts_with("float'"))
        CHECK_EQ(without_encoding_indicators(*text), without_encoding_indicators(*expected_text));

    // The nesting depth defaults to 128 (owner, 2026-10-06). A vector nested deeper is read with the depth set to
    // the limit and gives nesting_depth_exceeded at the default.
    test::nesting_depth_max_guard const shallow_depth{cbor::validity::nesting_depth_default};
    if (auto const shallow = decoded(v.encoded); !shallow) {
        CHECK_EQ(shallow.error(), error::nesting_depth_exceeded);
        ++count.nesting_depth_exceeded;
    }
    ++count.vectors;
}

} // namespace

// The test vectors of the CBOR working group (github.com/cbor-wg/cbor-test-vectors, BSD 2-Clause) that are
// well-formed and valid. Each one goes through decode, lazy and diagnostic_notation, and is compared with its "decoded"
// item in the extended generic data model.
TEST_CASE("cbor-wg test vectors: every good vector decodes to its decoded item")
{
    test::nesting_depth_max_guard const depth{depth_limit};
    wg_count count{};
    for (char const *const file :
         {"/rfc8949/good.cbor", "/rfc8949-appendixA/mt0.cbor", "/rfc8949-appendixA/mt1.cbor",
          "/rfc8949-appendixA/mt2.cbor", "/rfc8949-appendixA/mt3.cbor", "/rfc8949-appendixA/mt4.cbor",
          "/rfc8949-appendixA/mt5.cbor", "/rfc8949-appendixA/mt6.cbor", "/rfc8949-appendixA/mt7-float.cbor",
          "/rfc8949-appendixA/mt7-simple.cbor", "/spike/spike.cbor"}) {
        CAPTURE(file);
        for (wg_vector const &v : wg_vectors_of(std::string(CBOR_TEST_VECTORS "/tests") + file)) {
            REQUIRE_FALSE(v.fail);
            wg_good_check(v, count);
        }
    }
    CHECK_EQ(count.vectors, 88u + 11u + 5u + 2u + 7u + 4u + 5u + 8u + 22u + 6u + 1165u);
    CHECK_EQ(count.nesting_depth_exceeded, 3u);
}

// bad.cbor holds items that are not well-formed or not valid. Each one fails in decode, lazy and diagnostic_notation,
// except where a decision of this library differs from the vector.
TEST_CASE("cbor-wg test vectors: every bad vector fails")
{
    test::nesting_depth_max_guard const depth{depth_limit};
    std::size_t checked = 0;
    for (wg_vector const &v : wg_vectors_of(CBOR_TEST_VECTORS "/tests/rfc8949/bad.cbor")) {
        CAPTURE(v.description);
        CAPTURE(v.encoded);
        REQUIRE(v.fail);
        auto const d = decoded(v.encoded);
        auto const l = lazy_value_of(v.encoded);
        auto const text = cbor::diagnostic_notation(v.encoded);
        if (v.encoded == "\x62\xc0\xae"sv) {
            // The library never checks UTF-8 (owner, 2026-10-05 and 2026-10-08): the text is the
            // application's.
            CHECK(d.has_value());
            CHECK(l.has_value());
            CHECK(text.has_value());
        } else if (v.encoded == "\xc1\xa1\x61\x61\x00"sv || v.encoded == "\xc0\xa1\x61\x61\x00"sv) {
            // RFC 8949 3.4.1 and 3.4.2 make a map invalid as the content of tag 0 or 1, and RFC 8949 5.3.2
            // names the error.
            CHECK_EQ(d.error(), error::inadmissible_type_for_tag_content);
            CHECK_EQ(l.error(), error::inadmissible_type_for_tag_content);
            CHECK_EQ(text.error(), error::inadmissible_type_for_tag_content);
        } else {
            CHECK_FALSE(d.has_value());
            CHECK_FALSE(l.has_value());
            CHECK_FALSE(text.has_value());
        }
        ++checked;
    }
    CHECK_EQ(checked, 47u);
}

// streaming.cbor holds indefinite-length items, also in its "decoded" members, so the library cannot read the
// file: indefinite length is never read (owner, 2026-10-02 and 2026-10-08). The encoded items are taken from
// streaming.edn. decode and lazy give indefinite_length; diagnostic_notation writes the item.
TEST_CASE("cbor-wg test vectors: indefinite length is refused")
{
    test::nesting_depth_max_guard const depth{depth_limit};
    std::string const file = file_read(CBOR_TEST_VECTORS "/tests/rfc8949-appendixA/streaming.cbor");
    auto const top = cbor::lazy::from(std::string_view(file));
    REQUIRE(top.has_value());
    auto const tests = top->at("tests");
    REQUIRE(tests.has_value());
    CHECK_EQ(tests->at(1).error(), error::indefinite_length);

    std::string const edn = file_read(CBOR_TEST_VECTORS "/tests/rfc8949-appendixA/streaming.edn");
    std::size_t checked = 0;
    constexpr std::string_view member = "\"encoded\": h'";
    for (std::size_t at = 0; (at = edn.find(member, at)) != std::string::npos;) {
        at += member.size();
        std::string hex;
        for (; edn.at(at) != '\''; ++at)
            if (edn.at(at) != ' ')
                hex.push_back(edn.at(at));
        CAPTURE(hex);
        std::string const wire = bytes_of_hex(hex);
        CHECK_EQ(decoded(wire).error(), error::indefinite_length);
        CHECK_EQ(lazy_value_of(wire).error(), error::indefinite_length);
        CHECK(cbor::diagnostic_notation(wire).has_value());
        ++checked;
    }
    CHECK_EQ(checked, 11u);
}

// RFC 8949 3.4.1: the content of tag 0 is a text string. RFC 8949 3.4.2: the content of tag 1 is an unsigned or
// negative integer (major types 0 and 1) or a float (major type 7 with additional information 25, 26 or 27).
// Every other content is invalid. One item of each major type, each float width and two simple values goes
// into each tag, and decode, lazy and diagnostic_notation give the same answer.
TEST_CASE("tags 0 and 1: decode, lazy and diagnostic_notation refuse every content that RFC 8949 3.4.1 and 3.4.2 forbid")
{
    test::nesting_depth_max_guard const depth{depth_limit};
    struct content {
        std::string_view encoded;
        bool date_time_string;
        bool epoch_based_date_time;
    };
    std::array<content, 12> const contents{{
        {"\x00"sv, false, true},
        {"\x20"sv, false, true},
        {"\x40"sv, false, false},
        {"\x60"sv, true, false},
        {"\x80"sv, false, false},
        {"\xa0"sv, false, false},
        {"\xc2\x40"sv, false, false},
        {"\xf4"sv, false, false},
        {"\xf8\x20"sv, false, false},
        {"\xf9\x3c\x00"sv, false, true},
        {"\xfa\x3f\x80\x00\x00"sv, false, true},
        {"\xfb\x3f\xf0\x00\x00\x00\x00\x00\x00"sv, false, true},
    }};
    for (content const &c : contents)
        for (char const tag : {'\xc0', '\xc1'}) {
            std::string const wire = std::string(1, tag) + std::string(c.encoded);
            CAPTURE(wire);
            bool const admitted = tag == '\xc0' ? c.date_time_string : c.epoch_based_date_time;
            auto const d = decoded(wire);
            auto const l = lazy_value_of(wire);
            auto const text = cbor::diagnostic_notation(wire);
            if (admitted) {
                CHECK(d.has_value());
                CHECK(l.has_value());
                CHECK(text.has_value());
            } else {
                CHECK_EQ(d.error(), error::inadmissible_type_for_tag_content);
                CHECK_EQ(l.error(), error::inadmissible_type_for_tag_content);
                CHECK_EQ(text.error(), error::inadmissible_type_for_tag_content);
            }
        }
}

// A content of tag 0 or 1 may be a tag 28 or a tag 29 (RFC 8949 3.4.1, 3.4.2; value-sharing). The check of the
// content type must read the value that a tag 29 names. A fuzzer found [28(0), 0(29(0))] accepted by decode,
// and the encoder wrote 0(0), which no reader accepts. decode, lazy and diagnostic_notation give the same answer for each.
TEST_CASE("tags 0 and 1: a content behind tag 28 or tag 29 is checked as the value it names")
{
    test::nesting_depth_max_guard const depth{depth_limit};
    struct shared_content {
        std::string_view encoded;
        error refused;
    };
    std::array<shared_content, 12> const contents{{
        {"\x82\xd8\x1c\x00\xc0\xd8\x1d\x00"sv, error::inadmissible_type_for_tag_content},
        {"\x82\xd8\x1c\x60\xc0\xd8\x1d\x00"sv, error{}},
        {"\x82\xd8\x1c\x00\xc1\xd8\x1d\x00"sv, error{}},
        {"\x82\xd8\x1c\x60\xc1\xd8\x1d\x00"sv, error::inadmissible_type_for_tag_content},
        {"\xc1\xd8\x1c\x00"sv, error{}},
        {"\xc0\xd8\x1c\x00"sv, error::inadmissible_type_for_tag_content},
        {"\xc1\xd8\x1d\x00"sv, error::sharedref_index_not_marked},
        {"\xd8\x1c\x81\xc1\xd8\x1d\x00"sv, error::inadmissible_type_for_tag_content},
        {"\xd8\x1c\xd8\x1c\xc1\xd8\x1d\x01"sv, error::inadmissible_type_for_tag_content},
        {"\x83\xd8\x1c\x00\xd8\x1c\xd8\x1d\x00\xc1\xd8\x1d\x01"sv, error{}},
        {"\xd8\x1c\x82\x00\xc1\xd8\x1d\x00"sv, error::inadmissible_type_for_tag_content},
        {"\xd8\x1c\xc1\xd8\x1d\x00"sv, error::inadmissible_type_for_tag_content},
    }};
    for (shared_content const &c : contents) {
        CAPTURE(c.encoded);
        auto const d = decoded(c.encoded);
        auto const l = lazy_value_of(c.encoded);
        auto const text = cbor::diagnostic_notation(c.encoded);
        if (c.refused == error{}) {
            CHECK(d.has_value());
            CHECK(l.has_value());
            CHECK(text.has_value());
        } else {
            CHECK_EQ(d.error(), c.refused);
            CHECK_EQ(l.error(), c.refused);
            CHECK_EQ(text.error(), c.refused);
        }
    }
}

#include "../small_stack.hpp"
#include "../owned_copy.hpp"
#include "../reference.hpp"
#include "../sequence_reader.hpp"
#include <cbor/describe.hpp>
#include <cbor/schema.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// This binary is built with -DCBOR_STRING_LENGTH_DEFAULT=2 and -DCBOR_CONTAINER_ELEMENTS_DEFAULT=2, as a user sets the
// size limits. Each limit is read at the bound, one below and one above it. The expectation is the decision of the owner
// of 2026-10-09. A value up to the limit passes. A value above it fails with the error of that limit.

namespace
{

constexpr std::size_t limit = 2;

std::string text_of_length(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x60 + n)) + std::string(n, 'a');
}

std::string bytes_of_length(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x40 + n)) + std::string(n, 'b');
}

std::string array_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0x80 + n)) + std::string(n, '\x00');
}

std::string map_of(std::size_t const n)
{
    return std::string(1, static_cast<char>(0xa0 + n)) + std::string(2 * n, '\x00');
}

} // namespace

// Each test below depends on the limits of this build. The test exists so that a build without the -D values fails
// here, and not with a wrong reason below.
TEST_CASE("limits sizes 2: the build sets both size limits")
{
    CHECK_EQ(cbor::limits::string_length, limit);
    CHECK_EQ(cbor::limits::container_elements, limit);
}

// The length of each string head is checked in every reader: the well-formedness walk, the lazy item, the
// item tree and the diagnostic notation.
TEST_CASE("limits sizes 2: string_length on each string head")
{
    for (std::size_t const n : {limit - 1, limit, limit + 1}) {
        CAPTURE(n);
        bool const passes = n <= limit;
        for (std::string const &encoded :
             {text_of_length(n), bytes_of_length(n), "\x82"s + text_of_length(n) + "\x00"s}) {
            auto const size = cbor::item_size(encoded);
            auto const decoded = test::make_value_from_item(*test::make_lazy_from_copy(encoded));
            auto const notation = cbor::diagnostic_notation(encoded);
            CHECK_EQ(size.has_value(), passes);
            CHECK_EQ(decoded.has_value(), passes);
            CHECK_EQ(notation.has_value(), passes);
            if (!passes) {
                CHECK_EQ(size.error(), error::string_length_exceeded);
                CHECK_EQ(decoded.error(), error::string_length_exceeded);
                CHECK_EQ(notation.error(), error::string_length_exceeded);
            }
        }
        CHECK_EQ(test::make_lazy_from_copy(text_of_length(n))->get<std::string_view>().has_value(), passes);
    }
}

// The count of each array and map head is checked; a map counts its pairs.
TEST_CASE("limits sizes 2: container_elements on each array and map head")
{
    for (std::size_t const n : {limit - 1, limit, limit + 1}) {
        CAPTURE(n);
        bool const passes = n <= limit;
        for (std::string const &encoded : {array_of(n), map_of(n), "\x81"s + array_of(n)}) {
            auto const size = cbor::item_size(encoded);
            auto const decoded = test::make_value_from_item(*test::make_lazy_from_copy(encoded));
            auto const notation = cbor::diagnostic_notation(encoded);
            CHECK_EQ(size.has_value(), passes);
            CHECK_EQ(decoded.has_value(), passes);
            CHECK_EQ(notation.has_value(), passes);
            if (!passes) {
                CHECK_EQ(size.error(), error::container_elements_exceeded);
                CHECK_EQ(decoded.error(), error::container_elements_exceeded);
                CHECK_EQ(notation.error(), error::container_elements_exceeded);
            }
        }
        CHECK_EQ(test::make_lazy_from_copy(array_of(n))->elements().has_value(), passes);
        CHECK_EQ(test::make_lazy_from_copy(map_of(n))->entries().has_value(), passes);
    }
}

// A head that claims 2^64 - 1 bytes or elements over no content is refused at the head, before the bytes are
// read. The build with no size limit reads the same heads and finds too little data.
TEST_CASE("limits sizes 2: a head that claims 2^64 - 1 is refused at the head")
{
    std::string const text = "\x7b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    std::string const array = "\x9b\xff\xff\xff\xff\xff\xff\xff\xff"s;
    auto const length = cbor::item_size(text);
    CHECK_EQ(length.error(), error::string_length_exceeded);
    auto const size = cbor::item_size(array);
    CHECK_EQ(size.error(), error::container_elements_exceeded);
    CHECK_EQ(cbor::diagnostic_notation(text).error(), error::string_length_exceeded);
    CHECK_EQ(cbor::diagnostic_notation(array).error(), error::container_elements_exceeded);
}

// The stack is checked in the form of the code that checks the size limits too. Take n tags 100(...) around 0. They hold
// an integer at depth n. A depth that every stack holds is read; an input deeper than a small stack holds is refused on
// a thread with that stack.
TEST_CASE("limits sizes 2: the stack is checked beside the size limits")
{
    std::string const held = test::repeat("\xd8\x64"sv, test::levels_every_stack_holds) + '\x00';
    std::string const deep = test::repeat("\xd8\x64"sv, test::levels_beyond_small_stack) + '\x00';
    CHECK(test::make_lazy_from_copy(held)->to_item().has_value());
    CHECK(cbor::diagnostic_notation(held).has_value());
    CHECK_EQ(test::error_on_small_stack([&deep] { return test::make_lazy_from_copy(deep)->to_item(); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&deep] { return cbor::diagnostic_notation(deep); }),
             error::nesting_depth_exceeded);
}

// An iterator of elements() or entries() only skips the items it passes. The owner decided on 2026-10-10 that limits
// count only what is read or decoded. So a string longer than the limit does not stop the walk, and the read of that
// string does.
TEST_CASE("limits sizes 2: the iterators of elements() and entries() skip without counting")
{
    std::string const encoded = "\x82\x63"
                                "abc\x01"s;
    std::string const map = "\xa2\x63"
                            "abc\x01\x02\x03"s;
    auto const l = test::make_lazy_from_copy(encoded);
    auto const m = test::make_lazy_from_copy(map);
    REQUIRE(l.has_value());
    REQUIRE(m.has_value());
    auto const elements = l->elements();
    REQUIRE(elements.has_value());
    auto it = elements->begin();
    auto const read = (*it)->get<std::string_view>();
    CHECK_EQ(read.error(), error::string_length_exceeded);
    ++it;
    REQUIRE(it != std::default_sentinel);
    CHECK_EQ((*it)->get<int>(), 1);
    auto const entries = m->entries();
    REQUIRE(entries.has_value());
    auto pair = entries->begin();
    REQUIRE((*pair).has_value());
    auto const read_2 = (*pair)->first.get<std::string_view>();
    CHECK_EQ(read_2.error(), error::string_length_exceeded);
    CHECK_EQ((*pair)->second.get<int>(), 1);
    ++pair;
    CHECK_EQ((*pair)->second.get<int>(), 3);
}

// RFC 8949 does not bound what a decoder skips; the bytes bound it (check_bytes_left). The owner decided on 2026-10-10
// and 2026-10-18 what string_length and container_elements count. They count only the heads that a path or a lazy reads.
// These are the containers on the way, the keys it compares and the target. A string or an array above its limit that is
// only skipped does not count. This holds in each form: the typed read over bytes, the typed read from a lazy, at_path,
// query and the lazy chain. The same item, read, is refused.
TEST_CASE(
    "limits sizes 2: an item that a path or a lazy only skips does not count, and the same item read does")
{
    std::string const strings = "\xa2\x61"
                                "a\x63xyz\x61"
                                "n\x01"s;
    std::string const arrays = "\xa2\x61"
                               "a\x83\x01\x02\x03\x61"
                               "n\x01"s;
    for (auto const &[doc, wanted] : {std::tuple{strings, error::string_length_exceeded},
                                      std::tuple{arrays, error::container_elements_exceeded}}) {
        auto const l = *test::make_lazy_from_copy(doc);
        CHECK_EQ(cbor::at_path<"$.n", int>(doc), 1);
        CHECK_EQ(l.at_path<"$.n", int>(), 1);
        auto const n = l.at("n");
        CHECK_EQ(n->get<int>(), 1);
        CHECK(test::read_value_at_path("$.n", l) == V(1));
        CHECK(test::query("$.n", l) == A(1));
        CHECK_EQ(test::read_value_at_path("$.a", l).error(), wanted);
        auto const a = l.at("a");
        CHECK_EQ(test::make_value_from_item(*a).error(), wanted);
    }
    std::string const two = "\x82\x63xyz\x83\x01\x02\x03"s;
    auto const l = *test::make_lazy_from_copy("\x82\x82\x63xyz\x83\x01\x02\x03\x05"s);
    CHECK_EQ(l.at_path<"$[1]", int>(), 5);
    auto const at_1 = l.at(1);
    CHECK_EQ(at_1->get<int>(), 5);
    CHECK_EQ(l.at_path<"$[0][1]", int>().error(), error::container_elements_exceeded);
    CHECK_EQ(l.at_path<"$[0][0]", std::int64_t>().error(), error::string_length_exceeded);
    auto const size = cbor::item_size(two);
    CHECK_EQ(size.error(), error::string_length_exceeded);
}

// The former basics guide, section "Limits": the list names
// the error of each size limit.
TEST_CASE("doc basics: the error of each size limit")
{
    auto const read = test::make_lazy_from_copy(encode_in_preferred_form(V("xyz"s)))->get<std::string_view>();
    CHECK_EQ(read.error(),
             error::string_length_exceeded);
    std::string const three = encode_in_preferred_form(A(1, 2, 3));
    auto const count = test::make_lazy_from_copy(three)->size();
    CHECK_EQ(count.error(), error::container_elements_exceeded);
    CHECK_EQ(cbor::at_path<"$[0]", int>(three).error(), error::container_elements_exceeded);
}

// This comes from the former advanced basics guide, section "Limits". A limit is checked by the step that reads the
// item. So a string that is too long gives string_length_exceeded at get, not at lazy::copy. A user expected it at
// lazy::copy.
TEST_CASE("doc advanced basics: a string limit is checked at get")
{
    auto const l = test::make_lazy_from_copy(encode_in_preferred_form(V("xyz"s)));
    REQUIRE(l.has_value());
    auto const read = l->get<std::string_view>();
    CHECK_EQ(read.error(), error::string_length_exceeded);
    auto const count = test::make_lazy_from_copy(array_of(3))->size();
    CHECK_EQ(count.error(), error::container_elements_exceeded);
}

// The former advanced basics guide, section "Limits": the limits
// count what a call reads. A skip does not count.
TEST_CASE("doc advanced basics: a skip does not count")
{
    std::string const bytes = encode_in_preferred_form(M("a"s, "xyz"s, "n"s, 1));
    auto const n = test::make_lazy_from_copy(bytes)->at("n");
    CHECK_EQ(n->get<std::int64_t>(), 1);
    auto const a = test::make_lazy_from_copy(bytes)->at("a");
    auto const read = a->get<std::string_view>();
    CHECK_EQ(read.error(),
             error::string_length_exceeded);
    auto const size = cbor::item_size(bytes);
    CHECK_EQ(size.error(), error::string_length_exceeded);
    CHECK_EQ(test::make_value_from_item(*test::make_lazy_from_copy(bytes)).error(), error::string_length_exceeded);
}

// This comes from the former advanced path guide, section "Limits". A
// string that a path reads counts. A string that it only skips does not count. Every container that the walk reads
// counts.
TEST_CASE("doc advanced path: the size limits")
{
    std::string const bytes = encode_in_preferred_form(M("long"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::at_path<"$.n", int>(bytes).error(), error::string_length_exceeded);
    std::string const skipped = encode_in_preferred_form(M("a"s, "xyz"s, "n"s, 1));
    CHECK_EQ(cbor::at_path<"$.n", int>(skipped), 1);
    CHECK_EQ(cbor::at_path<"$.a", std::string_view>(std::string(skipped)).error(),
             error::string_length_exceeded);
    std::string const three = encode_in_preferred_form(M("a"s, 1, "b"s, 2, "n"s, 3));
    CHECK_EQ(cbor::at_path<"$.n", int>(three).error(), error::container_elements_exceeded);
}

#ifdef __cpp_impl_reflection

namespace
{

struct note {
    std::string text;
    std::vector<std::int32_t> numbers;
};

struct names {
    std::vector<std::string> list;
};

} // namespace

template <>
struct cbor::describe_members<note> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1590};
    static constexpr std::tuple members{
        CBOR_MEMBER(note, text),
        CBOR_MEMBER(note, numbers),
    };
};

template <>
struct cbor::describe_members<names> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1591};
    static constexpr std::tuple members{
        CBOR_MEMBER(names, list),
    };
};

// packed checks the string and container limits on each head it reads. The list of std::int32_t is a typed
// array (RFC 8746), a tag over a byte string of 4 bytes for each element.
TEST_CASE("limits sizes 2: the string and container limits of packed")
{
    CHECK(cbor::schema<note>::decode(*cbor::schema<note>::encode(note{"ab", {}})).has_value());
    CHECK_EQ(cbor::schema<note>::decode(*cbor::schema<note>::encode(note{"abc", {}})).error(),
             error::string_length_exceeded);
    CHECK_EQ(cbor::schema<note>::decode(*cbor::schema<note>::encode(note{"ab", {1}})).error(),
             error::string_length_exceeded);
    CHECK(cbor::schema<names>::decode(*cbor::schema<names>::encode(names{{"a", "b"}})).has_value());
    CHECK_EQ(cbor::schema<names>::decode(*cbor::schema<names>::encode(names{{"a", "b", "c"}})).error(),
             error::container_elements_exceeded);
}

#endif

// Limits apply to the output only (owner, 2026-10-18). The reader skips over an item to find its end. It gives a string
// or a container above the limit as an item. A read of its value gives the error of the limit.
TEST_CASE("limits sizes 2: sequence_reader gives an item above a size limit, and its read fails")
{
    for (test::feed_form const form : test::feed_forms) {
        CAPTURE(static_cast<int>(form));
        cbor::sequence_reader r;
        r = test::feed_piece_to_reader(std::move(r), "\x63"
                            "ab"sv, form, 0);
        r = test::feed_piece_to_reader(std::move(r), "c\x83\x01\x02"sv, form, 1);
        r = test::feed_piece_to_reader(std::move(r), "\x03"sv, form, 2);
        auto const text = r.next();
        auto const array = r.next();
        REQUIRE(text.has_value());
        REQUIRE(array.has_value());
        auto const read = text->get<std::string_view>();
        CHECK_EQ(read.error(), error::string_length_exceeded);
        auto const count = array->size();
        CHECK_EQ(count.error(), error::container_elements_exceeded);
        CHECK(r.empty());
    }
}

namespace
{

struct labelled {
    std::variant<std::int32_t, std::string> label;
};

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(labelled, cbor::tag{1749}, label);

// A text alternative of a variant member is a string item. So string_length counts its length as it counts a text
// member. A text at the limit reads. A text one above it is string_length_exceeded in decode and on the path.
TEST_CASE("limits sizes 2: string_length on a text alternative of a variant member")
{
    auto const fits = cbor::schema<labelled>::encode(labelled{std::string(limit, 'a')});
    CHECK(cbor::schema<labelled>::decode(std::string(*fits)).has_value());
    auto const above = cbor::schema<labelled>::encode(labelled{std::string(limit + 1, 'a')});
    CHECK_EQ(cbor::schema<labelled>::decode(std::string(*above)).error(), error::string_length_exceeded);
    CHECK_EQ(cbor::schema<labelled>::adopt(std::string(*above))->at_path<"$.label">().error(), error::string_length_exceeded);
}

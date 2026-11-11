#include "small_stack.hpp"
#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;

// Each test in this file is one example of a reference page of the tools group, from the API comment of a header.
// The tests exist so that every example compiles and gives the result that its page states.

// tools group: the example of the group tools. A CBOR sequence holds [1, 2] and then 7. The test exists so that the
// example of the group page compiles and gives the values that its comments state. The expected values come from
// RFC 8949 and RFC 8742.
TEST_CASE("doc reference tools: the example of the group tools")
{
    auto const owner =
        std::make_shared<std::string const>(test::preferred(test::A(1, 2)) + test::preferred(test::V(7)));
    // [tools group]
    // owner: the bytes 82 01 02 07, the sequence [1, 2] then 7
    auto const first = cbor::item_size(*owner);   // 3, the bytes of [1, 2]
    std::vector<std::string> text;
    std::size_t covered = 0;
    for (auto const item : cbor::sequence(owner))
        if (item) {
            text.push_back(cbor::diagnostic_notation(*item).value());   // "[1, 2]", then "7"
            covered += item->size();                                    // 3, then 1
        }
    // [tools group]
    CHECK_EQ(first, 3u);
    REQUIRE_EQ(text.size(), 2u);
    CHECK_EQ(text[0], "[1, 2]"s);
    CHECK_EQ(text[1], "7"s);
    CHECK_EQ(covered, 4u);
}

// item_size.hpp, snippet item_size: item_size gives the bytes of the first item, and a cut item gives
// too_little_data.
TEST_CASE("doc reference tools: item_size")
{
    std::string const bytes = test::preferred(test::A(1, 2)) + test::preferred(test::V(7));
    // [item_size]
    // bytes: [1, 2] followed by 7, 4 bytes
    auto const size = cbor::item_size(bytes);                              // 3
    auto const cut = cbor::item_size(std::string_view(bytes).substr(0, 2)); // too_little_data
    // [_ 1]
    auto const open = cbor::item_size("\x9f\x01\xff"sv);                   // indefinite_length
    // [item_size]
    CHECK_EQ(size, 3u);
    CHECK_EQ(cut.error(), cbor::error::too_little_data);
    CHECK_EQ(open.error(), cbor::error::indefinite_length);
}

// sequence.hpp, snippet sequence: the loop gives each item of a CBOR sequence as a view into the bytes of the owner.
TEST_CASE("doc reference tools: sequence")
{
    auto const owner = std::make_shared<std::string const>(
        test::preferred(test::V(1)) + test::preferred(test::V(2)) + test::preferred(test::V(3)));
    // use stands for the code of the reader; here it checks the sum.
    auto const use = [](std::int64_t const s) { CHECK_EQ(s, 6); };
    // [sequence]
    // owner: a std::shared_ptr<std::string const> with the sequence 1, 2, 3
    std::int64_t sum = 0;
    for (auto const item : cbor::sequence(owner))
        if (item)
            sum += cbor::at_path<"$", std::int64_t>(*item).value_or(0);
    use(sum);   // 6
    // [sequence]
}

// sequence.hpp, the comment of sequence: a broken item gives its error once, and the loop then ends.
TEST_CASE("doc reference tools: sequence with a broken item")
{
    auto const owner = std::make_shared<std::string const>("\x01\x19\x01"s);
    int good = 0;
    int bad = 0;
    for (auto const item : cbor::sequence(owner)) {
        if (item)
            ++good;
        else if (item.error() == cbor::error::too_little_data)
            ++bad;
    }
    CHECK_EQ(good, 1);
    CHECK_EQ(bad, 1);
    CHECK_THROWS_AS(cbor::sequence(std::shared_ptr<std::string const>{}), std::logic_error);
}

// sequence_reader.hpp, snippet sequence_reader: [1, 2] arrives in two pieces, and the second piece also holds 3. The
// item waits for its last byte, and each piece is a std::string that the reader takes by move.
TEST_CASE("doc reference tools: sequence_reader")
{
    std::string first = "\x82\x01"s;
    std::string second = "\x02\x03"s;
    // [sequence_reader]
    cbor::sequence_reader r;
    r.feed(std::move(first));       // 82 01: the start of [1, 2]
    auto const waiting = r.next();  // too_little_data
    r.feed(std::move(second));      // 02 03
    auto const pair = r.next();     // [1, 2]
    auto const three = r.next();    // 3
    // [sequence_reader]
    CHECK_EQ(waiting.error(), cbor::error::too_little_data);
    CHECK_EQ(pair->size(), 2u);
    CHECK_EQ(three->get<std::int64_t>(), 3);
    CHECK(r.empty());
}

// sequence_reader.hpp, the comment of next(): after bytes that are not well-formed every next() gives the error, and a
// null piece throws.
TEST_CASE("doc reference tools: sequence_reader errors")
{
    cbor::sequence_reader r;
    r.feed("\x1c"s);
    CHECK_EQ(r.next().error(), cbor::error::syntax_error);
    r.feed("\x01"s);
    CHECK_EQ(r.next().error(), cbor::error::syntax_error);
    CHECK(r.empty());
    CHECK_THROWS_AS(r.feed(std::shared_ptr<std::string const>{}), std::logic_error);
    // The reader does not recurse, so no depth bounds it.
    cbor::sequence_reader deep;
    deep.feed(std::string(test::levels_beyond_small_stack, '\x81') + "\x00"s);
    CHECK(deep.next().has_value());
}

// diagnostic_notation.hpp, snippet diagnostic_notation: the text of RFC 8949 section 8 for an item; a byte after the
// item is syntax_error.
TEST_CASE("doc reference tools: diagnostic_notation")
{
    std::string const bytes = test::preferred(test::M("a"s, test::A(1, 2.5, test::bytes{"\x01"s})));
    // [diagnostic_notation]
    // bytes: {"a": [1, 2.5, h'01']}
    auto const text = cbor::diagnostic_notation(bytes);   // "{\"a\": [1, 2.5, h'01']}"
    // [diagnostic_notation]
    CHECK_EQ(text, "{\"a\": [1, 2.5, h'01']}"s);
    CHECK_EQ(cbor::diagnostic_notation("\x9f\x01\xff"sv), "[_ 1]"s);
    CHECK_EQ(cbor::diagnostic_notation("\x01\x02"sv).error(), cbor::error::syntax_error);
}

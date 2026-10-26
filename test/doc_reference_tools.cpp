#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;

// Each test in this file is one example of a page in doc/reference/tools/. The tests exist so that every
// example compiles and gives the result that its page states.

// item_size.md: item_size gives the bytes of the first item, and a cut item gives too_little_data.
TEST_CASE("doc reference tools: item_size")
{
    std::string const bytes = test::preferred(test::A(1, 2)) + test::preferred(test::V(7));
    CHECK_EQ(cbor::item_size(bytes), 3u);
    CHECK_EQ(cbor::item_size(std::string_view(bytes).substr(0, 2)).error(), cbor::error::too_little_data);
    CHECK_EQ(cbor::item_size("\x9f\x01\xff"sv).error(), cbor::error::indefinite_length);
}

// sequence.md: the loop gives each item of a CBOR sequence as a view into the bytes of the owner.
TEST_CASE("doc reference tools: sequence")
{
    auto const owner = std::make_shared<std::string const>(
        test::preferred(test::V(1)) + test::preferred(test::V(2)) + test::preferred(test::V(3)));
    std::int64_t sum = 0;
    for (auto const item : cbor::sequence(owner))
        if (item)
            sum += cbor::at_path<"$", std::int64_t>(*item).value_or(0);
    CHECK_EQ(sum, 6);
}

// sequence.md, section Errors: a broken item gives its error once, and the loop then ends.
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

// diagnostic_notation.md: the text of RFC 8949 section 8 for an item; a byte after the item is syntax_error.
TEST_CASE("doc reference tools: diagnostic_notation")
{
    std::string const bytes = test::preferred(test::M("a"s, test::A(1, 2.5, test::bytes{"\x01"s})));
    CHECK_EQ(cbor::diagnostic_notation(bytes), "{\"a\": [1, 2.5, h'01']}"s);
    CHECK_EQ(cbor::diagnostic_notation("\x9f\x01\xff"sv), "[_ 1]"s);
    CHECK_EQ(cbor::diagnostic_notation("\x01\x02"sv).error(), cbor::error::syntax_error);
}

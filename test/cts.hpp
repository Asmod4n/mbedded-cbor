// Reads the JSONPath Compliance Test Suite (test/jsonpath) and checks a path of the compile mode against each
// case. https://github.com/jsonpath-standard/jsonpath-compliance-test-suite
#pragma once

#include "owned_copy.hpp"
#include "reference.hpp"

#include <algorithm>
#include <array>
#include <expected>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

namespace test
{

// These selectors are invalid in RFC 9535 and are valid EDN (draft-ietf-cbor-edn-literals-28): a number with
// a plus sign, without an integer digit or without a fractional digit, -0, the escapes \' and \" in either
// quote, and
// \u{...}. A path takes an EDN literal in brackets and in a filter, so these cases are counted apart.
inline constexpr auto edn_accepted = std::to_array<std::string_view>(
    {"$[?@.a==+1]", "$[?@.a==.1]", "$[?@.a==-.1]", "$[?@.a==1.]", "$[?@.a==1.e1]", "$[1.0]", "$[+1]", "$[-0]",
     "$[\"\\'\"]", "$[\"\\u{1234}\"]", "$[\"\\u{10ffff}\"]", "$['\\\"']"});

inline bool edn_extension(std::string_view const selector)
{
    return std::ranges::find(edn_accepted, selector) != edn_accepted.end();
}

inline bool function_without_support(std::string_view const selector)
{
    return selector.find("match(") != std::string_view::npos ||
           selector.find("search(") != std::string_view::npos;
}

inline cbor::lazy suite_of(char const *const file)
{
    std::ifstream in(file, std::ios::binary);
    REQUIRE(in.good());
    std::string const suite{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto const root = lazy_of_copy(suite);
    REQUIRE(root.has_value());
    return *root;
}

// Each valid case: the compile mode gives one of the nodelists that the suite allows, as an array of exactly
// these values, and the same result as cbor::path::compile of the same text. The text in the entry is the
// selector of the case at its index, so an entry that no longer fits cts.cbor fails.
template <std::size_t N>
void check_valid_cases(
    cbor::lazy const &suite,
    std::array<std::tuple<std::size_t, std::string_view, std::expected<value, error> (*)(cbor::lazy const &)>,
               N> const &entries)
{
    for (auto const &[index, selector, query_of] : entries) {
        CAPTURE(index);
        CAPTURE(selector);
        auto const c = value_of(*suite.at(index));
        REQUIRE(c.has_value());
        array const &fields = *get_if<array>(*c);
        REQUIRE_EQ(fields.size(), 4u);
        CHECK_EQ(fields.at(1).text(), selector);
        auto const document = suite.at(index)->at(2);
        REQUIRE(document.has_value());
        auto const compiled = query_of(*document);
        CHECK_EQ(compiled, query(selector, *document));
        REQUIRE(compiled.has_value());
        array const &results = *get_if<array>(fields.at(3));
        CHECK(std::ranges::any_of(results, [&](value const &nodelist) { return *compiled == nodelist; }));
    }
}

// Each other case: a selector that RFC 9535 refuses, or that holds match() or search(), is not a valid path
// at compile time, and an EDN form that RFC 9535 refuses is valid.
template <std::size_t N>
void check_validity(cbor::lazy const &suite,
                    std::array<std::tuple<std::size_t, std::string_view, bool>, N> const &entries)
{
    for (auto const &[index, selector, valid] : entries) {
        CAPTURE(index);
        CAPTURE(selector);
        auto const c = value_of(*suite.at(index));
        REQUIRE(c.has_value());
        array const &fields = *get_if<array>(*c);
        CHECK_EQ(fields.at(1).text(), selector);
        CHECK_EQ(valid, fields.size() == 2 && edn_extension(selector));
    }
}

} // namespace test

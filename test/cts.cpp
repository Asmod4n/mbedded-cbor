#include "binding.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using cbor::error;

namespace
{

// A nodelist of the suite matches the result of cbor::query, which gives every nodelist as an array, also the
// nodelist of a singular query. The other two forms stay for a reader that gives the value of the one node.
bool nodelist_matches(std::expected<value, cbor::error> const &r, value const &nodelist)
{
    array const &nodes = *get_if<array>(nodelist);
    if (!r)
        return nodes.empty() && (r.error() == error::key_not_found || r.error() == error::index_out_of_bounds ||
                                 r.error() == error::not_indexable);
    return *r == nodelist || (nodes.size() == 1 && *r == nodes.front());
}

// These selectors are invalid in RFC 9535 and are valid EDN (draft-ietf-cbor-edn-literals-28): a number with a
// plus sign, without an integer digit or without a fractional digit, -0, the escapes \' and \" in either quote, and
// \u{...}. A path takes an EDN literal in brackets and in a filter, so these cases are counted apart.
constexpr auto edn_accepted = std::to_array<std::string_view>(
    {"$[?@.a==+1]", "$[?@.a==.1]", "$[?@.a==-.1]", "$[?@.a==1.]", "$[?@.a==1.e1]", "$[1.0]", "$[+1]", "$[-0]",
     "$[\"\\'\"]", "$[\"\\u{1234}\"]", "$[\"\\u{10ffff}\"]", "$['\\\"']"});

bool edn_extension(std::string_view const selector)
{
    return std::ranges::find(edn_accepted, selector) != edn_accepted.end();
}

bool function_without_support(std::string_view const selector)
{
    return selector.find("match(") != std::string_view::npos || selector.find("search(") != std::string_view::npos;
}

} // namespace

// The JSONPath Compliance Test Suite checks RFC 9535 case by case. Each document is the CBOR form of the JSON
// document of the suite, so the run time form of cbor::query must answer as a JSONPath implementation does, except
// where a path takes an EDN literal that RFC 9535 refuses. The owner decided that the library has no regular expressions, so a selector with
// match() or search() must be refused; those cases are counted apart.
TEST_CASE("path: the JSONPath Compliance Test Suite")
{
    std::ifstream in(JSONPATH_CTS "/cts.cbor", std::ios::binary);
    REQUIRE(in.good());
    std::string const suite{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto const root = cbor::lazy::from(suite);
    REQUIRE(root.has_value());
    test_binding binding;
    auto const all = cbor::lazy_decode(binding, *root);
    REQUIRE(all.has_value());
    array const *const suite_cases = get_if<array>(*all);
    REQUIRE(suite_cases != nullptr);
    array const &cases = *suite_cases;
    CHECK_EQ(cases.size(), 706);
    std::size_t refused = 0;
    std::size_t extended = 0;
    std::size_t passed = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        array const *const test_case = get_if<array>(cases.at(i));
        REQUIRE(test_case != nullptr);
        array const &c = *test_case;
        std::string const name(c.at(0).text());
        std::string const selector(c.at(1).text());
        CAPTURE(name);
        CAPTURE(selector);
        if (c.size() == 2 && edn_extension(selector)) {
            auto const r = cbor::query(binding, selector, *cbor::lazy::from(std::string_view("\xf6")));
            CHECK(r.has_value());
            extended += r.has_value() ? 1uz : 0uz;
            continue;
        }
        if (c.size() == 2 || function_without_support(selector)) {
            auto const document = c.size() == 2 ? cbor::lazy::from(std::string_view("\xf6")) : root->at(i)->at(2);
            REQUIRE(document.has_value());
            auto const r = cbor::query(binding, selector, *document);
            bool const invalid = !r && r.error() == error::invalid_path;
            CHECK(invalid);
            refused += c.size() == 2 ? 0uz : 1uz;
            passed += invalid ? 1uz : 0uz;
            continue;
        }
        auto const document = root->at(i)->at(2);
        REQUIRE(document.has_value());
        auto const r = cbor::query(binding, selector, *document);
        CAPTURE((r.has_value() ? error{} : r.error()));
        bool matched = false;
        array const *const results = get_if<array>(c.at(3));
        REQUIRE(results != nullptr);
        for (value const &nodelist : *results)
            matched = matched || nodelist_matches(r, nodelist);
        CHECK(matched);
        passed += matched ? 1uz : 0uz;
    }
    CHECK_EQ(extended, edn_accepted.size());
    MESSAGE("JSONPath CTS: " << passed << " of " << cases.size() << " as expected; " << refused
                             << " valid cases with match() or search() refused by decision; " << extended
                             << " invalid cases valid as EDN");
}

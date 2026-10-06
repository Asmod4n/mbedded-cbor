#include "binding.hpp"

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using cbor::error;

namespace
{

// A nodelist of the suite matches the result of at_path in one of three forms: a query that is not singular gives
// its nodelist as an array; a singular query gives the value of its one node; a singular query that selects
// nothing gives the error of the missing node.
bool nodelist_matches(cbor::result<value> const &r, value const &nodelist)
{
    array const &nodes = *get_if<array>(nodelist);
    if (!r)
        return nodes.empty() && (r.error() == error::key_not_found || r.error() == error::index_out_of_bounds ||
                                 r.error() == error::not_indexable);
    return *r == nodelist || (nodes.size() == 1 && *r == nodes.front());
}

bool function_without_support(std::string_view const selector)
{
    return selector.find("match(") != std::string_view::npos || selector.find("search(") != std::string_view::npos;
}

} // namespace

// The JSONPath Compliance Test Suite checks RFC 9535 case by case. Each document is the CBOR form of the JSON
// document of the suite, so the run time form of at_path, which reads RFC 9535 and no extension, must answer as a
// JSONPath implementation does. The owner decided that the library has no regular expressions, so a selector with
// match() or search() must be refused; those cases are counted apart.
TEST_CASE("path: the JSONPath Compliance Test Suite")
{
    std::ifstream in(JSONPATH_CTS "/cts.cbor", std::ios::binary);
    REQUIRE(in.good());
    std::string const suite{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto const root = cbor::decode<64>(suite);
    REQUIRE(root.has_value());
    test_binding binding;
    auto const all = cbor::lazy_decode<64>(binding, *root);
    REQUIRE(all.has_value());
    array const &cases = *get_if<array>(*all);
    CHECK_EQ(cases.size(), 706);
    std::size_t refused = 0;
    std::size_t passed = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        array const &c = *get_if<array>(cases.at(i));
        std::string const name(c.at(0).text());
        std::string const selector(c.at(1).text());
        CAPTURE(name);
        CAPTURE(selector);
        if (c.size() == 2 || function_without_support(selector)) {
            auto const document = c.size() == 2 ? cbor::decode<64>(std::string_view("\xf6")) : root->at<64>(static_cast<std::int64_t>(i))->at<64>(std::int64_t{2});
            REQUIRE(document.has_value());
            auto const r = cbor::at_path<64>(binding, selector, *document);
            bool const invalid = !r && r.error() == error::invalid_path;
            CHECK(invalid);
            refused += c.size() == 2 ? 0uz : 1uz;
            passed += invalid ? 1uz : 0uz;
            continue;
        }
        auto const document = root->at<64>(static_cast<std::int64_t>(i))->at<64>(std::int64_t{2});
        REQUIRE(document.has_value());
        auto const r = cbor::at_path<64>(binding, selector, *document);
        CAPTURE((r.has_value() ? error{} : r.error()));
        bool matched = false;
        for (value const &nodelist : *get_if<array>(c.at(3)))
            matched = matched || nodelist_matches(r, nodelist);
        CHECK(matched);
        passed += matched ? 1uz : 0uz;
    }
    MESSAGE("JSONPath CTS: " << passed << " of " << cases.size() << " as expected; " << refused
                             << " valid cases with match() or search() refused by decision");
}

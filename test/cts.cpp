#include "cts.hpp"
#include "owned_copy.hpp"
#include "reference.hpp"

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
        return nodes.empty() &&
               (r.error() == error::key_not_found || r.error() == error::index_out_of_bounds ||
                r.error() == error::not_indexable);
    return *r == nodelist || (nodes.size() == 1 && *r == nodes.front());
}

} // namespace

// The JSONPath Compliance Test Suite checks RFC 9535 case by case. Each document is the CBOR form of the JSON document
// of the suite. So the run-time form of cbor::query must answer as a JSONPath implementation does. One exception holds:
// a path can take an EDN literal that RFC 9535 refuses. The owner decided that the library has no regular expressions,
// so a selector with match() or search() must be refused; those cases are counted apart.
TEST_CASE("path: the JSONPath Compliance Test Suite")
{
    std::ifstream in(JSONPATH_CTS "/cts.cbor", std::ios::binary);
    REQUIRE(in.good());
    std::string const suite{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto const root = test::make_lazy_from_copy(suite);
    REQUIRE(root.has_value());
    auto const all = test::make_value_from_item(*root);
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
        if (c.size() == 2 && test::edn_extension(selector)) {
            auto const r = test::query(selector, *test::make_lazy_from_copy(std::string_view("\xf6")));
            CHECK(r.has_value());
            extended += r.has_value() ? 1uz : 0uz;
            continue;
        }
        if (c.size() == 2 || test::function_without_support(selector)) {
            auto const null_document = test::make_lazy_from_copy(std::string_view("\xf6"));
            auto const entry = root->at(i);
            REQUIRE(entry.has_value());
            auto const selected = entry->at(2);
            bool const has_document = c.size() == 2 ? null_document.has_value() : selected.has_value();
            REQUIRE(has_document);
            auto const r = test::query(selector, c.size() == 2 ? *null_document : *selected);
            bool const invalid = !r && r.error() == error::invalid_path;
            CHECK(invalid);
            refused += c.size() == 2 ? 0uz : 1uz;
            passed += invalid ? 1uz : 0uz;
            continue;
        }
        auto const entry = root->at(i);
        REQUIRE(entry.has_value());
        auto const document = entry->at(2);
        REQUIRE(document.has_value());
        auto const r = test::query(selector, *document);
        CAPTURE((r.has_value() ? error{} : r.error()));
        bool matched = false;
        array const *const results = get_if<array>(c.at(3));
        REQUIRE(results != nullptr);
        for (value const &nodelist : *results)
            matched = matched || nodelist_matches(r, nodelist);
        CHECK(matched);
        passed += matched ? 1uz : 0uz;
    }
    CHECK_EQ(extended, test::edn_accepted.size());
    MESSAGE("JSONPath CTS: " << passed << " of " << cases.size() << " as expected; " << refused
                             << " valid cases with match() or search() refused by decision; " << extended
                             << " invalid cases valid as EDN");
}

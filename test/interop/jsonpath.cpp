// Runs the same JSONPath queries over the same documents in the library and in jsoncons::jsonpath, the
// JSONPath library that Debian packages, and prints every difference. Exits 1 when a difference exists.
// https://www.rfc-editor.org/rfc/rfc9535
#include <cbor/cbor.hpp>
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

using jsoncons::ojson;

// The documents come from a fixed seed, so a difference shows again in the next run.
constexpr std::uint32_t seed = 9535;
constexpr std::size_t documents_per_family = 3000;
constexpr int depth_max = 4;
constexpr int elements_max = 4;
constexpr auto keys = std::to_array<std::string_view>({"a", "b", "c", "d"});

// jsoncons::jsonpath reads null, false, [], {} and "" as false where RFC 9535 2.3.5.2.1 asks only whether a
// node exists, in an existence test and under !, && and ||. A query with an existence test therefore runs
// over the plain family, whose documents hold none of these five values; every other query runs over the full
// family too. The members of a map stand in the order of the keys, so equal maps have equal member order:
// jsoncons compares maps member by member in order, RFC 9535 2.3.5.2.2 by names.
ojson document_of(std::mt19937 &random, int const depth, bool const plain)
{
    std::uniform_int_distribution<int> kind(0, depth < depth_max ? 9 : 7);
    switch (kind(random)) {
    case 0:
    case 1: {
        constexpr int integer_min = -3;
        constexpr int integer_max = 3;
        int const n = std::uniform_int_distribution<int>(integer_min, integer_max)(random);
        return plain && n == 0 ? ojson(1) : ojson(n);
    }
    case 2: {
        constexpr auto floats = std::to_array<double>({1.0, 1.5, -0.0, 2.5, -1.25});
        return ojson(floats[std::uniform_int_distribution<std::size_t>(0, floats.size() - 1)(random)]);
    }
    case 3:
    case 4: {
        constexpr auto strings =
            std::to_array<std::string_view>({"a", "b", "x", "\xc3\xa9", "\xf0\x9f\x98\x80", ""});
        std::size_t const last = strings.size() - (plain ? 2 : 1);
        return ojson(std::string(strings[std::uniform_int_distribution<std::size_t>(0, last)(random)]));
    }
    case 5:
        return plain ? ojson(true) : ojson(std::uniform_int_distribution<int>(0, 1)(random) == 1);
    case 6:
        return plain ? ojson(2) : ojson::null();
    case 7:
        return ojson(3);
    case 8: {
        ojson a(jsoncons::json_array_arg);
        int const n = std::uniform_int_distribution<int>(plain ? 1 : 0, elements_max)(random);
        for (int i = 0; i < n; ++i)
            a.push_back(document_of(random, depth + 1, plain));
        return a;
    }
    default: {
        ojson m(jsoncons::json_object_arg);
        for (std::string_view const key : keys)
            if (std::uniform_int_distribution<int>(0, 1)(random) == 1)
                m.insert_or_assign(std::string(key), document_of(random, depth + 1, plain));
        if (plain && m.empty())
            m.insert_or_assign("a", ojson(1));
        return m;
    }
    }
}

std::vector<ojson> documents_of(bool const plain)
{
    std::mt19937 random(seed + (plain ? 1 : 0));
    std::vector<ojson> documents;
    for (std::size_t i = 0; i < documents_per_family; ++i) {
        ojson top(jsoncons::json_array_arg);
        int const n = std::uniform_int_distribution<int>(1, elements_max)(random);
        for (int j = 0; j < n; ++j)
            top.push_back(document_of(random, 1, plain));
        documents.push_back(std::move(top));
        documents.push_back(document_of(random, 1, plain));
    }
    return documents;
}

ojson nodes_of(std::expected<cbor::nodelist, cbor::error> const &r)
{
    if (!r)
        return ojson("error " + std::error_code{r.error()}.message());
    ojson a(jsoncons::json_array_arg);
    for (cbor::lazy const &node : *r) {
        std::string_view const rest = node.top_level.encoded.substr(node.offset);
        auto const size = cbor::item_size(rest);
        if (!size)
            return ojson("error " + std::error_code{size.error()}.message());
        a.push_back(jsoncons::cbor::decode_cbor<ojson>(rest.substr(0, *size)));
    }
    return a;
}

ojson sorted(ojson a)
{
    std::ranges::sort(a.array_range(),
                      [](ojson const &x, ojson const &y) { return x.to_string() < y.to_string(); });
    return a;
}

struct tally {
    std::size_t compared = 0;
    std::size_t order_only = 0;
    std::size_t findings = 0;
};

template <cbor::fixed_string Path>
void compare(std::vector<ojson> const &documents, tally &t)
{
    constexpr std::size_t shown_max = 5;
    std::size_t shown = 0;
    auto const run_time = cbor::path::compile<cbor::nodelist>(Path.view());
    for (ojson const &document : documents) {
        std::vector<std::uint8_t> encoded;
        jsoncons::cbor::encode_cbor(document, encoded);
        auto const doc = cbor::lazy::adopt(std::string(encoded.begin(), encoded.end()));
        ojson const compiled = nodes_of(doc->at_path<Path, cbor::nodelist>());
        ojson const interpreted = nodes_of(doc->at_path(*run_time));
        ojson const reference = jsoncons::jsonpath::json_query(document, std::string(Path.view()));
        ++t.compared;
        bool const same = compiled == interpreted && compiled == reference;
        bool const order_only =
            !same && compiled == interpreted && compiled.is_array() && sorted(compiled) == sorted(reference);
        if (same)
            continue;
        if (order_only) {
            ++t.order_only;
            continue;
        }
        ++t.findings;
        if (shown++ < shown_max)
            std::cout << "FINDING " << Path.view() << " over " << document << "\n  compile mode  " << compiled
                      << "\n  run-time mode " << interpreted << "\n  jsoncons      " << reference << "\n";
    }
}

template <cbor::fixed_string... Paths>
tally compare_all(std::vector<ojson> const &documents)
{
    tally t;
    (compare<Paths>(documents, t), ...);
    return t;
}

}

// Each path of the full family uses no existence test, so the truthiness of jsoncons cannot change its
// result: keys, indexes, wildcards, slices, descendants, comparisons of numbers and strings, &&, ||, !,
// length(), nested and absolute queries.
int main()
{
    std::vector<ojson> const full = documents_of(false);
    std::vector<ojson> const plain = documents_of(true);
    tally const a =
        compare_all<"$", "$.a", "$.b.c", "$[0]", "$[-1]", "$[1].a", "$[*]", "$.*", "$..*", "$..a", "$..[0]",
                    "$[1:3]", "$[::-1]", "$[::2]", "$[-2:]", "$[0,1]", "$['a','b']", "$.a[*]", "$[?@ == 1]",
                    "$[?@ != 1]", "$[?@ < 2]", "$[?@ > 1.5]", "$[?@ == 'a']", "$[?@ < 'b']", "$[?@ == 1.0]",
                    "$[?@.a == 1]", "$[?@.a != 1]", "$[?@.a < 2]", "$..[?@ == 2]", "$[?@.a == 1 || @.b == 2]",
                    "$[?@.a < 3 && @.a > -1]", "$[?!(@ == 1)]", "$[?length(@) == 2]", "$.*[?@ > 0]",
                    "$[?@ == 'é']", "$[?@ < '😀']">(full);
    // The plain family adds the existence tests, also under !, && and ||, and the comparison of two queries,
    // of which one may be absent: RFC 9535 2.3.5.2.2 makes an absent operand equal only to another absent
    // operand, and jsoncons reads an absent operand as null, so the two agree only where no node is null.
    tally const b =
        compare_all<"$[?@.a]", "$[?!@.a]", "$[?@.a && @.b]", "$[?@.a || @[0]]", "$[?@[?@ > 1]]", "$..[?@.a]",
                    "$[?@.*]", "$[?@.a == @.b]", "$[?$.a]", "$[?@[0] && !@[1]]", "$[?@[0] == $[0]]">(plain);
    std::cout << "jsonpath (libjsoncons-dev): full family " << a.compared << " compared, " << a.order_only
              << " order only, " << a.findings << " findings; plain family " << b.compared << " compared, "
              << b.order_only << " order only, " << b.findings << " findings\n";
    return a.findings + b.findings == 0 ? 0 : 1;
}

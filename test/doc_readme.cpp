#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "reference.hpp"
#include "limits_guard.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace std::string_literals;

// Each test in this file is one example of README.md. The tests exist so that every example in the README
// compiles and gives the result that the README states.

// Step 2 reads a message in place: size, a missing key, and a loop over the elements of an array.
TEST_CASE("doc readme: read a message")
{
    std::string const bytes = test::preferred(test::M("sensor"s, "t1"s, "value"s, 21.5, "samples"s, test::A(3, 4, 5)));
    auto const doc = cbor::lazy::copy(bytes).value();

    std::uint64_t const pairs = doc.size().value();
    auto const missing = doc.at("unit");
    CHECK_EQ(missing.error(), cbor::error::key_not_found);
    CHECK_EQ(pairs, 3u);

    int sum = 0;
    if (auto const samples = doc.at("samples"))
        for (auto const &x : samples->elements().value())
            if (x)
                sum += x->get<int>().value_or(0);
    CHECK_EQ(sum, 12);
}

// Step 3 reads one value by its path; a missing member gives key_not_found.
TEST_CASE("doc readme: read one value by its path")
{
    std::string const bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
    auto const doc = cbor::lazy::copy(bytes).value();

    auto const id = doc.at_path<"$.user.id", std::int64_t>();
    auto const name = doc.at_path<"$.user.name", std::string_view>();
    auto const age = doc.at_path<"$.user.age", int>();
    CHECK_EQ(*id, 42);
    CHECK_EQ(**name, "ann");
    CHECK_EQ(age.error(), cbor::error::key_not_found);
}

// Step 4 cuts the last byte: a read of the cut value gives too_little_data, which converts to
// std::error_code; a read of a value before the cut still succeeds, because a read stops at its item.
TEST_CASE("doc readme: handle bad input")
{
    std::string const bytes = test::preferred(test::M("user"s, test::M("id"s, 42, "name"s, "ann"s)));
    std::string const cut = bytes.substr(0, bytes.size() - 1);

    auto const doc = cbor::lazy::copy(cut).value();
    auto const name = doc.at_path<"$.user.name", std::string_view>();
    REQUIRE_FALSE(name.has_value());
    CHECK_EQ(name.error(), cbor::error::too_little_data);
    std::string const why = std::error_code(name.error()).message();
    CHECK_EQ(why, "too little data");
    CHECK_EQ(cbor::at_path<"$.user.id", std::int64_t>(cut), 42);
}

// Step 5 sets limits; an array of 101 elements is over a limit of 100.
TEST_CASE("doc readme: set limits")
{
    test::limits_guard const guard{{.nesting_depth = 16, .string_length = 4096, .container_elements = 100}};

    test::array a;
    for (int i = 0; i < 101; ++i)
        a.push_back(test::V(i));
    std::string const bytes = test::preferred(test::value{std::move(a)});
    auto const n = cbor::lazy::copy(bytes).value().size();
    CHECK_EQ(n.error(), cbor::error::container_elements_exceeded);
}

// Step 6 reads a CBOR sequence item by item and prints each item in diagnostic notation.
TEST_CASE("doc readme: read a stream of items")
{
    auto const log = std::make_shared<std::string const>(test::preferred(test::V(1)) +
                                                         test::preferred(test::value{"a"s}) + test::preferred(test::A(1, 2)));
    std::vector<std::string> printed;
    for (auto const &item : cbor::sequence{log})
        if (item)
            printed.push_back(cbor::diagnostic_notation(*item).value());
    CHECK_EQ(printed, std::vector<std::string>{"1", "\"a\"", "[1, 2]"});
}

#ifdef __cpp_impl_reflection

namespace
{

struct [[=cbor::tag(1900)]] series {
    std::string unit;
    std::vector<double> values;
};

struct reading {
    std::string sensor;
    double value;
    std::vector<std::uint16_t> samples;
};

} // namespace

// Step 7 writes a struct as Packed CBOR, reads one element at its offset and sums a view of an array.
TEST_CASE("doc readme: packed")
{
    std::string const bytes = cbor::packed<series>::encode({"m", {1.5, 2.5, 3.5}}).value();
    auto const second = cbor::packed<series>::at_path<"$.values[1]">(bytes);
    CHECK_EQ(*second, 2.5);
    auto const root = cbor::packed<series>::path(bytes).value();
    auto const values = root.view<"$.values">();
    double const sum = std::ranges::fold_left(*values, 0.0, std::plus{});
    CHECK_EQ(sum, 7.5);
}

// Step 8 writes a struct as deterministic CBOR: the keys in RFC 8949 4.2.1 order, every number shortest.
TEST_CASE("doc readme: structs")
{
    std::string const bytes = cbor::structs<reading>::encode({"t1", 21.5, {3, 4, 5}}).value();
    std::string const text = cbor::diagnostic_notation(bytes).value();
    CHECK_EQ(text, R"({"value": 21.5, "sensor": "t1", "samples": [3, 4, 5]})");
    CHECK_EQ(bytes.size(), 32u);
}

#endif

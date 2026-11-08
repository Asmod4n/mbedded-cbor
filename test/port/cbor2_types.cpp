// The cases of tests/test_types.py of the Python library cbor2, ported to the public interface of this library.
// These tests exist to show that the C++ forms of a tag and of a simple value behave as CBORTag and
// CBORSimpleValue of cbor2. The expected values come from the upstream tests and are checked against RFC 8949 in
// refs/.
//
// Upstream: https://github.com/agronholm/cbor2, commit 5fa04929b44c82b8416d6afc064afb0da198f278.
//
// The license of the upstream tests:
//
// The MIT License (MIT)
//
// Copyright (c) 2016 Alex Grönholm
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "cbor2.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using cbor::error;
using cbor2::unhexlify;

// Upstream: TestCBORTag.test_attr. cbor::tagged_item holds the tag number and the content.
TEST_CASE("cbor2 types: TestCBORTag.test_attr")
{
    using tag = cbor::tagged_item<1, std::string>;
    tag const t{"foo"};
    CHECK_EQ(tag::number, 1u);
    CHECK_EQ(t.content, "foo");
}

// Upstream: TestCBORTag.test_self_referential_hash, test_self_referential_eq and test_self_referential_repr. The
// payload 28(999(29(0))) refers to itself, as in cbor2: cbor::decode resolves the tags (owner decision), so the
// content of tag 999 is a shared_reference to the tag 999 node itself, a cycle through a tag that is not 28 (allowed
// by the owner). The reference model shows the reference as 29(0).
TEST_CASE("cbor2 types: TestCBORTag self-referential payload")
{
    auto const decoded = cbor2::loads(unhexlify("d81cd903e7d81d00"));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == test::value{test::tagged{999, test::value{test::tagged{29, V(0)}}}});
    auto const tree = cbor::decode(unhexlify("d81cd903e7d81d00"));
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->get<cbor::type::tag>().value().second->get<cbor::type::shared_reference>().value().second,
             &**tree);
}

// Upstream: TestCBORSimpleValue.test_equals, the comparisons of two simple values. RFC 8949 5.6.1: "Simple values
// are equal if they simply have the same value". The comparisons of a simple value with an integer are not
// ported: cbor::simple_value is an enum class and does not compare with an integer; RFC 8949 5.6.1 also says that
// "a simple value 2 is not equivalent to an integer 2".
TEST_CASE("cbor2 types: TestCBORSimpleValue.test_equals")
{
    cbor::simple_value const tag1{1};
    cbor::simple_value const tag2{1};
    cbor::simple_value const tag3{21};
    cbor::simple_value const tag4{99};
    CHECK(tag1 == tag2);
    CHECK(tag1 != tag3);
    CHECK(tag4 > tag1);
    CHECK(tag4 >= tag3);
    CHECK(tag1 <= tag4);
}

// Upstream: TestCBORSimpleValue.test_ordering.
TEST_CASE("cbor2 types: TestCBORSimpleValue.test_ordering")
{
    std::vector<cbor::simple_value> disordered;
    for (int const v : {9, 7, 3, 8, 4, 0, 2, 5, 6, 1})
        disordered.push_back(cbor::simple_value{static_cast<std::uint8_t>(v)});
    std::vector<cbor::simple_value> expected;
    for (std::uint8_t v = 0; v < 10; ++v)
        expected.push_back(cbor::simple_value{v});
    std::ranges::sort(disordered);
    CHECK(disordered == expected);
}

// Upstream: TestCBORSimpleValue.test_simple_value_out_of_range, the values 24 and 31. RFC 8949 3.3: the simple
// values 24 to 31 are reserved. encode.hpp: "@retval reserved_simple_value `value` is from 24 to 31". The values -1
// and 256 are not ported: cbor::simple_value has 8 bits, so they do not compile.
TEST_CASE("cbor2 types: TestCBORSimpleValue.test_simple_value_out_of_range")
{
    for (int const v : {24, 31}) {
        INFO("value: ", v);
        std::string out;
        cbor::encoder e(out);
        CHECK_EQ(e.simple_value(cbor::simple_value{static_cast<std::uint8_t>(v)}).error_or(error{}),
                 error::reserved_simple_value);
    }
}

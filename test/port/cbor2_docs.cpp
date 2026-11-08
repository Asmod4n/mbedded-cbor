// The examples of docs/usage.rst and docs/customizing.rst of the Python library cbor2, ported to the public
// interface of this library. These tests exist to show that the examples of the cbor2 documentation give the same
// bytes and values here. The expected bytes are the bytes that the documentation prints, or the preferred
// serialization of RFC 8949 4.2.2 that cbor2 writes where the documentation prints none.
//
// Upstream: https://github.com/agronholm/cbor2, commit 5fa04929b44c82b8416d6afc064afb0da198f278.
//
// The license of the upstream documentation:
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

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using cbor::error;
using cbor2::dumps;
using cbor2::loads;
using cbor2::unhexlify;

namespace
{

struct point {
    int x;
    int y;
};

struct typed_point {
    std::string typename_;
    int x;
    int y;
};

} // namespace

template <>
struct cbor::describe_members<point> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{"x", &point::x},
        cbor::member{"y", &point::y},
    };
};

template <>
struct cbor::describe_members<typed_point> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{"typename", &typed_point::typename_},
        cbor::member{"x", &typed_point::x},
        cbor::member{"y", &typed_point::y},
    };
};

// Upstream: docs/usage.rst, "Basic usage": dumps(['hello', 'world']) and loads of the result.
TEST_CASE("cbor2 docs: basic usage")
{
    std::vector<std::string> const value{"hello", "world"};
    std::string const data = dumps(value);
    CHECK_EQ(data, "826568656c6c6f65776f726c64");
    auto const obj = cbor::structs<std::vector<std::string>>::decode(unhexlify(data));
    REQUIRE(obj.has_value());
    CHECK_EQ(**obj, value);
}

// Upstream: docs/usage.rst, "Decoding concatenated objects". A CBOR sequence (RFC 8742) is items one after the
// other. cbor::sequence gives each item.
TEST_CASE("cbor2 docs: decoding concatenated objects")
{
    auto const data = std::make_shared<std::string const>(unhexlify("01") + unhexlify("6568656c6c6f") + unhexlify("f6"));
    std::vector<test::value> values;
    for (auto const item : cbor::sequence(data)) {
        REQUIRE(item.has_value());
        auto const decoded = loads(*item);
        REQUIRE(decoded.has_value());
        values.push_back(*decoded);
    }
    CHECK(values == std::vector<test::value>{V(1), V("hello"), V(test::simple{22})});

    // "An empty stream yields no objects."
    CHECK(cbor::sequence(std::make_shared<std::string const>()).empty());

    // "EOF during an object means truncated data and must remain an error."
    std::vector<error> errors;
    for (auto const item : cbor::sequence(std::make_shared<std::string const>(unhexlify("016568656c")))) {
        if (!item.has_value())
            errors.push_back(item.error());
    }
    CHECK(errors == std::vector<error>{error::too_little_data});
}

// Upstream: docs/usage.rst, the tag 55799 of the file utility: "This will be ignored on decode and the original
// data content will be returned." RFC 8949 3.4.6. decode.hpp: "One leading tag 55799 is read as its content".
TEST_CASE("cbor2 docs: self-described CBOR")
{
    std::string out;
    cbor::encoder e(out);
    REQUIRE(e.tag(55799).has_value());
    REQUIRE(e.array(2).has_value());
    REQUIRE(e.text_string("hello").has_value());
    REQUIRE(e.text_string("world").has_value());
    REQUIRE(e.finish().has_value());
    auto const decoded = loads(out);
    REQUIRE(decoded.has_value());
    CHECK(*decoded == A("hello", "world"));
}

// Upstream: docs/customizing.rst, the semantic decoder of tag 1: dumps(CBORTag(1, 1363896240)) gives
// datetime(2013, 3, 21, 20, 4, tzinfo=timezone.utc). structs.hpp: "std::chrono::sys_seconds | tag 1 on an
// integer".
TEST_CASE("cbor2 docs: semantic decoder of tag 1")
{
    std::string const payload = dumps(cbor::tagged_item<1, std::int64_t>{1363896240});
    CHECK_EQ(payload, "c11a514b67b0");
    auto const decoded = cbor::structs<std::chrono::sys_seconds>::decode(unhexlify(payload));
    REQUIRE(decoded.has_value());
    using namespace std::chrono;
    CHECK((**decoded == sys_days{year{2013} / 3 / 21} + hours{20} + minutes{4}));
}

// Upstream: docs/customizing.rst, the tag hook and the encoder of Point: "prints b'\xd9\x0f\xa0\x82\x04\x05'" for
// tag 4000 on [4, 5].
TEST_CASE("cbor2 docs: tag 4000 of Point")
{
    using tagged_point = cbor::tagged_item<4000, std::tuple<int, int>>;
    std::string const payload = dumps(tagged_point{{4, 5}});
    CHECK_EQ(payload, "d90fa0820405");
    auto const decoded = cbor::structs<tagged_point>::decode(unhexlify(payload));
    REQUIRE(decoded.has_value());
    CHECK_EQ(std::get<0>((*decoded)->content), 4);
    CHECK_EQ(std::get<1>((*decoded)->content), 5);
}

// Upstream: docs/customizing.rst, the object hook: dumps({"typename": "Point", "x": 4, "y": 5}) read as a Point.
// The payload is the map in the order of the Python dict.
TEST_CASE("cbor2 docs: object hook")
{
    auto const decoded = cbor::structs<typed_point>::decode(unhexlify("a368747970656e616d6565506f696e74617804617905"));
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->typename_, "Point");
    CHECK_EQ((*decoded)->x, 4);
    CHECK_EQ((*decoded)->y, 5);
}

// Upstream: docs/customizing.rst, the semantic decoder of tag 258: a set and a map with a set as its key come back
// equal. structs.hpp: "std::set, std::flat_set | tag 258 on an array".
TEST_CASE("cbor2 docs: semantic decoder of tag 258")
{
    std::set<std::string> const set{"aa", "bb"};
    auto const s = cbor::structs<std::set<std::string>>::decode(unhexlify(dumps(set)));
    REQUIRE(s.has_value());
    CHECK_EQ(**s, set);

    std::map<std::set<std::string>, std::string> const keyed{{{"aa", "bb"}, "value"}};
    auto const m = cbor::structs<std::map<std::set<std::string>, std::string>>::decode(unhexlify(dumps(keyed)));
    REQUIRE(m.has_value());
    CHECK_EQ(**m, keyed);
}

// Upstream: docs/customizing.rst, the default encoder: "prints b'\xd9\xc3P\x82n__main__.Point\xa2ax\x04ay\x05'",
// tag 50000 on the name of the class and a map of its attributes.
TEST_CASE("cbor2 docs: default encoder")
{
    using named = cbor::tagged_item<50000, std::tuple<std::string, point>>;
    CHECK_EQ(dumps(named{{"__main__.Point", point{4, 5}}}), "d9c350826e5f5f6d61696e5f5f2e506f696e74a2617804617905");
}

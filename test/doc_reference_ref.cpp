#include <cbor/cbor.hpp>
#include <doctest/doctest.h>

#include "doc_types.hpp"
#include "reference.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;

// Each test in this file is one example of a reference page of the ref group. The comment of the header gives the
// example through the snippet markers. The tests exist so that every example compiles and gives the result that the page
// states.

using reference_ref::person;

// The snippet of the group ref, between the markers "ref group". The example on the page of the group ref shows that a
// ref keeps the bytes. They stay after the lazy that read them ends. That clone copies the bytes. The test checks each
// value that the comments of the example state, and the bytes against the encoding of RFC 8949.
TEST_CASE("doc reference ref: the example of the group ref")
{
    std::string bytes = test::encode_in_preferred_form(test::M("name"s, "ann"s));
    CHECK_EQ(bytes, "\xa1\x64\x6e\x61\x6d\x65\x63\x61\x6e\x6e"s);
    std::optional<cbor::ref<std::string_view>> kept;
    // [ref group]
    // bytes: a std::string with {"name": "ann"}
    {
        auto const doc = cbor::lazy::adopt(std::move(bytes));
        auto const name_2 = doc->at("name");
        auto const name = name_2->get<std::string_view>();
        if (name)
            kept = *name;  // a copy of the ref: it shares the owner of the bytes
    }  // doc and name end here; kept keeps the bytes
    std::string const copy = kept->clone();  // "ann", in new memory
    // [ref group]
    REQUIRE(kept.has_value());
    CHECK_EQ(**kept, "ann");
    CHECK_EQ(copy, "ann");
    CHECK_NE(copy.data(), (**kept).data());
}

// ref.hpp, the comment of ref: *, -> and == read the value. A copy shares the owner, so a view stays valid after the
// first ref ends.
TEST_CASE("doc reference ref: cbor::ref")
{
    std::optional<cbor::ref<person>> keep;
    {
        std::string bytes = test::encode_in_preferred_form(test::M("name"s, "ann"s, "age"s, 30));
        auto const p = cbor::structs<person>::decode(std::move(bytes));
        REQUIRE(p.has_value());
        keep = *p;
    }
    CHECK_EQ(keep->operator->()->name, "ann");
    CHECK_EQ((**keep).age, 30);

    auto const age = cbor::structs<int>::decode(test::encode_in_preferred_form(test::V(30)));
    REQUIRE(age.has_value());
    CHECK(*age == 30);
    CHECK_EQ(**age, 30);
}

// ref.hpp, the comment of ref: operator* gives a view by value; a view taken out of the ref is valid while the ref
// lives.
TEST_CASE("doc reference ref: a view by value")
{
    auto const owner = std::make_shared<std::string const>(test::encode_in_preferred_form(test::V("abc"s)));
    auto const r = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(r.has_value());
    std::string_view const v = **r;
    CHECK_EQ(v, "abc");
    CHECK_EQ(v.data(), std::span(*owner).subspan(1).data());
    CHECK(*r == "abc");
}

// ref.hpp, snippet ref: the rvalue form of decode gives a ref that views the moved bytes.
TEST_CASE("doc reference ref: the example of ref")
{
    std::string bytes = test::encode_in_preferred_form(test::V("abc"s));
    std::string_view v;
    bool same = false;
    // [ref]
    // bytes: a std::string with "abc"
    auto const r = cbor::structs<std::string_view>::decode(std::move(bytes));
    if (r) {
        v = **r;             // "abc", valid while *r lives
        same = *r == "abc";  // true
    }
    // [ref]
    REQUIRE(r.has_value());
    CHECK_EQ(v, "abc");
    CHECK(same);
}

// ref.hpp, snippet owning: clone of a struct with a view gives an owning whose view stays valid after the ref ends.
TEST_CASE("doc reference ref: the example of owning")
{
    std::optional<cbor::owning<person>> kept;
    {
        auto const decoded = cbor::structs<person>::decode(test::encode_in_preferred_form(test::M("name"s, "ann"s, "age"s, 30)));
        auto const p = decoded.value();
        // [owning]
        // p: the cbor::ref<person> of {"name": "ann", "age": 30}
        auto const own = p.clone();
        std::string_view const name = own->name;  // "ann", valid after p ends
        // [owning]
        CHECK_EQ(name, "ann");
        kept = own;
    }
    CHECK_EQ((*kept)->name, "ann");
}

// ref.hpp, the comment of ref: a moved-from ref holds a value-initialized T.
TEST_CASE("doc reference ref: move")
{
    auto const decoded = cbor::structs<std::string_view>::decode(test::encode_in_preferred_form(test::V("abc"s)));
    auto r = decoded.value();
    auto const s = std::move(r);
    CHECK_EQ(*s, "abc");
}

// ref.hpp, the comments of ref and owning: clone of a text view gives a std::string. Clone of a struct with a view gives
// an owning that holds its own copy of the bytes of each view.
TEST_CASE("doc reference ref: clone")
{
    auto const decoded = cbor::structs<std::string_view>::decode(test::encode_in_preferred_form(test::V("abc"s)));
    auto const text = decoded.value();
    std::string const copy = text.clone();
    CHECK_EQ(copy, "abc");

    std::optional<cbor::owning<person>> own;
    std::string_view seen;
    {
        auto const owner = std::make_shared<std::string const>(test::encode_in_preferred_form(test::M("name"s, "ann"s, "age"s, 30)));
        auto const read = cbor::structs<person>::decode(owner);
        auto const p = read.value();
        own = p.clone();
        seen = (*own)->name;
        CHECK_NE(seen.data(), p->name.data());
    }
    CHECK_EQ((**own).name, "ann");
    CHECK_EQ((*own)->age, 30);
    auto const second = *own;
    CHECK_EQ(second->name.data(), seen.data());
}


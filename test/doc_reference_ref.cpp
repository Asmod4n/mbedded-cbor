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

// Each test in this file is one example of a page in doc/reference/ref/. The tests exist so that every example
// compiles and gives the result that the page states.


using reference_ref::person;

// ref.md: *, -> and == read the value; a copy shares the owner, so a view stays valid after the first ref ends.
TEST_CASE("doc reference ref: cbor::ref")
{
    std::optional<cbor::ref<person>> keep;
    {
        std::string bytes = test::preferred(test::M("name"s, "ann"s, "age"s, 30));
        auto const p = cbor::structs<person>::decode(std::move(bytes));
        REQUIRE(p.has_value());
        keep = *p;
    }
    CHECK_EQ(keep->operator->()->name, "ann");
    CHECK_EQ((**keep).age, 30);

    auto const age = cbor::structs<int>::decode(test::preferred(test::V(30)));
    REQUIRE(age.has_value());
    CHECK(*age == 30);
    CHECK_EQ(**age, 30);
}

// ref.md: operator* gives a view by value; a view taken out of the ref is valid while the ref lives.
TEST_CASE("doc reference ref: a view by value")
{
    auto const owner = std::make_shared<std::string const>(test::preferred(test::V("abc"s)));
    auto const r = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(r.has_value());
    std::string_view const v = **r;
    CHECK_EQ(v, "abc");
    CHECK_EQ(v.data(), owner->data() + 1);
    CHECK(*r == "abc");
}

// ref.md, section Example: the rvalue form of decode gives a ref that views the moved bytes.
TEST_CASE("doc reference ref: the example of ref.md")
{
    std::string bytes = test::preferred(test::V("abc"s));
    auto const r = cbor::structs<std::string_view>::decode(std::move(bytes));
    REQUIRE(r.has_value());
    std::string_view const v = **r;
    CHECK_EQ(v, "abc");
    CHECK(*r == "abc");
}

// ref.md: a moved-from ref holds a value-initialized T.
TEST_CASE("doc reference ref: move")
{
    auto r = cbor::structs<std::string_view>::decode(test::preferred(test::V("abc"s))).value();
    auto const s = std::move(r);
    CHECK_EQ(*s, "abc");
}

// ref.md and owning.md: clone of a text view gives a std::string; clone of a struct with a view gives an owning
// that holds its own copy of the bytes of each view.
TEST_CASE("doc reference ref: clone")
{
    auto const text = cbor::structs<std::string_view>::decode(test::preferred(test::V("abc"s))).value();
    std::string const copy = text.clone();
    CHECK_EQ(copy, "abc");

    std::optional<cbor::owning<person>> own;
    std::string_view seen;
    {
        auto const owner = std::make_shared<std::string const>(test::preferred(test::M("name"s, "ann"s, "age"s, 30)));
        auto const p = cbor::structs<person>::decode(owner).value();
        own = p.clone();
        seen = (*own)->name;
        CHECK_NE(seen.data(), p->name.data());
    }
    CHECK_EQ((**own).name, "ann");
    CHECK_EQ((*own)->age, 30);
    auto const second = *own;
    CHECK_EQ(second->name.data(), seen.data());
}


#include "reference.hpp"
#include "owned_copy.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/path.md. The tests exist so that every example in the document
// compiles and gives the result that the document states. Where the document writes a comment with the input as
// text, the test builds the same input with preferred().

namespace
{

template <cbor::fixed_string Path, class T>
concept doc_path_compiles = requires(std::string_view const s) { cbor::at_path<Path, T>(s); };

} // namespace

// The section "Read one value" reads a number, a double and a bool, and shows the error of a missing key.
TEST_CASE("doc path: read one value")
{
    std::string const bytes =
        preferred(M("user"s, M("id"s, 42, "score"s, 2.5, "admin"s, simple{21}, "name"s, "ann"s)));
    std::int64_t used = 0;
    auto const use = [&used](std::int64_t const v) { used = v; };
    auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
    if (id)
        use(*id);
    CHECK_EQ(used, 42);
    CHECK_EQ(cbor::at_path<"$.user.score", double>(bytes), 2.5);
    CHECK_EQ(cbor::at_path<"$.user.admin", bool>(bytes), true);
    CHECK_EQ(cbor::at_path<"$.user.age", std::int64_t>(bytes).error(), error::key_not_found);
    CHECK_EQ(cbor::at_path<"$.user.id", std::int64_t>(std::string_view{bytes}), 42);
}

// The section "Read one value" names the types of T, the one conversion, and the types that do not compile.
TEST_CASE("doc path: the types of a number")
{
    CHECK_EQ(cbor::at_path<"$", double>(preferred(V(1))), 1.0);
    CHECK_EQ(cbor::at_path<"$", std::int64_t>(preferred(V("a"s))).error(), error::incorrect_type);
    CHECK_EQ(cbor::at_path<"$", std::uint8_t>(preferred(V(256))).error(), error::number_out_of_range);
    CHECK_FALSE((doc_path_compiles<"$", float>));
    CHECK((doc_path_compiles<"$", std::string>));
    CHECK_FALSE((doc_path_compiles<"$.a[", int>));
}

// The section "Read one value" shows that std::string as T copies the text into a result that needs no owner.
TEST_CASE("doc path: std::string as T copies the text")
{
    std::string const bytes = preferred(M("user"s, M("name"s, "ann"s)));
    auto const name = cbor::at_path<"$.user.name", std::string>(bytes);
    CHECK_EQ(name, "ann"s);
    CHECK(std::is_same_v<decltype(name), std::expected<std::string, error> const>);
}

// The section "Read a text" shows that a text needs an owner, and that the view points into the bytes of that
// owner.
TEST_CASE("doc path: read a text")
{
    std::string bytes = preferred(M("user"s, M("name"s, "ann"s, "key"s, test::bytes{"\x01\x02"})));
    std::string_view used;
    auto const use = [&used](std::string_view const v) { used = v; };
    auto const owner = std::make_shared<std::string const>(std::move(bytes));
    auto const name = cbor::at_path<"$.user.name", std::string_view>(owner);
    if (name)
        use(**name);
    CHECK_EQ(used, "ann"sv);
    CHECK_GE(used.data(), owner->data());
    CHECK_LT(used.data(), owner->data() + owner->size());
    auto const key = cbor::at_path<"$.user.key", std::span<std::byte const>>(owner);
    REQUIRE(key.has_value());
    CHECK_EQ((*key)->size(), 2u);
    CHECK_FALSE((doc_path_compiles<"$.user.name", std::string_view>));
}

// The section "Start at a lazy" shows that $ is the value of the lazy and that a text needs no separate owner there.
TEST_CASE("doc path: start at a lazy")
{
    std::string bytes = preferred(M("cars"s, A(M("hp"s, 5), M("hp"s, 7, "make"s, "kia"s))));
    auto const cars = cbor::lazy::take_ownership(std::move(bytes)).and_then([](cbor::lazy const &doc) { return doc.at("cars"); });
    REQUIRE(cars.has_value());
    if (cars) {
        CHECK_EQ(cars->at_path<"$[1].hp", std::int64_t>(), 7);
        auto const make = cars->at_path<"$[1].make", std::string_view>();
        REQUIRE(make.has_value());
        CHECK_EQ(**make, "kia"sv);
    }
}

// The section "Path syntax" gives each row of its table. The paths that select many values go through query, as the
// section states.
TEST_CASE("doc path: path syntax")
{
    value const doc = M("store"s, M("book"s, A(M("title"s, "A"s, "price"s, 8), M("title"s, "B"s, "price"s, 12),
                                              M("title"s, "C"s, "price"s, 9, "isbn"s, "x"s)),
                                    "bicycle"s, M("price"s, 20)));
    std::string const bytes = preferred(doc);
    auto const l = *test::lazy_of_copy(bytes);
    CHECK(*test::query("$", l) == A(doc));
    CHECK_EQ(cbor::at_path<"$.store.bicycle.price", int>(bytes), 20);
    CHECK_EQ(cbor::at_path<"$['store']['bicycle']['price']", int>(bytes), 20);
    auto const owner = std::make_shared<std::string const>(bytes);
    CHECK(*cbor::at_path<"$.store.book[0].title", std::string_view>(owner) == "A"sv);
    CHECK(*cbor::at_path<"$.store.book[-1].title", std::string_view>(owner) == "C"sv);
    CHECK(*test::query("$.store.book[*].price", l) == A(8, 12, 9));
    CHECK(*test::query("$..price", l) == A(8, 12, 9, 20));
    CHECK(*test::query("$.store.book[?@.price < 10].title", l) == A("A"s, "C"s));
    CHECK(*test::query("$.store.book[0:2].title", l) == A("A"s, "B"s));
    CHECK_FALSE((doc_path_compiles<"$.store.book[*].price", int>));
    CHECK_FALSE((doc_path_compiles<"$..price", int>));
    CHECK(*test::query("$.store.book[?@.price != 8 && @.title >= 'B' || !@.price].title", l) == A("B"s, "C"s));
    CHECK(*test::query("$.store.book[?length(@.title) == 1 && count(@.*) == 3 && value(@.price) == 9].title",
                       l) == A("C"s));
    CHECK_EQ(cbor::at_path<"$[1]", std::int64_t>(preferred(M(1, 7))), 7);
}

// The section "Errors" tells a missing field from broken input with a switch on the error.
TEST_CASE("doc path: tell a missing field from broken input")
{
    auto const read = [](std::string const &bytes) {
        std::int64_t used = -1;
        std::optional<error> rejected;
        auto const use = [&used](std::int64_t const v) { used = v; };
        auto const reject = [&rejected](error const e) { rejected = e; };
        auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
        if (!id) {
            switch (id.error()) {
            case cbor::error::key_not_found:
                use(0);
                break;
            default:
                reject(id.error());
            }
        }
        return std::pair{used, rejected};
    };
    CHECK(read(preferred(M("user"s, M("name"s, "ann"s)))) == std::pair{std::int64_t{0}, std::optional<error>{}});
    std::string const whole = preferred(M("user"s, M("id"s, 42)));
    CHECK(read(whole.substr(0, 5)) == std::pair{std::int64_t{-1}, std::optional<error>{error::too_little_data}});
}

// The section "Errors" states that a path reads up to its value, that a count larger than the bytes left is
// too_little_data, and that item_size checks the whole value.
TEST_CASE("doc path: a path does not read past its value")
{
    std::string const cut = "\x82\x01"s;
    CHECK_EQ(cbor::at_path<"$[0]", int>(cut).error(), error::too_little_data);
    CHECK_EQ(cbor::at_path<"$[0]", int>("\x82\x01\xff"s), 1);
    CHECK_EQ(cbor::item_size(cut).error(), error::too_little_data);
    std::string const whole = preferred(A(1, 2));
    CHECK_EQ(cbor::item_size(whole), whole.size());
}

// The section "What is not supported" names match and search, which a user of other JSONPath libraries tries.
TEST_CASE("doc path: what is not supported")
{
    auto const l = *test::lazy_of_copy(preferred(A("abc"s)));
    CHECK_EQ(test::query("$[?match(@, 'a.c')]", l).error(), error::invalid_path);
    CHECK_EQ(test::query("$[?search(@, 'b')]", l).error(), error::invalid_path);
    CHECK_EQ(test::query("$[?foo(@)]", l).error(), error::invalid_path);
}

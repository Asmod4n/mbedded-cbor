#include <cbor/diagnostic_notation.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>
#include <cbor/result.hpp>
#include <cbor/schema.hpp>
#include <cbor/sequence.hpp>
#include <cbor/sequence_reader.hpp>
#include <cbor/structs.hpp>
#include <doctest/doctest.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using cbor::error;
using namespace std::literals;

namespace
{

// The document {"a": 7}, written by the encoder of the library.
cbor::lazy make_document()
{
    auto encoded = cbor::encode<std::map<std::string, std::int64_t>>({{"a", 7}});
    REQUIRE(encoded.has_value());
    auto const doc = cbor::lazy::adopt(std::move(*encoded));
    REQUIRE(doc.has_value());
    return *doc;
}

constexpr auto read_number = [](cbor::lazy const &l) { return l.get<std::int64_t>(); };

constexpr auto size_of_encoded = [](cbor::lazy const &l) { return l.encoded().size(); };

constexpr auto code_of = [](error const e) { return static_cast<int>(e); };

struct read_a {
    cbor::lazy doc;

    cbor::result<cbor::lazy> operator()(error const &) const
    {
        return doc.at("a");
    }
};

using lazy_result = cbor::result<cbor::lazy>;

using number_result = cbor::result<std::int64_t>;

template <class R>
concept deref_of_call = requires(R (*f)()) { *f(); };

template <class R>
concept arrow_of_call = requires(R (*f)()) { f()->size(); };

template <class R>
concept value_of_call = requires(R (*f)()) { f().value(); };

template <class R>
concept error_of_call = requires(R (*f)()) { f().error(); };

template <class R>
concept deref_of_moved = requires(R r) { *std::move(r); };

template <class R>
concept loop_over_call = requires(R (*f)()) {
    { *f() } -> std::same_as<typename R::value_type const &>;
};

template <class R, class F>
concept and_then_takes = requires(R r, F f) { std::forward<R>(r).and_then(f); };

template <class R, class F>
concept transform_takes = requires(R r, F f) { std::forward<R>(r).transform(f); };

template <class R, class F>
concept or_else_takes = requires(R r, F f) { std::forward<R>(r).or_else(f); };

template <class R, class F>
concept transform_error_takes = requires(R r, F f) { std::forward<R>(r).transform_error(f); };

template <class R>
concept deref_of_named = requires(R const &r) { *r; };

template <class R>
concept loop_over_named = requires(R const &r) {
    { *r } -> std::same_as<typename R::value_type const &>;
    r->size();
};

using tree_result = decltype(cbor::decode(std::declval<std::string_view>()));

using elements_result = decltype(std::declval<cbor::item const &>().elements());

constexpr auto elements_of_tree = [](cbor::owning<cbor::item> const &t) { return t->elements(); };

constexpr auto type_of_tree = [](cbor::owning<cbor::item> const &t) { return t->type(); };

constexpr auto decode_empty = [](error const &) { return cbor::decode(std::string_view{}); };

using encoded_result = decltype(cbor::encode<int>(0));

using written_result = decltype(cbor::encode(std::declval<std::string &>(), std::declval<cbor::item>()));

using struct_result = decltype(cbor::structs<int>::decode(std::string()));

constexpr auto size_of_text = [](std::string const &s) { return cbor::item_size(s); };

constexpr auto length_of_text = [](std::string const &s) { return s.size(); };

constexpr auto encode_zero = [](error const &) { return cbor::encode<int>(0); };

using edn_result = decltype(cbor::diag2cbor(std::declval<std::string_view>()));

using text_result = decltype(cbor::to_edn(std::declval<cbor::item>()));

using parsed_result = decltype(cbor::parse_edn(std::declval<std::string_view>()));

constexpr auto parse_text = [](std::string const &text) { return cbor::parse_edn(text); };

constexpr auto read_empty = [](error const &) { return cbor::diag2cbor(std::string_view{}); };

using nodes_result = decltype(std::declval<cbor::lazy const &>().at_path<"$.a", cbor::nodelist>());

using view_of_elements = decltype(std::declval<cbor::lazy const &>().elements());

using view_of_entries = decltype(std::declval<cbor::lazy const &>().entries());

using query_result = decltype(cbor::path::compile<cbor::nodelist>(std::declval<std::string_view>()));
using owner_text = decltype(cbor::at_path<"$.name", std::string_view>(std::declval<std::shared_ptr<std::string const> const &>()));
using lazy_text = decltype(std::declval<cbor::lazy const &>().at_path<"$.name", std::string_view>());
using owner_query_text = decltype(cbor::at_path(std::declval<std::shared_ptr<std::string const> const &>(),
                                                std::declval<cbor::path::query<std::string_view> const &>()));
using lazy_query_text = decltype(std::declval<cbor::lazy const &>().at_path(std::declval<cbor::path::query<std::string_view> const &>()));

using next_result = decltype(std::declval<cbor::sequence_reader &>().next());

constexpr auto compile_root = [](cbor::nodelist const &) { return cbor::path::compile<cbor::lazy>("$"); };

constexpr auto count_of_nodes = [](cbor::nodelist const &n) { return n.size(); };

constexpr auto nodes_of_document = [](error const &) { return make_document().at_path<"$.a", cbor::nodelist>(); };

struct route_point {
    std::int32_t x;
    std::int32_t y;
};

struct route_stop {
    std::string name;
    std::vector<route_point> points;
};

struct route {
    std::vector<route_stop> stops;
    std::vector<route_point> points;
    std::vector<double> track;
    std::variant<std::int64_t, std::string> label;
    std::uint16_t number;
};

route make_route()
{
    return route{{route_stop{"a", {{1, 2}}}, route_stop{"b", {{3, 4}, {5, 6}}}}, {{7, 8}, {9, 10}}, {1.0, 2.0, 3.0}, std::int64_t{12}, 5};
}

} // namespace

CBOR_DEFINE_TYPE_MEMBERS(route_point, cbor::tag{1811}, x, y);
CBOR_DEFINE_TYPE_MEMBERS(route_stop, cbor::tag{1812}, name, points);
CBOR_DEFINE_TYPE_MEMBERS(route, cbor::tag{1813}, stops, points, track, label, number);

namespace
{

using route_root = decltype(cbor::schema<route>::adopt(std::string()));

using route_number = decltype(cbor::schema<route>::at_path<"$.number">(std::string_view()));

using route_message = decltype(cbor::schema<route>::encode(std::declval<route const &>()));

using route_tree = decltype(std::declval<cbor::lazy const &>().to_item());

using adopted_lazy = decltype(cbor::lazy::adopt(std::string()));

using track_view = decltype(std::declval<route_root::value_type const &>().view<"$.track">());

using track_part = decltype(std::declval<track_view::value_type const &>().first(1));

constexpr auto points_of_root = [](route_root::value_type const &r) { return r.at_path<"$.points">(); };

} // namespace

// A read that finds its value gives a result that holds it. Each reader of a named result gives that value, on a
// const and on a non-const result.
TEST_CASE("result: the readers of a named result give the value")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    REQUIRE(a.has_value());
    CHECK(static_cast<bool>(a));
    auto const number = a->get<std::int64_t>();
    CHECK_EQ(*number, 7);
    CHECK_EQ(number.value(), 7);
    CHECK_EQ(number.value_or(0), 7);
    CHECK_EQ((*a).encoded(), a.value().encoded());
    auto changed = a->get<std::int64_t>();
    *changed = 8;
    CHECK_EQ(changed.value(), 8);
    changed.value() = 9;
    CHECK_EQ(*changed, 9);
    auto other = doc.at("a");
    CHECK_EQ(other->encoded(), a->encoded());
    CHECK_EQ(std::move(changed).value_or(0), 9);
}

// A read that fails gives a result that holds its error. The readers give that error, value() throws, and value_or
// gives the fallback.
TEST_CASE("result: the readers of a named result give the error")
{
    cbor::lazy const doc = make_document();
    auto const b = doc.at("b");
    CHECK_FALSE(b.has_value());
    CHECK_FALSE(static_cast<bool>(b));
    CHECK_EQ(b.error(), error::key_not_found);
    CHECK_THROWS_AS(static_cast<void>(b.value()), std::bad_expected_access<error>);
    auto const a = doc.at("a");
    auto const wrong = a->get<bool>();
    CHECK_EQ(wrong.error(), error::incorrect_type);
    CHECK_EQ(wrong.value_or(true), true);
    auto moved = a->get<bool>();
    CHECK_EQ(std::move(moved).value_or(true), true);
}

// has_value, the conversion to bool and value_or give no reference into the result, so they also work on the result
// of a call.
TEST_CASE("result: has_value, bool and value_or work on a temporary")
{
    cbor::lazy const doc = make_document();
    CHECK(doc.at("a").has_value());
    CHECK_FALSE(doc.at("b").has_value());
    CHECK(static_cast<bool>(doc.size()));
    CHECK_EQ(doc.size().value_or(0), 1u);
    CHECK_EQ(doc.type().value_or(cbor::type::array), cbor::type::map);
    CHECK_EQ(doc.empty().value_or(true), false);
}

// and_then gives the value to the next read, and gives the error of the first read without a call.
TEST_CASE("result: and_then on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a.and_then(read_number);
    CHECK_EQ(number, 7);
    auto const b = doc.at("b");
    bool called = false;
    auto const missing = b.and_then([&called](cbor::lazy const &l) {
        called = true;
        return l.get<std::int64_t>();
    });
    CHECK_FALSE(called);
    CHECK_EQ(missing.error(), error::key_not_found);
}

// transform gives a result that holds the value of the function, or the error of the read.
TEST_CASE("result: transform on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const size = a.transform(size_of_encoded);
    CHECK_EQ(size, std::size_t{1});
    auto const b = doc.at("b");
    auto const missing = b.transform(size_of_encoded);
    CHECK_EQ(missing.error(), error::key_not_found);
}

// or_else gives a copy of the value without a call, and gives the error to the function.
TEST_CASE("result: or_else on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    bool called = false;
    auto const kept = a.or_else([&called, &doc](error const &) {
        called = true;
        return doc.at("a");
    });
    CHECK_FALSE(called);
    CHECK_EQ(kept->encoded(), a->encoded());
    auto const b = doc.at("b");
    auto const fallback = b.or_else(read_a{doc});
    REQUIRE(fallback.has_value());
    CHECK_EQ(fallback->encoded(), a->encoded());
}

// transform_error gives a copy of the value, or a result that holds the value of the function for the error.
TEST_CASE("result: transform_error on a value and on an error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a->get<std::int64_t>();
    auto const kept = number.transform_error(code_of);
    CHECK_EQ(*kept, 7);
    auto const b = doc.at("b");
    auto const code = b.transform_error(code_of);
    CHECK_EQ(code.error(), static_cast<int>(error::key_not_found));
}

// == compares as two std::expected compare: two results, a result with a value, and a result with an error.
TEST_CASE("result: == compares the value or the error")
{
    cbor::lazy const doc = make_document();
    auto const a = doc.at("a");
    auto const number = a->get<std::int64_t>();
    auto const same = a->get<std::int64_t>();
    auto const wrong = same.transform([](std::int64_t const n) { return n + 1; });
    CHECK(number == same);
    CHECK_FALSE(number == wrong);
    CHECK(number == 7);
    CHECK_FALSE(number == 8);
    CHECK_FALSE(number == std::unexpected(error::key_not_found));
    auto const b = doc.at("b");
    auto const missing = b.and_then(read_number);
    auto const also_missing = b.and_then(read_number);
    CHECK(missing == also_missing);
    CHECK_FALSE(missing == number);
    CHECK(missing == std::unexpected(error::key_not_found));
    CHECK_FALSE(missing == std::unexpected(error::incorrect_type));
    CHECK_FALSE(missing == 7);
}

// A reference or a pointer into the result of a call ends with the full expression. So each reader that gives one,
// and each monadic form, does not compile on a temporary or a moved result: the program names the result first.
TEST_CASE("result: a reader of a temporary result does not compile")
{
    CHECK_FALSE(deref_of_call<lazy_result>);
    CHECK_FALSE(deref_of_call<number_result>);
    CHECK_FALSE(arrow_of_call<lazy_result>);
    CHECK_FALSE(value_of_call<lazy_result>);
    CHECK_FALSE(value_of_call<number_result>);
    CHECK_FALSE(error_of_call<lazy_result>);
    CHECK_FALSE(error_of_call<number_result>);
    CHECK_FALSE(deref_of_moved<lazy_result>);
    CHECK_FALSE(deref_of_moved<number_result>);
    CHECK_FALSE(loop_over_call<lazy_result>);
    CHECK_FALSE((std::is_constructible_v<std::expected<std::int64_t, error>, number_result>));
    CHECK_FALSE((std::is_convertible_v<number_result, std::expected<std::int64_t, error>>));
}

// The monadic forms read the value or the error in place, so they take a named result only. The same call on a named
// const result compiles.
TEST_CASE("result: a monadic form of a temporary result does not compile")
{
    using after = decltype(read_number);
    using measure = decltype(size_of_encoded);
    using code = decltype(code_of);
    CHECK((and_then_takes<lazy_result const &, after>));
    CHECK_FALSE((and_then_takes<lazy_result &&, after>));
    CHECK_FALSE((and_then_takes<lazy_result const &&, after>));
    CHECK((transform_takes<lazy_result const &, measure>));
    CHECK_FALSE((transform_takes<lazy_result &&, measure>));
    CHECK_FALSE((transform_takes<lazy_result const &&, measure>));
    CHECK((or_else_takes<lazy_result const &, read_a>));
    CHECK_FALSE((or_else_takes<lazy_result &&, read_a>));
    CHECK_FALSE((or_else_takes<lazy_result const &&, read_a>));
    CHECK((transform_error_takes<lazy_result const &, code>));
    CHECK_FALSE((transform_error_takes<lazy_result &&, code>));
    CHECK_FALSE((transform_error_takes<lazy_result const &&, code>));
}

// Only the library makes a result. A program copies and moves one that a read gave, and makes none of its own.
TEST_CASE("result: a program cannot make a result")
{
    CHECK_FALSE((std::is_constructible_v<number_result, std::int64_t>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::unexpected<error>>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::unexpect_t, error>));
    CHECK_FALSE((std::is_constructible_v<number_result, std::in_place_t, std::int64_t>));
    CHECK_FALSE(std::is_default_constructible_v<number_result>);
    CHECK_FALSE((std::is_constructible_v<lazy_result, cbor::lazy>));
    CHECK(std::is_copy_constructible_v<number_result>);
    CHECK(std::is_move_constructible_v<number_result>);
    CHECK(std::is_copy_assignable_v<number_result>);
    CHECK(std::is_move_assignable_v<number_result>);
}

// cbor::decode and cbor::item::elements give a result too, so the same rules hold for them: a reader or a monadic
// form of a temporary result does not compile, and the same call on a named result does. The range of the elements
// is the case that a loop reads.
TEST_CASE("result: a temporary result of cbor::decode or cbor::item::elements cannot be read")
{
    CHECK(deref_of_named<tree_result>);
    CHECK_FALSE(deref_of_call<tree_result>);
    CHECK_FALSE(value_of_call<tree_result>);
    CHECK_FALSE(error_of_call<tree_result>);
    CHECK_FALSE(deref_of_moved<tree_result>);
    CHECK(loop_over_named<elements_result>);
    CHECK_FALSE(loop_over_call<elements_result>);
    CHECK_FALSE(arrow_of_call<elements_result>);
    CHECK_FALSE(value_of_call<elements_result>);
    CHECK_FALSE(deref_of_moved<elements_result>);
    using after = decltype(elements_of_tree);
    using kind = decltype(type_of_tree);
    using again = decltype(decode_empty);
    using code = decltype(code_of);
    CHECK((and_then_takes<tree_result const &, after>));
    CHECK_FALSE((and_then_takes<tree_result &&, after>));
    CHECK((transform_takes<tree_result const &, kind>));
    CHECK_FALSE((transform_takes<tree_result &&, kind>));
    CHECK((or_else_takes<tree_result const &, again>));
    CHECK_FALSE((or_else_takes<tree_result &&, again>));
    CHECK((transform_error_takes<tree_result const &, code>));
    CHECK_FALSE((transform_error_takes<tree_result &&, code>));
    CHECK_FALSE((std::is_convertible_v<tree_result, std::expected<cbor::owning<cbor::item>, error>>));
    CHECK_FALSE((std::is_constructible_v<tree_result, cbor::owning<cbor::item>>));
    CHECK_FALSE((std::is_constructible_v<tree_result, std::unexpect_t, error>));
}

// A named result of each function of the group gives its value or its error.
TEST_CASE("result: cbor::decode, cbor::item_size and the reads of cbor::item give a result")
{
    auto encoded = cbor::encode<std::map<std::string, std::int64_t>>({{"a", 7}});
    REQUIRE(encoded.has_value());
    std::string_view const bytes = *encoded;
    auto const tree = cbor::decode(bytes);
    REQUIRE(tree.has_value());
    auto const pairs = (*tree)->pairs();
    REQUIRE(pairs.has_value());
    CHECK_EQ(pairs->size(), 1u);
    auto const value = pairs->front().second.get<std::int64_t>();
    CHECK_EQ(*value, 7);
    auto const elements = (*tree)->elements();
    CHECK_EQ(elements.error(), error::incorrect_type);
    auto const kind = tree.transform(type_of_tree);
    CHECK_EQ(*kind, cbor::type::map);
    auto const size = cbor::item_size(bytes);
    CHECK_EQ(*size, bytes.size());
    auto const cut = cbor::item_size(bytes.substr(0, 1));
    CHECK_EQ(cut.error(), error::too_little_data);
    auto const empty = cbor::decode_cycles(std::string_view{});
    auto const fallback = empty.or_else(decode_empty);
    CHECK_EQ(fallback.error(), error::too_little_data);
}

// cbor::encode, cbor::encode_iov and cbor::structs give a result too, so the same rules hold for them: a reader or a
// monadic form of a temporary result does not compile, and the same call on a named result does. The bytes of encode
// are the case that a loop reads.
TEST_CASE("result: a temporary result of cbor::encode or cbor::structs cannot be read")
{
    CHECK(deref_of_named<encoded_result>);
    CHECK(loop_over_named<encoded_result>);
    CHECK_FALSE(deref_of_call<encoded_result>);
    CHECK_FALSE(loop_over_call<encoded_result>);
    CHECK_FALSE(arrow_of_call<encoded_result>);
    CHECK_FALSE(value_of_call<encoded_result>);
    CHECK_FALSE(error_of_call<encoded_result>);
    CHECK_FALSE(deref_of_moved<encoded_result>);
    CHECK_FALSE(deref_of_call<written_result>);
    CHECK_FALSE(value_of_call<written_result>);
    CHECK_FALSE(error_of_call<written_result>);
    CHECK_FALSE(deref_of_call<struct_result>);
    CHECK_FALSE(value_of_call<struct_result>);
    CHECK_FALSE(error_of_call<struct_result>);
    CHECK_FALSE(deref_of_moved<struct_result>);
    using after = decltype(size_of_text);
    using length = decltype(length_of_text);
    using again = decltype(encode_zero);
    using code = decltype(code_of);
    CHECK((and_then_takes<encoded_result const &, after>));
    CHECK_FALSE((and_then_takes<encoded_result &&, after>));
    CHECK((transform_takes<encoded_result const &, length>));
    CHECK_FALSE((transform_takes<encoded_result &&, length>));
    CHECK((or_else_takes<encoded_result const &, again>));
    CHECK_FALSE((or_else_takes<encoded_result &&, again>));
    CHECK((transform_error_takes<encoded_result const &, code>));
    CHECK_FALSE((transform_error_takes<encoded_result &&, code>));
    CHECK_FALSE((std::is_convertible_v<encoded_result, std::expected<std::string, error>>));
    CHECK_FALSE((std::is_convertible_v<written_result, std::expected<std::size_t, error>>));
    CHECK_FALSE((std::is_convertible_v<struct_result, std::expected<cbor::ref<int>, error>>));
    CHECK_FALSE((std::is_constructible_v<encoded_result, std::string>));
    CHECK_FALSE((std::is_constructible_v<written_result, std::size_t>));
    CHECK_FALSE((std::is_constructible_v<struct_result, std::unexpect_t, error>));
}

// A named result of each function of the group gives its value or its error. Each overload of encode and
// encode_iov is called once here.
TEST_CASE("result: cbor::encode, cbor::encode_iov and cbor::structs give a result")
{
    auto encoded = cbor::encode<std::map<std::string, std::int64_t>>({{"a", 7}});
    REQUIRE(encoded.has_value());
    std::string out;
    auto const appended = cbor::encode<std::map<std::string, std::int64_t>>(out, {{"a", 7}});
    CHECK_EQ(*appended, encoded->size());
    CHECK_EQ(out, *encoded);
    auto const tree = cbor::decode(*encoded);
    REQUIRE(tree.has_value());
    std::string again;
    auto const written = cbor::encode(again, **tree);
    CHECK_EQ(*written, encoded->size());
    CHECK_EQ(again, *encoded);
    std::string room(encoded->size() - 1, '\0');
    auto const refused = cbor::encode(std::span<char>(room), **tree);
    CHECK_EQ(refused.error(), error::no_buffer_space);
#if __has_include(<sys/uio.h>)
    auto const copied = cbor::encode_iov(**tree);
    REQUIRE(copied.has_value());
    CHECK_FALSE((*copied)->empty());
    auto const referred = cbor::encode_iov(*tree);
    REQUIRE(referred.has_value());
    CHECK_FALSE((*referred)->empty());
#endif
    auto const back = cbor::structs<std::map<std::string, std::int64_t>>::decode(std::move(*encoded));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->at("a"), 7);
    auto const struct_bytes = cbor::structs<std::map<std::string, std::int64_t>>::encode(**back);
    REQUIRE(struct_bytes.has_value());
    std::string struct_out;
    auto const struct_count = cbor::structs<std::map<std::string, std::int64_t>>::encode(struct_out, **back);
    CHECK_EQ(*struct_count, struct_bytes->size());
    auto const cut = cbor::structs<std::map<std::string, std::int64_t>>::decode(struct_bytes->substr(0, 1));
    CHECK_EQ(cut.error(), error::too_little_data);
    auto const length = struct_bytes.transform(length_of_text);
    CHECK_EQ(*length, struct_out.size());
}

// cbor::diag2cbor, cbor::diagnostic_notation, cbor::to_edn and cbor::parse_edn give a result too, so the same rules
// hold for them: a reader or a monadic form of a temporary result does not compile, and the same call on a named
// result does. The bytes of diag2cbor are the case that a loop reads.
TEST_CASE("result: a temporary result of cbor::diag2cbor, cbor::to_edn or cbor::parse_edn cannot be read")
{
    CHECK(deref_of_named<edn_result>);
    CHECK(loop_over_named<edn_result>);
    CHECK_FALSE(deref_of_call<edn_result>);
    CHECK_FALSE(loop_over_call<edn_result>);
    CHECK_FALSE(arrow_of_call<edn_result>);
    CHECK_FALSE(value_of_call<edn_result>);
    CHECK_FALSE(error_of_call<edn_result>);
    CHECK_FALSE(deref_of_moved<edn_result>);
    CHECK_FALSE(deref_of_call<text_result>);
    CHECK_FALSE(value_of_call<text_result>);
    CHECK_FALSE(error_of_call<text_result>);
    CHECK_FALSE(deref_of_call<parsed_result>);
    CHECK_FALSE(value_of_call<parsed_result>);
    CHECK_FALSE(error_of_call<parsed_result>);
    CHECK_FALSE(deref_of_moved<parsed_result>);
    using parse = decltype(parse_text);
    using length = decltype(length_of_text);
    using again = decltype(read_empty);
    using code = decltype(code_of);
    CHECK((and_then_takes<edn_result const &, parse>));
    CHECK_FALSE((and_then_takes<edn_result &&, parse>));
    CHECK((transform_takes<edn_result const &, length>));
    CHECK_FALSE((transform_takes<edn_result &&, length>));
    CHECK((or_else_takes<edn_result const &, again>));
    CHECK_FALSE((or_else_takes<edn_result &&, again>));
    CHECK((transform_error_takes<edn_result const &, code>));
    CHECK_FALSE((transform_error_takes<edn_result &&, code>));
    CHECK_FALSE((std::is_convertible_v<edn_result, std::expected<std::string, error>>));
    CHECK_FALSE((std::is_convertible_v<parsed_result, std::expected<cbor::owning<cbor::item>, error>>));
    CHECK_FALSE((std::is_constructible_v<edn_result, std::string>));
    CHECK_FALSE((std::is_constructible_v<text_result, std::unexpect_t, error>));
}

// A named result of each function of the group gives its value or its error. Each overload of diag2cbor,
// diagnostic_notation, to_edn and parse_edn is called once here.
TEST_CASE("result: cbor::diag2cbor, cbor::diagnostic_notation, cbor::to_edn and cbor::parse_edn give a result")
{
    auto const bytes = cbor::diag2cbor("[1, 2_1]");
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->size(), 5u);
    auto const text = cbor::diagnostic_notation(std::string_view(*bytes));
    CHECK_EQ(*text, "[1, 2_1]");
    auto const of_range = cbor::diagnostic_notation(std::span<char const>(*bytes));
    CHECK_EQ(of_range, *text);
    auto const tree = cbor::parse_edn(*text);
    REQUIRE(tree.has_value());
    auto const of_root = cbor::to_edn(**tree);
    CHECK_EQ(*of_root, "[1, 2_1]");
    auto const of_tree = cbor::to_edn(*tree);
    CHECK_EQ(of_tree, *of_root);
    auto const of_value = cbor::to_edn<std::map<std::string, std::int64_t>>({{"a", 7}});
    CHECK_EQ(*of_value, R"({"a": 7})");
    auto const parsed = text.and_then(parse_text);
    REQUIRE(parsed.has_value());
    auto const refused = cbor::diag2cbor("[1,");
    CHECK_EQ(refused.error(), error::syntax_error);
    auto const cut = cbor::diagnostic_notation(std::string_view("\x82\x01"));
    CHECK_EQ(cut.error(), error::too_little_data);
    auto const unread = cbor::parse_edn("[1,");
    CHECK_EQ(unread.error(), error::syntax_error);
}

// cbor::diag2cbor is constexpr, and a program calls it at compile time to make a constant. So its result is made,
// copied and read in a constant expression.
TEST_CASE("result: cbor::diag2cbor gives its result in a constant expression")
{
    constexpr std::size_t size = [] {
        auto const encoded = cbor::diag2cbor("[1, 2_1]");
        auto const copied = encoded;
        return copied.has_value() ? copied->size() : 0;
    }();
    CHECK_EQ(size, 5u);
    constexpr bool refused = [] {
        auto const encoded = cbor::diag2cbor("[1,");
        return encoded.error() == error::syntax_error;
    }();
    CHECK(refused);
}

// cbor::at_path, cbor::path::compile, cbor::lazy::at_path, cbor::lazy::elements, cbor::lazy::entries, the elements of
// a cbor::sequence and cbor::sequence_reader::next give a result. The view of elements is the case that a loop over a
// temporary would leave dangling, so neither a reader nor a monadic form of a temporary result compiles, and the same
// call on a named result does.
// A reference to the ref inside a temporary result of at_path ends with its statement, and no compiler warns, so the
// forms auto const &r = *at_path(...) and at_path(...).value() must not compile, for the owner form, the member form
// and both forms with a compiled query.
TEST_CASE("result: a temporary result of at_path, elements, compile or next cannot be read")
{
    CHECK(deref_of_named<nodes_result>);
    CHECK(loop_over_named<nodes_result>);
    CHECK_FALSE(deref_of_call<nodes_result>);
    CHECK_FALSE(loop_over_call<nodes_result>);
    CHECK_FALSE(arrow_of_call<nodes_result>);
    CHECK_FALSE(value_of_call<nodes_result>);
    CHECK_FALSE(error_of_call<nodes_result>);
    CHECK_FALSE(deref_of_moved<nodes_result>);
    CHECK(loop_over_named<view_of_elements>);
    CHECK_FALSE(deref_of_call<view_of_elements>);
    CHECK_FALSE(loop_over_call<view_of_elements>);
    CHECK_FALSE(arrow_of_call<view_of_elements>);
    CHECK_FALSE(value_of_call<view_of_elements>);
    CHECK(loop_over_named<view_of_entries>);
    CHECK_FALSE(loop_over_call<view_of_entries>);
    CHECK_FALSE(arrow_of_call<view_of_entries>);
    CHECK_FALSE(deref_of_call<query_result>);
    CHECK_FALSE(value_of_call<query_result>);
    CHECK_FALSE(error_of_call<query_result>);
    CHECK_FALSE(deref_of_call<next_result>);
    CHECK_FALSE(error_of_call<next_result>);
    CHECK_FALSE(deref_of_call<cbor::sequence::iterator::value_type>);
    CHECK_FALSE(arrow_of_call<cbor::sequence::iterator::value_type>);
    CHECK_FALSE(deref_of_call<cbor::lazy_elements::iterator::value_type>);
    CHECK_FALSE(deref_of_call<cbor::lazy_entries::iterator::value_type>);
    CHECK(deref_of_named<owner_text>);
    CHECK_FALSE(deref_of_call<owner_text>);
    CHECK_FALSE(value_of_call<owner_text>);
    CHECK(deref_of_named<lazy_text>);
    CHECK_FALSE(deref_of_call<lazy_text>);
    CHECK(deref_of_named<owner_query_text>);
    CHECK_FALSE(deref_of_call<owner_query_text>);
    CHECK_FALSE(value_of_call<owner_query_text>);
    CHECK(deref_of_named<lazy_query_text>);
    CHECK_FALSE(deref_of_call<lazy_query_text>);
    using compile = decltype(compile_root);
    using count = decltype(count_of_nodes);
    using again = decltype(nodes_of_document);
    using code = decltype(code_of);
    CHECK((and_then_takes<nodes_result const &, compile>));
    CHECK_FALSE((and_then_takes<nodes_result &&, compile>));
    CHECK((transform_takes<nodes_result const &, count>));
    CHECK_FALSE((transform_takes<nodes_result &&, count>));
    CHECK((or_else_takes<nodes_result const &, again>));
    CHECK_FALSE((or_else_takes<nodes_result &&, again>));
    CHECK((transform_error_takes<nodes_result const &, code>));
    CHECK_FALSE((transform_error_takes<nodes_result &&, code>));
    CHECK_FALSE((std::is_convertible_v<nodes_result, std::expected<cbor::nodelist, error>>));
    CHECK_FALSE((std::is_convertible_v<next_result, std::expected<cbor::lazy, error>>));
    CHECK_FALSE((std::is_constructible_v<nodes_result, cbor::nodelist>));
    CHECK_FALSE((std::is_constructible_v<next_result, std::unexpect_t, error>));
    CHECK_FALSE((std::is_constructible_v<view_of_elements, std::in_place_t>));
    CHECK((std::is_same_v<cbor::lazy_elements::iterator::value_type, cbor::result<cbor::lazy>>));
    CHECK((std::is_same_v<cbor::lazy_entries::iterator::value_type, cbor::result<std::pair<cbor::lazy, cbor::lazy>>>));
    CHECK((std::is_same_v<cbor::sequence::iterator::value_type, cbor::result<std::string_view>>));
    CHECK(std::ranges::forward_range<cbor::lazy_elements>);
    CHECK(std::ranges::forward_range<cbor::lazy_entries>);
    CHECK(std::ranges::input_range<cbor::sequence>);
}

// A named result of each function of the group gives its value or its error. Each friend overload of at_path is
// called here: the scalar form over a std::string_view, the view form with an owner and a range, and the copied form.
// The forms that return the result of another public form are called too, and so are the forms with a query.
TEST_CASE("result: cbor::at_path and cbor::path::compile give a result")
{
    auto encoded = cbor::encode<std::map<std::string, std::string>>({{"t", "x"}});
    REQUIRE(encoded.has_value());
    auto const owner = std::make_shared<std::string const>(*encoded);
    std::string_view const bytes(*owner);
    auto const number = cbor::encode<std::map<std::string, std::int64_t>>({{"a", 7}});
    REQUIRE(number.has_value());
    std::string_view const number_bytes(*number);

    auto const scalar = cbor::at_path<"$.a", std::int64_t>(number_bytes);
    CHECK_EQ(*scalar, 7);
    auto const of_range = cbor::at_path<"$.a", std::int64_t>(std::span<char const>(*number));
    CHECK_EQ(of_range, 7);
    auto const view = cbor::at_path<"$.t", std::string_view>(owner, bytes);
    REQUIRE(view.has_value());
    CHECK_EQ(**view, "x");
    auto const of_owner = cbor::at_path<"$.t", std::string_view>(owner);
    REQUIRE(of_owner.has_value());
    CHECK_EQ(**of_owner, "x");
    auto const moved_in = cbor::at_path<"$.t", std::string_view>(std::string(*owner));
    REQUIRE(moved_in.has_value());
    CHECK_EQ(**moved_in, "x");
    auto const copied = cbor::at_path<"$.t", std::string>(bytes);
    CHECK_EQ(*copied, "x");
    auto const missing = cbor::at_path<"$.b", std::int64_t>(number_bytes);
    CHECK_EQ(missing.error(), error::key_not_found);

    auto const a = cbor::path::compile<std::int64_t>("$.a");
    REQUIRE(a.has_value());
    auto const t = cbor::path::compile<std::string_view>("$.t");
    REQUIRE(t.has_value());
    auto const node = cbor::path::compile<cbor::lazy>("$.t");
    REQUIRE(node.has_value());
    auto const all = cbor::path::compile<cbor::nodelist>("$.*");
    REQUIRE(all.has_value());
    auto const broken = cbor::path::compile<cbor::lazy>("$.a[");
    CHECK_EQ(broken.error(), error::invalid_path);
    auto const many = cbor::path::compile<std::int64_t>("$.*");
    CHECK_EQ(many.error(), error::not_singular_query);
    auto const by_query = cbor::at_path(number_bytes, *a);
    CHECK_EQ(*by_query, 7);
    auto const by_query_of_range = cbor::at_path(std::span<char const>(*number), *a);
    CHECK_EQ(by_query_of_range, 7);
    auto const view_by_query = cbor::at_path(owner, bytes, *t);
    REQUIRE(view_by_query.has_value());
    CHECK_EQ(**view_by_query, "x");
    auto const node_by_query = cbor::at_path(owner, bytes, *node);
    REQUIRE(node_by_query.has_value());
    auto const text = node_by_query->get<std::string_view>();
    CHECK_EQ(**text, "x");
    auto const nodes_by_query = cbor::at_path(owner, bytes, *all);
    REQUIRE(nodes_by_query.has_value());
    CHECK_EQ(nodes_by_query->size(), 1u);
    auto const of_owner_by_query = cbor::at_path(owner, *t);
    REQUIRE(of_owner_by_query.has_value());
    CHECK_EQ(**of_owner_by_query, "x");
    auto const moved_by_query = cbor::at_path(std::string(*owner), *all);
    CHECK_EQ(moved_by_query->size(), 1u);
    auto const t_as_number = cbor::path::compile<std::int64_t>("$.t");
    auto const wrong_type = cbor::at_path(bytes, *t_as_number);
    CHECK_EQ(wrong_type.error(), error::incorrect_type);
}

// cbor::lazy::at_path, elements and entries give a result, and so does each element of their views. Each form of
// lazy::at_path is called here.
TEST_CASE("result: cbor::lazy::at_path, elements and entries give a result")
{
    auto const doc = make_document();
    auto const node = doc.at_path<"$.a">();
    CHECK_EQ(node->get<std::int64_t>(), 7);
    auto const scalar = doc.at_path<"$.a", std::int64_t>();
    CHECK_EQ(*scalar, 7);
    auto const nodes = doc.at_path<"$.*", cbor::nodelist>();
    CHECK_EQ(nodes->size(), 1u);
    auto const missing = doc.at_path<"$.b", std::int64_t>();
    CHECK_EQ(missing.error(), error::key_not_found);
    auto const query = cbor::path::compile<std::int64_t>("$.a");
    auto const by_query = doc.at_path(*query);
    CHECK_EQ(*by_query, 7);
    auto encoded = cbor::encode<std::map<std::string, std::string>>({{"t", "x"}});
    REQUIRE(encoded.has_value());
    auto const text_doc = cbor::lazy::adopt(std::move(*encoded));
    REQUIRE(text_doc.has_value());
    auto const text = text_doc->at_path<"$.t", std::string_view>();
    CHECK_EQ(**text, "x");

    auto const entries = doc.entries();
    REQUIRE(entries.has_value());
    CHECK_EQ(entries->size(), 1u);
    auto const pair = *entries->begin();
    REQUIRE(pair.has_value());
    CHECK_EQ(pair->second.get<std::int64_t>(), 7);
    auto const not_array = doc.elements();
    CHECK_EQ(not_array.error(), error::not_indexable);
    auto array = cbor::encode<std::vector<std::int64_t>>({1, 2});
    REQUIRE(array.has_value());
    auto const list = cbor::lazy::adopt(std::move(*array));
    REQUIRE(list.has_value());
    auto const elements = list->elements();
    REQUIRE(elements.has_value());
    std::int64_t sum = 0;
    for (auto const &element : *elements)
        sum += element->get<std::int64_t>().value_or(0);
    CHECK_EQ(sum, 3);
    auto const not_map = list->entries();
    CHECK_EQ(not_map.error(), error::not_indexable);
}

// A cbor::sequence gives each item as a result, and cbor::sequence_reader::next gives each item, or the error, as a
// result.
TEST_CASE("result: cbor::sequence and cbor::sequence_reader::next give a result")
{
    auto const owner = std::make_shared<std::string const>("\x01\x02\x19"s);
    cbor::sequence const s(owner);
    auto it = s.begin();
    auto const first = *it;
    CHECK_EQ(*first, "\x01"sv);
    ++it;
    ++it;
    auto const cut = *it;
    CHECK_EQ(cut.error(), error::too_little_data);

    cbor::sequence_reader r;
    auto const waiting = r.next();
    CHECK_EQ(waiting.error(), error::too_little_data);
    r.feed(std::string("\x01\x02"));
    auto const one = r.next();
    REQUIRE(one.has_value());
    CHECK_EQ(one->get<std::int64_t>(), 1);
    auto const two = r.next();
    CHECK_EQ(two->get<std::int64_t>(), 2);
    CHECK(r.empty());
}

// A temporary result of the group cannot be read: the opening of a message, a read of one value, a write, a view of
// a typed array and its parts, the tree of a lazy and the opening of a lazy.
TEST_CASE("result: a temporary result of schema, static_dictionary, typed_array_view or lazy::adopt cannot be read")
{
    CHECK(deref_of_named<route_root>);
    CHECK_FALSE(deref_of_call<route_root>);
    CHECK_FALSE(value_of_call<route_root>);
    CHECK_FALSE(error_of_call<route_root>);
    CHECK_FALSE(deref_of_moved<route_root>);
    CHECK_FALSE(deref_of_call<route_number>);
    CHECK_FALSE(value_of_call<route_message>);
    CHECK_FALSE(arrow_of_call<route_message>);
    CHECK_FALSE(arrow_of_call<track_view>);
    CHECK_FALSE(loop_over_call<track_view>);
    CHECK(loop_over_named<track_view>);
    CHECK_FALSE(arrow_of_call<track_part>);
    CHECK_FALSE(deref_of_call<route_tree>);
    CHECK_FALSE(arrow_of_call<adopted_lazy>);
    CHECK_FALSE(error_of_call<adopted_lazy>);
    using points = decltype(points_of_root);
    CHECK((transform_takes<route_root const &, points>));
    CHECK_FALSE((transform_takes<route_root &&, points>));
    CHECK_FALSE((std::is_convertible_v<route_root, std::expected<route_root::value_type, error>>));
    CHECK_FALSE((std::is_constructible_v<route_message, std::string>));
    CHECK_FALSE((std::is_constructible_v<track_part, std::unexpect_t, error>));
}

// Each public member of cbor::schema, cbor::static_dictionary and their accessors that gives a result is called here,
// for a value and for an error. These classes and cbor::typed_array_view are the friends of the group.
TEST_CASE("result: cbor::schema and its accessors give a result")
{
    using form = cbor::schema<route>;
    auto encoded = form::encode(make_route());
    REQUIRE(encoded.has_value());
    std::string const bytes = *encoded;
    std::string appended;
    auto const written = form::encode(appended, make_route());
    CHECK_EQ(written, bytes.size());
    auto const owner = std::make_shared<std::string const>(bytes);

    auto const adopted = form::adopt(std::move(*encoded));
    REQUIRE(adopted.has_value());
    auto const viewed = form::view(owner);
    REQUIRE(viewed.has_value());
    auto const borrowed = form::view(std::shared_ptr<void const>(owner), std::string_view(*owner));
    REQUIRE(borrowed.has_value());
    auto const cut = form::adopt(std::string(bytes, 0, 4));
    CHECK_EQ(cut.error(), error::too_little_data);
    auto const number = form::at_path<"$.number">(bytes);
    CHECK_EQ(*number, 5u);
    auto const decoded = form::decode(std::string(bytes));
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->number, 5u);
    CHECK(form::decode(owner).has_value());
    CHECK(form::decode(owner, route{}).has_value());
    CHECK(form::decode(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    CHECK(form::decode(std::shared_ptr<void const>(owner), std::string_view(*owner), route{}).has_value());
    auto const loose = form::unverified_adopt(std::string(bytes));
    REQUIRE(loose.has_value());
    CHECK(form::unverified_view(owner).has_value());
    CHECK(form::unverified_view(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    auto const loose_number = form::unverified_at_path<"$.number">(bytes);
    CHECK_EQ(*loose_number, 5u);
    CHECK(form::unverified_decode(std::string(bytes)).has_value());
    CHECK(form::unverified_decode(owner).has_value());
    CHECK(form::unverified_decode(owner, route{}).has_value());
    CHECK(form::unverified_decode(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    CHECK(form::unverified_decode(std::shared_ptr<void const>(owner), std::string_view(*owner), route{}).has_value());

    auto const points = adopted->at_path<"$.points">();
    REQUIRE(points.has_value());
    auto const second = points->at_path<"@[]">(1u);
    REQUIRE(second.has_value());
    auto const x = second->at_path<"@.x">();
    CHECK_EQ(*x, 9);
    auto const outside = points->at_path<"@[]">(2u);
    CHECK_EQ(outside.error(), error::index_out_of_bounds);
    auto const label = adopted->at_path<"$.label">();
    REQUIRE(label.has_value());
    auto const twelve = label->get<std::int64_t>();
    CHECK_EQ(*twelve, 12);
    auto const text = label->get<std::string>();
    CHECK_EQ(text.error(), error::incorrect_type);
    auto const stops = adopted->at_path<"$.stops">();
    REQUIRE(stops.has_value());
    std::vector<std::string_view> names;
    std::vector<std::size_t> counts;
    stops->for_each([&](auto const &stop) {
        auto const name = stop.template at_path<"@.name">();
        names.push_back(*name);
        auto const stop_points = stop.template at_path<"@.points">();
        counts.push_back(stop_points->size());
    });
    CHECK_EQ(names, (std::vector<std::string_view>{"a", "b"}));
    CHECK_EQ(counts, (std::vector<std::size_t>{1, 2}));

    auto const track = adopted->view<"$.track">();
    REQUIRE(track.has_value());
    auto const head = track->first(2);
    CHECK_EQ(head->size(), 2u);
    auto const tail = track->last(1);
    CHECK_EQ(tail->front(), 3.0);
    auto const too_many = track->first(4);
    CHECK_EQ(too_many.error(), error::index_out_of_bounds);
    auto const element = track->at(1);
    CHECK_EQ(*element, 2.0);
    auto const past = track->at(3);
    CHECK_EQ(past.error(), error::index_out_of_bounds);
    auto taken = track->at(0);
    CHECK_EQ(taken, 1.0);
    auto moved = *track;
    auto const moved_head = std::move(moved).first(1);
    CHECK_EQ(moved_head->size(), 1u);
    auto moved_again = *track;
    auto const moved_tail = std::move(moved_again).last(2);
    CHECK_EQ(moved_tail->size(), 2u);
}

// Each public member of cbor::static_dictionary gives a result, as the form of cbor::schema with the same name does.
TEST_CASE("result: cbor::static_dictionary gives a result")
{
    using form = cbor::static_dictionary<route>;
    auto encoded = form::encode(make_route());
    REQUIRE(encoded.has_value());
    std::string const bytes = *encoded;
    std::string appended;
    auto const written = form::encode(appended, make_route());
    CHECK_EQ(written, bytes.size());
    auto const owner = std::make_shared<std::string const>(bytes);
    auto const adopted = form::adopt(std::move(*encoded));
    REQUIRE(adopted.has_value());
    auto const number = adopted->at_path<"$.number">();
    CHECK_EQ(*number, 5u);
    CHECK(form::view(owner).has_value());
    CHECK(form::view(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    auto const read = form::at_path<"$.number">(bytes);
    CHECK_EQ(*read, 5u);
    CHECK(form::decode(std::string(bytes)).has_value());
    CHECK(form::decode(owner).has_value());
    CHECK(form::decode(owner, route{}).has_value());
    CHECK(form::decode(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    CHECK(form::decode(std::shared_ptr<void const>(owner), std::string_view(*owner), route{}).has_value());
    CHECK(form::unverified_adopt(std::string(bytes)).has_value());
    CHECK(form::unverified_view(owner).has_value());
    CHECK(form::unverified_view(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    auto const loose = form::unverified_at_path<"$.number">(bytes);
    CHECK_EQ(*loose, 5u);
    CHECK(form::unverified_decode(std::string(bytes)).has_value());
    CHECK(form::unverified_decode(owner).has_value());
    CHECK(form::unverified_decode(owner, route{}).has_value());
    CHECK(form::unverified_decode(std::shared_ptr<void const>(owner), std::string_view(*owner)).has_value());
    CHECK(form::unverified_decode(std::shared_ptr<void const>(owner), std::string_view(*owner), route{}).has_value());
    auto const with_description = cbor::schema<route>::encode(make_route());
    REQUIRE(with_description.has_value());
    auto const refused = form::adopt(std::string(*with_description));
    CHECK_EQ(refused.error(), error::incorrect_type);
}

// cbor::lazy::view, cbor::lazy::adopt and cbor::lazy::to_item give a result.
TEST_CASE("result: cbor::lazy::view, adopt and to_item give a result")
{
    auto encoded = cbor::encode<std::vector<std::string>>({"a", "b"});
    REQUIRE(encoded.has_value());
    auto const owner = std::make_shared<std::string const>(*encoded);
    auto const viewed = cbor::lazy::view(owner);
    REQUIRE(viewed.has_value());
    auto const borrowed = cbor::lazy::view(std::shared_ptr<void const>(owner), std::string_view(*owner));
    REQUIRE(borrowed.has_value());
    auto const adopted = cbor::lazy::adopt(std::move(*encoded));
    REQUIRE(adopted.has_value());
    auto const tree = adopted->to_item();
    REQUIRE(tree.has_value());
    CHECK_EQ((*tree)->type(), cbor::type::array);
    auto const cut = cbor::lazy::adopt(std::string(*owner, 0, owner->size() - 1));
    REQUIRE(cut.has_value());
    auto const broken = cut->to_item();
    CHECK_EQ(broken.error(), error::too_little_data);
}

// operator* and operator-> of a result that holds an error read no value: the wrong use throws std::logic_error.
TEST_CASE("result: operator* and operator-> of a result that holds an error throw std::logic_error")
{
    auto const failed = cbor::decode(std::string_view{});
    REQUIRE_FALSE(failed.has_value());
    CHECK_THROWS_AS(static_cast<void>(*failed), std::logic_error);
    CHECK_THROWS_AS(static_cast<void>(failed.operator->()), std::logic_error);
}

#include "types.hpp"

namespace schema_types
{








template <cbor::fixed_string Path>
auto view_of(std::string const &bytes)
{
    auto const root = form<numbers>::path(std::string(bytes));
    REQUIRE(root.has_value());
    return root->view<Path>();
}

template <class E>
std::uint64_t bits_of(E const value)
{
    if constexpr (std::is_floating_point_v<E>)
        return std::bit_cast<std::conditional_t<sizeof(E) == 2, std::uint16_t, std::conditional_t<sizeof(E) == 4, std::uint32_t, std::uint64_t>>>(value);
    else
        return static_cast<std::uint64_t>(value);
}

template <class E>
std::vector<std::uint64_t> bits_of_all(std::vector<E> const &values)
{
    std::vector<std::uint64_t> out;
    for (E const x : values)
        out.push_back(bits_of(x));
    return out;
}

} // namespace schema_types

// view checks the tag and the length of a typed array once and then reads an element with one bounds check. The test
// reads every element type of RFC 8746 that the schema writes. The expected bits come from RFC 8746 figure 6 and IEEE
// 754: 1.0 is 3c00 in binary16, 3f800000 in binary32 and 3ff0000000000000 in binary64; -2 is all ones but the lowest
// bit in two's complement. at(i) with an index equal to the size is index_out_of_bounds, an error value and no read,
// as std::vector::at gives std::out_of_range; operator[] with that index is a wrong use and throws std::logic_error.
TEST_CASE("view: every typed array gives its elements back, and an index past the end is an error value")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto check = [&]<cbor::fixed_string Path>(std::uint64_t const expected) {
        auto const v = view_of<Path>(bytes);
        REQUIRE(v.has_value());
        REQUIRE_EQ(v->size(), 1u);
        auto const x = v->at(0);
        REQUIRE(x.has_value());
        CHECK_EQ(bits_of(*x), expected);
        CHECK_EQ(bits_of((*v)[0]), expected);
        CHECK_EQ(v->at(1).error(), error::index_out_of_bounds);
        CHECK_EQ(v->at(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
        CHECK_THROWS_AS((void)(*v)[1], std::logic_error);
        CHECK_THROWS_AS((void)(*v)[std::numeric_limits<std::size_t>::max()], std::logic_error);
    };
    check.operator()<"$.u16">(0x0102);
    check.operator()<"$.u32">(0x01020304);
    check.operator()<"$.u64">(0x0102030405060708);
    check.operator()<"$.s8">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s16">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s32">(static_cast<std::uint64_t>(std::int64_t{-2}));
    check.operator()<"$.s64">(static_cast<std::uint64_t>(std::int64_t{-2}));
#if defined(__STDCPP_FLOAT16_T__)
    check.operator()<"$.f16">(0x3c00);
#endif
    check.operator()<"$.f32">(0x3f800000);
    check.operator()<"$.f64">(0x3ff0000000000000);
}

// A view of an empty typed array has size 0, and index 0 is already out of bounds.
TEST_CASE("view: an empty typed array has no element")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(v->size(), 0u);
    CHECK_EQ(v->at(0).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS((void)(*v)[0], std::logic_error);
}

// view makes no view when the tag is not the one of the element type or the length is not a multiple of the element
// size. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746). The errors are the same as at_path gives.
TEST_CASE("view: a typed array with a wrong tag or a wrong length gives no view")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    auto refused = [](std::string const &wrong) {
        auto const root = form<doubles>::path(std::string(wrong));
        REQUIRE(root.has_value());
        auto const v = root->view<"$.v">();
        REQUIRE_FALSE(v.has_value());
        return v.error();
    };
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(refused(wrong), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(refused(odd), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(refused(longer), error::too_little_data);
}

// The iterator of a view yields each element by value. It is a random access iterator by the concepts of the
// standard library, so that std::ranges algorithms and range-for take the view as they take a std::span. The view is
// a sized random access range. It is not a borrowed range: its iterators read bytes that the view keeps alive.
TEST_CASE("view: the iterator and the view model the standard concepts")
{
    using V = cbor::typed_array_view<double>;
    using I = V::iterator;
    CHECK((std::random_access_iterator<I>));
    CHECK((std::sized_sentinel_for<I, I>));
    CHECK((std::same_as<std::iter_value_t<I>, double>));
    CHECK((std::same_as<std::iter_reference_t<I>, double>));
    CHECK((std::ranges::random_access_range<V const>));
    CHECK((std::ranges::sized_range<V const>));
    CHECK((std::ranges::common_range<V const>));
    CHECK((std::random_access_iterator<std::ranges::iterator_t<std::ranges::reverse_view<std::ranges::ref_view<V const>>>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().rbegin()), std::reverse_iterator<I>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().cbegin()), I>));
    CHECK((std::same_as<decltype(std::declval<V const &>()[0]), double>));
    CHECK((std::same_as<decltype(std::declval<V const &>().at(0)), std::expected<double, cbor::error>>));
    CHECK((std::same_as<decltype(std::declval<V const &>().front()), double>));
    CHECK((!std::ranges::borrowed_range<V>));
    CHECK((!std::ranges::contiguous_range<V>));
    CHECK((!std::output_iterator<I, double>));
}

// Each element type of RFC 8746 that the schema writes is read three ways: range-for, the iterator with index and
// arithmetic, and std::ranges algorithms. The expected bits are those of the values that were encoded. The order of
// the values makes the maximum the middle element and the minimum the last one for the signed types.
TEST_CASE("view: range-for, the iterator and std::ranges algorithms read every element type")
{
    std::string const bytes = schema_bytes(three_numbers);
    auto check = [&]<cbor::fixed_string Path, class E>(std::vector<E> const &source) {
        auto const v = view_of<Path>(bytes);
        REQUIRE(v.has_value());
        cbor::typed_array_view<E> const &view = *v;
        REQUIRE_EQ(view.size(), 3u);
        CHECK_FALSE(view.empty());
        std::vector<E> seen;
        for (E const x : view)
            seen.push_back(x);
        CHECK_EQ(bits_of_all(seen), bits_of_all(source));
        auto const b = view.begin();
        CHECK_EQ(view.end() - b, 3);
        CHECK_EQ(b - view.end(), -3);
        CHECK_EQ(bits_of(b[2]), bits_of(source[2]));
        CHECK_EQ(bits_of(*(b + 1)), bits_of(source[1]));
        CHECK_EQ(bits_of(*(1 + b)), bits_of(source[1]));
        CHECK_EQ(bits_of(*(view.end() - 1)), bits_of(source[2]));
        auto i = b;
        CHECK_EQ(bits_of(*i++), bits_of(source[0]));
        CHECK_EQ(bits_of(*i), bits_of(source[1]));
        CHECK_EQ(bits_of(*--i), bits_of(source[0]));
        i += 2;
        CHECK_EQ(bits_of(*i), bits_of(source[2]));
        i -= 1;
        CHECK_EQ(bits_of(*i--), bits_of(source[1]));
        CHECK(i == b);
        CHECK(b < view.end());
        CHECK(view.end() > b);
        CHECK(b <= b);
        CHECK_EQ(bits_of(std::ranges::fold_left(view, E{}, std::plus<>{})),
                 bits_of(std::ranges::fold_left(source, E{}, std::plus<>{})));
        CHECK_EQ(bits_of(std::ranges::max(view)), bits_of(std::ranges::max(source)));
        CHECK_EQ(std::ranges::find(view, source[1]) - view.begin(), 1);
        CHECK(std::ranges::find(view, E{0}) == view.end());
        CHECK_EQ(std::ranges::distance(view), 3);
    };
    check.operator()<"$.u16">(three_numbers.u16);
    check.operator()<"$.u32">(three_numbers.u32);
    check.operator()<"$.u64">(three_numbers.u64);
    check.operator()<"$.s8">(three_numbers.s8);
    check.operator()<"$.s16">(three_numbers.s16);
    check.operator()<"$.s32">(three_numbers.s32);
    check.operator()<"$.s64">(three_numbers.s64);
#if defined(__STDCPP_FLOAT16_T__)
    check.operator()<"$.f16">(three_numbers.f16);
#endif
    check.operator()<"$.f32">(three_numbers.f32);
    check.operator()<"$.f64">(three_numbers.f64);
}

// front and back read the first and the last element with one load each, and rbegin and rend walk the view
// backwards, as std::span does. first(n) and last(n) give a sub-view of n elements. n equal to the size gives the
// whole view; n one past it is index_out_of_bounds as an error value, where std::span would have undefined behaviour.
TEST_CASE("view: front, back, first and last")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(v->front()), 0x3ff0000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(v->back()), 0x4010000000000000u);
    std::vector<std::uint64_t> backwards;
    for (auto i = v->rbegin(); i != v->rend(); ++i)
        backwards.push_back(std::bit_cast<std::uint64_t>(*i));
    CHECK(backwards == std::vector<std::uint64_t>{0x4010000000000000u, 0x4000000000000000u, 0x3ff0000000000000u});
    CHECK(v->crbegin() == v->rbegin());
    CHECK(v->cbegin() == v->begin());
    CHECK(v->cend() == v->end());
    auto const head = v->first(2);
    REQUIRE(head.has_value());
    REQUIRE_EQ(head->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(head->back()), 0x4000000000000000u);
    CHECK_EQ(head->at(2).error(), error::index_out_of_bounds);
    auto const tail = v->last(2);
    REQUIRE(tail.has_value());
    REQUIRE_EQ(tail->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(tail->front()), 0x4000000000000000u);
    CHECK_EQ(v->first(3)->size(), 3u);
    CHECK_EQ(v->last(3)->size(), 3u);
    CHECK_EQ(v->first(0)->size(), 0u);
    CHECK_EQ(v->last(0)->size(), 0u);
    CHECK_EQ(v->first(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(4).error(), error::index_out_of_bounds);
    CHECK_EQ(v->first(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(std::numeric_limits<std::size_t>::max()).error(), error::index_out_of_bounds);
}

// The requirements of a reversible container in the C++ standard ([container.rev.reqmts]) say: a.rend() is
// reverse_iterator(a.begin()), a.crend() is const_cast<X const&>(a).rend(), and the distance from crbegin to crend is
// the size. The test checks each requirement on a view of three elements and on an empty view, and walks from crend
// back to crbegin, so that the last element read is the first element of the view.
TEST_CASE("view: crend meets the requirements of a reversible container")
{
    using V = cbor::typed_array_view<double>;
    CHECK((std::same_as<decltype(std::declval<V const &>().crend()), std::reverse_iterator<V::iterator>>));
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK(v->crend() == std::as_const(*v).rend());
    CHECK(v->crend() == std::reverse_iterator<V::iterator>(v->begin()));
    CHECK(v->crend().base() == v->begin());
    CHECK_EQ(std::distance(v->crbegin(), v->crend()), 3);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*std::ranges::prev(v->crend())), 0x3ff0000000000000u);
    std::vector<std::uint64_t> forwards;
    for (auto i = v->crend(); i != v->crbegin();)
        forwards.push_back(std::bit_cast<std::uint64_t>(*--i));
    CHECK(forwards == std::vector<std::uint64_t>{0x3ff0000000000000u, 0x4000000000000000u, 0x4010000000000000u});
    auto const empty_root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(empty_root.has_value());
    auto const e = empty_root->view<"$.v">();
    REQUIRE(e.has_value());
    CHECK(e->crend() == e->crbegin());
    CHECK_EQ(std::distance(e->crbegin(), e->crend()), 0);
}

// An empty view has no front and no back: each is a wrong use and throws std::logic_error, as the precondition of
// std::span::front is. There is no element to iterate. first(0) and last(0)
// are empty views, and first(1) is out of bounds.
TEST_CASE("view: an empty view has no front, no back and no element")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK(v->empty());
    CHECK_THROWS_AS((void)v->front(), std::logic_error);
    CHECK_THROWS_AS((void)v->back(), std::logic_error);
    CHECK(v->rbegin() == v->rend());
    CHECK(v->begin() == v->end());
    CHECK_EQ(std::ranges::fold_left(*v, 0.0, std::plus<>{}), 0.0);
    CHECK(v->first(0)->empty());
    CHECK(v->last(0)->empty());
    CHECK_EQ(v->first(1).error(), error::index_out_of_bounds);
    CHECK_EQ(v->last(1).error(), error::index_out_of_bounds);
}

#include "schema_types.hpp"

// path checks the least size of the message and the table of keys once, as decode does. The test exists so that a
// change to either side shows here.
TEST_CASE("path: a message shorter than the least size is too_little_data, a foreign key is incorrect_type")
{
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    CHECK_EQ(at_path<login, "$.id">(bytes), 5u);
    CHECK_EQ(at_path<login, "$.id">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(form<login>::decode(std::string(other)).error(), error::incorrect_type);
    CHECK_EQ(at_path<login, "$.id">(other).error(), error::incorrect_type);
}

// A path over fixed fields becomes one offset at compile time. A text is copied into a std::string, so it cannot
// outlive the bytes it came from.
TEST_CASE("at_path: fixed fields give the value")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.balance">(bytes), -7);
    CHECK_EQ(at_path<vehicle, "$.motor.cc">(bytes), 1800u);
    CHECK_EQ(at_path<vehicle, "$.motor.horsepower">(bytes), 300u);
    CHECK_EQ(at_path<vehicle, "$.spare[1].diameter">(bytes), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[0].airPressure">(bytes), 2.5f);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 1uz), 16u);
    CHECK_EQ(at_path<vehicle, "$.spare[].diameter">(bytes, 2uz).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.code">(bytes), "ABCD"s);
}

// A step over a part of variable size reads an offset and a length from the wire. An empty pair of brackets takes
// its index from the arguments, in the order of the path.
TEST_CASE("at_path: parts of variable size")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    CHECK_EQ(at_path<vehicle, "$.make">(bytes), "Tesla"s);
    CHECK_EQ(at_path<vehicle, "$.wheels[2].diameter">(bytes), 19u);
    CHECK_EQ(at_path<vehicle, "$.wheels[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(at_path<vehicle, "$.wheels[3].diameter">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<vehicle, "$.wheels[].diameter">(bytes, 3uz).error(), error::index_out_of_bounds);
    CHECK_EQ(*at_path<vehicle, "$.owner">(bytes), std::optional<std::uint8_t>{9});
    CHECK_FALSE(at_path<vehicle, "$.none">(bytes)->has_value());
}

// A directory offset from the wire that points at bytes of another kind, or a length past the start of the next
// item, is refused and reads nothing. The directory of vehicle starts behind the prefix and its head 5a.
TEST_CASE("at_path: a broken offset is an error")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const head = bytes.find("\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string backward = bytes;
    backward.replace(head + 5, 4, "\x00\x00\x00\x01"s);
    CHECK_FALSE(at_path<vehicle, "$.make">(backward).has_value());
    std::size_t const make = bytes.find("\x7a\x00\x00\x00\x05Tesla"s);
    REQUIRE_NE(make, std::string::npos);
    std::string past = bytes;
    past.replace(make + 1, 4, "\x00\x00\xff\xff"s);
    CHECK_FALSE(at_path<vehicle, "$.make">(past).has_value());
    CHECK_FALSE(form<vehicle>::decode(std::string(backward)).has_value());
    CHECK_FALSE(form<vehicle>::decode(std::string(past)).has_value());
}

// A fixed-length array of bytes is a byte string laid out inline (RFC 8949 3.1), the same as a fixed text.
// The reader gives the bytes themselves, not an offset and a length read out of them.
TEST_CASE("at_path: a fixed byte array is read inline")
{
    badge const b{{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}, shade::light, "x"};
    std::string const bytes = schema_bytes(b);
    CHECK_EQ(at_path<badge, "$.mac">(bytes), "\x01\x02\x03\x04"s);
    CHECK_EQ(at_path<badge, "$.tone">(bytes), shade::light);
    std::string zeros = bytes;
    std::size_t const mac = bytes.size() - form<badge>::fixed_size() + form<badge>::member_offset<&badge::mac>();
    zeros.replace(mac + 1, 4, "\x00\x00\x00\x00"s);
    CHECK_EQ(at_path<badge, "$.mac">(zeros), "\x00\x00\x00\x00"s);
}

// decode and path read every element type back from its typed array. The floats are compared by their bits.
TEST_CASE("decode and at_path: every typed array gives its elements back")
{
    std::string const bytes = schema_bytes(sample_numbers);
    auto const back = form<numbers>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    numbers const &n = **back;
    CHECK_EQ(n.u16, sample_numbers.u16);
    CHECK_EQ(n.u32, sample_numbers.u32);
    CHECK_EQ(n.u64, sample_numbers.u64);
    CHECK_EQ(n.s8, sample_numbers.s8);
    CHECK_EQ(n.s16, sample_numbers.s16);
    CHECK_EQ(n.s32, sample_numbers.s32);
    CHECK_EQ(n.s64, sample_numbers.s64);
#if defined(__STDCPP_FLOAT16_T__)
    REQUIRE_EQ(n.f16.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(n.f16[0]), 0x3c00u);
    CHECK_EQ(std::bit_cast<std::uint16_t>(*at_path<numbers, "$.f16[0]">(bytes)), 0x3c00u);
#endif
    REQUIRE_EQ(n.f32.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint32_t>(n.f32[0]), 0x3f800000u);
    REQUIRE_EQ(n.f64.size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(n.f64[0]), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.u16[0]">(bytes), 0x0102u);
    CHECK_EQ(at_path<numbers, "$.u32[]">(bytes, 0uz), 0x01020304u);
    CHECK_EQ(at_path<numbers, "$.u64[0]">(bytes), 0x0102030405060708u);
    CHECK_EQ(at_path<numbers, "$.s8[0]">(bytes), std::int8_t{-2});
    CHECK_EQ(at_path<numbers, "$.s16[0]">(bytes), std::int16_t{-2});
    CHECK_EQ(at_path<numbers, "$.s32[0]">(bytes), -2);
    CHECK_EQ(at_path<numbers, "$.s64[]">(bytes, 0uz), std::int64_t{-2});
    CHECK_EQ(std::bit_cast<std::uint32_t>(*at_path<numbers, "$.f32[0]">(bytes)), 0x3f800000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*at_path<numbers, "$.f64[0]">(bytes)), 0x3ff0000000000000u);
    CHECK_EQ(at_path<numbers, "$.f64[1]">(bytes).error(), error::index_out_of_bounds);
    auto const root = form<numbers>::path(std::string(bytes));
    REQUIRE(root.has_value());
    auto const list = root->at_path<"$.u32">();
    REQUIRE(list.has_value());
    CHECK_EQ(list->size(), 1u);
    CHECK_EQ(list->at_path<"@[0]">(), 0x01020304u);
    CHECK_EQ(list->at_path<"@[]">(1uz).error(), error::index_out_of_bounds);
}

// A list of doubles that does not carry tag 86, or whose byte string is not a multiple of eight bytes, is refused
// as an error value by decode and by path. Tag 82 is binary64 in big endian and tag 85 is binary32 (RFC 8746).
TEST_CASE("decode and at_path: a typed array with a wrong tag or a wrong length is refused")
{
    std::string const bytes = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const at = bytes.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(at, std::string::npos);
    REQUIRE(form<doubles>::decode(std::string(bytes)).has_value());
    for (char const tag : {'\x52', '\x55'}) {
        std::string wrong = bytes;
        wrong[at + 1] = tag;
        CHECK_EQ(form<doubles>::decode(std::string(wrong)).error(), error::incorrect_type);
        CHECK_EQ(at_path<doubles, "$.v[0]">(wrong).error(), error::incorrect_type);
    }
    std::string odd = bytes;
    odd[at + 6] = '\x0f';
    CHECK_EQ(form<doubles>::decode(std::string(odd)).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(at_path<doubles, "$.v[0]">(odd).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[at + 6] = '\x18';
    CHECK_EQ(form<doubles>::decode(std::string(longer)).error(), error::too_little_data);
    CHECK_EQ(at_path<doubles, "$.v[0]">(longer).error(), error::too_little_data);
}

namespace
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

} // namespace

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

// The view holds the owner of the bytes. The test reads it after the accessor and the string it came from are gone,
// so that the sanitizer sees a read of freed memory if the view did not keep the bytes alive.
TEST_CASE("view: the view keeps the bytes alive after the accessor is gone")
{
    std::optional<cbor::typed_array_view<double>> kept;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        kept.emplace(std::move(*v));
    }
    REQUIRE_EQ(kept->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*kept)[1]), 0x4000000000000000u);
}

namespace
{

numbers const three_numbers{{1, 2, 0xfffe},
                            {1, 2, 0xfffffffe},
                            {1, 2, 0xfffffffffffffffe},
                            {-1, 2, -3},
                            {-1, 2, -3},
                            {-1, 2, -3},
                            {-1, 2, -3},
#if defined(__STDCPP_FLOAT16_T__)
                            {-1.0f16, 2.0f16, -4.0f16},
#endif
                            {-1.0f, 2.0f, -4.0f},
                            {-1.0, 2.0, -4.0}};

template <class E>
std::vector<std::uint64_t> bits_of_all(std::vector<E> const &values)
{
    std::vector<std::uint64_t> out;
    for (E const x : values)
        out.push_back(bits_of(x));
    return out;
}

} // namespace

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

// A sub-view holds the owner as the view does. The test drops the string, the accessor and the parent view, then
// reads the sub-views, so that the sanitizer sees a read of freed memory if a sub-view borrowed from its parent.
// The sub-view of an rvalue view takes the owner over, and the test reads it after the same drops. A view made from
// an rvalue accessor takes the owner over too.
TEST_CASE("view: a view and a sub-view keep the bytes alive after the parent is gone")
{
    std::optional<cbor::typed_array_view<double>> copied;
    std::optional<cbor::typed_array_view<double>> moved;
    std::optional<cbor::typed_array_view<double>> taken;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        auto c = v->last(2);
        REQUIRE(c.has_value());
        copied.emplace(std::move(*c));
        auto m = std::move(*v).first(1);
        REQUIRE(m.has_value());
        moved.emplace(std::move(*m));
        auto r = form<doubles>::path(schema_bytes(doubles{{8.0}}));
        REQUIRE(r.has_value());
        auto t = std::move(*r).view<"$.v">();
        REQUIRE(t.has_value());
        taken.emplace(std::move(*t));
    }
    REQUIRE_EQ(copied->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(copied->back()), 0x4010000000000000u);
    CHECK_EQ(std::ranges::fold_left(*copied, 0.0, std::plus<>{}), 6.0);
    REQUIRE_EQ(moved->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(moved->front()), 0x3ff0000000000000u);
    REQUIRE_EQ(taken->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(taken->front()), 0x4020000000000000u);
}

// A move gives the owner of the bytes to the new object. The moved-from view or accessor must not read the bytes
// after the new owner is gone, because they may be freed. The test drops every other owner and then uses each
// moved-from object, so that the sanitizer sees a read of freed memory if a moved-from object kept its view. A
// moved-from view is empty: operator[], front and back are a wrong use, at is index_out_of_bounds. A use of a
// moved-from accessor is a wrong use. Each case is std::logic_error, as for any other wrong use of the library.
TEST_CASE("view: a moved-from view and a moved-from accessor read no bytes")
{
    using V = cbor::typed_array_view<double>;
    std::optional<V> view;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        view.emplace(std::move(*v));
    }
    {
        V const taken = std::move(*view);
        CHECK_EQ(taken.size(), 2u);
    }
    CHECK(view->empty());
    CHECK_EQ(view->size(), 0u);
    CHECK(view->begin() == view->end());
    CHECK_THROWS_AS((void)(*view)[0], std::logic_error);
    CHECK_THROWS_AS((void)view->front(), std::logic_error);
    CHECK_EQ(view->at(0).error(), error::index_out_of_bounds);

    std::optional<V> sliced;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        sliced.emplace(std::move(*v));
    }
    {
        auto const head = std::move(*sliced).first(1);
        REQUIRE(head.has_value());
        CHECK_EQ(head->size(), 1u);
    }
    CHECK(sliced->empty());
    CHECK_THROWS_AS((void)(*sliced)[0], std::logic_error);

    V assigned = [] {
        auto const root = form<doubles>::path(schema_bytes(doubles{{4.0}}));
        return *root->view<"$.v">();
    }();
    {
        V target = [] {
            auto const root = form<doubles>::path(schema_bytes(doubles{{8.0}}));
            return *root->view<"$.v">();
        }();
        target = std::move(assigned);
        CHECK_EQ(std::bit_cast<std::uint64_t>(target.front()), 0x4010000000000000u);
    }
    CHECK(assigned.empty());
    CHECK_THROWS_AS((void)assigned.back(), std::logic_error);

    auto root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(root.has_value());
    {
        auto const taken = std::move(*root);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"$.v[0]">()), 0x3ff0000000000000u);
    }
    CHECK_THROWS_AS((void)root->at_path<"$.v[0]">(), std::logic_error);
    CHECK_THROWS_AS((void)root->view<"$.v">(), std::logic_error);

    auto list_root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(list_root.has_value());
    auto list = list_root->at_path<"$.v">();
    REQUIRE(list.has_value());
    list_root = std::unexpected(error::incorrect_type);
    {
        auto const taken = std::move(*list);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"@[1]">()), 0x4000000000000000u);
    }
    CHECK_THROWS_AS((void)list->at_path<"@[1]">(), std::logic_error);
}

// RFC 8949 3.1 major type 4: a list is an array. Each element has a fixed size, so element i lies at i times
// that size behind the data, and a path reads it without walking the ones before. A list of lists takes one
// index for each level.
TEST_CASE("at_path: a list of structs, strings and lists, by index")
{
    std::string const bytes = schema_bytes(sample_garage);
    std::uint32_t sum = 0;
    for (std::size_t i = 0;; ++i) {
        auto const d = at_path<garage, "$.tires[].diameter">(bytes, i);
        if (!d) {
            CHECK_EQ(d.error(), error::index_out_of_bounds);
            CHECK_EQ(i, 3u);
            break;
        }
        sum += *d;
    }
    CHECK_EQ(sum, 17u + 18u + 19u);
    CHECK_EQ(at_path<garage, "$.tires[1].airPressure">(bytes), 3.0f);
    CHECK_EQ(at_path<garage, "$.names[1]">(bytes), "bc"s);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 2uz, 0uz), 3u);
    CHECK_EQ(at_path<garage, "$.rows[][]">(bytes, 0uz, 1uz), 2u);
    CHECK_EQ(at_path<garage, "$.rows[1][0]">(bytes).error(), error::index_out_of_bounds);
}

namespace
{

template <class T, cbor::fixed_string Path, class I>
concept path_reads_at = requires(typename form<T>::template accessor<> const &a, I const i) { a.template at_path<Path>(i); };

} // namespace

// A path that ends at a leaf gives the value. A path that ends at a struct, a list, a fixed array or a map gives an
// accessor. A name that is not a member, an index into a text or a map, an index past a fixed array, and a count of
// indexes that does not match the empty brackets do not compile. A path starts at the root of the top-level item with $
// (RFC 9535 2.2) or at the node of the accessor with @ (RFC 9535 2.3.5); on the root accessor both are the same
// node.
TEST_CASE("path: a path compiles where it names a part of the type")
{
    CHECK(path_reads<garage, "$.names[0]">);
    CHECK(path_reads<garage, "$.tires">);
    CHECK(path_reads<garage, "$.tires[0]">);
    CHECK(path_reads<garage, "$.owners">);
    CHECK(path_reads<garage, "$.rows[0]">);
    CHECK(path_reads<garage, "$">);
    CHECK(path_reads<vehicle, "$.motor">);
    CHECK(path_reads<vehicle, "$.spare">);
    CHECK(path_reads_at<garage, "$.tires[].diameter", std::size_t>);
    CHECK(path_reads_at<garage, "$.tires[]", std::size_t>);
    CHECK_FALSE(path_reads<garage, "$.owners[7]">);
    CHECK_FALSE(path_reads<garage, "$.names[0][0]">);
    CHECK_FALSE(path_reads<garage, "$.nothing">);
    CHECK_FALSE(path_reads<garage, "$.tires[1">);
    CHECK_FALSE(path_reads<garage, "$.tires[].diameter">);
    CHECK_FALSE(path_reads_at<garage, "$.tires[0].diameter", std::size_t>);
    CHECK_FALSE(path_reads<vehicle, "$.spare[2].diameter">);
    CHECK_FALSE(path_reads<vehicle, "$.code[0]">);
    CHECK_FALSE(path_reads<vehicle, "$.make[0]">);
    CHECK_FALSE(path_reads<garage, ".tires">);
    CHECK_FALSE(path_reads<garage, "tires">);
    CHECK(path_reads<garage, "@.tires">);
    using A = form<garage>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.names[0]">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires[]">(0uz)), std::expected<form<garage>::accessor<cbor::fixed_element<tire>>, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires">()),
                       std::expected<form<garage>::accessor<std::vector<tire>>, cbor::error>>);
}

// An accessor of a list gives its size and its elements; an accessor of a struct gives its fields. A leaf through
// several steps gives the same value as the same steps through named accessors.
TEST_CASE("path: an accessor reads relative to the part it names")
{
    std::string const bytes = schema_bytes(sample_garage);
    auto const opened = form<garage>::path(std::string(bytes));
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const tires = root.at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    auto const second = tires->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.diameter">(), 18u);
    CHECK_EQ(second->at_path<"@.airPressure">(), root.at_path<"$.tires[].airPressure">(1uz));
    CHECK_EQ(tires->at_path<"@[]">(3uz).error(), error::index_out_of_bounds);
    auto const rows = root.at_path<"$.rows">();
    REQUIRE(rows.has_value());
    CHECK_EQ(rows->size(), 3u);
    auto const first = rows->at_path<"@[0]">();
    REQUIRE(first.has_value());
    CHECK_EQ(first->size(), 2u);
    CHECK_EQ(first->at_path<"@[1]">(), 2u);
    auto const owners = root.at_path<"$.owners">();
    REQUIRE(owners.has_value());
    CHECK_EQ(owners->size(), 2u);
    auto const names = root.at_path<"$.names">();
    REQUIRE(names.has_value());
    CHECK_EQ(names->at_path<"@[]">(1uz), "bc"sv);
}

// The copying form of path holds its own copy of the message. The message here is a temporary that is destroyed at
// the end of the full expression. The test exists because a view into those bytes would read freed memory, and the
// address sanitizer reports that read.
TEST_CASE("path: the copying form outlives the message of the caller")
{
    auto const opened = form<ticket>::path(ticket_bytes());
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    CHECK_EQ(root.at_path<"$.holder">(), sample_ticket.holder);
    CHECK_EQ(root.at_path<"$.seats[]">(1uz), sample_ticket.seats.at(1));
}

// The owner form keeps the owner in the root accessor. The caller releases its own pointer to the owner before it
// reads. The test exists because the root alone must keep the bytes alive.
TEST_CASE("path: the root keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    auto const opened = form<ticket>::path(std::move(owner));
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const holder = root.at_path<"$.holder">();
    auto const seat = root.at_path<"$.seats[1]">();
    REQUIRE(holder.has_value());
    REQUIRE(seat.has_value());
    CHECK_EQ(*holder, sample_ticket.holder);
    CHECK_EQ(*seat, sample_ticket.seats.at(1));
}

namespace
{

std::string text_of(std::expected<std::string_view, cbor::error> const &r)
{
    return r ? std::string(*r) : "(error)"s;
}

// One read of every field of car i, through leaf paths from the root and through an accessor of the car and of each
// wheel, compared with the decoded car.
void lot_car_matches(form<parking_lot>::accessor<> const &lot, parking_lot const &back, std::size_t const i)
{
    CAPTURE(i);
    lot_car const &c = back.cars.at(i);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].make">(i)), c.make);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].model">(i)), c.model);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(i), c.seats);
    CHECK_EQ(lot.at_path<"$.cars[].engine.cc">(i), c.engine.cc);
    CHECK_EQ(lot.at_path<"$.cars[].engine.horsepower">(i), c.engine.horsepower);
    auto const note = *lot.at_path<"$.cars[].note">(i);
    CHECK_EQ(note.has_value(), c.note.has_value());
    if (note && c.note)
        CHECK_EQ(*note, *c.note);
    auto const opened = lot.at_path<"$.cars[]">(i);
    REQUIRE(opened.has_value());
    auto const &car = *opened;
    CHECK_EQ(text_of(car.at_path<"@.make">()), c.make);
    CHECK_EQ(car.at_path<"@.engine.cc">(), c.engine.cc);
    CHECK_EQ(car.at_path<"@.wheels">()->size(), c.wheels.size());
    CHECK_EQ(text_of(lot.at_path<"$.cars[].code">(i)), std::string_view(c.code, 2));
    for (std::size_t k = c.wheels.size() + 1; k-- > 0;) {
        CAPTURE(k);
        if (k == c.wheels.size()) {
            CHECK_EQ(lot.at_path<"$.cars[].wheels[].diameter">(i, k).error(), error::index_out_of_bounds);
            continue;
        }
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].diameter">(i, k), c.wheels[k].diameter);
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].airPressure">(i, k), c.wheels[k].airPressure);
        CHECK_EQ(lot.at_path<"$.cars[].wheels[].snowTires">(i, k), c.wheels[k].snowTires);
        auto const wheel = car.at_path<"@.wheels[]">(k);
        REQUIRE(wheel.has_value());
        CHECK_EQ(wheel->at_path<"@.airPressure">(), c.wheels[k].airPressure);
        CHECK_EQ(car.at_path<"@.wheels[].diameter">(k), c.wheels[k].diameter);
    }
}

template <class D, cbor::fixed_string Path>
concept lot_reads = requires(D &d) { d.template at_path<Path>(0uz); };

template <class D, cbor::fixed_string Path>
concept temporary_lot_reads = requires(D &&d) { std::move(d).template at_path<Path>(0uz); };

template <class D>
concept temporary_car_reads = requires(D const &d) { (*d.template at_path<"$.cars[]">(0uz)).template at_path<"@.make">(); };

} // namespace

// An accessor reads each field of a list of several cars as decode reads it, in order, in the reverse order and in
// a shuffled order. The test exists because a read that goes back must not take a value of another car.
TEST_CASE("path: an accessor gives the values of decode in every order")
{
    std::string const bytes = schema_bytes(sample_lot());
    auto const decoded = *form<parking_lot>::decode(std::string(bytes));
    parking_lot const &back = *decoded;
    auto lot = *form<parking_lot>::path(std::string(bytes));
    std::vector<std::size_t> order(back.cars.size());
    std::iota(order.begin(), order.end(), 0uz);
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    for (std::size_t const i : order | std::views::reverse)
        lot_car_matches(lot, back, i);
    std::ranges::shuffle(order, std::mt19937(5));
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(back.cars.size()).error(), error::index_out_of_bounds);
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(2uz)), "third"sv);
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(0uz)), "first"sv);
    CHECK_EQ(lot.at_path<"$.rows[][]">(2uz, 0uz), 3u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(0uz, 1uz), 2u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(1uz, 0uz).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<parking_lot, "$.cars[].wheels[].diameter">(bytes, 3uz, 0uz), back.cars[3].wheels[0].diameter);
    CHECK_EQ(at_path<parking_lot, "$.cars[].model">(bytes, 3uz), "mmm"s);
}

// An accessor is read through an lvalue. The test exists because a text and a nested accessor borrow the bytes of
// the root, and a read through a temporary root would be the only use of a root that ends at once.
TEST_CASE("path: an accessor is not read through a temporary")
{
    using D = form<parking_lot>::accessor<>;
    CHECK(lot_reads<D, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[]">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[].seats">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[]">);
    CHECK_FALSE(temporary_car_reads<D>);
    CHECK_THROWS_AS((void)form<parking_lot>::path(std::shared_ptr<std::string const>{}), std::logic_error);
}

// The copying form keeps its copy: a text and a car accessor stay valid after the caller frees the message, while the
// root lives.
TEST_CASE("path: the copying form keeps its copy after the caller frees the message")
{
    auto bytes = std::make_unique<std::string>(schema_bytes(sample_lot()));
    auto lot = *form<parking_lot>::path(std::string(*bytes));
    auto const make = *lot.at_path<"$.cars[].make">(3uz);
    auto const car = *lot.at_path<"$.cars[]">(4uz);
    bytes.reset();
    CHECK_EQ(make, "make3"sv);
    CHECK_EQ(car.at_path<"@.make">(), "make4"sv);
    CHECK_EQ(car.at_path<"$.cars[].make">(4uz), "make4"sv);
}

// A path reads one leaf of one element at an offset the compiler knows, after the vector was checked once.
TEST_CASE("at_path: a leaf of one element of a fixed-size vector")
{
    std::string const bytes = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}});
    CHECK_EQ(at_path<gauges, "$.g[0].on">(bytes), true);
    CHECK_EQ(at_path<gauges, "$.g[1].on">(bytes), false);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(bytes), color::white);
    CHECK_EQ(at_path<gauges, "$.g[0].at.x">(bytes), 3);
    CHECK_EQ(at_path<gauges, "$.g[1].at.y">(bytes), 5);
    CHECK_EQ(at_path<gauges, "$.g[0].range[1]">(bytes), 0x0203u);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 1uz, 0uz), 7u);
    CHECK_EQ(at_path<gauges, "$.g[2].on">(bytes).error(), error::index_out_of_bounds);
    CHECK_EQ(at_path<gauges, "$.g[].range[]">(bytes, 0uz, 2uz).error(), error::index_out_of_bounds);
    std::string const points = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    CHECK_EQ(at_path<cloud, "$.points[1].y">(points), -1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(points), 1.0f);
    CHECK_EQ(at_path<cloud, "$.points[0].x">(schema_bytes(cloud{})).error(), error::index_out_of_bounds);
}

// An accessor of the vector gives its size; an accessor of one element or of its inner struct reads the leaves
// relative to it.
TEST_CASE("at_path: an accessor of a fixed-size vector and of one element")
{
    auto const owner = std::make_shared<std::string const>(
        schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK_EQ(g->size(), 2u);
    CHECK_EQ(g->at_path<"@[1].at.x">(), -6);
    CHECK_EQ(g->at_path<"@[]">(2uz).error(), error::index_out_of_bounds);
    auto const second = g->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK(std::same_as<std::remove_cvref_t<decltype(*second)>, form<gauges>::accessor<cbor::fixed_element<gauge>>>);
    CHECK_EQ(second->at_path<"@.range[0]">(), 7u);
    CHECK_EQ(second->at_path<"@.shade">(), color::black);
    auto const inner = root->at_path<"$.g[0].at">();
    REQUIRE(inner.has_value());
    CHECK_EQ(inner->at_path<"@.y">(), -2);
}

// The accessor of a fixed-size vector is a random access range of element accessors, so a loop, a page through
// std::views::drop and std::views::take, and a count work as with any std range. A page reads only its elements.
TEST_CASE("at_path: a fixed-size vector is a range of its elements")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const root = form<gauges>::path(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    CHECK(std::ranges::random_access_range<decltype(*g)>);
    CHECK(std::ranges::sized_range<decltype(*g)>);
    std::vector<int> xs;
    for (auto const e : *g)
        xs.push_back(*e.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6, 9}));
    CHECK_EQ(std::ranges::size(*g), 3u);
    auto page = *g | std::views::drop(1) | std::views::take(1);
    CHECK_EQ(std::ranges::distance(page), 1);
    auto const first = *std::ranges::begin(page);
    CHECK_EQ(first.at_path<"@.range[1]">(), 8u);
    auto const third = g->begin()[2];
    CHECK_EQ(third.at_path<"@.on">(), true);
    std::string const empty = schema_bytes(gauges{});
    auto const none = form<gauges>::path(std::string(empty));
    REQUIRE(none.has_value());
    auto const nothing = none->at_path<"$.g">();
    REQUIRE(nothing.has_value());
    CHECK(std::ranges::empty(*nothing));
}

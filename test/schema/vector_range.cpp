#include "types.hpp"

// The accessor of a fixed-size vector gives element accessors at the same places as at_path. The test exists because the
// fixed-size element has no tag and no text. It compares a number and an enum of each element with at_path. It reads the
// list backwards and by page. It checks every position outside of the list.
TEST_CASE("at_path: a fixed-size vector reads each element as at_path does")
{
    auto const owner = std::make_shared<std::string const>(encode_with_schema(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const root = form<gauges>::view(owner);
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    using A = std::remove_cvref_t<decltype(*g)>;
    CHECK(std::ranges::random_access_range<A const>);
    CHECK(std::ranges::sized_range<A const>);
    CHECK(std::random_access_iterator<A::iterator>);
    REQUIRE_EQ(g->size(), 3u);
    CHECK_EQ(std::ranges::distance(*g), 3);
    CHECK_EQ(g->end() - g->begin(), 3);
    std::size_t i = 0;
    for (auto const e : *g) {
        CHECK_EQ(*e.at_path<"@.at.x">(), *root->at_path<"$.g[].at.x">(i));
        CHECK_EQ(*e.at_path<"@.shade">(), *root->at_path<"$.g[].shade">(i));
        ++i;
    }
    CHECK_EQ(i, 3u);
    auto const second = g->begin()[1];
    CHECK_EQ(*second.at_path<"@.range[0]">(), 7u);
    auto const last = *(g->end() - 1);
    CHECK_EQ(*last.at_path<"@.at.x">(), 9);
    std::vector<int> backwards;
    for (auto r = g->rbegin(); r != g->rend(); ++r) {
        auto const e = *r;
        backwards.push_back(*e.at_path<"@.at.x">());
    }
    CHECK(backwards == std::vector<int>{9, -6, 3});
    std::vector<int> reversed;
    for (auto const e : *g | std::views::reverse)
        reversed.push_back(*e.at_path<"@.at.x">());
    CHECK(reversed == backwards);
    std::vector<int> page;
    for (auto const e : *g | std::views::drop(1) | std::views::take(1))
        page.push_back(*e.at_path<"@.at.x">());
    CHECK(page == std::vector<int>{-6});
    check_positions_outside(*g);
    auto const loose = form<gauges>::unverified_view(owner);
    REQUIRE(loose.has_value());
    auto const h = loose->at_path<"$.g">();
    REQUIRE(h.has_value());
    std::vector<int> unverified;
    for (auto const e : *h)
        unverified.push_back(*e.at_path<"@.at.x">());
    CHECK(unverified == std::vector<int>{3, -6, 9});
    check_positions_outside(*h);
    auto const none = form<gauges>::adopt(encode_with_schema(gauges{}));
    REQUIRE(none.has_value());
    auto const nothing = none->at_path<"$.g">();
    REQUIRE(nothing.has_value());
    CHECK(nothing->begin() == nothing->end());
    check_positions_outside(*nothing);
}

// A fixed-size element carries no tag, so a wrong byte in it is a bool that is not 0 or 1. The test exists because
// the element from the iterator must give the same error as at_path with the index.
TEST_CASE("at_path: a wrong element in a fixed-size vector gives the error of at_path")
{
    std::string flags = encode_with_schema(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    flags[element + 2] = '\x02';
    auto const root = form<gauges>::adopt(std::string(flags));
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    auto const first = *g->begin();
    CHECK_EQ(root->at_path<"$.g[].on">(0).error(), error::incorrect_type);
    CHECK_EQ(first.at_path<"@.on">().error(), root->at_path<"$.g[].on">(0).error());
    CHECK_EQ(*first.at_path<"@.shade">(), color::white);
}

// An iterator of a fixed-size vector holds the owner, and a move empties the source. The test exists for the same
// reasons as for a list of records. The iterator reads after its accessor is gone. The source of a move gives no list.
// The target and a move into itself read.
TEST_CASE("at_path: a fixed-size vector keeps its bytes in the iterator and moves its list")
{
    using A = std::remove_cvref_t<decltype(*std::declval<form<gauges>::accessor<> const &>().at_path<"$.g">())>;
    A::iterator kept;
    std::optional<A> target;
    {
        auto owner = std::make_shared<std::string const>(encode_with_schema(
            gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}}));
        auto const root = form<gauges>::view(owner);
        REQUIRE(root.has_value());
        std::optional<A> source(*root->at_path<"$.g">());
        kept = source->begin() + 1;
        target.emplace(std::move(*source));
        CHECK_EQ(source->size(), 0u);
        CHECK_THROWS_AS((void)source->begin(), std::logic_error);
        CHECK_THROWS_AS((void)*(source->end() - 1), std::logic_error);
        source.reset();
        owner.reset();
    }
    auto const e = *kept;
    CHECK_EQ(*e.at_path<"@.at.x">(), -6);
    auto const first = *target->begin();
    CHECK_EQ(*first.at_path<"@.at.x">(), 3);
    auto &same = *target;
    *target = std::move(same);
    REQUIRE_EQ(target->size(), 2u);
    auto const again = target->begin()[1];
    CHECK_EQ(*again.at_path<"@.range[1]">(), 8u);
}

// The iterator of a typed array view refuses each position outside of the view, as the iterators of the lists do.
// The test exists because the view iterator reads through the same kind of element function, and the last element
// is one before the end.
TEST_CASE("view: the iterator refuses each position outside of the view")
{
    auto const root = form<doubles>::adopt(encode_with_schema(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(*(v->end() - 1)), 0x4010000000000000u);
    check_positions_outside(*v);
    auto const empty_root = form<doubles>::adopt(encode_with_schema(doubles{}));
    REQUIRE(empty_root.has_value());
    auto const e = empty_root->view<"$.v">();
    REQUIRE(e.has_value());
    check_positions_outside(*e);
}

// A move leaves an empty view in the source. The test exists because begin equals end in the source after the move. An
// iterator taken before the move still reads, because it holds the owner.
TEST_CASE("view: a moved view has begin equal to end")
{
    auto const root = form<doubles>::adopt(encode_with_schema(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto source = *root->view<"$.v">();
    auto const kept = source.begin() + 2;
    auto const target = std::move(source);
    CHECK(source.begin() == source.end());
    CHECK_THROWS_AS((void)*source.begin(), std::logic_error);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*kept), 0x4010000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(target[0]), 0x3ff0000000000000u);
}

// for_each gives each element of a vector of records and of a vector of fixed-size structs, in order. The test exists
// because for_each is the read of a whole list that copies no owner per element: each value must be the one that
// at_path with the index gives, for the verified and for the unverified accessor.
TEST_CASE("for_each: every element of a vector, in order")
{
    parking_lot const sample = make_sample_lot();
    auto const owner = std::make_shared<std::string const>(encode_with_schema(sample));
    auto const root = form<parking_lot>::view(owner);
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::vector<std::string_view> makes;
    std::vector<std::uint8_t> seats;
    cars->for_each([&](auto const &car) {
        makes.push_back(*car.template at_path<"@.make">());
        seats.push_back(*car.template at_path<"@.seats">());
    });
    std::vector<std::string_view> expected_makes;
    std::vector<std::uint8_t> expected_seats;
    for (lot_car const &c : sample.cars) {
        expected_makes.push_back(c.make);
        expected_seats.push_back(c.seats);
    }
    CHECK(makes == expected_makes);
    CHECK(seats == expected_seats);
    auto const g_owner = std::make_shared<std::string const>(encode_with_schema(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const loose = form<gauges>::unverified_view(g_owner);
    REQUIRE(loose.has_value());
    auto const g = loose->at_path<"$.g">();
    REQUIRE(g.has_value());
    std::vector<int> xs;
    g->for_each([&xs](auto const &e) { xs.push_back(*e.template at_path<"@.at.x">()); });
    CHECK(xs == std::vector<int>{3, -6, 9});
}

// An empty vector has no element, so f is never called. The test exists because the loop of for_each reads the
// count of the list and must not touch an element that is not there.
TEST_CASE("for_each: an empty vector calls nothing")
{
    auto const root = form<parking_lot>::adopt(encode_with_schema(parking_lot{}));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::size_t calls = 0;
    cars->for_each([&calls](auto const &) { ++calls; });
    CHECK_EQ(calls, 0u);
}

// A path to a vector of the element gives the vector without an owner, and it offers for_each the same way. A path to
// a record gives an accessor with the owner. The test exists because a nested list must read the wheels of each car in
// order, through the owner of the outer accessor.
TEST_CASE("for_each: a nested for_each reads the vector of each element")
{
    parking_lot const sample = make_sample_lot();
    auto const root = form<parking_lot>::adopt(encode_with_schema(sample));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::vector<std::vector<std::uint16_t>> diameters;
    std::vector<std::size_t> sizes;
    cars->for_each([&](auto const &car) {
        auto const wheels = car.template at_path<"@.wheels">();
        REQUIRE(wheels.has_value());
        CHECK(std::same_as<std::remove_cvref_t<decltype(car)>, form<parking_lot>::borrowed_accessor<lot_car, true>>);
        CHECK(std::same_as<std::remove_cvref_t<decltype(*wheels)>,
                           form<parking_lot>::borrowed_accessor<std::vector<lot_wheel>, true>>);
        CHECK(std::same_as<std::remove_cvref_t<decltype(*car.template at_path<"@.engine">())>,
                           form<parking_lot>::accessor<lot_engine, true>>);
        sizes.push_back(wheels->size());
        diameters.emplace_back();
        wheels->for_each([&](auto const &wheel) { diameters.back().push_back(*wheel.template at_path<"@.diameter">()); });
    });
    std::vector<std::vector<std::uint16_t>> expected;
    std::vector<std::size_t> expected_sizes;
    for (lot_car const &c : sample.cars) {
        expected.emplace_back();
        for (lot_wheel const &w : c.wheels)
            expected.back().push_back(w.diameter);
        expected_sizes.push_back(c.wheels.size());
    }
    CHECK(diameters == expected);
    CHECK(sizes == expected_sizes);
}

// for_each catches nothing. The test exists because an exception from f must reach the caller unchanged and stop the
// loop at the element that threw.
TEST_CASE("for_each: an exception from f passes through")
{
    auto const root = form<parking_lot>::adopt(encode_with_schema(make_sample_lot()));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::size_t calls = 0;
    auto const second_throws = [&calls](auto const &) {
        ++calls;
        if (calls == 2)
            throw std::range_error("second car");
    };
    CHECK_THROWS_AS(cars->for_each(second_throws), std::range_error);
    CHECK_EQ(calls, 2u);
}

// The element of for_each points to the owner of the accessor that called it, so it must not leave the call. The test
// exists because a copy or a move of the element would let it outlive that owner. A moved-from accessor is a wrong use.
TEST_CASE("for_each: the element cannot be copied or moved")
{
    using record = form<parking_lot>::borrowed_accessor<lot_car, true>;
    using fixed = form<parking_lot>::borrowed_accessor<cbor::fixed_element<lot_wheel>, true>;
    using list = form<parking_lot>::borrowed_accessor<std::vector<lot_wheel>, false>;
    CHECK_FALSE(std::is_copy_constructible_v<record>);
    CHECK_FALSE(std::is_move_constructible_v<record>);
    CHECK_FALSE(std::is_copy_assignable_v<record>);
    CHECK_FALSE(std::is_move_assignable_v<record>);
    CHECK_FALSE(std::is_copy_constructible_v<fixed>);
    CHECK_FALSE(std::is_move_constructible_v<fixed>);
    CHECK_FALSE(std::is_copy_constructible_v<list>);
    CHECK_FALSE(std::is_move_constructible_v<list>);
    auto root = form<parking_lot>::adopt(encode_with_schema(make_sample_lot()));
    REQUIRE(root.has_value());
    auto cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    auto const moved = std::move(*cars);
    CHECK_THROWS_AS(cars->for_each([](auto const &) {}), std::logic_error);
}

#include "types.hpp"

// The accessor of a fixed-size vector gives element accessors at the same places as at_path. The test exists
// because the fixed-size element has no tag and no text: it compares a number and an enum of each element with
// at_path, reads the list backwards and by page, and checks every position outside of the list.
TEST_CASE("at_path: a fixed-size vector reads each element as at_path does")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(
        gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}, {true, color::black, {0, 9}, {0, 0}}}}));
    auto const root = form<gauges>::path(owner);
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
    auto const loose = form<gauges>::unverified_path(owner);
    REQUIRE(loose.has_value());
    auto const h = loose->at_path<"$.g">();
    REQUIRE(h.has_value());
    std::vector<int> unverified;
    for (auto const e : *h)
        unverified.push_back(*e.at_path<"@.at.x">());
    CHECK(unverified == std::vector<int>{3, -6, 9});
    check_positions_outside(*h);
    auto const none = form<gauges>::path(schema_bytes(gauges{}));
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
    std::string flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    flags[element + 2] = '\x02';
    auto const root = form<gauges>::path(std::string(flags));
    REQUIRE(root.has_value());
    auto const g = root->at_path<"$.g">();
    REQUIRE(g.has_value());
    auto const first = *g->begin();
    CHECK_EQ(root->at_path<"$.g[].on">(0).error(), error::incorrect_type);
    CHECK_EQ(first.at_path<"@.on">().error(), root->at_path<"$.g[].on">(0).error());
    CHECK_EQ(*first.at_path<"@.shade">(), color::white);
}

// An iterator of a fixed-size vector holds the owner, and a move empties the source. The test exists for the same
// reasons as for a list of records: the iterator reads after its accessor is gone, the source of a move gives no
// list, and the target and a move into itself read.
TEST_CASE("at_path: a fixed-size vector keeps its bytes in the iterator and moves its list")
{
    using A = std::remove_cvref_t<decltype(*std::declval<form<gauges>::accessor<> const &>().at_path<"$.g">())>;
    A::iterator kept;
    std::optional<A> target;
    {
        auto owner = std::make_shared<std::string const>(schema_bytes(
            gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}, {false, color::black, {5, -6}, {7, 8}}}}));
        auto const root = form<gauges>::path(owner);
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
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto const v = root->view<"$.v">();
    REQUIRE(v.has_value());
    CHECK_EQ(std::bit_cast<std::uint64_t>(*(v->end() - 1)), 0x4010000000000000u);
    check_positions_outside(*v);
    auto const empty_root = form<doubles>::path(schema_bytes(doubles{}));
    REQUIRE(empty_root.has_value());
    auto const e = empty_root->view<"$.v">();
    REQUIRE(e.has_value());
    check_positions_outside(*e);
}

// A move leaves an empty view in the source. The test exists because begin equals end in the source after the move,
// and an iterator taken before the move still reads, because it holds the owner.
TEST_CASE("view: a moved view has begin equal to end")
{
    auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
    REQUIRE(root.has_value());
    auto source = *root->view<"$.v">();
    auto const kept = source.begin() + 2;
    auto const target = std::move(source);
    CHECK(source.begin() == source.end());
    CHECK_THROWS_AS((void)*source.begin(), std::logic_error);
    CHECK_EQ(std::bit_cast<std::uint64_t>(*kept), 0x4010000000000000u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(target[0]), 0x3ff0000000000000u);
}

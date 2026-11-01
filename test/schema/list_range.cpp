#include "types.hpp"

namespace
{

struct people_view {
    std::span<named const> people;
};

struct cloud_view {
    std::span<point const> points;
};

} // namespace

template <>
struct cbor::describe_members<people_view> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1795};
    static constexpr std::tuple members{
        CBOR_MEMBER(people_view, people),
    };
};

template <>
struct cbor::describe_members<cloud_view> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1796};
    static constexpr std::tuple members{
        CBOR_MEMBER(cloud_view, points),
    };
};

namespace
{

std::string make_of(auto const &car)
{
    auto const make = car.template at_path<"@.make">();
    return make ? std::string(*make) : "(error)"s;
}

} // namespace

// The accessor of a list of records is a random access range of record accessors. The test exists so that each
// element that the iterator gives reads the same text and the same number as at_path with the same index. The
// distance from begin to end is the size, and operator[] gives the element at its index.
TEST_CASE("at_path: a list of records is a range of its elements")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(sample_lot()));
    auto const root = form<parking_lot>::path(owner);
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    using A = std::remove_cvref_t<decltype(*cars)>;
    CHECK(std::ranges::random_access_range<A const>);
    CHECK(std::ranges::sized_range<A const>);
    CHECK(std::random_access_iterator<A::iterator>);
    REQUIRE_EQ(cars->size(), 5u);
    CHECK_EQ(std::ranges::distance(*cars), 5);
    CHECK_EQ(cars->end() - cars->begin(), 5);
    std::size_t i = 0;
    for (auto const car : *cars) {
        CHECK_EQ(*car.at_path<"@.make">(), *root->at_path<"$.cars[].make">(i));
        CHECK_EQ(*car.at_path<"@.seats">(), *root->at_path<"$.cars[].seats">(i));
        ++i;
    }
    CHECK_EQ(i, 5u);
    auto const fourth = cars->begin()[3];
    CHECK_EQ(make_of(fourth), "make3");
    CHECK_EQ(*fourth.at_path<"@.seats">(), std::uint8_t{5});
    auto const last = *(cars->end() - 1);
    CHECK_EQ(make_of(last), "make4");
    check_positions_outside(*cars);
}

// A list of records is read backwards through rbegin and rend and through std::views::reverse, and a page through
// std::views::drop and std::views::take. The test exists because each of these forms moves the position in another
// way, and each must reach the same elements as the index.
TEST_CASE("at_path: a list of records reads backwards and by page")
{
    auto const root = form<parking_lot>::path(schema_bytes(sample_lot()));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::vector<std::string> backwards;
    for (auto r = cars->rbegin(); r != cars->rend(); ++r)
        backwards.push_back(make_of(*r));
    CHECK(backwards == std::vector<std::string>{"make4", "make3", "make2", "make1", "make0"});
    std::vector<std::string> reversed;
    for (auto const car : *cars | std::views::reverse)
        reversed.push_back(make_of(car));
    CHECK(reversed == backwards);
    CHECK(cars->crbegin() == cars->rbegin());
    CHECK(cars->cbegin() == cars->begin());
    CHECK(cars->cend() == cars->end());
    std::vector<std::string> page;
    for (auto const car : *cars | std::views::drop(1) | std::views::take(2))
        page.push_back(make_of(car));
    CHECK(page == std::vector<std::string>{"make1", "make2"});
}

// An empty list has no element. The test exists because begin equals end there, and each position, the begin too,
// is outside of the list.
TEST_CASE("at_path: an empty list of records has no element")
{
    auto const root = form<parking_lot>::path(schema_bytes(parking_lot{}));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    CHECK(std::ranges::empty(*cars));
    CHECK(cars->begin() == cars->end());
    CHECK_EQ(std::ranges::distance(*cars), 0);
    CHECK_THROWS_AS((void)*cars->begin(), std::logic_error);
    check_positions_outside(*cars);
}

// The unverified form reads the same elements as the verified form. The test exists because the iterator takes the
// form of its accessor, and the element function of the unverified form must cut each element at the same place.
TEST_CASE("unverified_path: a list of records is a range of its elements")
{
    auto const owner = std::make_shared<std::string const>(schema_bytes(sample_lot()));
    auto const root = form<parking_lot>::unverified_path(owner);
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    std::size_t i = 0;
    for (auto const car : *cars) {
        CHECK_EQ(*car.at_path<"@.make">(), *root->at_path<"$.cars[].make">(i));
        CHECK_EQ(*car.at_path<"@.seats">(), *root->at_path<"$.cars[].seats">(i));
        ++i;
    }
    CHECK_EQ(i, 5u);
    check_positions_outside(*cars);
}

// A record accessor from the iterator gives the lists inside the record. The test exists because the nested list
// is a list of fixed-size elements, so its iterator is the other kind, and it must read what at_path reads with
// two indexes.
TEST_CASE("at_path: a list inside a record from the iterator is a range")
{
    auto const root = form<parking_lot>::path(schema_bytes(sample_lot()));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    auto const fourth = cars->begin()[3];
    auto const wheels = fourth.at_path<"@.wheels">();
    REQUIRE(wheels.has_value());
    REQUIRE_EQ(wheels->size(), 3u);
    std::size_t k = 0;
    for (auto const wheel : *wheels) {
        CHECK_EQ(*wheel.at_path<"@.diameter">(), *root->at_path<"$.cars[].wheels[].diameter">(3, k));
        CHECK_EQ(*wheel.at_path<"@.snowTires">(), *root->at_path<"$.cars[].wheels[].snowTires">(3, k));
        ++k;
    }
    CHECK_EQ(k, 3u);
}

// A list whose element type is const reads as a list of the element type. The test exists because the iterator
// takes the element type without its qualifiers, as at_path does.
TEST_CASE("at_path: a list of const elements is a range of its elements")
{
    std::vector<named> const source{{"a"}, {"bc"}, {"def"}};
    auto const root = form<people_view>::path(schema_bytes(people_view{source}));
    REQUIRE(root.has_value());
    auto const people = root->at_path<"$.people">();
    REQUIRE(people.has_value());
    std::vector<std::string> names;
    for (auto const person : *people)
        names.emplace_back(*person.at_path<"@.n">());
    CHECK(names == std::vector<std::string>{"a", "bc", "def"});
    std::vector<point> const points{{1.0f, 2.0f}, {3.0f, 4.0f}};
    auto const cloud_root = form<cloud_view>::path(schema_bytes(cloud_view{points}));
    REQUIRE(cloud_root.has_value());
    auto const list = cloud_root->at_path<"$.points">();
    REQUIRE(list.has_value());
    std::vector<std::uint32_t> ys;
    for (auto const p : *list)
        ys.push_back(std::bit_cast<std::uint32_t>(*p.at_path<"@.y">()));
    CHECK(ys == std::vector<std::uint32_t>{std::bit_cast<std::uint32_t>(2.0f), std::bit_cast<std::uint32_t>(4.0f)});
}

// A record with a wrong class tag is an error. The test exists because the element from the iterator must give the
// same error as at_path with the index, and the other elements must still read.
TEST_CASE("at_path: a record with a wrong tag in a list gives the error of at_path")
{
    std::string bytes = schema_bytes(sample_lot());
    std::size_t const tag = bytes.find("\xd9\x05\xf6"s);
    REQUIRE_NE(tag, std::string::npos);
    bytes[tag + 2] = '\x00';
    auto const root = form<parking_lot>::path(std::string(bytes));
    REQUIRE(root.has_value());
    auto const cars = root->at_path<"$.cars">();
    REQUIRE(cars.has_value());
    auto const first = *cars->begin();
    CHECK_EQ(root->at_path<"$.cars[].make">(0).error(), error::incorrect_type);
    CHECK_EQ(first.at_path<"@.make">().error(), root->at_path<"$.cars[].make">(0).error());
    auto const second = cars->begin()[1];
    CHECK_EQ(make_of(second), "make1");
}

// An iterator holds the owner of the bytes. The test exists because the iterator must still read a text of a
// record after its accessor, the root accessor and the first std::shared_ptr are gone.
TEST_CASE("at_path: an iterator of a list of records outlives its accessor")
{
    using A = std::remove_cvref_t<decltype(*std::declval<form<parking_lot>::accessor<> const &>().at_path<"$.cars">())>;
    A::iterator kept;
    {
        auto owner = std::make_shared<std::string const>(schema_bytes(sample_lot()));
        auto const root = form<parking_lot>::path(owner);
        REQUIRE(root.has_value());
        auto const cars = root->at_path<"$.cars">();
        REQUIRE(cars.has_value());
        kept = cars->begin() + 2;
        owner.reset();
    }
    auto const car = *kept;
    CHECK_EQ(*car.at_path<"@.make">(), "make2"sv);
}

// A move gives the owner, the bytes and the length to the target. The test exists because the source keeps no list:
// its begin throws, and an iterator from it reads nothing. The target reads after the source is gone, and a move
// into itself keeps the accessor.
TEST_CASE("at_path: a moved list of records keeps no list in the source")
{
    using A = std::remove_cvref_t<decltype(*std::declval<form<parking_lot>::accessor<> const &>().at_path<"$.cars">())>;
    std::optional<A> target;
    {
        auto const root = form<parking_lot>::path(schema_bytes(sample_lot()));
        REQUIRE(root.has_value());
        std::optional<A> source(*root->at_path<"$.cars">());
        target.emplace(std::move(*source));
        CHECK_EQ(source->size(), 0u);
        CHECK(std::ranges::empty(*source));
        CHECK_THROWS_AS((void)source->begin(), std::logic_error);
        CHECK_THROWS_AS((void)*(source->end() - 1), std::logic_error);
        A assigned = *target;
        A other = *target;
        assigned = std::move(other);
        CHECK_EQ(other.size(), 0u);
        CHECK_THROWS_AS((void)other.begin(), std::logic_error);
        CHECK_EQ(make_of(assigned.begin()[1]), "make1");
        source.reset();
    }
    REQUIRE_EQ(target->size(), 5u);
    CHECK_EQ(make_of(target->begin()[4]), "make4");
    auto &same = *target;
    *target = std::move(same);
    REQUIRE_EQ(target->size(), 5u);
    CHECK_EQ(make_of(*target->begin()), "make0");
}

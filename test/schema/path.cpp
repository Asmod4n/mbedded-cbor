#include "types.hpp"

namespace schema_types
{


template <class T, cbor::fixed_string Path, class I>
concept path_reads_at = requires(typename form<T>::template accessor<> const &a, I const i) { a.template at_path<Path>(i); };

template <class = void>
std::string text_of(cbor::result<std::string_view> const &r)
{
    return r ? std::string(*r) : "(error)"s;
}

// One read of every field of car i is compared with the decoded car. It uses leaf paths from the root. It also uses an
// accessor of the car and of each wheel.
template <class = void>
void lot_car_matches(form<parking_lot>::accessor<> const &lot, parking_lot const &back, std::size_t const i)
{
    CAPTURE(i);
    lot_car const &c = back.cars.at(i);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].make">(i)), c.make);
    CHECK_EQ(text_of(lot.at_path<"$.cars[].model">(i)), c.model);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(i), c.seats);
    CHECK_EQ(lot.at_path<"$.cars[].engine.cc">(i), c.engine.cc);
    CHECK_EQ(lot.at_path<"$.cars[].engine.horsepower">(i), c.engine.horsepower);
    auto const found_note = lot.at_path<"$.cars[].note">(i);
    auto const note = *found_note;
    CHECK_EQ(note.has_value(), c.note.has_value());
    if (note && c.note)
        CHECK_EQ(*note, *c.note);
    auto const opened = lot.at_path<"$.cars[]">(i);
    REQUIRE(opened.has_value());
    auto const &car = *opened;
    CHECK_EQ(text_of(car.at_path<"@.make">()), c.make);
    CHECK_EQ(car.at_path<"@.engine.cc">(), c.engine.cc);
    auto const wheels = car.at_path<"@.wheels">();
    CHECK_EQ(wheels->size(), c.wheels.size());
    CHECK_EQ(text_of(lot.at_path<"$.cars[].code">(i)), std::string_view(c.code, 2));
    for (std::size_t k = c.wheels.size() + 1; k-- > 0;) {
        CAPTURE(k);
        if (k == c.wheels.size()) {
            CHECK_EQ(lot.at_path<"$.cars[].wheels[].diameter">(i, k), std::unexpected(error::index_out_of_bounds));
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

} // namespace schema_types

// path checks the least size of the message. Only decode checks the table of keys; a verified read checks only the
// item that it reads, so a foreign key in the table leaves the value readable. The test exists so that a change to
// either side shows here.
TEST_CASE("path: a message shorter than the least size is too_little_data, a foreign key is incorrect_type in decode")
{
    std::string const bytes = encode_with_schema(login{5, true, "ab"});
    CHECK_EQ(read_leaf_with_copying_path<login, "$.id">(bytes), 5u);
    CHECK_EQ(read_leaf_with_copying_path<login, "$.id">(std::string_view(bytes).substr(0, 20)).error(), error::too_little_data);
    std::string other = bytes;
    other.replace(8, 2, "ID");
    CHECK_EQ(form<login>::decode(std::string(other)), std::unexpected(error::incorrect_type));
    CHECK_EQ(read_leaf_with_copying_path<login, "$.id">(other), 5u);
}

// A path that ends at a leaf gives the value. A path that ends at a struct, a list, a fixed array or a map gives an
// accessor. A name that is not a member, an index into a text or a map, an index past a fixed array. A count of indexes
// that does not match the empty brackets do not compile. A path starts at the root of the top-level item with $ (RFC
// 9535 2.2). It can also start at the node of the accessor with @ (RFC 9535 2.3.5). On the root accessor both are the
// same node.
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
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.names[0]">()), cbor::result<std::string_view>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires[]">(0uz)), cbor::result<form<garage>::accessor<cbor::fixed_element<tire>>>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.tires">()),
                       cbor::result<form<garage>::accessor<std::vector<tire>>>>);
}

// An accessor of a list gives its size and its elements; an accessor of a struct gives its fields. A leaf through
// several steps gives the same value as the same steps through named accessors.
TEST_CASE("path: an accessor reads relative to the part it names")
{
    std::string const bytes = encode_with_schema(sample_garage);
    auto const opened = form<garage>::adopt(std::string(bytes));
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const tires = root.at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 3u);
    auto const second = tires->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.diameter">(), 18u);
    CHECK_EQ(second->at_path<"@.airPressure">(), root.at_path<"$.tires[].airPressure">(1uz));
    CHECK_EQ(tires->at_path<"@[]">(3uz), std::unexpected(error::index_out_of_bounds));
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
    auto const opened = form<ticket>::adopt(build_ticket_message());
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    CHECK_EQ(root.at_path<"$.holder">(), sample_ticket.holder);
    CHECK_EQ(root.at_path<"$.seats[]">(1uz), sample_ticket.seats.at(1));
}

// The owner form keeps the owner in the root accessor. The caller releases its own pointer to the owner before it
// reads. The test exists because the root alone must keep the bytes alive.
TEST_CASE("path: the root keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(build_ticket_message());
    auto const opened = form<ticket>::view(std::move(owner));
    REQUIRE(opened.has_value());
    auto const &root = *opened;
    auto const holder = root.at_path<"$.holder">();
    auto const seat = root.at_path<"$.seats[1]">();
    REQUIRE(holder.has_value());
    REQUIRE(seat.has_value());
    CHECK_EQ(*holder, sample_ticket.holder);
    CHECK_EQ(*seat, sample_ticket.seats.at(1));
}

// An accessor reads each field of a list of several cars as decode reads it. It reads them in order, in the reverse
// order and in a shuffled order. The test exists because a read that goes back must not take a value of another car.
TEST_CASE("path: an accessor gives the values of decode in every order")
{
    std::string const bytes = encode_with_schema(make_sample_lot());
    auto const decoded = form<parking_lot>::decode(std::string(bytes));
    auto const decoded_2 = *decoded;
    parking_lot const &back = *decoded_2;
    auto const adopted = form<parking_lot>::adopt(std::string(bytes));
    auto lot = *adopted;
    std::vector<std::size_t> order(back.cars.size());
    std::iota(order.begin(), order.end(), 0uz);
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    for (std::size_t const i : order | std::views::reverse)
        lot_car_matches(lot, back, i);
    std::ranges::shuffle(order, std::mt19937(5));
    for (std::size_t const i : order)
        lot_car_matches(lot, back, i);
    CHECK_EQ(lot.at_path<"$.cars[].seats">(back.cars.size()), std::unexpected(error::index_out_of_bounds));
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(2uz)), "third"sv);
    CHECK_EQ(text_of(lot.at_path<"$.notes[]">(0uz)), "first"sv);
    CHECK_EQ(lot.at_path<"$.rows[][]">(2uz, 0uz), 3u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(0uz, 1uz), 2u);
    CHECK_EQ(lot.at_path<"$.rows[][]">(1uz, 0uz), std::unexpected(error::index_out_of_bounds));
    CHECK_EQ(read_leaf_with_copying_path<parking_lot, "$.cars[].wheels[].diameter">(bytes, 3uz, 0uz), back.cars[3].wheels[0].diameter);
    CHECK_EQ(read_leaf_with_copying_path<parking_lot, "$.cars[].model">(bytes, 3uz), "mmm"s);
}

// An accessor is read through an lvalue. The test exists because a text and a nested accessor borrow the bytes of the
// root. A read through a temporary root would be the only use of a root that ends at once.
TEST_CASE("path: an accessor is not read through a temporary")
{
    using D = form<parking_lot>::accessor<>;
    CHECK(lot_reads<D, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[].seats">);
    CHECK(lot_reads<D const, "$.cars[]">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[].seats">);
    CHECK_FALSE(temporary_lot_reads<D, "$.cars[]">);
    CHECK_FALSE(temporary_car_reads<D>);
    CHECK_THROWS_AS((void)form<parking_lot>::view(std::shared_ptr<std::string const>{}), std::logic_error);
}

// The copying form keeps its copy: a text and a car accessor stay valid after the caller frees the message, while the
// root lives.
TEST_CASE("path: the copying form keeps its copy after the caller frees the message")
{
    auto bytes = std::make_unique<std::string>(encode_with_schema(make_sample_lot()));
    auto const adopted = form<parking_lot>::adopt(std::string(*bytes));
    auto lot = *adopted;
    auto const found = lot.at_path<"$.cars[].make">(3uz);
    auto const make = *found;
    auto const found_2 = lot.at_path<"$.cars[]">(4uz);
    auto const car = *found_2;
    bytes.reset();
    CHECK_EQ(make, "make3"sv);
    CHECK_EQ(car.at_path<"@.make">(), "make4"sv);
    CHECK_EQ(car.at_path<"$.cars[].make">(4uz), "make4"sv);
}

// The value category of the bytes says what decode and path do. A moved std::string becomes the owner and is not
// copied; an lvalue and a const rvalue are copied once. The result stays valid after the caller reuses or destroys
// its buffer.
TEST_CASE("decode and path: an rvalue string is moved, everything else is copied")
{
    std::string const name(40, 'n');
    std::string const message = encode_with_schema(login{7, true, name});
    auto range = [](std::string_view const inner, std::span<char const> const outer) {
        return std::less_equal<>{}(outer.data(), inner.data()) &&
               std::less_equal<>{}(std::to_address(inner.end()), std::to_address(outer.end()));
    };

    auto buffer = std::make_unique<std::string>(message);
    std::span<char const> const data(*buffer);
    auto const moved = form<login>::adopt(std::move(*buffer));
    REQUIRE(moved.has_value());
    auto const moved_name = moved->at_path<"$.name">();
    REQUIRE(moved_name.has_value());
    CHECK(range(*moved_name, data));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(*moved_name, name);

    std::string lvalue = message;
    auto const copied = form<login>::adopt(std::string(lvalue));
    REQUIRE(copied.has_value());
    auto const copied_name = copied->at_path<"$.name">();
    REQUIRE(copied_name.has_value());
    CHECK_FALSE(range(*copied_name, lvalue));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(*copied_name, name);

    std::string const constant = message;
    // Owner decision 2026-10-17: no form copies the input. A const string, a named string, a view and a literal
    // would need a copy, so none of them compiles.
    auto const takes = []<class S>(S &&) { return requires(S &&s) { form<login>::decode(std::forward<S>(s)); }; };
    auto const opens = []<class S>(S &&) { return requires(S &&s) { form<login>::adopt(std::forward<S>(s)); }; };
    CHECK_FALSE(opens(std::move(constant)));
    CHECK_FALSE(takes(std::move(constant)));
    CHECK_FALSE(takes(lvalue));
    CHECK_FALSE(takes(std::string_view(message)));
    CHECK_FALSE(takes(""));
    CHECK_FALSE(opens(""));

    auto decode_buffer = std::make_unique<std::string>(message);
    auto const decoded = form<login>::decode(std::move(*decode_buffer));
    decode_buffer.reset();
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->name, name);
    CHECK_EQ(form<login>::decode(std::string()), std::unexpected(error::too_little_data));
    CHECK_EQ(form<login>::adopt(std::string()), std::unexpected(error::too_little_data));
}

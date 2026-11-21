#include "types.hpp"

namespace schema_types
{

// The forms of cbor::static_dictionary are a finite set. So each read form is called here on the data form, and the
// tests below call the two forms of encode.
template <class T>
void check_description_apart(T const &value)
{
    std::string const inside = encode_with_schema(value);
    std::string const data = encode_data_apart(value);
    auto const description = form<T>::description();
    CHECK_EQ(data.size() + description.size(), inside.size());
    CHECK(std::ranges::equal(description, std::span(inside).subspan(8, description.size())));
    check_one_item(data);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::decode(std::string(data))), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::unverified_decode(std::string(data))), inside);
    auto const owner = std::make_shared<std::string const>(data);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::decode(owner)), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::decode(std::shared_ptr<void const>(owner), std::string_view(*owner))), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::decode(owner, T{})), inside);
    CHECK(cbor::static_dictionary<T>::adopt(std::string(data)).has_value());
    CHECK(cbor::static_dictionary<T>::view(owner).has_value());
    CHECK(cbor::static_dictionary<T>::unverified_view(owner).has_value());
    std::shared_ptr<void const> const borrowed(owner);
    std::string_view const bytes(*owner);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::decode(borrowed, bytes, T{})), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::unverified_decode(owner)), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::unverified_decode(owner, T{})), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::unverified_decode(borrowed, bytes)), inside);
    CHECK_EQ(encode_again(cbor::static_dictionary<T>::unverified_decode(borrowed, bytes, T{})), inside);
    CHECK(cbor::static_dictionary<T>::view(borrowed, bytes).has_value());
    CHECK(cbor::static_dictionary<T>::unverified_view(borrowed, bytes).has_value());
    CHECK(cbor::static_dictionary<T>::unverified_adopt(std::string(data)).has_value());
}

} // namespace schema_types

// Owner decision 2026-10-20: the description of a struct is stored once, apart from the data of each key, so the data
// does not repeat it. The data form is the message without the table entries of the description. This library reads it
// with the type, which knows the description at compile time.
TEST_CASE("description apart: the data and the description together hold what the message holds")
{
    check_description_apart(sample_everything);
    check_description_apart(sample_vehicle);
    check_description_apart(empty{});
    check_description_apart(gauges{});
    check_description_apart(fifteen_entries{});
    eighteen_entries points{};
    points.p = {{1, 2}, {3, 4}};
    check_description_apart(points);
}

// Every read form gives on the data form what it gives on the message.
TEST_CASE("description apart: each read form reads the data form")
{
    std::string const data = encode_data_apart(sample_everything);
    CHECK_EQ((cbor::static_dictionary<everything>::at_path<"$.big">(data)), -5);
    CHECK_EQ((cbor::static_dictionary<everything>::at_path<"$.gauges[].at.x">(data, 1uz)), -6);
    CHECK_EQ((cbor::static_dictionary<everything>::unverified_at_path<"$.tires[].airPressure">(data, 1uz)), 3.0f);
    auto const root = cbor::static_dictionary<everything>::adopt(std::string(data));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    CHECK_EQ(root->view<"$.track">()->at(1), 2.0);
    CHECK_EQ(root->at_path<"$.owners">()->size(), 2u);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    std::vector<int> xs;
    auto const gauges = *root->at_path<"$.gauges">();
    for (auto const g : gauges)
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18u);
    auto const unverified = cbor::static_dictionary<everything>::unverified_adopt(std::string(data));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->at_path<"$.names[0]">(), "a"sv);
}

// Each target of encode writes the same data form.
TEST_CASE("description apart: every target of encode writes the same bytes")
{
    std::string const data = encode_data_apart(sample_everything);
    std::string appended = "ab";
    REQUIRE_EQ(cbor::static_dictionary<everything>::encode(appended, sample_everything), data.size());
    CHECK_EQ(appended, "ab" + data);
    std::vector<char> chars;
    REQUIRE_EQ(cbor::static_dictionary<everything>::encode(chars, sample_everything), data.size());
    CHECK(std::ranges::equal(chars, data));
    std::vector<char> room(data.size() + 64);
    REQUIRE_EQ(cbor::static_dictionary<everything>::encode(std::span<char>(room), sample_everything), data.size());
    CHECK(std::ranges::equal(std::span(room).first(data.size()), data));
    std::vector<char> exact(data.size());
    REQUIRE_EQ(cbor::static_dictionary<everything>::encode(std::span<char>(exact), sample_everything), data.size());
    CHECK(std::ranges::equal(exact, data));
    string_writer w;
    REQUIRE_EQ(cbor::static_dictionary<everything>::encode(w, sample_everything), data.size());
    CHECK_EQ(w.encoded, data);
}

// A message read as the data form is refused. The data form read as a message is refused. The byte after the head of the
// table is the head of the directory in one form. In the other form it is the first entry of the description. A short
// input ends as too_little_data before that byte is read.
TEST_CASE("description apart: one form read as the other is refused")
{
    for (std::string const &inside : {encode_with_schema(sample_everything), encode_with_schema(empty{}), encode_with_schema(eighteen_entries{})}) {
        auto const as_data = cbor::static_dictionary<everything>::adopt(std::string(inside));
        REQUIRE_FALSE(as_data.has_value());
    }
    std::string const inside = encode_with_schema(sample_everything);
    std::string const data = encode_data_apart(sample_everything);
    CHECK_EQ(cbor::static_dictionary<everything>::adopt(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ(cbor::static_dictionary<everything>::decode(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ((cbor::static_dictionary<everything>::at_path<"$.big">(inside)).error(), error::incorrect_type);
    // The data form holds no description. So read as the form with the description inside it is shorter than its table:
    // the doc of schema::adopt gives too_little_data for that.
    for (auto const e : {form<everything>::adopt(std::string(data)).error(),
                         form<everything>::decode(std::string(data)).error()})
        CHECK_EQ(e, error::too_little_data);
    std::string const small = encode_data_apart(empty{});
    CHECK_EQ(form<empty>::adopt(std::string(small)).error(), error::too_little_data);
}

// The data form keeps every check of the message. A cut gives too_little_data. A forged directory entry and a forged
// filler give an error in the verified forms.
TEST_CASE("description apart: the checks of the message hold for the data form")
{
    std::string const data = encode_data_apart(sample_everything);
    for (std::size_t n = 0; n < data.size(); ++n) {
        CHECK_FALSE(cbor::static_dictionary<everything>::decode(data.substr(0, n)).has_value());
        CHECK_FALSE(cbor::static_dictionary<everything>::adopt(data.substr(0, n)).has_value());
    }
    std::size_t const directory = data.find('\x5a');
    REQUIRE_EQ(directory, 8u);
    std::string entry = data;
    for (std::size_t k = 0; k < sizeof(std::uint32_t); ++k)
        entry[directory + 5 + k] = '\xff';
    CHECK_FALSE(cbor::static_dictionary<everything>::decode(std::string(entry)).has_value());
    std::uint32_t const length = (std::uint32_t{static_cast<unsigned char>(data[directory + 3])} << 8) |
                                 static_cast<unsigned char>(data[directory + 4]);
    std::string filler = data;
    REQUIRE_EQ(filler[directory + 5 + length], '\xf7');
    filler[directory + 5 + length] = '\xf6';
    CHECK_EQ(cbor::static_dictionary<everything>::adopt(std::string(filler)).error(), error::incorrect_type);
}

// The unverified forms read the data form inside its bounds: every changed byte and every cut. A message read as the
// data form, stay inside the input under AddressSanitizer.
TEST_CASE("description apart: the unverified forms stay inside the input")
{
    std::string const data = encode_data_apart(sample_everything);
    auto const reads = [](std::string const &bytes) {
        (void)cbor::static_dictionary<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz);
        (void)cbor::static_dictionary<everything>::unverified_decode(std::string(bytes));
        if (auto const root = cbor::static_dictionary<everything>::unverified_adopt(std::string(bytes))) {
            (void)root->at_path<"$.names[1]">();
            (void)root->at_path<"$.tires[1].diameter">();
            if (auto const track = root->view<"$.track">())
                for (double const d : *track)
                    (void)d;
        }
    };
    for (std::size_t i = 0; i < data.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~data[i])}) {
            std::string changed = data;
            changed[i] = to;
            reads(changed);
        }
    for (std::size_t n = 0; n < data.size(); ++n)
        reads(data.substr(0, n));
    reads(encode_with_schema(sample_everything));
}

// A value variant in the data form: the description apart changes the prefix only; each alternative reads back.
TEST_CASE("description apart: a value variant reads back from the data form")
{
    for (setting const &s : make_sample_settings()) {
        auto const data = cbor::static_dictionary<setting>::encode(s);
        REQUIRE(data.has_value());
        auto const back = cbor::static_dictionary<setting>::decode(std::string(*data));
        REQUIRE(back.has_value());
        CHECK(((*back)->value == s.value));
    }
}

#include "types.hpp"

namespace schema_types
{


template <class T>
void check_description_apart(T const &value)
{
    std::string const inside = schema_bytes(value);
    std::string const data = apart_bytes(value);
    auto const description = form<T>::description();
    CHECK_EQ(data.size() + description.size(), inside.size());
    CHECK(std::ranges::equal(description, std::span(inside).subspan(8, description.size())));
    check_one_item(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::string(data))), inside);
    CHECK_EQ(reencoded(form<T>::template unverified_decode<apart>(std::string(data))), inside);
    auto const owner = std::make_shared<std::string const>(data);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner)), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(std::shared_ptr<void const>(owner), std::string_view(*owner))), inside);
    CHECK_EQ(reencoded(form<T>::template decode<apart>(owner, T{})), inside);
    CHECK(form<T>::template path<apart>(std::string(data)).has_value());
    CHECK(form<T>::template path<apart>(owner).has_value());
    CHECK(form<T>::template unverified_path<apart>(owner).has_value());
}

} // namespace schema_types

// Owner decision 2026-10-20: the description of a struct is stored once, apart from the data of each key, so the
// data does not repeat it. The data form is the message without the table entries of the description; this
// library reads it with the type, which knows the description at compile time.
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
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(data)), -5);
    CHECK_EQ((form<everything>::at_path<"$.gauges[].at.x", apart>(data, 1uz)), -6);
    CHECK_EQ((form<everything>::unverified_at_path<"$.tires[].airPressure", apart>(data, 1uz)), 3.0f);
    auto const root = form<everything>::path<apart>(std::string(data));
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
    for (auto const g : *root->at_path<"$.gauges">())
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(root->at_path<"$.tires[1].diameter">(), 18u);
    auto const unverified = form<everything>::unverified_path<apart>(std::string(data));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->at_path<"$.names[0]">(), "a"sv);
}

// Each target of encode writes the same data form.
TEST_CASE("description apart: every target of encode writes the same bytes")
{
    std::string const data = apart_bytes(sample_everything);
    std::string appended = "ab";
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, appended), data.size());
    CHECK_EQ(appended, "ab" + data);
    std::vector<char> chars;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, chars), data.size());
    CHECK(std::ranges::equal(chars, data));
    std::vector<char> room(data.size() + 64);
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(room)), data.size());
    CHECK(std::ranges::equal(std::span(room).first(data.size()), data));
    std::vector<char> exact(data.size());
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, std::span<char>(exact)), data.size());
    CHECK(std::ranges::equal(exact, data));
    string_writer w;
    REQUIRE_EQ(form<everything>::encode<apart>(sample_everything, w), data.size());
    CHECK_EQ(w.encoded, data);
}

// A message read as the data form and the data form read as a message are refused: the byte after the head of
// the table is the head of the directory in one form and the first entry of the description in the other. A short
// input ends as too_little_data before that byte is read.
TEST_CASE("description apart: one form read as the other is refused")
{
    for (std::string const &inside : {schema_bytes(sample_everything), schema_bytes(empty{}), schema_bytes(eighteen_entries{})}) {
        auto const as_data = form<everything>::path<apart>(std::string(inside));
        REQUIRE_FALSE(as_data.has_value());
    }
    std::string const inside = schema_bytes(sample_everything);
    std::string const data = apart_bytes(sample_everything);
    CHECK_EQ(form<everything>::path<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode<apart>(std::string(inside)).error(), error::incorrect_type);
    CHECK_EQ((form<everything>::at_path<"$.big", apart>(inside)).error(), error::incorrect_type);
    // The data form holds no description, so read as the form with the description inside it is shorter than its
    // table: the doc of schema::path gives too_little_data for that.
    for (auto const e : {form<everything>::path(std::string(data)).error(),
                         form<everything>::decode(std::string(data)).error()})
        CHECK_EQ(e, error::too_little_data);
    std::string const small = apart_bytes(empty{});
    CHECK_EQ(form<empty>::path(std::string(small)).error(), error::too_little_data);
}

// The data form keeps every check of the message: a cut gives too_little_data, a forged directory entry and a
// forged filler give an error in the verified forms.
TEST_CASE("description apart: the checks of the message hold for the data form")
{
    std::string const data = apart_bytes(sample_everything);
    for (std::size_t n = 0; n < data.size(); ++n) {
        CHECK_FALSE(form<everything>::decode<apart>(data.substr(0, n)).has_value());
        CHECK_FALSE(form<everything>::path<apart>(data.substr(0, n)).has_value());
    }
    std::size_t const directory = data.find('\x5a');
    REQUIRE_EQ(directory, 8u);
    std::string entry = data;
    for (std::size_t k = 0; k < sizeof(std::uint32_t); ++k)
        entry[directory + 5 + k] = '\xff';
    CHECK_FALSE(form<everything>::decode<apart>(std::string(entry)).has_value());
    std::uint32_t const length = (std::uint32_t{static_cast<unsigned char>(data[directory + 3])} << 8) |
                                 static_cast<unsigned char>(data[directory + 4]);
    std::string filler = data;
    REQUIRE_EQ(filler[directory + 5 + length], '\xf7');
    filler[directory + 5 + length] = '\xf6';
    CHECK_EQ(form<everything>::path<apart>(std::string(filler)).error(), error::incorrect_type);
}

// The unverified forms read the data form inside its bounds: every changed byte and every cut, and a message read
// as the data form, stay inside the input under AddressSanitizer.
TEST_CASE("description apart: the unverified forms stay inside the input")
{
    std::string const data = apart_bytes(sample_everything);
    auto const reads = [](std::string const &bytes) {
        (void)form<everything>::unverified_at_path<"$.gauges[].at.x", apart>(bytes, 1uz);
        (void)form<everything>::unverified_decode<apart>(std::string(bytes));
        if (auto const root = form<everything>::unverified_path<apart>(std::string(bytes))) {
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
    reads(schema_bytes(sample_everything));
}

// A value variant in the data form: the description apart changes the prefix only; each alternative reads back.
TEST_CASE("description apart: a value variant reads back from the data form")
{
    for (setting const &s : sample_settings()) {
        auto const data = form<setting>::encode<apart>(s);
        REQUIRE(data.has_value());
        auto const back = form<setting>::decode<apart>(std::string(*data));
        REQUIRE(back.has_value());
        CHECK(((*back)->value == s.value));
    }
}

#include "types.hpp"

namespace schema_types
{


template <class T>
using unverified_root = std::remove_cvref_t<decltype(*form<T>::unverified_path(std::string()))>;

// Reads every kind of part through the unverified forms and returns nothing: the sweep below only needs each call
// to return.
template <class = void>
void unverified_reads(std::string const &bytes)
{
    auto const owner = std::make_shared<std::string const>(bytes);
    (void)form<everything>::unverified_at_path<"$.big">(bytes);
    (void)form<everything>::unverified_at_path<"$.engines[1].cc">(bytes);
    (void)form<everything>::unverified_at_path<"$.maybe">(bytes);
    (void)form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz);
    (void)form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz);
    (void)form<everything>::unverified_decode(owner);
    auto const root = form<everything>::unverified_path(owner);
    if (!root)
        return;
    (void)root->at_path<"$.text">();
    if (auto const inner = root->at_path<"$.inner">(); inner && *inner)
        (void)(*inner)->at_path<"@.x">();
    (void)root->at_path<"$.names[1]">();
    if (auto const track = root->view<"$.track">())
        for (double const d : *track)
            (void)d;
    if (auto const owners = root->at_path<"$.owners">())
        (void)owners->size();
    if (auto const gauges = root->at_path<"$.gauges">())
        for (auto const g : *gauges) {
            (void)g.at_path<"@.on">();
            (void)g.at_path<"@.range[1]">();
        }
    if (auto const tires = root->at_path<"$.tires">())
        for (std::size_t i = 0; i < tires->size() + 1; ++i)
            (void)tires->at_path<"@[].diameter">(i);
}

} // namespace schema_types

// Owner decision 2026-10-18: the unverified_ forms read a message from a trusted peer without the checks of its
// data, as FlatBuffers does without its Verifier. On a message that the encoder wrote they give what the verified
// forms give.
TEST_CASE("unverified: the same results as the verified forms on a message that the encoder wrote")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_at_path<"$.big">(bytes), at_path<everything, "$.big">(bytes));
    CHECK_EQ(form<everything>::unverified_at_path<"$.engines[1].cc">(bytes), 1200u);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz), -6);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(form<everything>::unverified_at_path<"$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
    auto const root = form<everything>::unverified_path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->at(1), 2.0);
    auto const gauges = root->at_path<"$.gauges">();
    REQUIRE(gauges.has_value());
    std::vector<int> xs;
    for (auto const g : *gauges)
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(reencoded(form<everything>::unverified_decode(std::string(bytes))), bytes);
    auto const owner = std::make_shared<std::string const>(schema_bytes(sample_vehicle));
    CHECK_EQ(reencoded(form<vehicle>::unverified_decode(owner)), *owner);
}

// The unverified forms skip the checks of the data: a forged class tag, a forged head of a number, a forged tag of
// a typed array and a forged head of a fixed-size vector give a value where the verified forms give incorrect_type.
TEST_CASE("unverified: a forged type byte gives a value, not an error")
{
    std::string const bytes = schema_bytes(sample_everything);
    auto const forged = [&bytes](std::string const &find, std::size_t const at, char const to) {
        std::size_t const where = bytes.rfind(find);
        REQUIRE_NE(where, std::string::npos);
        std::string out = bytes;
        out[where + at] = to;
        return out;
    };
    std::string const root_tag = forged("\xd9\x06\x7c"s, 2, '\x7d');
    CHECK_EQ(at_path<everything, "$.flag">(root_tag).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.flag">(root_tag), true);
    CHECK(form<everything>::unverified_decode(std::string(root_tag)).has_value());
    std::string const track_tag = forged("\xd8\x56\x5a"s, 1, '\x55');
    auto const verified = form<everything>::path(std::string(track_tag));
    REQUIRE(verified.has_value());
    CHECK_EQ(verified->view<"$.track">().error(), error::incorrect_type);
    auto const unverified = form<everything>::unverified_path(std::string(track_tag));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->view<"$.track">()->at(0), 1.0);
    std::string const vector_head = forged("\xd9\x80\x69"s, 1, '\x81');
    CHECK_EQ(at_path<everything, "$.gauges[1].on">(vector_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[1].on">(vector_head), false);
}

// The unverified forms keep every check of a bound: a cut message, an index past the end and a length past the
// end give the same error in both forms.
TEST_CASE("unverified: a bound is checked as in the verified forms")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_path(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_decode(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].diameter">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    std::string longer = bytes;
    std::size_t vectors = 0;
    for (std::size_t head = longer.find("\xd9\x80\x69"s); head != std::string::npos;
         head = longer.find("\xd9\x80\x69"s, head + 1), ++vectors)
        longer.replace(longer.find('\x5a', head) + 1, 4, "\xff\xff\xff\xff"s);
    CHECK_EQ(vectors, 2);
    CHECK_EQ(form<everything>::at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[0].diameter">(longer).error(), error::too_little_data);
}

// Every accessor that an unverified accessor gives is unverified, and every accessor that a verified accessor
// gives is verified, so a read never changes its form in the middle of a path.
TEST_CASE("unverified: the form of an accessor passes to its parts")
{
    using unverified = unverified_root<everything>;
    using verified = form<everything>::accessor<>;
    CHECK(std::same_as<unverified, form<everything>::accessor<everything, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges">())>,
                       form<everything>::accessor<std::vector<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.engines[0]">())>,
                       form<everything>::accessor<engine, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<verified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>>>);
}

// The unverified forms read where the data says, so the test changes every byte of a message that holds every kind
// of part, and cuts it at every length. Each call must return; in the build with AddressSanitizer and
// UndefinedBehaviorSanitizer no read leaves the input.
TEST_CASE("unverified: every changed byte and every cut stays inside the input")
{
    std::string const bytes = schema_bytes(sample_everything);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~bytes[i]), '\x7f', '\x80'}) {
            std::string changed = bytes;
            changed[i] = to;
            unverified_reads(changed);
        }
    for (std::size_t n = 0; n < bytes.size(); ++n)
        unverified_reads(bytes.substr(0, n));
    std::size_t const directory = bytes.find('\x5a');
    REQUIRE_NE(directory, std::string::npos);
    for (std::uint32_t const entry : {0u, 1u, 0x7fffffffu, 0xffffffffu, static_cast<std::uint32_t>(bytes.size())})
        for (std::size_t at = directory + 5; at + 4 <= bytes.size() && at < directory + 5 + 4 * 8; at += 4) {
            std::string changed = bytes;
            for (std::size_t k = 0; k < 4; ++k)
                changed[at + k] = static_cast<char>(entry >> (24 - 8 * k));
            unverified_reads(changed);
        }
    for (std::uint32_t const length : {0u, 3u, 4u, 0xffffffffu}) {
        std::string changed = bytes;
        for (std::size_t k = 0; k < 4; ++k)
            changed[directory + 1 + k] = static_cast<char>(length >> (24 - 8 * k));
        unverified_reads(changed);
    }
}

// A bool in a fixed-size struct is one byte. The verified form refuses a byte other than 0 and 1; the unverified
// form reads any byte other than 0 as true.
TEST_CASE("unverified: a bool byte 2 reads true")
{
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(form<gauges>::unverified_at_path<"$.g[0].on">(two), true);
    auto const back = form<gauges>::unverified_decode(std::string(two));
    REQUIRE(back.has_value());
    CHECK((*back)->g[0].on);
}

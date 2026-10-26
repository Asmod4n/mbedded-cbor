#include "reference.hpp"

#ifdef __cpp_impl_reflection

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <numeric>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/advanced/schema.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

namespace doc_advanced_packed
{

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

struct untagged {
    std::uint8_t a;
};

struct twin {
    std::uint8_t a;
};

struct pair_of_tags {
    tire t;
    twin w;
};

struct [[=cbor::tag(1710)]] [[=cbor::allowlist{}]] chosen {
    [[=cbor::key("id")]] [[=cbor::allow{}]] std::uint8_t number;
    [[=cbor::allow{}]] std::uint8_t kept;
    std::uint8_t secret;
};

struct [[=cbor::tag(1711)]] skipped {
    std::uint8_t kept;
    [[=cbor::skip{}]] std::uint8_t secret;
};

} // namespace doc_advanced_packed

template <>
struct cbor::describe_members<doc_advanced_packed::tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_packed::tire, diameter),
        CBOR_MEMBER(doc_advanced_packed::tire, pressure),
    };
};

template <>
struct cbor::describe_members<doc_advanced_packed::car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1701};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_packed::car, make),
        CBOR_MEMBER(doc_advanced_packed::car, seats),
        CBOR_MEMBER(doc_advanced_packed::car, tires),
        CBOR_MEMBER(doc_advanced_packed::car, track),
        CBOR_MEMBER(doc_advanced_packed::car, note),
    };
};

template <>
struct cbor::describe_members<doc_advanced_packed::twin> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_packed::twin, a),
    };
};

template <>
struct cbor::describe_members<doc_advanced_packed::pair_of_tags> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1702};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_packed::pair_of_tags, t),
        CBOR_MEMBER(doc_advanced_packed::pair_of_tags, w),
    };
};

namespace doc_advanced_packed
{

template <class A>
concept reads_seats = requires(A &&a) { std::forward<A>(a).template at_path<"$.seats">(); };

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt};

template <class T, cbor::fixed_string Path, class... Index>
concept schema_path_compiles = requires(std::string_view const s, Index const... i) {
    cbor::schema<T>::template at_path<Path>(s, i...);
};

// The section "The message" states that a message is one tag 113 of Packed CBOR, and that item_size of a message is
// its size.
TEST_CASE("doc advanced schema: the message")
{
    auto const bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->substr(0, 2), "\xd8\x71"s);
    CHECK_EQ(cbor::item_size(*bytes), bytes->size());
}

// The section "Structs and tags" says that every struct needs its own tag.
TEST_CASE("doc advanced schema: a struct needs a tag of its own")
{
    CHECK(cbor::tags_registered<car>());
    CHECK_FALSE(cbor::tags_registered<untagged>());
    CHECK_FALSE(cbor::tags_registered<pair_of_tags>());
}

// The section "Structs and tags" lists the annotations that choose the keys and the members.
TEST_CASE("doc advanced schema: annotations choose the keys and the members")
{
    auto const a = cbor::schema<chosen>::encode(chosen{7, 8, 9});
    REQUIRE(a.has_value());
    CHECK_NE(a->find("\x62" "id"sv), std::string::npos);
    CHECK_NE(a->find("\x64" "kept"sv), std::string::npos);
    CHECK_EQ(a->find("number"sv), std::string::npos);
    CHECK_EQ(a->find("secret"sv), std::string::npos);
    auto const back = cbor::schema<chosen>::decode(std::string(*a));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->number, 7u);
    CHECK_EQ((*back)->kept, 8u);
    CHECK_EQ((*back)->secret, 0u);
    auto const b = cbor::schema<skipped>::encode(skipped{1, 2});
    REQUIRE(b.has_value());
    CHECK_EQ(b->find("secret"sv), std::string::npos);
    CHECK_EQ(cbor::schema<skipped>::at_path<"$.kept">(*b), 1u);
}

// The section "Encode and decode" shows the round trip of a struct and the encode that appends to a target.
TEST_CASE("doc advanced schema: encode and decode")
{
    auto const bytes = cbor::schema<car>::encode(sample);
    REQUIRE(bytes.has_value());
    auto const back = cbor::schema<car>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->make, "Volvo");
    CHECK_EQ((*back)->tires.size(), 2u);
    CHECK_EQ((*back)->tires[1].diameter, 18u);
    CHECK_FALSE((*back)->note.has_value());
    std::string out = "\x00"s;
    auto const appended = cbor::schema<car>::encode(sample, out);
    REQUIRE(appended.has_value());
    CHECK_EQ(*appended, bytes->size());
    CHECK_EQ(out.substr(1), *bytes);

    auto const owner = std::make_shared<std::string const>(*bytes);
    std::string_view const part(*owner);
    auto const beside = cbor::schema<car>::decode(owner, part);
    REQUIRE(beside.has_value());
    CHECK_EQ((*beside)->seats, 5u);
    car target{};
    auto const into = cbor::schema<car>::decode(owner, part, std::move(target));
    REQUIRE(into.has_value());
    CHECK_EQ((*into)->make, "Volvo");
    auto const shared = cbor::schema<car>::decode(owner);
    REQUIRE(shared.has_value());
    CHECK_EQ((*shared)->track.size(), 3u);
}

// The section "One value with no accessor" shows schema<T>::at_path for a value that holds no view.
TEST_CASE("doc advanced schema: one value with no accessor")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(bytes), 5u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes), 18u);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0uz), 2.5f);
    CHECK_EQ(cbor::schema<car>::at_path<"$.track[2]">(bytes), 3.0);
    CHECK_EQ(cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz).error(), error::index_out_of_bounds);
}

// The section "One value with no accessor" says that a path to a view, to a member that is not trivially copyable,
// to a missing member or with a wrong count of indexes does not compile.
TEST_CASE("doc advanced schema: a wrong path does not compile")
{
    CHECK(schema_path_compiles<car, "$.seats">);
    CHECK_FALSE(schema_path_compiles<car, "$.make">);
    CHECK_FALSE(schema_path_compiles<car, "$.tires">);
    CHECK_FALSE(schema_path_compiles<car, "$.wheels">);
    CHECK_FALSE(schema_path_compiles<car, "$.tires[].diameter">);
    CHECK(schema_path_compiles<car, "$.tires[].diameter", std::size_t>);
}

// The section "The accessor" shows path, the root accessor, and relative paths that start with @.
TEST_CASE("doc advanced schema: the accessor")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    auto const root = cbor::schema<car>::path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    auto const tires = root->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    auto const second = tires->at_path<"@[]">(1uz);
    REQUIRE(second.has_value());
    CHECK_EQ(second->at_path<"@.diameter">(), 18u);
    CHECK_EQ(second->at_path<"$.seats">(), 5u);
    auto const note = root->at_path<"$.note">();
    REQUIRE(note.has_value());
    CHECK_FALSE(note->has_value());
}

// The section "The accessor" says that the accessor holds the owner, so it stays valid after the caller releases the
// bytes, that a caller who wants a copy makes it with std::string, that an empty owner throws and that a temporary
// accessor reads nothing.
TEST_CASE("doc advanced schema: the accessor holds the owner")
{
    auto owner = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
    auto const root = cbor::schema<car>::path(owner);
    owner.reset();
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.make">(), "Volvo"sv);
    std::string temp = *cbor::schema<car>::encode(sample);
    auto const copied = cbor::schema<car>::path(std::string(temp));
    temp.assign(temp.size(), '\0');
    REQUIRE(copied.has_value());
    CHECK_EQ(*copied->at_path<"$.make">(), "Volvo"sv);
    CHECK_THROWS_AS((void)cbor::schema<car>::path(std::shared_ptr<std::string const>{}), std::logic_error);
    auto const held = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
    auto const beside = cbor::schema<car>::path(held, std::string_view(*held));
    REQUIRE(beside.has_value());
    CHECK_EQ(beside->at_path<"$.seats">(), 5u);
    using A = cbor::schema<car>::accessor<>;
    CHECK(reads_seats<A const &>);
    CHECK_FALSE(reads_seats<A>);
}

// A list accessor taken from a temporary root accessor read freed bytes, because only the root held the owner of the
// bytes. The section "The accessor" states that every accessor holds the owner.
TEST_CASE("doc advanced schema: a list accessor keeps the bytes alive")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    auto const tires = cbor::schema<car>::path(std::string(bytes))->at_path<"$.tires">();
    REQUIRE(tires.has_value());
    CHECK_EQ(tires->size(), 2u);
    CHECK_EQ(*tires->at_path<"@[1].diameter">(), 18u);
}

// The section "Typed arrays" shows view, which gives a typed_array_view of a list of a fixed-width number.
TEST_CASE("doc advanced schema: a typed array view")
{
    CHECK((std::ranges::random_access_range<cbor::typed_array_view<double>>));
    CHECK((std::ranges::sized_range<cbor::typed_array_view<double>>));
    auto const root = cbor::schema<car>::path(*cbor::schema<car>::encode(sample));
    REQUIRE(root.has_value());
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->size(), 3u);
    CHECK_EQ(std::accumulate(track->begin(), track->end(), 0.0), 6.0);
    CHECK_EQ((*track)[1], 2.0);
    CHECK_EQ(track->at(3).error(), error::index_out_of_bounds);
    CHECK_THROWS_AS((*track)[3], std::logic_error);
    auto const tail = track->last(2);
    REQUIRE(tail.has_value());
    CHECK_EQ(tail->front(), 2.0);
    CHECK_EQ(track->first(4).error(), error::index_out_of_bounds);
    CHECK_EQ(std::ranges::fold_left(*track, 0.0, std::plus<>{}), 6.0);
}

// The section "Errors" gives the error of a cut message, of a message of another struct and of a forged head.
TEST_CASE("doc advanced schema: errors")
{
    std::string const bytes = *cbor::schema<car>::encode(sample);
    CHECK_EQ(cbor::schema<car>::decode(bytes.substr(0, 3)).error(), error::too_little_data);
    std::string const small = *cbor::schema<tire>::encode(tire{1, 1.0f});
    CHECK_EQ(cbor::schema<car>::decode(std::string(small)).error(), error::too_little_data);
    CHECK_EQ(cbor::schema<tire>::decode(std::string(bytes)).error(), error::incorrect_type);
    CHECK_EQ(cbor::schema<tire>::path(std::string(bytes)).error(), error::incorrect_type);
    std::size_t const seats = bytes.find("\x18\x05"sv);
    REQUIRE_NE(seats, std::string::npos);
    std::string forged = bytes;
    forged[seats] = '\x58';
    CHECK_EQ(cbor::schema<car>::at_path<"$.seats">(forged).error(), error::incorrect_type);
}

} // namespace doc_advanced_packed

#endif

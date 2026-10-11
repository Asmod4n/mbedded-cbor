#include "binding.hpp"

#ifdef __cpp_impl_reflection

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/structs.md. The tests exist so that every example in the document
// compiles and gives the result that the document states.

namespace doc_databind
{

struct person {
    std::string name;
    std::uint8_t age;
    bool operator==(person const &) const = default;
};

enum class level : std::uint8_t { low, high };

struct reading {
    std::optional<double> value;
    std::vector<std::uint16_t> samples;
    std::map<std::string, std::int64_t> counts;
    level state;
    std::variant<std::int64_t, std::string> id;
    std::tuple<bool, std::nullptr_t> pair;
    bool operator==(reading const &) const = default;
};

struct header {
    [[=cbor::key("content-type")]] std::string content_type;
    [[=cbor::skip{}]] std::string cache;
};

struct [[=cbor::allowlist{}]] account {
    [[=cbor::allow{}]] std::string user;
    std::string password;
};

// RFC 8428 6: SenML names the labels n and v with the integers 0 and 2.
struct senml {
    std::string n;
    double v;
    static constexpr std::array<std::int64_t, 2> keys{0, 2};
};

using epoch = cbor::tagged<1, std::int64_t>;


// The section "Encode and decode" shows the round trip of a struct and the bytes that encode writes.
TEST_CASE("doc structs: encode and decode a struct")
{
    auto const bytes = cbor::structs<person>::encode(person{"ann", 30});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, "\xa2\x63"
                     "age\x18\x1e\x64name\x63"
                     "ann"s);
    auto const back = cbor::structs<person>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->name, "ann");
    CHECK_EQ((*back)->age, 30u);
}

// The section "Encode and decode" says that encode with a target appends to the bytes that the target holds.
TEST_CASE("doc structs: encode appends to a target")
{
    std::string out = "\x82"s;
    CHECK_EQ(cbor::structs<std::uint8_t>::encode(1, out), 1u);
    CHECK_EQ(cbor::structs<std::uint8_t>::encode(2, out), 1u);
    CHECK_EQ(out, "\x82\x01\x02"s);
}

// The section "The owner of the bytes" lists the four forms of decode. A view in the result points into the bytes
// of the owner.
TEST_CASE("doc structs: the forms of decode")
{
    std::string const bytes = "\x63"
                              "abc"s;
    auto const copied = cbor::structs<std::string_view>::decode(bytes);
    REQUIRE(copied.has_value());
    CHECK_EQ(**copied, "abc"sv);
    CHECK_NE(static_cast<void const *>((*copied)->data()), static_cast<void const *>(bytes.data() + 1));

    std::string moved = "\x78\x28"s + std::string(40, 'x');
    char const *const data = moved.data();
    auto const taken = cbor::structs<std::string_view>::decode(std::move(moved));
    REQUIRE(taken.has_value());
    CHECK_EQ(static_cast<void const *>((*taken)->data()), static_cast<void const *>(data + 2));

    auto const owner = std::make_shared<std::string const>(bytes);
    auto const held = cbor::structs<std::string_view>::decode(owner, *owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>((*held)->data()), static_cast<void const *>(owner->data() + 1));

    CHECK_THROWS_AS((void)cbor::structs<std::string_view>::decode(std::shared_ptr<void const>{}, bytes),
                    std::logic_error);
}

// The section "Types" gives the CBOR item of each kind of C++ type.
TEST_CASE("doc structs: the item of each type")
{
    reading const r{2.5, {1, 2}, {{"a", -1}}, level::high, "x"s, {true, nullptr}};
    auto const bytes = cbor::structs<reading>::encode(r);
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, "\xa6"
                     "\x62id\x61x"
                     "\x64pair\x82\xf5\xf6"
                     "\x65state\x01"
                     "\x65value\xf9\x41\x00"
                     "\x66"
                     "counts\xa1\x61"
                     "a\x20"
                     "\x67samples\x82\x01\x02"s);
    auto const back = cbor::structs<reading>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK(**back == r);

    CHECK_EQ(*cbor::structs<std::set<std::uint8_t>>::encode({3, 1}), "\x82\x01\x03"s);
    CHECK_EQ(*cbor::structs<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}}), "\x42\x01\x02"s);
    CHECK_EQ(*cbor::structs<epoch>::encode(epoch{1700000000}), "\xc1\x1a\x65\x53\xf1\x00"s);
}

// The section "Optional members" says that an empty optional member has no key, and that a missing optional key
// reads as empty.
TEST_CASE("doc structs: an empty optional member is left out")
{
    reading const r{std::nullopt, {}, {}, level::low, std::int64_t{7}, {false, nullptr}};
    auto const bytes = cbor::structs<reading>::encode(r);
    REQUIRE(bytes.has_value());
    CHECK_EQ(bytes->front(), '\xa5');
    CHECK_EQ(bytes->find("value"), std::string::npos);
    auto const back = cbor::structs<reading>::decode(*bytes);
    REQUIRE(back.has_value());
    CHECK_FALSE((*back)->value.has_value());
}

// The section "Variants" says that decode takes the first alternative that accepts the head of the item.
TEST_CASE("doc structs: a variant takes the first alternative that accepts the item")
{
    using id = std::variant<std::uint8_t, std::int64_t, std::string>;
    auto const index = [](std::string encoded) {
        auto const r = cbor::structs<id>::decode(std::move(encoded));
        return r ? (**r).index() : std::variant_npos;
    };
    CHECK_EQ(index("\x18\x2a"s), 0u);
    CHECK_EQ(index("\x20"s), 1u);
    CHECK_EQ(index("\x61x"s), 2u);
    CHECK_EQ(index("\x19\x01\x00"s), 1u);
    CHECK_EQ(cbor::structs<id>::decode("\xf5"s).error(), error::incorrect_type);
}

// The section "Keys" shows the annotations cbor::key, cbor::skip and cbor::allowlist with cbor::allow, and integer
// keys from a static member keys.
TEST_CASE("doc structs: the key of a member")
{
    CHECK_EQ(*cbor::structs<header>::encode(header{"text/plain", "x"}),
             "\xa1\x6c"
             "content-type\x6a"
             "text/plain"s);
    CHECK_EQ(*cbor::structs<account>::encode(account{"ann", "secret"}),
             "\xa1\x64user\x63"
             "ann"s);
    CHECK_EQ(*cbor::structs<senml>::encode(senml{"t", 1.5}),
             "\xa2\x00\x61t\x02\xf9\x3e\x00"s);
}

// The section "Keys" says what decode does with an unknown key, a repeated key and a missing key.
TEST_CASE("doc structs: unknown, repeated and missing keys")
{
    auto const extra = cbor::structs<person>::decode("\xa3\x64name\x61x\x63"
                                                       "age\x01\x61z\x02"s);
    REQUIRE(extra.has_value());
    CHECK_EQ((*extra)->name, "x");
    auto const twice = cbor::structs<person>::decode("\xa3\x64name\x61x\x63"
                                                       "age\x01\x63"
                                                       "age\x02"s);
    REQUIRE(twice.has_value());
    CHECK_EQ((*twice)->age, 1u);
    CHECK_EQ(cbor::structs<person>::decode("\xa1\x64name\x61x"s).error(), error::key_not_found);
    CHECK_EQ(cbor::structs<person>::decode("\xa3\x64name\x61x\x63"
                                            "age\x01\x80\x00"s)
                 .error(),
             error::unsupported_value);
}

// The section "Errors" gives one input for each error that structs gives.
TEST_CASE("doc structs: errors")
{
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x19\x01\x00"s).error(), error::number_out_of_range);
    CHECK_EQ(cbor::structs<float>::decode("\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a"s).error(),
             error::number_out_of_range);
    CHECK_EQ(cbor::structs<std::string>::decode("\x01"s).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::string>::decode("\x63"
                                                 "ab"s)
                 .error(),
             error::too_little_data);
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x01\x02"s).error(), error::syntax_error);
    CHECK_EQ((cbor::structs<std::array<std::uint8_t, 2>>::decode("\x81\x01"s).error()), error::incorrect_type);
}

// The section "Shared references" says that decode reads tag 28 and tag 29 of the value-sharing registration.
TEST_CASE("doc structs: a shared reference reads as the item it names")
{
    auto const back = cbor::structs<std::vector<std::string>>::decode("\x82\xd8\x1c\x61x\xd8\x1d\x00"s);
    REQUIRE(back.has_value());
    CHECK_EQ(**back, std::vector<std::string>{"x", "x"});
}

// The section "Memory" shows a std::pmr target, and that a refused allocation is the error not_enough_memory.
TEST_CASE("doc structs: a std::pmr target")
{
    using texts = std::pmr::vector<std::pmr::string>;
    auto const owner = std::make_shared<std::string const>(*cbor::structs<std::vector<std::string>>::encode(
        {std::string(40, 'a'), std::string(40, 'b'), std::string(40, 'c')}));
    std::array<std::byte, 64> small{};
    std::pmr::monotonic_buffer_resource budget(small.data(), small.size(), std::pmr::null_memory_resource());
    CHECK_EQ(cbor::structs<texts>::decode(owner, *owner, texts(&budget)).error(), error::not_enough_memory);
    std::array<std::byte, 1024> large{};
    std::pmr::monotonic_buffer_resource enough(large.data(), large.size(), std::pmr::null_memory_resource());
    auto const fits = cbor::structs<texts>::decode(owner, *owner, texts(&enough));
    REQUIRE(fits.has_value());
    CHECK_EQ((*fits)->size(), 3u);
    CHECK_EQ((*fits)->front().get_allocator().resource(), &enough);
}

// The section "Structs and keys" states that structs writes the tag of a struct through cbor::tagged<N, E>, because
// a user who knows the schema form expects [[=cbor::tag(n)]] to do this.
struct tagged_person {
    std::string name;
    std::int64_t age;
};

TEST_CASE("doc structs: the tag of a struct")
{
    auto const bytes = cbor::structs<cbor::tagged<1800, tagged_person>>::encode({{"ann", 30}});
    REQUIRE(bytes.has_value());
    CHECK_EQ(*bytes, "\xd9\x07\x08\xa2\x63"
                     "age\x18\x1e\x64name\x63"
                     "ann"s);
    auto const text = cbor::diagnostic_notation(*bytes);
    REQUIRE(text.has_value());
    CHECK_EQ(*text, R"(1800({"age": 30, "name": "ann"}))");
}

} // namespace doc_databind

#endif

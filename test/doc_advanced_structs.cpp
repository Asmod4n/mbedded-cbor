#include "reference.hpp"

#ifdef __cpp_impl_reflection

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <chrono>
#include <map>
#include <memory>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace std::string_literals;
using namespace std::string_view_literals;
using cbor::error;

// Each test in this file is one example of doc/advanced/structs.md. The tests exist so that every example in the
// document compiles and gives the result that the document states.

namespace doc_advanced_structs
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

class account
{
    std::string password;

public:
    account() = default;
    account(std::string u, std::uint8_t const l, std::string p)
        : password(std::move(p)), user(std::move(u)), level(l)
    {
    }
    std::string user;
    std::uint8_t level = 0;

    std::string const &password_of() const
    {
        return password;
    }
};

// RFC 8428 6: SenML names the labels n and v with the integers 0 and 2.
struct senml {
    std::string n;
    double v;
};

struct tagged_person {
    std::string name;
    std::int64_t age;
};

} // namespace doc_advanced_structs

namespace doc_advanced_structs
{

template <class Part>
concept decodes_beside = requires(std::shared_ptr<void const> const &o, Part p) {
    cbor::structs<std::string_view>::decode(o, std::forward<Part>(p));
};

} // namespace doc_advanced_structs

template <>
struct cbor::describe_members<doc_advanced_structs::account> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160061, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_structs::account, user),
        cbor::member{1, &doc_advanced_structs::account::level},
    };
};

template <>
struct cbor::describe_members<doc_advanced_structs::tagged_person> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1800, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(doc_advanced_structs::tagged_person, name),
        CBOR_MEMBER(doc_advanced_structs::tagged_person, age),
    };
};

template <>
struct cbor::describe_members<doc_advanced_structs::senml> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &doc_advanced_structs::senml::n},
        cbor::member{2, &doc_advanced_structs::senml::v},
    };
};

namespace doc_advanced_structs
{

// The section "Encode into a buffer of your own" says that encode with a target appends to the bytes that the
// target holds.
TEST_CASE("doc advanced structs: encode appends to a target")
{
    std::string out;
    CHECK_EQ(cbor::structs<std::uint8_t>::encode(1, out), 1u);
    CHECK_EQ(cbor::structs<std::uint8_t>::encode(2, out), 1u);
    CHECK_EQ(out, "\x01\x02"s);
}

// The section "Views into the bytes and their owner" lists the forms of decode. A view in the result points into the
// bytes of the owner.
TEST_CASE("doc advanced structs: the forms of decode")
{
    std::string bytes = preferred(V("abc"s));
    auto const copied = cbor::structs<std::string_view>::decode(std::string(bytes));
    REQUIRE(copied.has_value());
    CHECK_EQ(**copied, "abc"sv);
    CHECK_NE(static_cast<void const *>((*copied)->data()), static_cast<void const *>(bytes.data() + 1));

    auto const owner = std::make_shared<std::string const>(bytes);
    auto const held = cbor::structs<std::string_view>::decode(owner);
    REQUIRE(held.has_value());
    CHECK_EQ(static_cast<void const *>((*held)->data()), static_cast<void const *>(owner->data() + 1));

    std::string moved = "\x78\x28"s + std::string(40, 'x');
    char const *const data = moved.data();
    auto const taken = cbor::structs<std::string_view>::decode(std::move(moved));
    REQUIRE(taken.has_value());
    CHECK_EQ(static_cast<void const *>((*taken)->data()), static_cast<void const *>(data + 2));

    CHECK_THROWS_AS((void)cbor::structs<std::string_view>::decode(std::shared_ptr<std::string const>{}),
                    std::logic_error);

    std::string_view const part(*owner);
    auto const beside = cbor::structs<std::string_view>::decode(owner, part);
    REQUIRE(beside.has_value());
    CHECK_EQ(static_cast<void const *>((*beside)->data()), static_cast<void const *>(owner->data() + 1));
    auto const into = cbor::structs<std::string>::decode(owner, part, std::string());
    REQUIRE(into.has_value());
    CHECK_EQ(**into, "abc");
    CHECK_THROWS_AS((void)cbor::structs<std::string_view>::decode(std::shared_ptr<void const>{}, part),
                    std::logic_error);
    CHECK(decodes_beside<std::string_view const &>);
    CHECK_FALSE(decodes_beside<std::string const &>);
}

// The section "Every type" gives the CBOR item of each kind of C++ type.
TEST_CASE("doc advanced structs: the item of each type")
{
    reading const r{2.5, {1, 2}, {{"a", -1}}, level::high, "x"s, {true, nullptr}};
    auto const bytes = cbor::structs<reading>::encode(r);
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*bytes),
             R"({"id": "x", "pair": [true, null], "state": 1, "value": 2.5, "counts": {"a": -1}, "samples": 69(h'01000200')})");
    auto const back = cbor::structs<reading>::decode(std::string(*bytes));
    REQUIRE(back.has_value());
    CHECK(**back == r);

    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<std::set<std::uint8_t>>::encode({3, 1})), "258([1, 3])");
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}})),
             "64(h'0102')");
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<std::chrono::sys_seconds>::encode(
                 std::chrono::sys_seconds{std::chrono::seconds{1700000000}})),
             "1(1700000000)");
    CHECK_EQ(cbor::structs<float>::decode("\xfb\x3f\xb9\x99\x99\x99\x99\x99\x9a"s).error(), error::number_out_of_range);
    CHECK_EQ(cbor::structs<std::vector<std::uint16_t>>::decode(preferred(A(1, 2))).error(), error::incorrect_type);
    CHECK_EQ((cbor::structs<std::array<double, 2>>::decode(preferred(A(1.5, 2.5))).error()), error::incorrect_type);
}

// The section "Every type" says that encode throws for a date that does not exist, because the value is a wrong use
// and no CBOR item stands for it.
TEST_CASE("doc advanced structs: encode of an invalid date throws")
{
    using namespace std::chrono;
    CHECK_THROWS_AS((void)cbor::structs<year_month_day>::encode(year_month_day{year{2026}, month{2}, day{30}}),
                    std::logic_error);
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<year_month_day>::encode(year_month_day{year{1970}, month{1}, day{2}})),
             "100(1)");
}

// The section "Structs and keys" shows a description of a class with a private member and an integer key, and
// a description with cbor::untagged that gives integer keys and no tag.
TEST_CASE("doc advanced structs: the key of a member")
{
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<account>::encode(account{"ann", 2, "secret"})),
             R"(160061({1: 2, "user": "ann"}))");
    CHECK_EQ(cbor::diagnostic_notation(*cbor::structs<senml>::encode(senml{"t", 1.5})), R"({0: "t", 2: 1.5})");
}

// The section "Structs and keys" says what decode does with an unknown key, a repeated key, a missing key and a key
// that is no text and no integer.
TEST_CASE("doc advanced structs: unknown, repeated and missing keys")
{
    auto const extra = cbor::structs<person>::decode(preferred(M("name"s, "x"s, "age"s, 1, "z"s, 2)));
    REQUIRE(extra.has_value());
    CHECK_EQ((*extra)->name, "x");
    CHECK_EQ(cbor::structs<person>::decode(preferred(M("name"s, "x"s, "age"s, 1, "age"s, 2))).error(),
             error::duplicate_key);
    CHECK_EQ(cbor::structs<person>::decode(preferred(M("name"s, "x"s))).error(), error::key_not_found);
    CHECK_EQ(cbor::structs<person>::decode(preferred(M("name"s, "x"s, "age"s, 1, A(), 0))).error(),
             error::unsupported_value);
}

// The section "Tags" states that a struct carries the tag of its description, and that cbor::tagged_item puts a tag on
// content that is not a struct, here COSE_Sign1 of RFC 9052 4.2: tag 18 on an array.
TEST_CASE("doc advanced structs: the tag of a struct")
{
    using cose_sign1 = cbor::tagged_item<18, std::tuple<std::span<std::byte const>, std::map<std::int64_t, std::int64_t>,
                                                        std::span<std::byte const>, std::span<std::byte const>>>;
    auto const sign1 = cbor::structs<cose_sign1>::decode("\xd2\x84\x40\xa0\x40\x40"s);
    REQUIRE(sign1.has_value());
    CHECK_EQ(*cbor::structs<cose_sign1>::encode(**sign1), "\xd2\x84\x40\xa0\x40\x40"s);
    auto const bytes = cbor::structs<tagged_person>::encode({"ann", 30});
    REQUIRE(bytes.has_value());
    CHECK_EQ(cbor::diagnostic_notation(*bytes), R"(1800({"age": 30, "name": "ann"}))");
}

// The section "Variants" says that decode takes the first alternative that accepts the item.
TEST_CASE("doc advanced structs: a variant takes the first alternative that accepts the item")
{
    using id = std::variant<std::uint8_t, std::int64_t, std::string>;
    auto const index = [](std::string encoded) {
        auto const r = cbor::structs<id>::decode(std::move(encoded));
        return r ? (**r).index() : std::variant_npos;
    };
    CHECK_EQ(index(preferred(V(42))), 0u);
    CHECK_EQ(index("\x20"s), 1u);
    CHECK_EQ(index(preferred(V(256))), 1u);
    CHECK_EQ(index(preferred(V("x"s))), 2u);
    CHECK_EQ(cbor::structs<id>::decode(preferred(V(simple{21}))).error(), error::incorrect_type);
    auto const back = cbor::structs<id>::decode(preferred(V(42)));
    REQUIRE(back.has_value());
    CHECK_EQ(std::get<std::uint8_t>(**back), 42u);
}

// The section "Shared values" says that decode accepts a tag 28 on any member, and a tag 29 only where a member can
// share a value; a plain member gives incorrect_type.
TEST_CASE("doc advanced structs: a tag 29 into a plain member is incorrect_type")
{
    auto const marked = cbor::structs<std::vector<std::string>>::decode("\x82\xd8\x1c\x61x\x61y"s);
    REQUIRE(marked.has_value());
    CHECK_EQ(**marked, std::vector<std::string>{"x", "y"});
    CHECK_EQ(cbor::structs<std::vector<std::string>>::decode("\x82\xd8\x1c\x61x\xd8\x1d\x00"s).error(),
             error::incorrect_type);
    CHECK_EQ(cbor::structs<std::vector<std::string>>::decode("\x81\xd8\x1d\x00"s).error(),
             error::sharedref_index_not_marked);
    CHECK_EQ(cbor::structs<person>::decode("\xa3\x61z\xd8\x1c\x01\x64name\xd8\x1d\x00\x63" "age\x01"s).error(),
             error::sharedref_not_complete);
}

// The section "A memory budget" shows a std::pmr target, and that a refused allocation is the error
// not_enough_memory.
TEST_CASE("doc advanced structs: a std::pmr target")
{
    using texts = std::pmr::vector<std::pmr::string>;
    auto const owner = std::make_shared<std::string const>(
        *cbor::structs<std::vector<std::string>>::encode({std::string(40, 'a'), std::string(40, 'b')}));
    std::array<std::byte, 64> small{};
    std::pmr::monotonic_buffer_resource budget(small.data(), small.size(), std::pmr::null_memory_resource());
    CHECK_EQ(cbor::structs<texts>::decode(owner, texts(&budget)).error(), error::not_enough_memory);
    std::array<std::byte, 1024> large{};
    std::pmr::monotonic_buffer_resource enough(large.data(), large.size(), std::pmr::null_memory_resource());
    auto const fits = cbor::structs<texts>::decode(owner, texts(&enough));
    REQUIRE(fits.has_value());
    CHECK_EQ((*fits)->size(), 2u);
    CHECK_EQ((*fits)->front().get_allocator().resource(), &enough);
}

// The section "Errors" gives one input for each error that structs gives.
TEST_CASE("doc advanced structs: errors")
{
    CHECK_EQ(cbor::structs<std::uint8_t>::decode(preferred(V(256))).error(), error::number_out_of_range);
    CHECK_EQ(cbor::structs<std::string>::decode(preferred(V(1))).error(), error::incorrect_type);
    CHECK_EQ((cbor::structs<std::tuple<int, int>>::decode(preferred(A(1))).error()), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::variant<int, std::string>>::decode(preferred(A())).error(), error::incorrect_type);
    CHECK_EQ(cbor::structs<std::string>::decode("\x63" "ab"s).error(), error::too_little_data);
    CHECK_EQ(cbor::structs<std::uint8_t>::decode("\x01\x02"s).error(), error::syntax_error);
}

} // namespace doc_advanced_structs

#endif

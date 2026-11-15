// cbor::schema takes a std::variant as a member of a described class. An alternative is a described class with its own
// tag, a number, bool, an enum, a text or a byte string. The class alternatives name one and the same direct base class.
// The head of the item chooses the alternative, so a wrong set of alternatives does not compile, with a message that
// names the cause. CBOR_TEST_CASE selects the case. Case 1: a list as an alternative. Case 2: an untagged alternative.
// Case 3: two alternatives with one tag. Case 4: alternatives with other base classes. Case 5: std::monostate. Case 6: a
// std::vector of a variant. Case 7: a std::optional of a variant. Case 8: a std::map to a variant. Case 9: a std::array
// of a variant. Case 10: one alternative twice. Case 11: a base class and its child. Case 12: get of a type that is no
// alternative. Case 13: a variant as the root. Case 14: a path into the variant. Case 15: a right use, which compiles.
// Case 16: std::int32_t with std::uint32_t. Case 17: an enum with its underlying type. Case 18: std::string with
// std::string_view. Case 19: a fixed string. Case 20: a nested variant. Case 21: two enums of one underlying type. Case
// 22: cbor::int128 with cbor::uint128. Case 23: char with std::uint8_t. Case 24: long double with std::float128_t (g++).
// Case 25: float with std::bfloat16_t (g++). Case 26: an enum with no fixed underlying type. Case 27: a class, a number
// and a text, which compiles.
#include <cbor/cbor.hpp>

#include <string>
#include <string_view>
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

struct animal {
    std::int32_t legs;
};

struct plant {
    std::int32_t leaves;
};

struct dog : animal {
    std::int32_t bark;
};

struct cat : animal {
    std::int32_t purr;
};

struct rose : plant {
    std::int32_t thorns;
};

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(dog, animal, cbor::tag{1660}, legs, bark);
#if CBOR_TEST_CASE == 2
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(cat, animal, cbor::untagged, legs, purr);
#elif CBOR_TEST_CASE == 3
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(cat, animal, cbor::tag{1660}, legs, purr);
#else
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(cat, animal, cbor::tag{1661}, legs, purr);
#endif
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(rose, plant, cbor::tag{1662}, leaves, thorns);
CBOR_DEFINE_TYPE_MEMBERS(animal, cbor::tag{1663}, legs);

enum class shade : std::uint16_t { dark, light };
enum class mood : std::uint16_t { calm, wild };
enum plain { first, second };

#if CBOR_TEST_CASE == 1
using pet_type = std::variant<dog, std::vector<int>>;
#elif CBOR_TEST_CASE == 16
using pet_type = std::variant<std::int32_t, std::uint32_t>;
#elif CBOR_TEST_CASE == 17
using pet_type = std::variant<shade, std::uint16_t>;
#elif CBOR_TEST_CASE == 18
using pet_type = std::variant<std::string, std::string_view>;
#elif CBOR_TEST_CASE == 19
using pet_type = std::variant<dog, std::array<char, 4>>;
#elif CBOR_TEST_CASE == 20
using pet_type = std::variant<dog, std::variant<std::int32_t, bool>>;
#elif CBOR_TEST_CASE == 21
using pet_type = std::variant<shade, mood>;
#elif CBOR_TEST_CASE == 22
using pet_type = std::variant<cbor::int128, cbor::uint128>;
#elif CBOR_TEST_CASE == 23
using pet_type = std::variant<char, std::uint8_t>;
#elif CBOR_TEST_CASE == 24
using pet_type = std::variant<long double, std::float128_t>;
#elif CBOR_TEST_CASE == 25
using pet_type = std::variant<float, std::bfloat16_t>;
#elif CBOR_TEST_CASE == 26
using pet_type = std::variant<dog, plain>;
#elif CBOR_TEST_CASE == 27
using pet_type = std::variant<dog, std::int64_t, std::string>;
#elif CBOR_TEST_CASE == 4
using pet_type = std::variant<dog, rose>;
#elif CBOR_TEST_CASE == 5
using pet_type = std::variant<std::monostate, dog, cat>;
#elif CBOR_TEST_CASE == 6
using pet_type = std::vector<std::variant<dog, cat>>;
#elif CBOR_TEST_CASE == 7
using pet_type = std::optional<std::variant<dog, cat>>;
#elif CBOR_TEST_CASE == 8
using pet_type = std::map<std::int32_t, std::variant<dog, cat>>;
#elif CBOR_TEST_CASE == 9
using pet_type = std::array<std::variant<dog, cat>, 2>;
#elif CBOR_TEST_CASE == 10
using pet_type = std::variant<dog, dog>;
#elif CBOR_TEST_CASE == 11
using pet_type = std::variant<animal, dog>;
#else
using pet_type = std::variant<dog, cat>;
#endif

struct home {
    pet_type pet;
};

CBOR_DEFINE_TYPE_MEMBERS(home, cbor::tag{1664}, pet);

int main()
{
#if CBOR_TEST_CASE == 13
    std::variant<dog, cat> const pet{};
    return cbor::schema<std::variant<dog, cat>>::encode(pet).has_value() ? 0 : 1;
#else
    home const value{};
    auto const bytes = cbor::schema<home>::encode(value);
    if (!bytes)
        return 1;
    auto const doc = cbor::schema<home>::adopt(std::string(*bytes));
    if (!doc)
        return 1;
#if CBOR_TEST_CASE == 12
    return doc->at_path<"$.pet">()->get<rose>().has_value() ? 0 : 1;
#elif CBOR_TEST_CASE == 14
    return doc->at_path<"$.pet.legs">().has_value() ? 0 : 1;
#elif CBOR_TEST_CASE == 15
    return doc->at_path<"$.pet">()->get<cat>().has_value() ? 0 : 1;
#elif CBOR_TEST_CASE == 27
    return doc->at_path<"$.pet">()->get<std::int64_t>().has_value() ? 0 : 1;
#else
    return 0;
#endif
#endif
}

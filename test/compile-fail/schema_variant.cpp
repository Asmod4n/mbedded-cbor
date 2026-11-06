// cbor::schema takes a std::variant as a member of a described class. Each alternative is a described class with
// its own tag, and all alternatives name one and the same direct base class. The tag of the record chooses the
// alternative, so a wrong set of alternatives does not compile, with a message that names the cause.
// CBOR_TEST_CASE selects the case: 1 an alternative with no description, 2 an untagged alternative, 3 two
// alternatives with one tag, 4 alternatives with other base classes, 5 std::monostate, 6 a std::vector of a
// variant, 7 a std::optional of a variant, 8 a std::map to a variant, 9 a std::array of a variant, 10 one
// alternative twice, 11 a base class and its child, 12 get of a type that is no alternative, 13 a variant as the
// root, 14 a path into the variant, 15 a right use, which compiles.
#include <cbor/cbor.hpp>

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

#if CBOR_TEST_CASE == 1
using pet_type = std::variant<dog, std::int32_t>;
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
    auto const doc = cbor::schema<home>::path(std::string(*bytes));
    if (!doc)
        return 1;
#if CBOR_TEST_CASE == 12
    return doc->at_path<"$.pet">()->get<rose>().has_value() ? 0 : 1;
#elif CBOR_TEST_CASE == 14
    return doc->at_path<"$.pet.legs">().has_value() ? 0 : 1;
#elif CBOR_TEST_CASE == 15
    return doc->at_path<"$.pet">()->get<cat>().has_value() ? 0 : 1;
#else
    return 0;
#endif
#endif
}

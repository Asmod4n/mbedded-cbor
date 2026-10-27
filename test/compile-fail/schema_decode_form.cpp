// cbor::schema decodes a class through its default constructor, or through a constructor that takes the described
// members in the order of the description. It takes the members of the class itself. A wrong use does not compile,
// with a message that names the cause. CBOR_TEST_CASE selects the case: 1 a class whose constructor takes other
// arguments, 2 a description that lists a member of a base class, 3 an aggregate whose member has no default
// constructor, 4 a right class with a constructor, which compiles. CBOR_TEST_FORM names the form, schema or packed.
#include <cbor/cbor.hpp>

#include <cstdint>
#include <string>

#ifndef CBOR_TEST_FORM
#define CBOR_TEST_FORM schema
#endif

class reading
{
public:
#if CBOR_TEST_CASE == 1
    explicit reading(std::string u) : unit(std::move(u))
    {
    }
#else
    reading(std::uint16_t const v, std::string u) : value(v), unit(std::move(u))
    {
    }
#endif

    std::uint16_t value = 0;
    std::string unit;
};

template <>
struct cbor::describe_members<reading> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1680};
    static constexpr std::tuple members{
        CBOR_MEMBER(reading, value),
        CBOR_MEMBER(reading, unit),
    };
};

struct base {
    std::int32_t b;
};

#if CBOR_TEST_CASE == 2
struct root : base {
    std::int32_t a;
};
#elif CBOR_TEST_CASE == 3
struct root {
    std::int32_t a;
    reading r;
};
#else
struct root {
    std::int32_t a;
    std::vector<reading> r;
};
#endif

template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1681};
#if CBOR_TEST_CASE == 2
    static constexpr std::tuple members{
        CBOR_MEMBER(root, a),
        cbor::member{cbor::map_key("b"), &root::b},
    };
#else
    static constexpr std::tuple members{
        CBOR_MEMBER(root, a),
        CBOR_MEMBER(root, r),
    };
#endif
};

int main()
{
    return cbor::CBOR_TEST_FORM<root>::fixed_size() > 0 ? 0 : 1;
}

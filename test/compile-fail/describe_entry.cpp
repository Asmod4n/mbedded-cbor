// An entry of a description names a public non-static data member of the type or of a public base. Every
// other kind of name must not compile. CBOR_TEST_CASE selects the case: 1 a private member, 2 a member of a
// protected base, 3 a member function, 4 a static data member, 5 a bit-field, 6 a reference member, 7 a
// member of another class. Case 8 has no tag, because a description without one would read as untagged on one
// build and fail on another.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

struct hidden_base {
    int inherited;
};

struct kinds : protected hidden_base {
    explicit kinds(int &r) : bits(0), reference(r)
    {
    }
    int bits : 3;
    int &reference;
    static int shared;
    int function() const
    {
        return own;
    }

private:
    int own = 0;
};

struct plain {
    int a;
};

struct other {
    int x;
};

#if CBOR_TEST_CASE == 1
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, own),
    };
};
#elif CBOR_TEST_CASE == 2
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, inherited),
    };
};
#elif CBOR_TEST_CASE == 3
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, function),
    };
};
#elif CBOR_TEST_CASE == 4
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, shared),
    };
};
#elif CBOR_TEST_CASE == 5
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, bits),
    };
};
#elif CBOR_TEST_CASE == 6
template <>
struct cbor::describe_members<kinds> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        CBOR_MEMBER(kinds, reference),
    };
};
#elif CBOR_TEST_CASE == 7
template <>
struct cbor::describe_members<plain> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001, 0};
    static constexpr std::tuple members{
        cbor::member{"x", &other::x},
    };
};
#elif CBOR_TEST_CASE == 8
template <>
struct cbor::describe_members<plain> {
    static constexpr std::tuple members{
        CBOR_MEMBER(plain, a),
    };
};
#endif

#if CBOR_TEST_CASE >= 7
using described = plain;
#else
using described = kinds;
#endif

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(described const &)>(&cbor::structs<described>::encode);
}

// cbor::schema takes each struct of its table only through cbor::describe_members<T>, so that the bytes come from
// one list of members, keys and tags on every build. A wrong description does not compile, with a message that
// names the cause. CBOR_TEST_CASE selects the case: 1 a root with no description, 2 an inner struct with no
// description, 3 a root with cbor::untagged, 4 two structs with one tag, 5 two members with one key, 6 a public
// member that the description leaves out, 7 a class with a base class that the description does not name, 8
// cbor::tag on a described class, 9 cbor::key on a member of a described class, 10 a right description, which
// compiles, 11 a class with a base class that CBOR_DEFINE_DERIVED_TYPE_MEMBERS names, which compiles, 12 a class
// with a named base class whose description leaves out the member of the base, which reflection finds.
#include <cbor/cbor.hpp>

#include <cstdint>

struct inner {
    std::int32_t m;
};

#if CBOR_TEST_CASE != 2
template <>
struct cbor::describe_members<inner> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1651};
    static constexpr std::tuple members{
        CBOR_MEMBER(inner, m),
    };
};
#endif

struct parent {
    std::int32_t b;
};

#if CBOR_TEST_CASE == 7 || CBOR_TEST_CASE == 11 || CBOR_TEST_CASE == 12
struct root : parent {
    std::int32_t a;
    inner i;
};
#elif CBOR_TEST_CASE == 8
struct [[=cbor::tag(1650)]] root {
    std::int32_t a;
    inner i;
};
#elif CBOR_TEST_CASE == 9
struct root {
    [[=cbor::key("x")]] std::int32_t a;
    inner i;
};
#else
struct root {
    std::int32_t a;
    inner i;
};
#endif

#if CBOR_TEST_CASE == 3
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        CBOR_MEMBER(root, a),
        CBOR_MEMBER(root, i),
    };
};
#elif CBOR_TEST_CASE == 4
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1651};
    static constexpr std::tuple members{
        CBOR_MEMBER(root, a),
        CBOR_MEMBER(root, i),
    };
};
#elif CBOR_TEST_CASE == 5
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1650};
    static constexpr std::tuple members{
        cbor::member{"i", &root::a},
        CBOR_MEMBER(root, i),
    };
};
#elif CBOR_TEST_CASE == 6
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1650};
    static constexpr std::tuple members{
        CBOR_MEMBER(root, i),
    };
};
#elif CBOR_TEST_CASE == 7
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1650};
    static constexpr std::tuple members{
        CBOR_MEMBER(root, b),
        CBOR_MEMBER(root, a),
        CBOR_MEMBER(root, i),
    };
};
#elif CBOR_TEST_CASE == 11
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(root, parent, cbor::tag{1650}, b, a, i);
#elif CBOR_TEST_CASE == 12
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(root, parent, cbor::tag{1650}, a, i);
#elif CBOR_TEST_CASE != 1
template <>
struct cbor::describe_members<root> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1650};
    static constexpr std::tuple members{
        CBOR_MEMBER(root, a),
        CBOR_MEMBER(root, i),
    };
};
#endif

int main()
{
    root const value{};
    return cbor::schema<root>::encode(value).has_value() ? 0 : 1;
}

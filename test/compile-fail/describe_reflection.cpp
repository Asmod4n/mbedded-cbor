// On a build with reflection, a description must list exactly the public members that reflection reads with
// unprivileged(), and a described type takes no annotation: the description is the only list of members, keys and
// tag. CBOR_TEST_CASE selects the case: 1 one member left out, 2 cbor::key, 3 cbor::tag.
#include <cbor/cbor.hpp>

#include <expected>
#include <string>

#if CBOR_TEST_CASE == 1
struct t {
    int a;
    int b;
};
#elif CBOR_TEST_CASE == 2
struct t {
    [[=cbor::key("x")]] int a;
    int b;
};
#elif CBOR_TEST_CASE == 3
struct [[=cbor::tag(160001)]] t {
    int a;
    int b;
};
#endif

#if CBOR_TEST_CASE == 1
template <>
struct cbor::describe_members<t> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(t, a),
    };
};
#else
template <>
struct cbor::describe_members<t> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(t, a),
        CBOR_MEMBER(t, b),
    };
};
#endif

int main()
{
    [[maybe_unused]] auto const encode =
        static_cast<std::expected<std::string, cbor::error> (*)(t const &)>(&cbor::structs<t>::encode);
}

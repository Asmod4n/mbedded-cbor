// cbor::structs reads a type with no description only when it is an aggregate with no base and no annotation,
// so that its bytes are the same on every build. CBOR_TEST_CASE selects the case: 1 a class with a
// constructor, 2 a class with a private member, 3 an aggregate with a base, 4 cbor::key, 5 cbor::skip, 6
// cbor::allow, 7 cbor::allowlist, 8 cbor::tag on an aggregate, 9 decode into a default-constructed object of
// a type with no default constructor.
#include <cbor/cbor.hpp>

#include <expected>
#include <memory>
#include <string>

struct b {
    int x;
};

#if CBOR_TEST_CASE == 1
struct t {
    t(int const v) : a(v)
    {
    }
    int a;
};
#elif CBOR_TEST_CASE == 2
class t
{
    int a = 0;

public:
    int a_of() const
    {
        return a;
    }
};
#elif CBOR_TEST_CASE == 3
struct t : b {
    int a;
};
#elif CBOR_TEST_CASE == 4
struct t {
    [[=cbor::key("x")]] int a;
};
#elif CBOR_TEST_CASE == 5
struct t {
    [[=cbor::skip{}]] int a;
};
#elif CBOR_TEST_CASE == 6
struct t {
    [[=cbor::allow{}]] int a;
};
#elif CBOR_TEST_CASE == 7
struct [[=cbor::allowlist{}]] t {
    int a;
};
#elif CBOR_TEST_CASE == 8
struct [[=cbor::tag(160001)]] t {
    int a;
};
#elif CBOR_TEST_CASE == 9
struct t {
    explicit t(int const v) : a(v)
    {
    }
    int a;
};
template <>
struct cbor::describe_members<t> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160001};
    static constexpr std::tuple members{
        CBOR_MEMBER(t, a),
    };
};
#endif

int main()
{
#if CBOR_TEST_CASE == 9
    auto const bytes = std::make_shared<std::string const>("\xda\x00\x02\x71\x01\xa0");
    return cbor::structs<t>::decode(bytes).has_value();
#else
    [[maybe_unused]] auto const encode =
        static_cast<cbor::result<std::string> (*)(t const &)>(&cbor::structs<t>::encode);
#endif
}

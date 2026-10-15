// On a build with reflection, a description must list exactly the public members that reflection reads with
// unprivileged(), and a described type takes no annotation: the macro is the only list of members, keys and
// tag. CBOR_TEST_CASE selects the case: 1 one member left out, 2 cbor::key, 3 cbor::tag, 4 cbor::skip, 5
// cbor::allow, 6 cbor::allowlist.
#include <cbor/cbor.hpp>

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
#elif CBOR_TEST_CASE == 4
struct t {
    [[=cbor::skip{}]] int a;
    int b;
};
#elif CBOR_TEST_CASE == 5
struct t {
    [[=cbor::allow{}]] int a;
    int b;
};
#elif CBOR_TEST_CASE == 6
struct [[=cbor::allowlist{}]] t {
    int a;
    int b;
};
#endif

#if CBOR_TEST_CASE == 1
CBOR_DESCRIBE_STRUCT(t, (160001, 0), (a))
#else
CBOR_DESCRIBE_STRUCT(t, (160001, 0), (a), (b))
#endif

int main()
{
}

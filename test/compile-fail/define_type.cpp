// CBOR_DEFINE_TYPE and CBOR_MEMBER are macros, so a wrong use shows as an error of the preprocessor or of the
// specialization they write. CBOR_TEST_CASE selects the case: 1 CBOR_MEMBER with a type whose name holds a comma,
// 2 CBOR_DEFINE_TYPE inside a namespace, 3 a right use, which compiles.
#include <cbor/cbor.hpp>

#include <cstdint>

namespace ns
{

template <class A, class B>
struct two {
    A a;
    B b;
};

using two_ints = two<int, int>;

#if CBOR_TEST_CASE == 2
CBOR_DEFINE_TYPE(two_ints, cbor::tag{160110}, CBOR_MEMBER(two_ints, a), CBOR_MEMBER(two_ints, b));
#endif

} // namespace ns

#if CBOR_TEST_CASE == 1
CBOR_DEFINE_TYPE(ns::two_ints, cbor::tag{160110}, CBOR_MEMBER(ns::two<int, int>, a), CBOR_MEMBER(ns::two_ints, b));
#elif CBOR_TEST_CASE == 3
CBOR_DEFINE_TYPE(ns::two_ints, cbor::tag{160110}, CBOR_MEMBER(ns::two_ints, a), CBOR_MEMBER(ns::two_ints, b));
#endif

int main()
{
    return cbor::packed<ns::two_ints>::encode(ns::two_ints{1, 2}).has_value() ? 0 : 1;
}

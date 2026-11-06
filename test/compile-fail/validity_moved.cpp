// The checks of the library stand in cbor::detail::validity: they are reachable, and the namespace says that they
// can change in any release. The old name cbor::validity must not compile, so that no code keeps it unseen.
#include <cbor/cbor.hpp>

int main()
{
    return cbor::validity::checked_add(1u, 2u).has_value() ? 0 : 1;
}

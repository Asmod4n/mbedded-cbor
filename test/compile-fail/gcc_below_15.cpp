// g++ 14 and older destroy a temporary in the range of a range-for before the loop reads it (C++23 P2718 came with
// GCC 15), and the documented loop `for (x : *l.elements())` reads freed memory there. The library must refuse such a
// compiler with a message, not compile into a read of dead memory. The test passes the version of g++ 14.
#include <cbor/cbor.hpp>

int main()
{
}

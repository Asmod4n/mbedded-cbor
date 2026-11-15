// An encoder over a cbor::iov_writer gives pieces, and finish gives a count. A finish there would end the encoder and
// lose the pieces, so it must not compile; finish_iov gives them. The test that builds this file expects the build to
// fail.
#include <cbor/encode.hpp>

int main()
{
    cbor::iov_writer w;
    cbor::encoder e(w);
    return e.finish().has_value() ? 0 : 1;
}

// The owner decided on 2026-11-01 that encoding::encoder is no part of the interface. A program writes
// through cbor::encoder, cbor::encode or cbor::encode_iov, so a use of encoding::encoder must not compile.
#include <cbor/cbor.hpp>

int main()
{
    cbor::iov_writer writer;
    cbor::encoding::encoder<cbor::iov_writer> out{writer};
    static_cast<void>(out);
}

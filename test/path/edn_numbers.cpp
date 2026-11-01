#include "edn.hpp"

#include <string_view>

// draft-ietf-cbor-edn-literals-28 5.1, note on decnumber: a decimal float is adjusted to the precision of a binary64
// when its range fits one. With std::from_chars a path compiled at run time takes every such literal, rounded to the
// nearest binary64 (IEEE 754); without it the library converts only a decimal that is exact in a binary64. RFC 8949
// Appendix A writes these three floats with more digits than an exact conversion takes.
TEST_CASE("path edn numbers: the long decimal floats of RFC 8949 Appendix A")
{
    if (from_chars_reads_double) {
        selects("3.4028234663852886e+38", "fa7f7fffff");
        selects("1.0e+300", "fb7e37e43c8800759c");
        selects("5.960464477539063e-8", "f90001");
    } else {
        refused("3.4028234663852886e+38");
        refused("1.0e+300");
        refused("5.960464477539063e-8");
    }
}

// IEEE 754 round to nearest: 0.1 is the binary64 3fb999999999999a on both paths, as its digits are exact enough;
// 25 digits that differ from 1 in the 25th round to 1.0, which preferred serialization writes as a half float.
// 5e-324 and 2.5e-324 round to the smallest subnormal binary64.
TEST_CASE("path edn numbers: decimal floats rounded to the nearest binary64")
{
    selects("0.1", "fb3fb999999999999a");
    if (from_chars_reads_double) {
        selects("1.000000000000000000000001", "f93c00");
        selects("5e-324", "fb0000000000000001");
        selects("2.5e-324", "fb0000000000000001");
    } else {
        refused("1.000000000000000000000001");
        refused("5e-324");
        refused("2.5e-324");
    }
}

// 5.1, note on decnumber: the range needs to fit a binary64. A value beyond the largest finite binary64, or below
// half the smallest subnormal, which would round to zero, is no literal on either path.
TEST_CASE("path edn numbers: decimal floats outside the range of a binary64")
{
    for (std::string_view const l : {"1e400"sv, "-1e400"sv, "2e-324"sv, "1e-400"sv})
        refused(l);
}

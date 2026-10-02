#include "host.hpp"

#if __cpp_impl_reflection

#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
{

struct sample {
    std::string name;
    double score;
    std::uint32_t id;
    bool ok;
};

struct outer {
    std::vector<std::int64_t> values;
    sample inner;
};

template <class T>
std::string struct_bytes(T const &value)
{
    string_writer w;
    cbor::encoder<string_writer> e{w};
    auto const r = e.struct_encode(value);
    REQUIRE(r.has_value());
    REQUIRE(e.flush().has_value());
    return w.bytes;
}

} // namespace

// RFC 8949 4.2.1 orders the keys by their encoded bytes: a shorter text key first, then bytewise. The members
// are declared in another order, so the test shows that the declaration order does not reach the wire.
TEST_CASE("struct_encode: keys in the order of core deterministic encoding")
{
    CHECK_EQ(struct_bytes(sample{"ab", 1.5, 7, true}),
             "\xa4"
             "\x62id\x07"
             "\x62ok\xf5"
             "\x64name\x62"
             "ab"
             "\x65score\xf9\x3e\x00"sv);
}

// A member that is a range is an array, and a member that is a struct is a map. RFC 8949 3.1 encodes a
// negative integer n as the argument -1 - n, and the smallest int64 is the edge where that must not overflow.
TEST_CASE("struct_encode: nested struct, array and the smallest int64")
{
    CHECK_EQ(struct_bytes(outer{{0, -1, INT64_MIN}, {"", 0.0, 0, false}}),
             "\xa2"
             "\x65inner\xa4\x62id\x00\x62ok\xf4\x64name\x60\x65score\xf9\x00\x00"
             "\x66values\x83\x00\x20\x3b\x7f\xff\xff\xff\xff\xff\xff\xff"sv);
}

#endif

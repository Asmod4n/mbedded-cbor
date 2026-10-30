// A schema struct must not take a tag that a standard type has (RFC 8949 3.4: 0, 1, 2, 3 and 24; RFC 8746: 64 to
// 87; RFC 8943: 100 and 1004; CBOR_SETS.md: 258), because a decoder reads that tag as the standard type and finds a
// record where it expects that content. Nor may it take a tag that the IANA registry calls always invalid. The same
// holds for an inner struct, so case 1 tags the root and case 2 tags the inner struct. CBOR_TEST_TAGS is a whole
// set, so one run of the compiler checks every member of it: each tag is a type of its own, and each type gives one
// error. A set of neighbours compiles.
#include <cbor/cbor.hpp>

#include <cstdint>

#if CBOR_TEST_CASE == 1
template <std::uint64_t N>
struct root {
    std::int32_t m;
};

template <std::uint64_t N>
struct cbor::describe_members<root<N>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{N};
    static constexpr std::tuple members{
        CBOR_MEMBER(root<N>, m),
    };
};
#else
template <std::uint64_t N>
struct inner {
    std::int32_t m;
};

template <std::uint64_t N>
struct root {
    inner<N> i;
};

template <std::uint64_t N>
struct cbor::describe_members<inner<N>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{N};
    static constexpr std::tuple members{
        CBOR_MEMBER(inner<N>, m),
    };
};

template <std::uint64_t N>
struct cbor::describe_members<root<N>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1500};
    static constexpr std::tuple members{
        CBOR_MEMBER(root<N>, i),
    };
};
#endif

template <std::uint64_t... N>
int encode_all()
{
    return (0 + ... + (cbor::schema<root<N>>::encode(root<N>{}).has_value() ? 0 : 1));
}

int main()
{
    return encode_all<CBOR_TEST_TAGS>();
}

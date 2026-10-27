// A description must not take a tag that a standard type has (RFC 8949 3.4: 0, 1, 2, 3 and 24; RFC 8746: 64 to 87;
// RFC 8943: 100 and 1004; CBOR_SETS.md: 258), because decode would read that tag as the standard type in one place
// and as the struct in another. Nor may it take a tag that the IANA registry calls always invalid
// (refs/cbor-tags.xml: 65535, 4294967295 and 18446744073709551615; refs/draft-bormann-cbor-notable-tags.md: 21334).
// CBOR_TEST_TAGS is a whole set, so one run of the compiler checks every member of it: each tag is a type of its
// own, and each type gives one error. A set of neighbours compiles.
#include <cbor/cbor.hpp>

#include <cstdint>
#include <expected>
#include <string>

template <std::uint64_t N>
struct t {
    int m;
};

template <std::uint64_t N>
struct cbor::describe_members<t<N>> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{N};
    static constexpr std::tuple members{
        CBOR_MEMBER(t<N>, m),
    };
};

template <std::uint64_t... N>
void encode_all()
{
    ([] {
        [[maybe_unused]] auto const encode =
            static_cast<std::expected<std::string, cbor::error> (*)(t<N> const &)>(&cbor::structs<t<N>>::encode);
    }(),
     ...);
}

int main()
{
    encode_all<CBOR_TEST_TAGS>();
}

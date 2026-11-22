#include <doctest/doctest.h>

#include <cbor/cbor.hpp>

#include <string>
#include <string_view>

#ifdef CBOR_HAS_INT128

namespace
{

// RFC 8949 3.1: major type 1 holds -1 minus an argument of up to 2^64 - 1, so the smallest integer is -2^64 and the
// largest of major type 0 is 2^64 - 1. Only a type of more than 64 bits holds both.
constexpr cbor::uint128 largest_unsigned = (cbor::uint128{1} << 64) - 1;
constexpr cbor::int128 smallest_negative = -(cbor::int128{1} << 64);

std::string bytes_of(std::string_view const edn)
{
    auto const tree = cbor::parse_edn(edn);
    REQUIRE(tree.has_value());
    std::string out;
    REQUIRE(cbor::encode(out, **tree).has_value());
    return out;
}

}

// The test file builds without GNU extensions too, where std::integral<__int128> can be false. The library names
// cbor::int128 and cbor::uint128 itself, so get<T> of both compiles in every mode.
TEST_CASE("int128: item get reads -2^64 and 2^64 - 1")
{
    auto const tree = cbor::parse_edn("[-18446744073709551616, 18446744073709551615, -1]");
    REQUIRE(tree.has_value());
    auto const elements = (*tree)->elements();
    REQUIRE(elements.has_value());
    auto it = elements->begin();
    CHECK((*it).get<cbor::int128>() == smallest_negative);
    ++it;
    CHECK((*it).get<cbor::int128>() == cbor::int128{largest_unsigned});
    CHECK((*it).get<cbor::uint128>() == largest_unsigned);
    ++it;
    auto const it_number = (*it).get<cbor::int128>();
    CHECK(it_number.value() == -1);
    auto const number = (*it).get<cbor::uint128>();
    CHECK_EQ(number.error(), cbor::error::number_out_of_range);
}

TEST_CASE("int128: lazy get reads -2^64 and 2^64 - 1")
{
    auto const doc = cbor::lazy::adopt(bytes_of("[-18446744073709551616, 18446744073709551615, -1]"));
    REQUIRE(doc.has_value());
    auto const at_0 = doc->at(0);
    auto const read = at_0->get<cbor::int128>();
    CHECK(read.value() == smallest_negative);
    auto const at_1 = doc->at(1);
    auto const read_2 = at_1->get<cbor::uint128>();
    CHECK(read_2.value() == largest_unsigned);
    auto const at_2 = doc->at(2);
    auto const read_3 = at_2->get<cbor::uint128>();
    CHECK_EQ(read_3.error(), cbor::error::number_out_of_range);
}

// RFC 8949 3.4.3: a bignum of at most 64 bits is the same integer as a head; one above 64 bits is not read as an
// integer by get, so it gives number_out_of_range on item and on lazy.
TEST_CASE("int128: a bignum above 64 bits is out of range, one of at most 64 bits is read")
{
    std::string_view const edn = "[2(h'010000000000000000'), 2(h'01')]";
    auto const tree = cbor::parse_edn(edn);
    REQUIRE(tree.has_value());
    auto const tree_elements = (*tree)->elements();
    auto const number = tree_elements->front().get<cbor::int128>();
    CHECK_EQ(number.error(), cbor::error::number_out_of_range);
    auto const doc = cbor::lazy::adopt(bytes_of(edn));
    REQUIRE(doc.has_value());
    auto const at_0 = doc->at(0);
    auto const read = at_0->get<cbor::int128>();
    CHECK_EQ(read.error(), cbor::error::number_out_of_range);
    auto const at_1 = doc->at(1);
    auto const read_2 = at_1->get<cbor::int128>();
    CHECK(read_2.value() == 1);
}

TEST_CASE("int128: at_path reads -2^64")
{
    std::string const bytes = bytes_of(R"({"n": -18446744073709551616})");
    auto const n = cbor::at_path<"$.n", cbor::int128>(std::string_view(bytes));
    REQUIRE(n.has_value());
    CHECK(*n == smallest_negative);
}

#endif

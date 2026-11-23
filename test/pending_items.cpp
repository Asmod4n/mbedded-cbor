#include "owned_copy.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/item_size.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>
#include <cbor/sequence_reader.hpp>
#include <cbor/structs.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <doctest/doctest.h>

using namespace std::string_view_literals;
using cbor::error;

namespace
{

std::string encode_edn(std::string_view const edn)
{
    auto bytes = cbor::diag2cbor(edn);
    REQUIRE(bytes.has_value());
    return std::move(*bytes);
}

struct one_int {
    int a;
};

} // namespace

// RFC 8949 3 and Appendix C: an array or a map ends after the data items that its head encloses. An item that ends
// before those items is not well-formed, and a reader that waits for more bytes calls it too_little_data. Each reader
// checks the count of a head against the bytes left in its own walk. So the test asks every reader the same question
// and expects the same answer. The array claims 3 items and holds 1 byte. The map claims 4 items and holds 2 bytes.
TEST_CASE("pending items: every reader refuses a container that claims more items than its bytes hold")
{
    std::string const array = encode_edn("[1, 2, 3]").substr(0, 2);
    std::string const map = encode_edn("{1: 2, 3: 4}").substr(0, 3);
    for (std::string const &cut : {array, map}) {
        CAPTURE(cut);
        auto const length = cbor::item_size(cut);
        CHECK_EQ(length.error(), error::too_little_data);
        auto const tree = cbor::decode(cut);
        CHECK_EQ(tree.error(), error::too_little_data);
        auto const doc = test::make_lazy_from_copy(cut);
        REQUIRE(doc.has_value());
        auto const count = doc->size();
        CHECK_EQ(count.error(), error::too_little_data);
    }
    auto const a = test::make_lazy_from_copy(array);
    REQUIRE(a.has_value());
    CHECK_EQ(a->elements(), std::unexpected(error::too_little_data));
    auto const at_0 = a->at(0);
    CHECK_EQ(at_0.error(), error::too_little_data);
    auto const m = test::make_lazy_from_copy(map);
    REQUIRE(m.has_value());
    CHECK_EQ(m->entries(), std::unexpected(error::too_little_data));
    auto const at_1 = m->at(1);
    CHECK_EQ(at_1.error(), error::too_little_data);
    auto const first = cbor::path::compile<std::int64_t>("$[0]");
    REQUIRE(first.has_value());
    CHECK_EQ(cbor::at_path(std::string_view(array), *first), std::unexpected(error::too_little_data));
    auto const second = cbor::path::compile<std::int64_t>("$[1]");
    REQUIRE(second.has_value());
    CHECK_EQ(cbor::at_path(std::string_view(map), *second), std::unexpected(error::too_little_data));
}

// RFC 8742 2: a reader of a sequence waits until the bytes of an item are there. A head that claims more items than
// the bytes hold is such a wait, not an error of the input. After the missing bytes, the reader gives the whole item.
TEST_CASE("pending items: a sequence reader waits for the items that a head claims")
{
    std::string const whole = encode_edn("[1, 2, 3]");
    cbor::sequence_reader r;
    r.feed(whole.substr(0, 2));
    CHECK_EQ(r.next(), std::unexpected(error::too_little_data));
    r.feed(whole.substr(2));
    auto const item = r.next();
    REQUIRE(item.has_value());
    CHECK_EQ(item->encoded(), whole);
}

// item_size reads a head in one step when 9 bytes are left, and in a slower step otherwise. Each input is cut to
// 9 bytes, so the head is read in the one step, and the count of 12 items is checked against 8 bytes there.
TEST_CASE("pending items: item_size refuses the count when 9 bytes are left")
{
    for (std::string_view const edn :
         {"[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]", "{1: 2, 3: 4, 5: 6, 7: 8, 9: 10, 11: 12}"}) {
        CAPTURE(edn);
        std::string const cut = encode_edn(edn).substr(0, 9);
        auto const size = cbor::item_size(cut);
        CHECK_EQ(size.error(), error::too_little_data);
    }
}

// The check must not refuse an item that holds all the items its head claims. A count that equals the items is the
// boundary of the check.
TEST_CASE("pending items: a container that holds every item it claims is read whole")
{
    std::string const array = encode_edn("[1, 2, 3]");
    std::string const map = encode_edn("{1: 2, 3: 4}");
    CHECK_EQ(test::make_lazy_from_copy(array)->size().value(), 3u);
    CHECK_EQ(test::make_lazy_from_copy(map)->size().value(), 2u);
    CHECK_EQ(cbor::item_size(array), array.size());
    CHECK_EQ(cbor::item_size(map).value(), map.size());
}

// structs skips a map key that names no member. An array, a map and a tag key enclose items, and the skip reads them
// all. A key that claims more items than the bytes hold is refused before the skip reads them.
TEST_CASE("pending items: structs skips the enclosed items of a key that names no member")
{
    for (std::string_view const edn : {R"({[1, 2]: 0, "a": 1})", R"({{1: 2}: 0, "a": 1})", R"({1(2): 0, "a": 1})"}) {
        CAPTURE(edn);
        auto const r = cbor::structs<one_int>::decode(encode_edn(edn));
        REQUIRE(r.has_value());
        CHECK_EQ((**r).a, 1);
    }
    std::string const cut = encode_edn(R"({[1, 2, 3]: 0, "a": 1})").substr(0, 3);
    CHECK_EQ(cbor::structs<one_int>::decode(std::string(cut)), std::unexpected(error::too_little_data));
}

// The counts 2^63 for a map and 2^64 - 1 for an array overflow when the decoder doubles the map count or adds the
// count to the items that are still open. EDN cannot state a head with such a count, so these rows stay byte
// literals until the open rewrite of byte literals.
TEST_CASE("pending items: decode refuses a count that overflows")
{
    for (std::string_view const wire : {"\xbb\x80\x00\x00\x00\x00\x00\x00\x00\x00"sv,
                                        "\x81\x9b\xff\xff\xff\xff\xff\xff\xff\xff"sv}) {
        CAPTURE(wire);
        auto const tree = cbor::decode(wire);
        CHECK_EQ(tree.error(), error::too_little_data);
    }
}

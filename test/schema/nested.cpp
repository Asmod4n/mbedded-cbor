#include "types.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/item_size.hpp>

namespace schema_types
{

struct sprout {
    std::string name;
    std::vector<std::string> tags;
};

struct field_grid {
    std::vector<std::vector<sprout>> rows;
    std::string title;
    std::optional<sprout> extra;
    std::variant<sprout, std::string> pick;
};

} // namespace schema_types

CBOR_DEFINE_TYPE_MEMBERS(sprout, cbor::tag{1950}, name, tags);
CBOR_DEFINE_TYPE_MEMBERS(field_grid, cbor::tag{1951}, rows, title, extra, pick);

namespace schema_types
{

template <class = void>
field_grid make_sample_grid()
{
    return field_grid{{{{"a", {"a0", "a1"}}, {"b", {}}}, {}, {{"c", {"c0"}}}},
                      "grid",
                      sprout{"xname", {"x0"}},
                      sprout{"p", {"p0", "p1", "p2"}}};
}

template <class = void>
void check_grid(field_grid const &a, field_grid const &b)
{
    REQUIRE_EQ(a.rows.size(), b.rows.size());
    for (std::size_t i = 0; i < a.rows.size(); ++i) {
        REQUIRE_EQ(a.rows[i].size(), b.rows[i].size());
        for (std::size_t j = 0; j < a.rows[i].size(); ++j) {
            CHECK_EQ(a.rows[i][j].name, b.rows[i][j].name);
            CHECK_EQ(a.rows[i][j].tags, b.rows[i][j].tags);
        }
    }
    CHECK_EQ(a.title, b.title);
    REQUIRE_EQ(a.extra.has_value(), b.extra.has_value());
    if (a.extra) {
        CHECK_EQ(a.extra->name, b.extra->name);
        CHECK_EQ(a.extra->tags, b.extra->tags);
    }
    REQUIRE_EQ(a.pick.index(), b.pick.index());
    if (auto const *s = std::get_if<sprout>(&a.pick)) {
        CHECK_EQ(s->name, std::get<sprout>(b.pick).name);
        CHECK_EQ(s->tags, std::get<sprout>(b.pick).tags);
    } else {
        CHECK_EQ(std::get<std::string>(a.pick), std::get<std::string>(b.pick));
    }
}

} // namespace schema_types

// A record writes the items of its own references first and the items of its nested records after them. The test
// exists because the reader gives each member of the root a fixed directory entry. The title and the name of the
// optional struct, which stands inside the root, come before every text of a nested record.
TEST_CASE("nested: the items of the root come before the items of its nested records")
{
    std::string const bytes = encode_with_schema(make_sample_grid());
    check_one_item(bytes);
    std::size_t const title = bytes.find("grid"sv);
    REQUIRE_NE(title, std::string::npos);
    for (std::string_view const nested : {"a0"sv, "c0"sv, "p0"sv})
        CHECK_LT(title, bytes.find(nested));
    CHECK_LT(bytes.find("xname"sv), bytes.find("a0"sv));
}

// A list of lists of structs, an optional struct and a variant that holds a struct each hold references of their own.
// The test exists because each element finds its references from its first reference, and decode walks the records in
// the order of the items. Every value comes back, in the verified and in the unverified forms.
TEST_CASE("nested: a list of lists, an optional and a variant of structs read back")
{
    field_grid const value = make_sample_grid();
    std::string const bytes = encode_with_schema(value);
    auto const back = form<field_grid>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    check_grid(**back, value);
    auto const fast = form<field_grid>::unverified_decode(std::string(bytes));
    REQUIRE(fast.has_value());
    check_grid(**fast, value);
    CHECK_EQ(encode_with_schema(**back), bytes);

    auto const root = form<field_grid>::adopt(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ((root->at_path<"$.rows[0][0].tags[1]">()), "a1"sv);
    CHECK_EQ((root->at_path<"$.rows[2][0].name">()), "c"sv);
    CHECK_EQ((root->at_path<"$.rows[][].tags[]">(2uz, 0uz, 0uz)), "c0"sv);
    auto const found = root->at_path<"$.rows[0][1].tags">();
    CHECK_EQ(found->size(), 0u);
    auto const found_2 = root->at_path<"$.rows[1]">();
    CHECK_EQ(found_2->size(), 0u);
    CHECK_EQ(root->at_path<"$.rows[0][2].name">(), std::unexpected(error::index_out_of_bounds));
    CHECK_EQ((root->at_path<"$.title">()), "grid"sv);
    auto const extra = root->at_path<"$.extra">();
    REQUIRE(extra.has_value());
    REQUIRE(extra->has_value());
    CHECK_EQ(((*extra)->at_path<"@.tags[0]">()), "x0"sv);
    auto const pick = root->at_path<"$.pick">();
    REQUIRE(pick.has_value());
    auto const held = pick->get<sprout>();
    REQUIRE(held.has_value());
    CHECK_EQ((held->at_path<"@.tags[2]">()), "p2"sv);
    CHECK_EQ((held->at_path<"@.name">()), "p"sv);

    auto const lane = form<field_grid>::unverified_adopt(std::string(bytes));
    REQUIRE(lane.has_value());
    CHECK_EQ((lane->at_path<"$.rows[0][0].tags[1]">()), "a1"sv);
    CHECK_EQ((lane->at_path<"$.title">()), "grid"sv);
}

// An absent optional struct keeps one item for each of its references: an undefined value, f7 (RFC 8949 3.3). The
// test exists because the members behind it keep their fixed directory entries, and decode passes over those items.
TEST_CASE("nested: an absent optional struct keeps the places of the members behind it")
{
    field_grid value = make_sample_grid();
    value.extra.reset();
    value.pick = std::string("text");
    std::string const bytes = encode_with_schema(value);
    check_one_item(bytes);
    auto const back = form<field_grid>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    check_grid(**back, value);
    auto const root = form<field_grid>::adopt(std::string(bytes));
    REQUIRE(root.has_value());
    auto const extra = root->at_path<"$.extra">();
    REQUIRE(extra.has_value());
    CHECK_FALSE(extra->has_value());
    auto const found = root->at_path<"$.pick">();
    CHECK_EQ((found->get<std::string>()), "text"sv);
    auto const adopted = form<field_grid>::unverified_adopt(std::string(bytes));
    CHECK_EQ((adopted->at_path<"$.title">()), "grid"sv);
}

// The verified forms read f7 in the place of each reference of an absent optional struct and a real item in the place
// of each reference of a present one. The test exists because a f7 that stood for a present struct, or an item that
// stood for an absent one, would give a value that the writer did not write.
TEST_CASE("nested: an undefined item stands only for an absent optional struct")
{
    field_grid absent = make_sample_grid();
    absent.extra.reset();
    std::string const bytes = encode_with_schema(absent);
    std::size_t const flag = bytes.find("\x82\xf4\x5a"sv);
    REQUIRE_NE(flag, std::string::npos);
    std::string present = bytes;
    present[flag + 1] = '\xf5';
    CHECK_EQ(form<field_grid>::decode(std::string(present)), std::unexpected(error::incorrect_type));
    std::size_t const undefined = bytes.find('\xf7', root_offset(bytes) + form<field_grid>::fixed_size());
    REQUIRE_NE(undefined, std::string::npos);
    std::string defined = bytes;
    defined[undefined] = '\xf6';
    CHECK_EQ(form<field_grid>::decode(std::string(defined)), std::unexpected(error::incorrect_type));
    CHECK(form<field_grid>::unverified_decode(std::string(defined)).has_value());

    std::string const full = encode_with_schema(make_sample_grid());
    std::size_t const name = full.find("\x7a\x00\x00\x00\x05xname"sv);
    REQUIRE_NE(name, std::string::npos);
    std::string undefined_name = full;
    undefined_name[name] = '\xf7';
    CHECK_EQ(form<field_grid>::decode(std::string(undefined_name)), std::unexpected(error::incorrect_type));
    auto const root = form<field_grid>::adopt(std::string(undefined_name));
    REQUIRE(root.has_value());
    auto const found = root->at_path<"$.extra">();
    CHECK_EQ((*found)->at_path<"@.name">(), std::unexpected(error::incorrect_type));
}

namespace schema_types
{

// A generic unpacker of draft-ietf-cbor-packed-19: it knows the table of tag 113, the references 6(N) and simple(0) to
// simple(15) of 2.2, and the record functions 114 that the straight references 128 to 135 and 6([N, rump]) of 2.3 and
// 4.2 apply. It knows nothing of cbor::schema. It writes the unpacked item in EDN; a reference to an undefined entry
// is an error.
struct packed_unpacker {
    std::vector<cbor::item> table;

    std::expected<std::string, cbor::error> entry(std::uint64_t const index) const
    {
        if (index >= table.size() || table[index].type() == cbor::type::undefined)
            return std::unexpected(cbor::error::unpopulated_table_index);
        return text(table[index]);
    }

    std::expected<std::string, cbor::error> record(std::uint64_t const index, cbor::item const rump) const
    {
        if (index >= table.size())
            return std::unexpected(cbor::error::unpopulated_table_index);
        auto const function = table[index].get<std::pair<std::uint64_t, cbor::item>>();
        constexpr std::uint64_t record_function = 114;
        if (!function || function->first != record_function)
            return std::unexpected(cbor::error::incorrect_type);
        auto const keys = function->second.elements();
        auto const values = rump.elements();
        if (!keys || !values || keys->size() != values->size())
            return std::unexpected(cbor::error::incorrect_type);
        std::string out = "{";
        auto value = values->begin();
        for (cbor::item const key : *keys) {
            auto const v = text(*value++);
            if (!v)
                return v;
            auto const edn = key.to_edn();
            out += (out.size() > 1 ? ", " : "") + edn.value() + ": " + *v;
        }
        return out + "}";
    }

    std::expected<std::string, cbor::error> text(cbor::item const x) const
    {
        constexpr std::uint64_t reference = 6;
        constexpr std::uint64_t straight_first = 128;
        constexpr std::uint64_t straight_last = 135;
        constexpr std::uint64_t shared_first = 16;
        constexpr std::uint64_t argument_first = 8;
        if (x.type() == cbor::type::simple_value) {
            auto const n = x.get<cbor::simple_value>();
            if (n && std::to_underlying(*n) < shared_first)
                return entry(std::to_underlying(*n));
        }
        if (x.type() == cbor::type::tag) {
            auto const tagged = x.get<std::pair<std::uint64_t, cbor::item>>();
            auto const [number, content] = *tagged;
            if (number == reference && content.type() == cbor::type::unsigned_integer) {
                auto const positive = content.get<std::uint64_t>();
                return entry(shared_first + 2 * *positive);
            }
            if (number == reference && content.type() == cbor::type::negative_integer) {
                auto const negative = content.get<std::int64_t>();
                return entry(shared_first - 2 * static_cast<std::uint64_t>(*negative) - 1);
            }
            if (number == reference && content.type() == cbor::type::array) {
                auto const pair = content.elements();
                if (!pair || pair->size() != 2)
                    return std::unexpected(cbor::error::incorrect_type);
                auto const argument = pair->front().get<std::uint64_t>();
                return record(argument_first + *argument, *std::next(pair->begin()));
            }
            if (number >= straight_first && number <= straight_last)
                return record(number - straight_first, content);
            auto const inner = text(content);
            if (!inner)
                return inner;
            return std::to_string(number) + "(" + *inner + ")";
        }
        if (x.type() == cbor::type::array) {
            std::string out = "[";
            auto const x_elements = x.elements();
            auto const elements = *x_elements;
            for (cbor::item const e : elements) {
                auto const v = text(e);
                if (!v)
                    return v;
                out += (out.size() > 1 ? ", " : "") + *v;
            }
            return out + "]";
        }
        if (x.type() == cbor::type::map) {
            std::string out = "{";
            auto const x_pairs = x.pairs();
            auto const pairs = *x_pairs;
            for (auto const &[k, v] : pairs) {
                auto const key = text(k);
                auto const value = text(v);
                if (!key || !value)
                    return std::unexpected(cbor::error::unpopulated_table_index);
                out += (out.size() > 1 ? ", " : "") + *key + ": " + *value;
            }
            return out + "}";
        }
        auto const edn = x.to_edn();
        if (!edn)
            return std::unexpected(edn.error());
        return *edn;
    }
};

template <class = void>
std::expected<std::string, cbor::error> unpack(std::string const &message)
{
    auto const tree = cbor::decode(message);
    REQUIRE(tree.has_value());
    auto const tagged = (*tree)->get<std::pair<std::uint64_t, cbor::item>>();
    auto const [number, content] = *tagged;
    constexpr std::uint64_t basic_packed = 113;
    REQUIRE_EQ(number, basic_packed);
    auto const content_elements = content.elements();
    auto const parts = *content_elements;
    packed_unpacker u;
    auto const parts_elements = parts.front().elements();
    auto const table = *parts_elements;
    for (cbor::item const e : table)
        u.table.push_back(e);
    return u.text(*std::next(parts.begin()));
}

} // namespace schema_types

// A generic unpacker of Packed CBOR resolves every reference of a message with an absent optional struct. The test
// exists because the absent struct holds a byte string of zeros and no reference: a reference there would name an
// undefined entry. The present struct unpacks to its keys and values.
TEST_CASE("nested: a generic unpacker reads a message with an absent and a present optional struct")
{
    auto const encoded = form<passkey_login>::encode(passkey_login{{std::byte{1}}, std::nullopt, std::nullopt});
    auto const absent = unpack(*encoded);
    REQUIRE(absent.has_value());
    CHECK_EQ(absent->find("6("), std::string::npos);
    CHECK_NE(absent->find("\"user\": [false, h'000000"), std::string::npos);
    CHECK_NE(absent->find("\"signature\": h'01'"), std::string::npos);
    auto const encoded_2 = form<passkey_login>::encode(passkey_login{{std::byte{1}}, passkey_user{{std::byte{9}}, "alice"}, "hi"});
    auto const present = unpack(*encoded_2);
    REQUIRE(present.has_value());
    CHECK_NE(present->find("\"user\": [true, 1516({\"id\": h'09'"), std::string::npos);
    CHECK_NE(present->find("\"name\": \"alice\""), std::string::npos);
    CHECK_NE(present->find("\"note\": [\"hi\""), std::string::npos);
    CHECK_EQ(unpack(encode_with_schema(make_sample_grid())).error_or(cbor::error{}), cbor::error{});
}

// A message ends where its directory says: the last entry is the start of the rump. The bytes behind the rump are the
// next item of a CBOR sequence (RFC 8742) and stay unread. The test exists because a reader that took the end of the
// buffer for the end of the message would read the second message as a part of the first. The generic item skip of the
// library finds the second message.
TEST_CASE("nested: a message followed by a second message reads as alone")
{
    field_grid const first = make_sample_grid();
    field_grid second = make_sample_grid();
    second.title = "next";
    second.rows.clear();
    std::string const one = encode_with_schema(first);
    std::string const both = one + encode_with_schema(second);
    auto const back = form<field_grid>::decode(std::string(both));
    REQUIRE(back.has_value());
    check_grid(**back, first);
    auto const fast = form<field_grid>::unverified_decode(std::string(both));
    REQUIRE(fast.has_value());
    check_grid(**fast, first);
    auto const adopted = form<field_grid>::adopt(std::string(both));
    CHECK_EQ((adopted->at_path<"$.title">()), "grid"sv);
    auto const adopted_2 = form<field_grid>::unverified_adopt(std::string(both));
    CHECK_EQ((adopted_2->at_path<"$.title">()), "grid"sv);
    auto const adopted_3 = form<field_grid>::adopt(std::string(both));
    CHECK_EQ((adopted_3->at_path<"$.rows[2][0].name">()), "c"sv);
    auto const end = cbor::item_size(both);
    REQUIRE(end.has_value());
    CHECK_EQ(*end, one.size());
    auto const next = form<field_grid>::decode(both.substr(*end));
    REQUIRE(next.has_value());
    check_grid(**next, second);
    auto const adopted_4 = form<field_grid>::adopt(both.substr(*end));
    CHECK_EQ((adopted_4->at_path<"$.title">()), "next"sv);
}

// A struct with no reference member has no item. Its directory holds the one entry 0: the start of the rump, which is
// the end of the directory (RFC 8949 3: the directory head 5a and its length take 5 bytes, the entry 4). The test
// exists because the verified forms find the end of the message there, and the unverified forms bound every read by it.
// Each cut gives too_little_data in every form.
TEST_CASE("nested: a struct with no reference member reads, and each cut of it is too_little_data")
{
    std::string const bytes = encode_with_schema(tire{17, 2.5f});
    std::size_t const directory = root_offset(bytes) + form<tire>::fixed_size();
    constexpr std::size_t directory_size = 9;
    CHECK_EQ(bytes.size(), directory + directory_size + rump_size);
    CHECK_EQ(form<tire>::at_path<"$.diameter">(bytes), 17u);
    CHECK_EQ(form<tire>::unverified_at_path<"$.diameter">(bytes), 17u);
    auto const back = form<tire>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((*back)->airPressure, 2.5f);
    auto const fast = form<tire>::unverified_decode(std::string(bytes));
    REQUIRE(fast.has_value());
    CHECK_EQ((*fast)->diameter, 17u);
    auto const adopted = form<tire>::adopt(std::string(bytes));
    CHECK_EQ(adopted->at_path<"$.airPressure">(), 2.5f);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        CAPTURE(size);
        std::string const cut = bytes.substr(0, size);
        CHECK_EQ(form<tire>::at_path<"$.diameter">(cut), std::unexpected(error::too_little_data));
        CHECK_EQ(form<tire>::unverified_at_path<"$.diameter">(cut), std::unexpected(error::too_little_data));
        CHECK_EQ(form<tire>::decode(std::string(cut)), std::unexpected(error::too_little_data));
        CHECK_EQ(form<tire>::unverified_decode(std::string(cut)), std::unexpected(error::too_little_data));
        CHECK_EQ(form<tire>::adopt(std::string(cut)), std::unexpected(error::too_little_data));
    }
}

// Each cut and each changed byte of a message with nested records reads no memory outside the message. The test exists
// because the base of a nested record comes from the message, and every entry that it selects is checked against the
// directory. A cut message gives no value in the verified forms and in unverified_decode, which reads every item. An
// unverified path does not read the rump, so a cut that leaves its item whole still reads it.
TEST_CASE("nested: a cut or a changed byte reads nothing outside the message")
{
    std::string const bytes = encode_with_schema(make_sample_grid());
    auto const verified = [](std::string const &m) {
        std::size_t n = 0;
        n += form<field_grid>::decode(std::string(m)).has_value();
        n += form<field_grid>::unverified_decode(std::string(m)).has_value();
        if (auto const opened = form<field_grid>::adopt(std::string(m))) {
            n += opened->at_path<"$.rows[][].tags[]">(0uz, 0uz, 1uz).has_value();
            if (auto const pick = opened->at_path<"$.pick">())
                if (auto const held = pick->get<sprout>())
                    n += held->at_path<"@.tags[0]">().has_value();
        }
        return n;
    };
    auto const unverified = [](std::string const &m) {
        auto const lane = form<field_grid>::unverified_adopt(std::string(m));
        return lane && lane->at_path<"$.rows[][].name">(2uz, 0uz).has_value();
    };
    CHECK_EQ(verified(bytes), 4u);
    CHECK(unverified(bytes));
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        CHECK_EQ(verified(bytes.substr(0, size)), 0u);
        unverified(bytes.substr(0, size));
    }
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        std::string f = bytes;
        f.at(at) = static_cast<char>(f.at(at) ^ 0x41);
        verified(f);
        unverified(f);
    }
}

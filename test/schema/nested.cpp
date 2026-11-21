#include "types.hpp"

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
    CHECK_EQ(root->at_path<"$.rows[0][0].tags[1]">().value(), "a1"sv);
    CHECK_EQ(root->at_path<"$.rows[2][0].name">().value(), "c"sv);
    CHECK_EQ(root->at_path<"$.rows[][].tags[]">(2uz, 0uz, 0uz).value(), "c0"sv);
    CHECK_EQ(root->at_path<"$.rows[0][1].tags">()->size(), 0u);
    CHECK_EQ(root->at_path<"$.rows[1]">()->size(), 0u);
    CHECK_EQ(root->at_path<"$.rows[0][2].name">().error(), error::index_out_of_bounds);
    CHECK_EQ(root->at_path<"$.title">().value(), "grid"sv);
    auto const extra = root->at_path<"$.extra">();
    REQUIRE(extra.has_value());
    REQUIRE(extra->has_value());
    CHECK_EQ((*extra)->at_path<"@.tags[0]">().value(), "x0"sv);
    auto const pick = root->at_path<"$.pick">();
    REQUIRE(pick.has_value());
    auto const held = pick->get<sprout>();
    REQUIRE(held.has_value());
    CHECK_EQ(held->at_path<"@.tags[2]">().value(), "p2"sv);
    CHECK_EQ(held->at_path<"@.name">().value(), "p"sv);

    auto const lane = form<field_grid>::unverified_adopt(std::string(bytes));
    REQUIRE(lane.has_value());
    CHECK_EQ(lane->at_path<"$.rows[0][0].tags[1]">().value(), "a1"sv);
    CHECK_EQ(lane->at_path<"$.title">().value(), "grid"sv);
}

// An absent optional struct keeps the items of its references, those of a struct made by its default constructor. The
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
    CHECK_EQ(root->at_path<"$.pick">()->get<std::string>().value(), "text"sv);
    CHECK_EQ(form<field_grid>::unverified_adopt(std::string(bytes))->at_path<"$.title">().value(), "grid"sv);
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

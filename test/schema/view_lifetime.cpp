#include "types.hpp"

// The view holds the owner of the bytes. The test reads it after the accessor and the string it came from are gone,
// so that the sanitizer sees a read of freed memory if the view did not keep the bytes alive.
TEST_CASE("view: the view keeps the bytes alive after the accessor is gone")
{
    std::optional<cbor::typed_array_view<double>> kept;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        kept.emplace(std::move(*v));
    }
    REQUIRE_EQ(kept->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>((*kept)[1]), 0x4000000000000000u);
}

// A sub-view holds the owner as the view does. The test drops the string, the accessor and the parent view, then
// reads the sub-views, so that the sanitizer sees a read of freed memory if a sub-view borrowed from its parent.
// The sub-view of an rvalue view takes the owner over, and the test reads it after the same drops. A view made from
// an rvalue accessor takes the owner over too.
TEST_CASE("view: a view and a sub-view keep the bytes alive after the parent is gone")
{
    std::optional<cbor::typed_array_view<double>> copied;
    std::optional<cbor::typed_array_view<double>> moved;
    std::optional<cbor::typed_array_view<double>> taken;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0, 4.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        auto c = v->last(2);
        REQUIRE(c.has_value());
        copied.emplace(std::move(*c));
        auto m = std::move(*v).first(1);
        REQUIRE(m.has_value());
        moved.emplace(std::move(*m));
        auto r = form<doubles>::path(schema_bytes(doubles{{8.0}}));
        REQUIRE(r.has_value());
        auto t = std::move(*r).view<"$.v">();
        REQUIRE(t.has_value());
        taken.emplace(std::move(*t));
    }
    REQUIRE_EQ(copied->size(), 2u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(copied->back()), 0x4010000000000000u);
    CHECK_EQ(std::ranges::fold_left(*copied, 0.0, std::plus<>{}), 6.0);
    REQUIRE_EQ(moved->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(moved->front()), 0x3ff0000000000000u);
    REQUIRE_EQ(taken->size(), 1u);
    CHECK_EQ(std::bit_cast<std::uint64_t>(taken->front()), 0x4020000000000000u);
}

// A move gives the owner of the bytes to the new object. The moved-from view or accessor must not read the bytes
// after the new owner is gone, because they may be freed. The test drops every other owner and then uses each
// moved-from object, so that the sanitizer sees a read of freed memory if a moved-from object kept its view. A
// moved-from view is empty: operator[], front and back are a wrong use, at is index_out_of_bounds. A use of a
// moved-from accessor is a wrong use. Each case is std::logic_error, as for any other wrong use of the library.
TEST_CASE("view: a moved-from view and a moved-from accessor read no bytes")
{
    using V = cbor::typed_array_view<double>;
    std::optional<V> view;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        view.emplace(std::move(*v));
    }
    {
        V const taken = std::move(*view);
        CHECK_EQ(taken.size(), 2u);
    }
    CHECK(view->empty());
    CHECK_EQ(view->size(), 0u);
    CHECK(view->begin() == view->end());
    CHECK_THROWS_AS((void)(*view)[0], std::logic_error);
    CHECK_THROWS_AS((void)view->front(), std::logic_error);
    CHECK_EQ(view->at(0).error(), error::index_out_of_bounds);

    std::optional<V> sliced;
    {
        auto const root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
        REQUIRE(root.has_value());
        auto v = root->view<"$.v">();
        REQUIRE(v.has_value());
        sliced.emplace(std::move(*v));
    }
    {
        auto const head = std::move(*sliced).first(1);
        REQUIRE(head.has_value());
        CHECK_EQ(head->size(), 1u);
    }
    CHECK(sliced->empty());
    CHECK_THROWS_AS((void)(*sliced)[0], std::logic_error);

    V assigned = [] {
        auto const root = form<doubles>::path(schema_bytes(doubles{{4.0}}));
        return *root->view<"$.v">();
    }();
    {
        V target = [] {
            auto const root = form<doubles>::path(schema_bytes(doubles{{8.0}}));
            return *root->view<"$.v">();
        }();
        target = std::move(assigned);
        CHECK_EQ(std::bit_cast<std::uint64_t>(target.front()), 0x4010000000000000u);
    }
    CHECK(assigned.empty());
    CHECK_THROWS_AS((void)assigned.back(), std::logic_error);

    auto root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(root.has_value());
    {
        auto const taken = std::move(*root);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"$.v[0]">()), 0x3ff0000000000000u);
    }
    CHECK_THROWS_AS((void)root->at_path<"$.v[0]">(), std::logic_error);
    CHECK_THROWS_AS((void)root->view<"$.v">(), std::logic_error);

    auto list_root = form<doubles>::path(schema_bytes(doubles{{1.0, 2.0}}));
    REQUIRE(list_root.has_value());
    auto list = list_root->at_path<"$.v">();
    REQUIRE(list.has_value());
    list_root = std::unexpected(error::incorrect_type);
    {
        auto const taken = std::move(*list);
        CHECK_EQ(std::bit_cast<std::uint64_t>(*taken.at_path<"@[1]">()), 0x4000000000000000u);
    }
    CHECK_THROWS_AS((void)list->at_path<"@[1]">(), std::logic_error);
}

// A std::string_view or std::span<std::byte const> alternative of a decoded value points into the message; the ref
// keeps the message alive after the input is gone. A std::u8string alternative holds a copy.
TEST_CASE("view: borrowed alternatives of a variant point into a message that the ref keeps alive")
{
    std::array<std::byte const, 2> const raw{std::byte{7}, std::byte{8}};
    std::vector<borrowed_value> const all{{std::string_view("view")}, {std::span<std::byte const>(raw)}, {std::int8_t{-3}}};
    for (borrowed_value const &b : all) {
        auto back = [&] {
            std::string bytes = *form<borrowed_value>::encode(b);
            return form<borrowed_value>::decode(std::move(bytes));
        }();
        REQUIRE(back.has_value());
        REQUIRE_EQ((*back)->value.index(), b.value.index());
        if (auto const *v = std::get_if<std::string_view>(&(*back)->value))
            CHECK_EQ(*v, "view"sv);
        if (auto const *s = std::get_if<std::span<std::byte const>>(&(*back)->value))
            CHECK(std::ranges::equal(*s, raw));
    }
    auto const u8 = [] {
        std::string bytes = *form<one_value<std::u8string>>::encode(one_value<std::u8string>{std::u8string(u8"u8")});
        return form<one_value<std::u8string>>::decode(std::move(bytes));
    }();
    REQUIRE(u8.has_value());
    CHECK(std::get<std::u8string>((*u8)->value) == u8"u8");
}

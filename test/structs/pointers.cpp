#include "../small_stack.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/error.hpp>
#include <cbor/item.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using cbor::error;

namespace structs_pointers
{
namespace
{

std::string bytes_of(std::string_view const edn)
{
    auto const bytes = cbor::diag2cbor(edn);
    REQUIRE(bytes.has_value());
    return *bytes;
}

struct leaf {
    int v;
};

struct empty_pointers {
    leaf *r;
    std::unique_ptr<leaf> u;
    std::shared_ptr<leaf> s;
};

struct raw_triple {
    leaf *a;
    leaf *b;
    leaf *c;
};

struct numbers {
    int a;
    int b;
    int *p;
};

struct shared_pair {
    std::shared_ptr<leaf> a;
    std::shared_ptr<leaf const> b;
};

struct node {
    std::string name;
    node *parent;
    std::vector<std::unique_ptr<node>> children;
};

struct tree {
    int v;
    std::vector<std::unique_ptr<tree>> kids;
};

struct plain {
    int a;
    std::vector<std::string> b;
    std::optional<int> c;
};

struct loop {
    loop *self;
};

struct two_loops {
    loop *a;
    loop *b;
};

struct ring {
    ring *next;
};

struct two_rings {
    ring *a;
    ring *b;
};

struct unique_link {
    std::unique_ptr<unique_link> next;
};

struct shared_link {
    std::shared_ptr<shared_link> next;
};

struct label {
    std::string_view text;
};

struct labelled {
    label *raw;
    std::shared_ptr<label const> shared;
};

struct family {
    std::string name;
    family *parent;
    std::vector<family> kids;
};

struct mixed {
    leaf l;
    loop *p;
};

template <class T>
cbor::ref<T> decoded(std::string bytes)
{
    auto r = cbor::structs<T>::decode(std::move(bytes));
    REQUIRE(r.has_value());
    return *std::move(r);
}

template <class T>
cbor::ref<T> decoded_cycles(std::string bytes)
{
    auto r = cbor::structs<T>::decode_cycles(std::move(bytes));
    REQUIRE(r.has_value());
    return *std::move(r);
}

template <class T>
void check_cycles_read_refused(std::string_view const edn, error const expected)
{
    auto const r = cbor::structs<T>::decode_cycles(bytes_of(edn));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), expected);
}


// Each entry of the encode writes the same bytes: cbor::encode into a new string and into a span,
// cbor::structs<T>::encode into a new string and into a span, and cbor::to_edn, whose text equals
// cbor::diagnostic_notation of these bytes.
template <class T>
void check_written(T const &value, std::string_view const edn)
{
    std::string const expected = bytes_of(edn);
    auto const encoded = cbor::encode<T>(value);
    REQUIRE(encoded.has_value());
    CHECK_EQ(*encoded, expected);
    std::array<char, 256> buffer{};
    auto const written = cbor::encode<T>(std::span<char>(buffer), value);
    REQUIRE(written.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *written), expected);
    auto const by_structs = cbor::structs<T>::encode(value);
    REQUIRE(by_structs.has_value());
    CHECK_EQ(*by_structs, expected);
    std::array<char, 256> structs_buffer{};
    auto const structs_written = cbor::structs<T>::encode(std::span<char>(structs_buffer), value);
    REQUIRE(structs_written.has_value());
    CHECK_EQ(std::string_view(structs_buffer.data(), *structs_written), expected);
    auto const text = cbor::to_edn<T>(value);
    auto const notation = cbor::diagnostic_notation(expected);
    REQUIRE(text.has_value());
    REQUIRE(notation.has_value());
    CHECK_EQ(*text, *notation);
}

// Both entries of cbor::encode_cycles write the same bytes: into a new string, into a span and into a string that
// they append to.
template <class T>
std::string check_cycles_written(T const &value, std::string_view const edn)
{
    std::string const expected = bytes_of(edn);
    auto const encoded = cbor::encode_cycles<T>(value);
    REQUIRE(encoded.has_value());
    CHECK_EQ(*encoded, expected);
    std::array<char, 256> buffer{};
    auto const written = cbor::encode_cycles<T>(std::span<char>(buffer), value);
    REQUIRE(written.has_value());
    CHECK_EQ(std::string_view(buffer.data(), *written), expected);
    std::string appended = "x";
    auto const appended_size = cbor::encode_cycles<T>(appended, value);
    REQUIRE(appended_size.has_value());
    CHECK_EQ(appended, "x" + expected);
    return expected;
}

template <class T>
void check_cycles_refused(T const &value, error const expected)
{
    auto const encoded = cbor::encode_cycles<T>(value);
    REQUIRE_FALSE(encoded.has_value());
    CHECK_EQ(encoded.error(), expected);
    std::array<char, 256> buffer{};
    auto const written = cbor::encode_cycles<T>(std::span<char>(buffer), value);
    REQUIRE_FALSE(written.has_value());
    CHECK_EQ(written.error(), expected);
}

cbor::item value_at(cbor::item const map, std::string_view const key)
{
    auto const pairs = map.pairs();
    REQUIRE(pairs.has_value());
    for (auto const &[k, v] : *pairs)
        if (k.get<std::string_view>() == key)
            return v;
    FAIL("the map has no key ", key);
    return map;
}

cbor::item target_of(cbor::item const reference)
{
    CHECK_EQ(reference.type(), cbor::type::shared_reference);
    auto const r = reference.get<std::pair<std::uint64_t, cbor::item>>();
    REQUIRE(r.has_value());
    return r->second;
}

} // namespace
} // namespace structs_pointers

CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::leaf, cbor::untagged, v);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::empty_pointers, cbor::untagged, r, u, s);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::raw_triple, cbor::untagged, a, b, c);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::numbers, cbor::untagged, a, b, p);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::shared_pair, cbor::untagged, a, b);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::node, cbor::untagged, name, parent, children);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::tree, cbor::untagged, v, kids);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::plain, cbor::untagged, a, b, c);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::loop, cbor::untagged, self);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::two_loops, cbor::untagged, a, b);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::ring, cbor::untagged, next);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::two_rings, cbor::untagged, a, b);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::unique_link, cbor::untagged, next);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::shared_link, cbor::untagged, next);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::label, cbor::untagged, text);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::labelled, cbor::untagged, raw, shared);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::family, cbor::untagged, name, parent, kids);
CBOR_DEFINE_TYPE_MEMBERS(structs_pointers::mixed, cbor::untagged, l, p);

namespace structs_pointers
{

// RFC 8949 3.3: null is the CBOR form of an empty pointer. A pointer member is no std::optional, so its key stands
// with null for each of the three pointer kinds.
TEST_CASE("structs pointers: an empty pointer of each kind writes null")
{
    check_written(empty_pointers{}, R"({"r": null, "s": null, "u": null})");
    check_written<leaf *>(nullptr, "null");
}

// The owner decided that cbor::encode keeps no identity: a pointer writes the value of its target, and an object
// that two pointers reach is written twice, with no tag.
TEST_CASE("structs pointers: cbor::encode writes the value of each target, once for each pointer")
{
    numbers n{1, 2, nullptr};
    n.p = &n.b;
    check_written(n, R"({"a": 1, "b": 2, "p": 2})");
    leaf x{1};
    leaf y{2};
    check_written(raw_triple{&x, &y, &y}, R"({"a": {"v": 1}, "b": {"v": 2}, "c": {"v": 2}})");
    check_written<leaf *>(&x, R"({"v": 1})");
    auto const shared = std::make_shared<leaf>(leaf{3});
    check_written(shared_pair{shared, shared}, R"({"a": {"v": 3}, "b": {"v": 3}})");
    auto u = std::make_unique<leaf>(leaf{4});
    check_written(empty_pointers{&x, std::move(u), shared}, R"({"r": {"v": 1}, "s": {"v": 3}, "u": {"v": 4}})");
}

// Each std::unique_ptr owns its own object, so the bytes hold each object once and no tag.
TEST_CASE("structs pointers: a tree of std::unique_ptr")
{
    tree root{1, {}};
    root.kids.push_back(std::make_unique<tree>(tree{2, {}}));
    root.kids.push_back(std::make_unique<tree>(tree{3, {}}));
    root.kids.back()->kids.push_back(std::make_unique<tree>(tree{4, {}}));
    check_written(root, R"({"v": 1, "kids": [{"v": 2, "kids": []},)"
                        R"( {"v": 3, "kids": [{"v": 4, "kids": []}]}]})");
}

// The owner decided that cbor::encode recurses through a cycle until the stack is full and then gives
// nesting_depth_exceeded. The check of the stack stands between two levels of the recursion, so the frames between
// two checks must fit the reserve of the stack; the test runs on a small stack, and in the ASan build at -O0, where
// the frames are largest. The span is larger than the bytes that the small stack lets the call write.
TEST_CASE("structs pointers: each entry of cbor::encode gives nesting_depth_exceeded for a node that points to itself")
{
    loop self{nullptr};
    self.self = &self;
    std::string buffer(test::small_stack_bytes, '\0');
    std::span<char> const out(buffer);
    CHECK_EQ(test::error_on_small_stack([&self] { return cbor::encode<loop>(self); }), error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&self, out] { return cbor::encode<loop>(out, self); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&self] { return cbor::structs<loop>::encode(self); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&self, out] { return cbor::structs<loop>::encode(out, self); }),
             error::nesting_depth_exceeded);
    CHECK_EQ(test::error_on_small_stack([&self] { return cbor::to_edn<loop>(self); }), error::nesting_depth_exceeded);
}

// A type without pointers takes the path of cbor::encode in both functions and writes the same bytes.
TEST_CASE("structs pointers: a type without pointers writes the same bytes in cbor::encode and cbor::encode_cycles")
{
    check_written(plain{1, {"x"}, std::nullopt}, R"({"a": 1, "b": ["x"]})");
    check_cycles_written(plain{1, {"x"}, std::nullopt}, R"({"a": 1, "b": ["x"]})");
    check_written(leaf{5}, R"({"v": 5})");
    check_cycles_written(leaf{5}, R"({"v": 5})");
}

// The owner decided that cbor::encode_cycles writes tags 28 and 29 only for a cycle. An object that two pointers
// reach outside a cycle is written twice as a value, as cbor::encode writes it.
TEST_CASE("structs pointers: cbor::encode_cycles writes a shared object that is no cycle as a value each time")
{
    leaf x{1};
    check_cycles_written(raw_triple{&x, &x, nullptr}, R"({"a": {"v": 1}, "b": {"v": 1}, "c": null})");
    auto const shared = std::make_shared<leaf>(leaf{3});
    check_cycles_written(shared_pair{shared, shared}, R"({"a": {"v": 3}, "b": {"v": 3}})");
    numbers n{1, 2, nullptr};
    n.p = &n.b;
    check_cycles_written(n, R"({"a": 1, "b": 2, "p": 2})");
}

// The value-sharing registration: a tag 28 marks the object, and a tag 29 with the index 0 names the first mark.
// cbor::decode_cycles reads the bytes back to a node whose member is a reference to that node.
TEST_CASE("structs pointers: cbor::encode_cycles writes a node that points to itself")
{
    loop self{nullptr};
    self.self = &self;
    auto const bytes = check_cycles_written(self, R"(28({"self": 29(0)}))");
    auto const tree = cbor::decode_cycles(bytes);
    REQUIRE(tree.has_value());
    CHECK(target_of(value_at(**tree, "self")) == **tree);
}

// A parent pointer leads back to an object whose write is open: to the root from the child, to the child from the
// grandchild. Each target carries a tag 28, and the index of tag 29 counts the tags 28 in the order of the bytes.
TEST_CASE("structs pointers: cbor::encode_cycles writes a tree with parent pointers")
{
    node root{"r", nullptr, {}};
    root.children.push_back(std::make_unique<node>(node{"c", &root, {}}));
    node &child = *root.children.front();
    child.children.push_back(std::make_unique<node>(node{"g", &child, {}}));
    auto const bytes = check_cycles_written(root, R"(28({"name": "r", "parent": null, "children": [)"
                                                  R"(28({"name": "c", "parent": 29(0), "children": [)"
                                                  R"({"name": "g", "parent": 29(1), "children": []}]})]}))");
    auto const tree = cbor::decode_cycles(bytes);
    REQUIRE(tree.has_value());
    auto const children = value_at(**tree, "children").elements();
    REQUIRE(children.has_value());
    cbor::item const decoded_child = *children->begin();
    CHECK(target_of(value_at(decoded_child, "parent")) == **tree);
    auto const grandchildren = value_at(decoded_child, "children").elements();
    REQUIRE(grandchildren.has_value());
    CHECK(target_of(value_at(*grandchildren->begin(), "parent")) == decoded_child);
}

// Review 11 (A1): one target reached on two paths is written on each path, each time with a tag 28 of its own. The
// tag 29 of each path names the tag 28 of that path, so cbor::decode_cycles gives two objects, each its own cycle.
TEST_CASE("structs pointers: cbor::encode_cycles marks a cycle that two pointers reach on each path")
{
    loop self{nullptr};
    self.self = &self;
    auto const bytes = check_cycles_written(two_loops{&self, &self}, R"({"a": 28({"self": 29(0)}),)"
                                                                      R"( "b": 28({"self": 29(1)})})");
    auto const tree = cbor::decode_cycles(bytes);
    REQUIRE(tree.has_value());
    cbor::item const a = value_at(**tree, "a");
    cbor::item const b = value_at(**tree, "b");
    CHECK(target_of(value_at(a, "self")) == a);
    CHECK(target_of(value_at(b, "self")) == b);
    CHECK_FALSE(a == b);
}

// Review 11 (A2): two objects that point to each other are both targets, each on the path that starts at the other.
// On the path that starts at one of them, the other carries a tag 28 that no tag 29 names, because the cycle closes
// at the first object. cbor::decode_cycles reads such a mark.
TEST_CASE("structs pointers: cbor::encode_cycles writes a tag 28 that no tag 29 names, and decode_cycles reads it")
{
    ring first{nullptr};
    ring second{&first};
    first.next = &second;
    auto const bytes = check_cycles_written(two_rings{&first, &second}, R"({"a": 28({"next": 28({"next": 29(0)})}),)"
                                                                        R"( "b": 28({"next": 28({"next": 29(2)})})})");
    auto const tree = cbor::decode_cycles(bytes);
    REQUIRE(tree.has_value());
    cbor::item const a = value_at(**tree, "a");
    CHECK(target_of(value_at(value_at(a, "next"), "next")) == a);
    cbor::item const b = value_at(**tree, "b");
    CHECK(target_of(value_at(value_at(b, "next"), "next")) == b);
}

// Plan 12 (R10.2): a cycle through a std::shared_ptr leaks, and a cycle through a std::unique_ptr gives an object two
// owners, so cbor::encode_cycles refuses both. The test ends each cycle before the objects end.
TEST_CASE("structs pointers: cbor::encode_cycles gives cyclic_data_structure for a cycle through a smart pointer")
{
    unique_link root;
    root.next.reset(&root);
    check_cycles_refused(root, error::cyclic_data_structure);
    unique_link *const released = root.next.release();
    CHECK_EQ(released, &root);
    auto const shared = std::make_shared<shared_link>();
    shared->next = shared;
    check_cycles_refused(*shared, error::cyclic_data_structure);
    shared->next.reset();
}

// Plan 12 (R10.8): cbor::decode gives each pointer kind back from the bytes of cbor::encode: null as an empty
// pointer, and a value as a new object with the same value.
TEST_CASE("structs pointers: each pointer kind reads back from the bytes of cbor::encode")
{
    auto const empty = decoded<empty_pointers>(*cbor::encode(empty_pointers{}));
    CHECK_EQ(empty->r, nullptr);
    CHECK_EQ(empty->u, nullptr);
    CHECK_EQ(empty->s, nullptr);
    leaf x{1};
    auto const full =
        decoded<empty_pointers>(*cbor::encode(empty_pointers{&x, std::make_unique<leaf>(leaf{4}), std::make_shared<leaf>(leaf{3})}));
    REQUIRE_NE(full->r, nullptr);
    REQUIRE_NE(full->u, nullptr);
    REQUIRE_NE(full->s, nullptr);
    CHECK_EQ(full->r->v, 1);
    CHECK_EQ(full->u->v, 4);
    CHECK_EQ(full->s->v, 3);
    numbers n{1, 2, nullptr};
    n.p = &n.b;
    auto const read_numbers = decoded<numbers>(*cbor::encode(n));
    REQUIRE_NE(read_numbers->p, nullptr);
    CHECK_EQ(*read_numbers->p, 2);
    auto const shared = std::make_shared<leaf>(leaf{3});
    auto const pair = decoded<shared_pair>(*cbor::encode(shared_pair{shared, shared}));
    REQUIRE_NE(pair->a, nullptr);
    REQUIRE_NE(pair->b, nullptr);
    CHECK_EQ(pair->a->v, 3);
    CHECK_EQ(pair->b->v, 3);
    tree root{1, {}};
    root.kids.push_back(std::make_unique<tree>(tree{2, {}}));
    root.kids.back()->kids.push_back(std::make_unique<tree>(tree{3, {}}));
    auto const read_tree = decoded<tree>(*cbor::encode(root));
    REQUIRE_EQ(read_tree->kids.size(), 1u);
    CHECK_EQ(read_tree->kids.front()->v, 2);
    REQUIRE_EQ(read_tree->kids.front()->kids.size(), 1u);
    CHECK_EQ(read_tree->kids.front()->kids.front()->v, 3);
    auto const single = decoded<leaf *>(*cbor::encode<leaf *>(&x));
    REQUIRE_NE(*single, nullptr);
    CHECK_EQ((*single)->v, 1);
}

// The owner decided that cbor::decode keeps no identity: two pointers to one object in the value give two objects
// with the same value.
TEST_CASE("structs pointers: cbor::decode gives each pointer its own object")
{
    leaf y{2};
    auto const triple = decoded<raw_triple>(*cbor::encode(raw_triple{&y, &y, nullptr}));
    REQUIRE_NE(triple->a, nullptr);
    REQUIRE_NE(triple->b, nullptr);
    CHECK_NE(triple->a, triple->b);
    CHECK_EQ(triple->a->v, 2);
    CHECK_EQ(triple->b->v, 2);
    CHECK_EQ(triple->c, nullptr);
}

// Review 11 (A3): the owner of the ref keeps the target of each U*. A copy and a move of the ref keep it, and the
// target stays valid after the first ref ends. ASan finds a read of a target that ended.
TEST_CASE("structs pointers: a target of cbor::decode stays valid after a copy and a move of the ref")
{
    leaf x{7};
    std::optional<cbor::ref<raw_triple>> source(decoded<raw_triple>(*cbor::encode(raw_triple{&x, nullptr, nullptr})));
    cbor::ref<raw_triple> const copy = *source;
    cbor::ref<raw_triple> const moved = std::move(*source);
    source.reset();
    REQUIRE_NE(copy->a, nullptr);
    CHECK_EQ(copy->a->v, 7);
    CHECK_EQ(moved->a, copy->a);
}

// Plan 12 (R10.8): cbor::decode reads no tag 28 and no tag 29, also where cbor::encode_cycles wrote them.
TEST_CASE("structs pointers: cbor::decode gives invalid_tag for the bytes of cbor::encode_cycles")
{
    loop self{nullptr};
    self.self = &self;
    auto const bytes = cbor::encode_cycles(self);
    REQUIRE(bytes.has_value());
    auto const r = cbor::structs<loop>::decode(std::string(*bytes));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::invalid_tag);
}

// Plan 11 (item 5): decode_cycles gives a node that points to itself, and the ref keeps the value in its owner, so
// a copy and a move of the ref keep the cycle.
TEST_CASE("structs pointers: decode_cycles reads a node that points to itself")
{
    loop self{nullptr};
    self.self = &self;
    std::optional<cbor::ref<loop>> source(decoded_cycles<loop>(*cbor::encode_cycles(self)));
    CHECK_EQ((*source)->self, &**source);
    cbor::ref<loop> const copy = *source;
    cbor::ref<loop> const moved = std::move(*source);
    source.reset();
    CHECK_EQ(copy->self, &*copy);
    CHECK_EQ(moved->self, &*copy);
}

// A parent pointer closes a cycle at each level: the child points to the root and the grandchild to the child.
// The children stand behind a std::unique_ptr in one tree and by value in a std::vector in the other.
TEST_CASE("structs pointers: decode_cycles reads a tree with parent pointers")
{
    node root{"r", nullptr, {}};
    root.children.push_back(std::make_unique<node>(node{"c", &root, {}}));
    node &child = *root.children.front();
    child.children.push_back(std::make_unique<node>(node{"g", &child, {}}));
    auto const read_root = decoded_cycles<node>(*cbor::encode_cycles(root));
    CHECK_EQ(read_root->parent, nullptr);
    REQUIRE_EQ(read_root->children.size(), 1u);
    node const &read_child = *read_root->children.front();
    CHECK_EQ(read_child.name, "c");
    CHECK_EQ(read_child.parent, &*read_root);
    REQUIRE_EQ(read_child.children.size(), 1u);
    CHECK_EQ(read_child.children.front()->name, "g");
    CHECK_EQ(read_child.children.front()->parent, &read_child);

    family top{"r", nullptr, {}};
    top.kids.push_back(family{"a", &top, {}});
    top.kids.push_back(family{"b", &top, {}});
    top.kids.front().kids.push_back(family{"g", &top.kids.front(), {}});
    auto const read_top = decoded_cycles<family>(*cbor::encode_cycles(top));
    REQUIRE_EQ(read_top->kids.size(), 2u);
    CHECK_EQ(read_top->kids[0].parent, &*read_top);
    CHECK_EQ(read_top->kids[1].parent, &*read_top);
    REQUIRE_EQ(read_top->kids[0].kids.size(), 1u);
    CHECK_EQ(read_top->kids[0].kids[0].parent, &read_top->kids[0]);
}

// Review 11 (A1, A2): one cycle reached on two paths gives two objects, and a tag 28 that no tag 29 names is read.
TEST_CASE("structs pointers: decode_cycles reads each path of a cycle as its own object")
{
    loop self{nullptr};
    self.self = &self;
    auto const loops = decoded_cycles<two_loops>(*cbor::encode_cycles(two_loops{&self, &self}));
    REQUIRE_NE(loops->a, nullptr);
    REQUIRE_NE(loops->b, nullptr);
    CHECK_NE(loops->a, loops->b);
    CHECK_EQ(loops->a->self, loops->a);
    CHECK_EQ(loops->b->self, loops->b);
    ring first{nullptr};
    ring second{&first};
    first.next = &second;
    auto const rings = decoded_cycles<two_rings>(*cbor::encode_cycles(two_rings{&first, &second}));
    REQUIRE_NE(rings->a, nullptr);
    REQUIRE_NE(rings->a->next, nullptr);
    CHECK_EQ(rings->a->next->next, rings->a);
    REQUIRE_NE(rings->b, nullptr);
    REQUIRE_NE(rings->b->next, nullptr);
    CHECK_EQ(rings->b->next->next, rings->b);
}

// cbor2 with value_sharing writes a tag 28 on every map and every array. decode_cycles counts each of them, also
// one in a value that the struct has no member for, so the index of a tag 29 names the right tag 28.
TEST_CASE("structs pointers: decode_cycles counts a tag 28 on every value and in a skipped value")
{
    auto const root = decoded_cycles<node>(bytes_of(R"(28({"name": "r", "parent": null, "children": 28([)"
                                                    R"(28({"name": "c", "parent": 29(0), "children": 28([])})])}))"));
    REQUIRE_EQ(root->children.size(), 1u);
    CHECK_EQ(root->children.front()->parent, &*root);
    auto const self = decoded_cycles<loop>(bytes_of(R"(28({"x": 28([1]), "self": 29(0)}))"));
    CHECK_EQ(self->self, &*self);
    check_cycles_read_refused<loop>(R"(28({"x": 28([1]), "self": 29(1)}))", error::incorrect_type);
}

// Plan 12 (item 6) and plan 2 (item 11): each tag 29 that does not close a cycle at a U* is refused, with the error
// that names the reason.
TEST_CASE("structs pointers: decode_cycles refuses a tag 29 that closes no cycle at a U*")
{
    check_cycles_read_refused<raw_triple>(R"({"a": 28({"v": 1}), "b": 29(0), "c": null})", error::sharedref_not_cyclic);
    check_cycles_read_refused<loop>(R"({"self": 29(0)})", error::sharedref_index_not_marked);
    check_cycles_read_refused<leaf>(R"(28({"v": 29(0)}))", error::incorrect_type);
    check_cycles_read_refused<mixed>(R"({"l": 28({"v": 1}), "p": 29(0)})", error::incorrect_type);
    check_cycles_read_refused<unique_link>(R"(28({"next": 29(0)}))", error::incorrect_type);
    check_cycles_read_refused<shared_link>(R"(28({"next": 29(0)}))", error::cyclic_data_structure);
    check_cycles_read_refused<loop>(R"(28({"self": 29("a")}))", error::inadmissible_type_for_tag_content);
}

// Plan 12 (item 7, A4): a clone of a ref from cbor::decode copies each target and the bytes of its views, so it
// stays valid after the ref and its bytes end.
TEST_CASE("structs pointers: clone of a ref from cbor::decode copies each target")
{
    label raw{"one"};
    auto const shared = std::make_shared<label const>(label{"two"});
    std::optional<cbor::ref<labelled>> source(decoded<labelled>(*cbor::encode(labelled{&raw, shared})));
    auto const own = source->clone();
    REQUIRE_NE(own->raw, nullptr);
    REQUIRE_NE(own->shared, nullptr);
    CHECK_NE(own->raw, (*source)->raw);
    CHECK_NE(own->shared, (*source)->shared);
    source.reset();
    CHECK_EQ(own->raw->text, "one");
    CHECK_EQ(own->shared->text, "two");
}

// Plan 12 (item 7): a clone of a ref from decode_cycles copies each cycle, so a pointer of the copy points into the
// copy.
TEST_CASE("structs pointers: clone of a ref from decode_cycles keeps each cycle")
{
    ring first{nullptr};
    ring second{&first};
    first.next = &second;
    std::optional<cbor::ref<ring>> source(decoded_cycles<ring>(*cbor::encode_cycles(first)));
    auto const own = source->clone();
    REQUIRE_NE(own->next, nullptr);
    CHECK_EQ(own->next->next, &*own);
    CHECK_NE(&*own, &**source);
    source.reset();
    CHECK_EQ(own->next->next->next, own->next);
}

} // namespace structs_pointers

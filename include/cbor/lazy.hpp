/**
 * @file
 * Reads a CBOR data item in place, in bytes with a shared owner.
 * Finds a value by key, by index or by path. Gives a number, a text
 * string, a byte string, an array or a map. Gives a string, an array
 * and a map as a view of these bytes. Reads one element of a typed
 * array. clone copies the data item into a tree of cbor::item.
 * https://www.rfc-editor.org/rfc/rfc8949
 * https://www.rfc-editor.org/rfc/rfc8746#section-2
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#ifdef __STDCPP_FLOAT128_T__
#include <stdfloat>
#endif

#include "rfc8746.hpp"
#include "rfc9535.hpp"
#include "item_size.hpp"
#include "error.hpp"
#include "validity.hpp"
#include "head.hpp"
#include "ref.hpp"
#include "item.hpp"
#include "decode.hpp"

namespace cbor
{

struct lazy_elements;

struct lazy_entries;

struct lazy;

/**
 * @brief The result of a path that can select many nodes: a wildcard, a descendant segment, a slice, a filter or
 * several selectors.
 *
 * Each node is a cbor::lazy and holds the owner of the bytes.
 * A nodelist holds the values, not their places in the item.
 * `lazy.at_path<Path, cbor::nodelist>()`, `compile<cbor::nodelist>` and cbor::at_path() with such a query give a
 * nodelist.
 * A path that selects nothing gives an empty nodelist and no error.
 * The errors of the walk apply (cbor::at_path()).
 * `nodelist_too_long`: one step selects more nodes than the top-level item has bytes, for example a step that
 * repeats one selector many times.
 *
 * @code{.callseq}
 * doc->at_path<"$.a[*]", cbor::nodelist>() → std::expected<nodelist, error>
 * nodes->size()                            → std::size_t
 * (*nodes)[i]                              → lazy const &
 * nodes->empty()                           → bool
 * @endcode
 * @code{.example}
 * message: {"a": [1, 2, 3]}
 * call: auto const nodes = doc->at_path<"$.a[*]", cbor::nodelist>()
 * result: nodelist of 3 nodes: 1, 2, 3
 * call: nodes->size()
 * result: 3
 * call: (*nodes)[2].get<int>()
 * result: 3
 * call: doc->at_path<"$.b", cbor::nodelist>()->empty()
 * result: true
 * @endcode
 *
 * @snippet doc_reference_path.cpp nodelist
 * @see cbor::path::compile()
 * @see cbor::lazy
 *
 * @ingroup path
 */
using nodelist = std::vector<lazy>;

template <class T>
concept scalar_result = (std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
                        std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
                        || std::is_same_v<T, std::float128_t>
#endif
    ;

template <class T>
concept view_result =
    std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>;

template <class T>
concept path_result = scalar_result<T> || view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>;

template <class E>
inline constexpr bool is_path_query_v = false;

/**
 * @brief A position in one top-level item: the owner of the bytes, the bytes and an offset.
 *
 * It holds no decoded value.
 * Each member function reads the bytes at the offset when it is called.
 * cbor::lazy::view() and cbor::lazy::adopt() set the data members.
 * A copy of a lazy is a second position in the same bytes.
 * Several threads read one lazy at the same time.
 * A `nodelist` is the result of a path that selects many nodes.
 * A lazy made with `cbor::lazy{}` holds no top-level item; each member function then throws `std::logic_error`.
 *
 * @code{.callseq}
 * cbor::lazy::adopt(bytes) → std::expected<lazy, error>
 * cbor::lazy const copy = *doc
 * @endcode
 * @code{.example}
 * message: [1, 2]
 * call: cbor::lazy const copy = *doc
 * result: lazy of [1, 2]
 * call: copy.at(1)->get<int>()
 * result: 2
 * @endcode
 *
 * @throws std::logic_error a member function of a lazy that holds no top-level item
 * @snippet doc_reference_lazy.cpp lazy
 * @see cbor::lazy::view()
 * @see cbor::lazy::adopt()
 *
 * @ingroup lazy
 */
struct lazy {
private:
    struct top_level_item {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
    };

public:
    top_level_item top_level;
    std::size_t offset;
    std::uint64_t typed_array_tag{};

    /**
     * @brief Makes the lazy of the top-level item in the bytes, in place.
     *
     * Nothing is copied.
     * The first form reads `encoded`, and `owner` keeps those bytes alive.
     * The second form reads `*owner`.
     * Every lazy, view and `ref` that comes from the result holds `owner`.
     * The call does not check that the bytes are one complete item.
     * A step on the lazy finds a missing part.
     * No step reads bytes after the item.
     * The call reads only the first head, to skip one tag 55799.
     *
     * @code{.callseq}
     * cbor::lazy::view(owner) → std::expected<lazy, error>
     * @endcode
     * @code{.example}
     * message: {"id": 42}
     * call: auto const owner = std::make_shared<std::string const>(bytes)
     * result: shared_ptr to 6 bytes: a1 62 69 64 18 2a
     * call: cbor::lazy::view(owner)->at("id")->get<std::int64_t>()
     * result: 42
     * @endcode
     *
     * @throws std::logic_error an empty `owner` in the first form
     * @snippet doc_reference_lazy.cpp view
     * @see cbor::lazy::adopt()
     * @see cbor::lazy::at()
     */
    template <borrowed_range_of_chars Encoded>
    static std::expected<lazy, error> view(std::shared_ptr<void const> owner, Encoded const &encoded);
    /**
     * @brief Makes the lazy of the top-level item in `*owner`, in place.
     * @throws std::logic_error a null `owner`
     */
    template <owning_range_of_chars Encoded>
    static std::expected<lazy, error> view(std::shared_ptr<Encoded const> owner);
    /**
     * @brief Makes the lazy of the top-level item in @p encoded, and moves the bytes into a new owner.
     *
     * Nothing is copied.
     * Every lazy, view and `ref` that comes from the result holds that owner, so the bytes stay alive as long as one
     * of them lives.
     * `Encoded` is a container of `char` that owns its bytes, for example `std::string`.
     * An lvalue, a `std::string_view` and a literal do not compile.
     * The call reads only the first head, to skip one tag 55799.
     * A step on the lazy reports broken input.
     *
     * @code{.callseq}
     * cbor::lazy::adopt(std::move(bytes)) → std::expected<lazy, error>
     * @endcode
     * @code{.example}
     * message: {"id": 42}
     * call: auto const doc2 = cbor::lazy::adopt(std::move(bytes))
     * result: lazy of {"id": 42}
     * call: doc2->at("id")->get<std::int64_t>()
     * result: 42
     * @endcode
     *
     * @retval not_enough_memory an allocation of the owner fails
     * @snippet doc_reference_lazy.cpp adopt
     * @see cbor::lazy::view()
     * @see cbor::lazy::at()
     */
    template <owning_range_of_chars Encoded>
    static std::expected<lazy, error> adopt(Encoded &&encoded);

    /**
     * @brief Gives the lazy of one child.
     *
     * `at(key)` gives the value of the first pair of a map whose key is the text `key`.
     * `at(cbor::key{n})` gives the value of the first pair whose key is the integer `n`; a bignum key that holds `n`
     * matches too.
     * `at(index)` gives the element of an array or of a typed array, counted from 0.
     * A text key does not match a byte string with the same bytes.
     * The call passes a tag that it does not interpret, and reads into a tag 24.
     * It reads no value; cbor::lazy::get() does.
     *
     * @code{.callseq}
     * doc->at("key")       → std::expected<lazy, error>
     * doc->at(cbor::key{7}) → std::expected<lazy, error>
     * doc->at(1)            → std::expected<lazy, error>
     * @endcode
     * @code{.example}
     * bytes: a2 64 63 61 72 73 82 a1 62 68 70 05 a1 62 68 70 07 07 65 73 65 76 65 6e
     * call: doc->at("cars")->at(1)->at("hp")->get<int>()
     * result: 7
     * call: doc->at(cbor::key{7})->get<std::string_view>()
     * result: ref to "seven"
     * call: doc->at("cars")->size()
     * result: 2
     * call: doc->at("cars")->at(0)->at("hp")->get<int>()
     * result: 5
     * @endcode
     *
     * @retval key_not_found no pair has the key
     * @retval index_out_of_bounds `index` is not less than the count
     * @retval not_indexable a key on a value that is not a map, or an index on a value that is not an array
     * @retval too_little_data, syntax_error, indefinite_length the bytes up to the child are broken
     * @snippet doc_reference_lazy.cpp at
     * @see cbor::lazy::operator[]()
     * @see cbor::lazy::at_path()
     * @see cbor::lazy::get()
     * @see cbor::lazy::entries()
     */
    std::expected<lazy, error> at(std::string_view key) const;

    std::expected<lazy, error> at(key k) const;

    std::expected<lazy, error> at(std::size_t index) const;

    /**
     * @brief Gives the same result as cbor::lazy::at() with the same argument.
     *
     * It does not throw for a missing key or a wrong index.
     * The result is a `std::expected`, so `l["a"]["b"]` does not compile.
     * A path reads a chain (cbor::lazy::at_path()).
     * The errors of cbor::lazy::at() apply.
     *
     * @code{.callseq}
     * (*doc)["key"]       → std::expected<lazy, error>
     * (*doc)[cbor::key{7}] → std::expected<lazy, error>
     * (*doc)[1]            → std::expected<lazy, error>
     * @endcode
     * @code{.example}
     * message: {"cars": [5, 7]}
     * call: (*(*doc)["cars"])[1]->get<int>()
     * result: 7
     * call: (*(*doc)["cars"])[0]->get<int>()
     * result: 5
     * @endcode
     *
     * @snippet doc_reference_lazy.cpp operator_subscript
     * @see cbor::lazy::at()
     * @see cbor::lazy::at_path()
     */
    std::expected<lazy, error> operator[](std::string_view key) const;

    std::expected<lazy, error> operator[](key k) const;

    std::expected<lazy, error> operator[](std::size_t index) const;

    /**
     * @brief Reads the JSONPath (RFC 9535) `Path` from this lazy as the root `$`.
     *
     * Without `T` it gives the lazy of the node.
     * With a scalar `T` it reads the value as cbor::lazy::get() does.
     * A text, a byte string and a typed array come back as a `ref` that holds the owner.
     * `nodelist` gives every selected node.
     * The form with `e` gives the result type of the query; `e` comes from `cbor::path::compile<T>(path)`, which
     * parses the path at run time.
     * A path that is not valid does not compile.
     * A path that can select more than one node does not compile, except with `nodelist`.
     *
     * @code{.callseq}
     * doc->at_path<"$.user.id", std::int64_t>()        → std::expected<std::int64_t, error>
     * doc->at_path<"$.user.name", std::string_view>()  → std::expected<ref<std::string_view>, error>
     * doc->at_path<"$.user.tags[*]", cbor::nodelist>() → std::expected<nodelist, error>
     * doc->at_path<"$.user">()                        → std::expected<lazy, error>
     * @endcode
     * @code{.example}
     * message: {"user": {"id": 42, "tags": ["a", "b"]}}
     * call: doc->at_path<"$.user.id", std::int64_t>()
     * result: 42
     * call: doc->at_path<"$.user.tags[-1]", std::string_view>()
     * result: ref to "b"
     * call: doc->at_path<"$.user.tags[*]", cbor::nodelist>()
     * result: nodelist of 2 nodes: "a", "b"
     * call: doc->at_path<"$.user">()
     * result: lazy of {"id": 42, "tags": ["a", "b"]}
     * @endcode
     *
     * @retval key_not_found, index_out_of_bounds, not_indexable a step finds no node
     * @retval incorrect_type, number_out_of_range the node does not read as `T`
     * @retval nesting_depth_exceeded a descendant segment goes deeper than the stack of the thread holds
     * @throws std::logic_error a query that is empty or moved from
     * @snippet doc_reference_lazy.cpp at_path
     * @see cbor::lazy::at()
     * @see cbor::lazy::get()
     */
    template <fixed_string Path>
        requires is_valid_path_v<Path>
    std::expected<lazy, error> at_path() const;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> && scalar_result<T>)
    std::expected<T, error> at_path() const;

    template <fixed_string Path, class T>
        requires(is_singular_query_v<Path> && view_result<T>)
    std::expected<ref<T>, error> at_path() const;

    template <fixed_string Path, std::same_as<nodelist> T>
        requires is_valid_path_v<Path>
    std::expected<nodelist, error> at_path() const;

    template <class E>
        requires is_path_query_v<E>
    std::expected<typename E::result_type, error> at_path(E const &e) const;

    /**
     * @brief Gives the count of the head: the elements of an array, the pairs of a map, or the elements of a typed
     * array.
     *
     * It reads only the head.
     * It does not read the children.
     *
     * @code{.callseq}
     * doc->size() → std::expected<std::uint64_t, error>
     * @endcode
     * @code{.example}
     * message: {"a": 1, "b": 2}
     * call: doc->size()
     * result: 2
     * call: doc->empty()
     * result: false
     * @endcode
     *
     * @retval not_indexable the value is not an array, a map or a typed array
     * @retval too_little_data the count is larger than the bytes that are left
     * @snippet doc_reference_lazy.cpp size
     * @see cbor::lazy::empty()
     * @see cbor::lazy::elements()
     * @see cbor::lazy::entries()
     */
    std::expected<std::uint64_t, error> size() const;

    /**
     * @brief Gives `true` when cbor::lazy::size() gives 0, and `false` when it gives another count.
     *
     * The errors of cbor::lazy::size() apply.
     *
     * @code{.callseq}
     * doc->empty() → std::expected<bool, error>
     * @endcode
     * @code{.example}
     * message: {"a": [], "b": [1]}
     * call: doc->at("a")->empty()
     * result: true
     * call: doc->at("b")->empty()
     * result: false
     * @endcode
     *
     * @snippet doc_reference_lazy.cpp empty
     * @see cbor::lazy::size()
     */
    std::expected<bool, error> empty() const;

    /**
     * @brief Gives the kind of the value at the lazy.
     *
     * A tag 2 or 3 whose magnitude fits 64 bits gives `unsigned_integer` or `negative_integer`.
     * A larger bignum gives `unsigned_bignum` or `negative_bignum`.
     * A typed array (RFC 8746) gives `typed_array`.
     * Every other tag gives `tag`; the call does not pass it.
     *
     * @code{.callseq}
     * doc->type() → std::expected<cbor::type, error>
     * @endcode
     * @code{.example}
     * bytes: 84 01 61 61 c2 41 05 c1 00
     * call: doc->at(0)->type()
     * result: cbor::type::unsigned_integer
     * call: doc->at(1)->type()
     * result: cbor::type::text_string
     * call: doc->at(2)->type()
     * result: cbor::type::unsigned_integer
     * call: doc->at(3)->type()
     * result: cbor::type::tag
     * @endcode
     *
     * @retval too_little_data, syntax_error the head is broken
     * @snippet doc_reference_lazy.cpp type
     * @see cbor::lazy::get()
     * @see cbor::lazy::clone()
     */
    std::expected<cbor::type, error> type() const;

    /**
     * @brief Reads the value at the lazy as `T`.
     *
     * | T | Reads |
     * |---|---|
     * | an integer type | an integer or a bignum that fits `T` |
     * | `double` | a float, or an integer or a bignum that a `double` holds exactly |
     * | `bool`, `std::nullptr_t` | `false` or `true`, `null` |
     * | `cbor::simple_value` | a simple value |
     * | `std::string_view`, `std::span<std::byte const>` | a text, a byte string, as a `ref` |
     * | `cbor::typed_array` | a typed array (RFC 8746), as a `ref` |
     * | `std::float128_t` | an element of a typed array of 128-bit floats |
     *
     * A `ref` views the bytes and holds their owner.
     * Nothing is copied.
     * The call passes a tag that it does not interpret.
     *
     * @code{.callseq}
     * doc->get<std::int64_t>()     → std::expected<std::int64_t, error>
     * doc->get<double>()           → std::expected<double, error>
     * doc->get<std::string_view>() → std::expected<ref<std::string_view>, error>
     * @endcode
     * @code{.example}
     * message: {"name": "ann", "age": 300}
     * call: auto const name = doc->at("name")->get<std::string_view>()
     * result: ref to "ann"
     * call: **name
     * result: "ann"
     * call: doc->at("age")->get<std::int64_t>()
     * result: 300
     * call: doc->at("age")->get<std::uint16_t>()
     * result: 300
     * call: doc->at("name")->type()
     * result: cbor::type::text_string
     * @endcode
     *
     * @retval incorrect_type the value is of another kind
     * @retval number_out_of_range the number does not fit `T`
     * @retval too_little_data a string is longer than the bytes that are left
     * @snippet doc_reference_lazy.cpp get
     * @see cbor::lazy::at()
     * @see cbor::lazy::type()
     * @see cbor::lazy::clone()
     */
    template <class T>
        requires scalar_result<T> || view_result<T>
    std::expected<std::conditional_t<view_result<T>, ref<T>, T>, error> get() const;

    /**
     * @brief Gives the elements of an array, or of a typed array, as a view.
     *
     * Each element of the view is a `std::expected<lazy, error>`.
     * The call reads only the head.
     * The view reads one element at each step.
     *
     * @code{.callseq}
     * doc->elements() → std::expected<lazy_elements, error>
     * @endcode
     * @code{.example}
     * message: [1, 2, 3]
     * call: doc->elements()
     * result: lazy_elements of 3 elements: 1, 2, 3
     * call: doc->elements()->size()
     * result: 3
     * @endcode
     *
     * @retval not_indexable the value is not an array or a typed array
     * @retval indefinite_length the array has an indefinite length
     * @retval too_little_data the count is larger than the bytes that are left
     * @snippet doc_reference_lazy.cpp elements
     * @see cbor::lazy_elements
     * @see cbor::lazy::entries()
     * @see cbor::lazy::size()
     */
    std::expected<lazy_elements, error> elements() const;

    /**
     * @brief Gives the pairs of a map, in the order of the bytes, as a view.
     *
     * Each element of the view is a `std::expected<std::pair<lazy, lazy>, error>`: the key in `first`, the value in
     * `second`.
     * A key that occurs twice occurs twice in the view.
     *
     * @code{.callseq}
     * doc->entries() → std::expected<lazy_entries, error>
     * @endcode
     * @code{.example}
     * message: {"a": 1, "b": 2}
     * call: doc->entries()
     * result: lazy_entries of 2 pairs: "a": 1, "b": 2
     * @endcode
     *
     * @retval not_indexable the value is not a map
     * @retval indefinite_length the map has an indefinite length
     * @retval too_little_data the count is larger than the bytes that are left
     * @snippet doc_reference_lazy.cpp entries
     * @see cbor::lazy_entries
     * @see cbor::lazy::elements()
     * @see cbor::lazy::at()
     */
    std::expected<lazy_entries, error> entries() const;

    /**
     * @brief Decodes the whole item at the lazy into a tree of cbor::item, as cbor::decode() does, except that tags 28
     * and 29 stay tag nodes, and that a tag 55799 inside the top-level item stays a tag node.
     *
     * The tree holds a copy of each text and byte string.
     * It does not view the bytes of the lazy, and stays valid after the lazy and its owner end.
     * The errors of cbor::decode() apply.
     *
     * @code{.callseq}
     * doc->clone() → std::expected<owning<item>, error>
     * @endcode
     * @code{.example}
     * message: {"a": [1, "x"]}
     * call: doc->at("a")->clone()
     * result: owning item of [1, "x"]
     * call: (*doc->at("a")->clone())->type
     * result: cbor::type::array
     * @endcode
     *
     * @retval too_little_data, syntax_error, indefinite_length, nesting_depth_exceeded a broken item, as
     * cbor::decode() gives
     * @snippet doc_reference_lazy.cpp clone
     * @see cbor::item
     * @see cbor::decode()
     * @see cbor::lazy::get()
     */
    std::expected<owning<item>, error> clone() const;

private:
    template <class Entries = lazy_entries>
    std::expected<typename Entries::iterator, error> find(std::string_view key) const;

    template <class Entries = lazy_entries>
    std::expected<typename Entries::iterator, error> find(std::int64_t key) const;

    void throw_logic_error_if_no_top_level(char const *message) const;

    struct resolved;

    static std::expected<resolved, error> resolve_container(top_level_item source, detail::validity::offset offset);

    static std::expected<resolved, error> resolve_tag_content(top_level_item source, detail::validity::offset offset);

    struct tag_content {
        detail::validity::offset offset;
        std::string_view bytes;
    };

    static std::expected<tag_content, error> read_typed_array_bytes(resolved const &found);

    static std::expected<std::string_view, error> read_typed_array_element(lazy const &node);

    static std::expected<resolved, error> resolve(lazy const &node);

    template <std::integral Index>
    static std::expected<lazy, error> select_index(resolved const &found, Index i);

    template <class Key, class Entries = lazy_entries>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<typename Entries::iterator, error> find_key(resolved found, Key key);

    template <class Iterator, class Last, class Key>
        requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
    static std::expected<std::pair<Iterator, bool>, error> find_key(Iterator first, Last last, Key key);

    static std::expected<bool, error> integer_key_equivalent(heads::decoder<true> const &probe, heads::head const &k,
                                                             std::int64_t key);

    template <class Iterator>
    static std::expected<lazy, error> value_of(std::expected<Iterator, error> found);

    friend struct lazy_elements;

    friend struct lazy_entries;

    friend class path;
};


/**
 * @brief The elements of one array, from cbor::lazy::elements().
 *
 * It is a forward range, a sized range and a borrowed range.
 * `size()` gives the count of the head.
 * `*it` gives the lazy of the element, or the error of the element.
 * An iterator holds the owner of the bytes, so it stays valid after the lazy and the view end.
 * `front()` does not compile.
 * When the iterator cannot step over an element, `*it` gives `too_little_data` or another error of the bytes, and
 * each later position gives that error.
 * The view gives at most `size()` values.
 *
 * @code{.callseq}
 * auto const view = *doc->elements()
 * view.size() → std::uint64_t
 * @endcode
 * @code{.example}
 * message: [{"age": 31}, {"age": 20}, {"age": 40}]
 * call: auto const view = *doc->elements()
 * result: lazy_elements of 3 elements: {"age": 31}, {"age": 20}, {"age": 40}
 * call: view.size()
 * result: 3
 * @endcode
 *
 * @snippet doc_reference_lazy.cpp lazy_elements
 * @see cbor::lazy::elements()
 * @see cbor::lazy_entries
 *
 * @ingroup lazy
 */
struct lazy_elements : std::ranges::view_interface<lazy_elements> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<lazy, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(lazy::top_level_item t, detail::validity::offset const o, std::uint64_t const l,
                 std::uint64_t const tag)
            : top_level(std::move(t)), offset(o), left(l), typed_array_tag(tag)
        {
        }

        lazy::top_level_item top_level;
        detail::validity::offset offset{};
        std::uint64_t left{};
        error failure{};
        std::uint64_t typed_array_tag{};

        friend struct lazy_elements;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count, typed_array_tag};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_elements(lazy::top_level_item t, detail::validity::offset const o, std::uint64_t const c,
                  std::uint64_t const tag)
        : top_level(std::move(t)), offset(o), count(c), typed_array_tag(tag)
    {
    }

    lazy::top_level_item top_level;
    detail::validity::offset offset;
    std::uint64_t count;
    std::uint64_t typed_array_tag;

    friend struct lazy;
};

/**
 * @brief The pairs of one map, from cbor::lazy::entries(), in the order of the bytes.
 *
 * It is a forward range, a sized range and a borrowed range.
 * `size()` gives the count of pairs in the head.
 * `*it` gives the key in `first` and the value in `second`, or the error of the pair.
 * An iterator holds the owner of the bytes, so it stays valid after the lazy and the view end.
 * `front()` does not compile.
 * When the iterator cannot step over a key or a value, `*it` gives `too_little_data` or another error of the bytes,
 * and each later position gives that error.
 * The view gives at most `size()` values.
 *
 * @code{.callseq}
 * auto const view = *doc->entries()
 * view.size() → std::uint64_t
 * @endcode
 * @code{.example}
 * message: {"a": 1, "b": 2, "c": 3}
 * call: auto const view = *doc->entries()
 * result: lazy_entries of 3 pairs: "a": 1, "b": 2, "c": 3
 * call: view.size()
 * result: 3
 * @endcode
 *
 * @snippet doc_reference_lazy.cpp lazy_entries
 * @see cbor::lazy::entries()
 * @see cbor::lazy_elements
 *
 * @ingroup lazy
 */
struct lazy_entries : std::ranges::view_interface<lazy_entries> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;

        iterator() = default;

        value_type operator*() const;

        iterator &operator++();

        iterator operator++(int)
        {
            iterator const before = *this;
            ++*this;
            return before;
        }

        bool operator==(iterator const &) const;

        bool operator==(std::default_sentinel_t) const
        {
            return left == 0;
        }

    private:
        iterator(lazy::top_level_item t, detail::validity::offset const k, std::uint64_t const l)
            : top_level(std::move(t)), key(k), value(k), left(l)
        {
            find_value();
        }

        void find_value();

        lazy::top_level_item top_level;
        detail::validity::offset key{};
        detail::validity::offset value{};
        std::uint64_t left{};
        error failure{};

        friend struct lazy_entries;

        friend struct lazy;
    };

    iterator begin() const
    {
        return iterator{top_level, offset, count};
    }

    std::default_sentinel_t end() const
    {
        return {};
    }

    std::uint64_t size() const
    {
        return count;
    }

    void front() const = delete;

private:
    lazy_entries(lazy::top_level_item t, detail::validity::offset const o, std::uint64_t const c)
        : top_level(std::move(t)), offset(o), count(c)
    {
    }

    lazy::top_level_item top_level;
    detail::validity::offset offset;
    std::uint64_t count;

    friend struct lazy;
};

}

/// @cond
template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_elements> = true;

template <>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::lazy_entries> = true;
/// @endcond

namespace cbor
{

namespace detail
{

class canonical_ids
{
public:
    canonical_ids() = default;
    canonical_ids(canonical_ids const &) = delete;
    canonical_ids &operator=(canonical_ids const &) = delete;
    canonical_ids(canonical_ids &&) = delete;
    canonical_ids &operator=(canonical_ids &&) = delete;

    enum class kind : std::uint8_t {
        positive_integer,
        negative_integer,
        floating,
        nan,
        byte_string,
        text_string,
        tag,
        simple,
        array,
        map,
    };

    struct node {
        kind of;
        std::uint64_t number;
        std::string_view bytes;
        std::size_t first;
        std::size_t count;
    };

    std::uint64_t id_of(node const &n)
    {
        auto const [at, added] = ids.try_emplace(n, ids.size());
        if (!added)
            children.resize(n.first);
        return at->second;
    }

    std::size_t children_end() const
    {
        return children.size();
    }

    std::uint64_t id_of(kind const of, std::uint64_t const number, std::span<std::uint64_t const> const items)
    {
        std::size_t const first = children.size();
        children.insert(children.end(), items.begin(), items.end());
        return id_of({of, number, {}, first, items.size()});
    }

    std::uint64_t id_of(std::vector<std::pair<std::uint64_t, std::uint64_t>> pairs)
    {
        std::ranges::sort(pairs);
        if (std::ranges::adjacent_find(pairs, {}, &std::pair<std::uint64_t, std::uint64_t>::first) != pairs.end())
            mark_unequal();
        std::size_t const first = children.size();
        for (auto const &[key, value] : pairs) {
            children.push_back(key);
            children.push_back(value);
        }
        return id_of({kind::map, 0, {}, first, children.size() - first});
    }

    std::size_t size() const
    {
        return ids.size();
    }

    void mark_unequal()
    {
        unequal_seen = true;
    }

    bool unequal() const
    {
        return unequal_seen;
    }

private:
    struct order {
        std::vector<std::uint64_t> const *children;

        bool operator()(node const &x, node const &y) const
        {
            if (auto const c = std::tie(x.of, x.number, x.bytes, x.count) <=> std::tie(y.of, y.number, y.bytes, y.count);
                c != 0)
                return c < 0;
            return std::ranges::lexicographical_compare(std::span(*children).subspan(x.first, x.count),
                                                        std::span(*children).subspan(y.first, y.count));
        }
    };

    std::vector<std::uint64_t> children;
    std::map<node, std::uint64_t, order> ids{order{&children}};
    bool unequal_seen = false;
};

}

template <bool Checked, class Message>
std::expected<std::pair<std::uint64_t, detail::validity::offset>, error>
detail::validity::intern_key(canonical_ids &ids, Message &message, offset const at)
{
    if (auto const r = check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    std::string_view encoded;
    offset item_at = at;
    if constexpr (std::same_as<Message, std::string_view const>) {
        encoded = message;
    } else {
        encoded = message.encoded;
        item_at = heads::self_described_cbor_content_at(message.encoded, at);
    }
    auto const h = heads::read_raw_head<Checked>(encoded, item_at);
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    if (error const c = check_definite_length(h->major, h->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    using kind = canonical_ids::kind;
    auto const scalar = [&ids](kind const of, std::uint64_t const number, std::string_view const bytes,
                               offset const end) -> std::pair<std::uint64_t, offset> {
        return {ids.id_of({of, number, bytes, ids.children_end(), 0}), end};
    };
    if (heads::is_bignum(h->major, h->argument)) {
        heads::decoder<Checked> d{subspan(encoded, h->at)};
        auto const read = d.decode_bignum(h->argument);
        if (!read) [[unlikely]]
            return std::unexpected(read.error());
        if (*read) {
            bool const negative = h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
            return scalar(negative ? kind::negative_integer : kind::positive_integer, **read, {},
                          distance(encoded, d.encoded));
        }
    }
    switch (h->major) {
    case major_type::unsigned_integer:
        return scalar(kind::positive_integer, h->argument, {}, h->at);
    case major_type::negative_integer:
        return scalar(kind::negative_integer, h->argument, {}, h->at);
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder<Checked> d{subspan(encoded, h->at)};
        auto const s = d.decode_string(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        return scalar(h->major == major_type::byte_string ? kind::byte_string : kind::text_string, 0, *s,
                      distance(encoded, d.encoded));
    }
    case major_type::array:
    case major_type::map: {
        bool const map = h->major == major_type::map;
        if (auto const r = check_pending_items(h->major, h->info, h->argument, subspan(encoded, h->at).size());
            !r) [[unlikely]]
            return std::unexpected(r.error());
        std::vector<std::uint64_t> items;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> pairs;
        offset next = h->at;
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const key = intern_key<Checked>(ids, message, next);
            if (!key) [[unlikely]]
                return std::unexpected(key.error());
            next = key->second;
            if (!map) {
                items.push_back(key->first);
                continue;
            }
            auto const value = intern_key<Checked>(ids, message, next);
            if (!value) [[unlikely]]
                return std::unexpected(value.error());
            next = value->second;
            pairs.emplace_back(key->first, value->first);
        }
        if (map)
            return std::pair{ids.id_of(std::move(pairs)), next};
        return std::pair{ids.id_of(kind::array, 0, items), next};
    }
    case major_type::tag: {
        auto const content = intern_key<Checked>(ids, message, h->at);
        if (!content) [[unlikely]]
            return std::unexpected(content.error());
        std::array<std::uint64_t, 1> const item{content->first};
        return std::pair{ids.id_of(kind::tag, h->argument, item), content->second};
    }
    case major_type::simple_float:
        break;
    }
    if (!heads::is_floating_point(h->major, h->info)) {
        if (error const c = check_simple_value(h->info, h->argument).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return scalar(kind::simple, h->argument, {}, h->at);
    }
    heads::float_key const p = heads::float_key_of(h->info, h->argument);
    if (p.nan) {
        constexpr std::uint64_t significand = (std::uint64_t{1} << heads::double_precision.significand_bits) - 1u;
        return scalar(kind::nan, p.widened & significand, {}, h->at);
    }
    return scalar(kind::floating, std::bit_cast<std::uint64_t>(p.value == 0.0 ? 0.0 : p.value), {}, h->at);
}

template <bool Checked, class First, class Second>
std::expected<bool, error>
detail::validity::keys_equivalent(First const &first, offset const first_at, Second const &second,
                                  offset const second_at)
{
    if (auto const r = check_recursion_limit(); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const encoded_of = []<class Message>(Message &message) -> std::string_view {
        if constexpr (std::same_as<Message, std::string_view const>)
            return message;
        else
            return message.encoded;
    };
    auto const skip = [](heads::decoder<Checked> &d) {
        well_formedness::no_marks none;
        return well_formedness::skip_item(d, none);
    };
    auto const resolve = []<class Message>(Message &message, offset const at) -> offset {
        if constexpr (std::same_as<Message, std::string_view const>)
            return at;
        else
            return heads::self_described_cbor_content_at(message.encoded, at);
    };
    std::string_view const a = encoded_of(first);
    std::string_view const b = encoded_of(second);
    auto const h = heads::read_raw_head<Checked>(a, resolve(first, first_at));
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    auto const k = heads::read_raw_head<Checked>(b, resolve(second, second_at));
    if (!k) [[unlikely]]
        return std::unexpected(k.error());
    if (error const c = check_definite_length(h->major, h->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (error const c = check_definite_length(k->major, k->info).error_or(error{}); c != error{}) [[unlikely]]
        return std::unexpected(c);
    if (h->major != k->major || heads::is_bignum(h->major, h->argument)) [[unlikely]] {
        auto const integer_of = [](std::string_view const encoded, heads::raw_head const n)
            -> std::expected<std::optional<std::pair<bool, std::uint64_t>>, error> {
            if (n.major == major_type::unsigned_integer || n.major == major_type::negative_integer)
                return std::pair{n.major == major_type::negative_integer, n.argument};
            if (!heads::is_bignum(n.major, n.argument))
                return std::nullopt;
            heads::decoder<Checked> d{subspan(encoded, n.at)};
            auto const read = d.decode_bignum(n.argument);
            if (!read) [[unlikely]]
                return std::unexpected(read.error());
            if (!*read)
                return std::nullopt;
            return std::pair{n.argument == std::to_underlying(rfc8949::tag_number::negative_bignum), **read};
        };
        auto const x_value = integer_of(a, *h);
        if (!x_value) [[unlikely]]
            return std::unexpected(x_value.error());
        auto const y_value = integer_of(b, *k);
        if (!y_value) [[unlikely]]
            return std::unexpected(y_value.error());
        if (*x_value && *y_value)
            return **x_value == **y_value;
        if (h->major != k->major)
            return false;
    }
    switch (h->major) {
    case major_type::unsigned_integer:
    case major_type::negative_integer:
        return h->argument == k->argument;
    case major_type::byte_string:
    case major_type::text_string: {
        heads::decoder<Checked> d{subspan(a, h->at)};
        auto const s = d.decode_string(h->argument);
        if (!s) [[unlikely]]
            return std::unexpected(s.error());
        heads::decoder<Checked> e{subspan(b, k->at)};
        auto const t = e.decode_string(k->argument);
        if (!t) [[unlikely]]
            return std::unexpected(t.error());
        return *s == *t;
    }
    case major_type::array: {
        if (h->argument != k->argument)
            return false;
        heads::decoder<Checked> d{subspan(a, h->at)};
        heads::decoder<Checked> e{subspan(b, k->at)};
        if (auto const r =
                detail::validity::check_pending_items(h->major, h->info, h->argument, d.encoded.size());
            !r) [[unlikely]]
            return std::unexpected(r.error());
        if (auto const r =
                detail::validity::check_pending_items(k->major, k->info, k->argument, e.encoded.size());
            !r) [[unlikely]]
            return std::unexpected(r.error());
        for (std::uint64_t i = 0; i < h->argument; ++i) {
            auto const equal = keys_equivalent<Checked>(first, distance(a, d.encoded), second,
                                                        distance(b, e.encoded));
            if (!equal || !*equal)
                return equal;
            if (auto const r = skip(d); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = skip(e); !r) [[unlikely]]
                return std::unexpected(r.error());
        }
        return true;
    }
    case major_type::map: {
        if (h->argument != k->argument)
            return false;
        canonical_ids ids;
        auto const x = intern_key<Checked>(ids, first, first_at);
        if (!x) [[unlikely]]
            return std::unexpected(x.error());
        auto const y = intern_key<Checked>(ids, second, second_at);
        if (!y) [[unlikely]]
            return std::unexpected(y.error());
        return x->first == y->first;
    }
    case major_type::tag:
        if (h->argument != k->argument)
            return false;
        return keys_equivalent<Checked>(first, h->at, second, k->at);
    case major_type::simple_float:
        break;
    }
    bool const h_float = heads::is_floating_point(h->major, h->info);
    bool const k_float = heads::is_floating_point(k->major, k->info);
    if (h_float != k_float)
        return false;
    if (!h_float)
        return h->argument == k->argument;
    heads::float_key const p = heads::float_key_of(h->info, h->argument);
    heads::float_key const q = heads::float_key_of(k->info, k->argument);
    if (p.nan || q.nan) {
        constexpr std::uint64_t significand =
            (std::uint64_t{1} << heads::double_precision.significand_bits) - 1u;
        return p.nan && q.nan && (p.widened & significand) == (q.widened & significand);
    }
    return p.value == q.value;
}

struct lazy::resolved {
    lazy::top_level_item source;
    heads::head h;
    heads::decoder<true> d;
    detail::validity::offset item_at;
};

inline std::expected<lazy::resolved, error> lazy::resolve_container(lazy::top_level_item source,
                                                                    detail::validity::offset offset)
{
    detail::validity::throw_logic_error_if(source.encoded.data() == nullptr,
                                           "cbor::lazy: the lazy holds no top-level item");
    for (;;) {
        detail::validity::offset const at = heads::self_described_cbor_content_at(source.encoded, offset);
        heads::decoder<true> d{detail::validity::subspan(source.encoded, at)};
        auto const h = d.decode_head();
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        if (h->major != major_type::tag ||
            h->argument != std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item))
            return resolved{std::move(source), *h, d, at};
        auto const r = d.decode_head();
        if (!r) [[unlikely]]
            return std::unexpected(r.error());
        if (error const c = detail::validity::check_tag_content(h->argument, r->major).error_or(error{});
            c != error{}) [[unlikely]]
            return std::unexpected(c);
        auto const embedded = d.decode_string(r->argument);
        if (!embedded) [[unlikely]]
            return std::unexpected(embedded.error());
        source = lazy::top_level_item{source.owner, *embedded};
        offset = detail::validity::offset{};
    }
}

inline std::expected<lazy::resolved, error> lazy::resolve_tag_content(lazy::top_level_item source,
                                                                      detail::validity::offset offset)
{
    for (;;) {
        auto found = resolve_container(std::move(source), offset);
        if (!found) [[unlikely]]
            return found;
        if (found->h.major != major_type::tag || !heads::is_tag_passed(found->h.argument)) [[likely]] {
            if (auto const r = detail::validity::check_pending_items(
                    found->h.major, found->h.info, found->h.argument, found->d.encoded.size());
                !r) [[unlikely]]
                return std::unexpected(r.error());
            return found;
        }
        source = std::move(found->source);
        offset = detail::validity::distance(source.encoded, found->d.encoded);
    }
}

inline std::expected<lazy::tag_content, error> lazy::read_typed_array_bytes(resolved const &found)
{
    if (found.h.major != major_type::tag) [[unlikely]]
        return std::unexpected(error::incorrect_type);
    if (error const r = detail::validity::check_typed_array(found.h.argument, 0).error_or(error{});
        r != error{}) [[unlikely]]
        return std::unexpected(r);
    heads::decoder<true> d = found.d;
    auto const r = d.decode_head();
    if (!r) [[unlikely]]
        return std::unexpected(r.error());
    if (error const c = detail::validity::check_typed_array_content(found.h.argument, r->major, r->argument)
                            .error_or(error{});
        c != error{}) [[unlikely]]
        return std::unexpected(c);
    auto const offset = detail::validity::offset{detail::validity::distance(found.source.encoded, d.encoded)};
    auto const bytes = d.decode_string(r->argument);
    if (!bytes) [[unlikely]]
        return std::unexpected(bytes.error());
    return tag_content{offset, *bytes};
}

inline lazy_elements::iterator::value_type lazy_elements::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return lazy{top_level, std::to_underlying(offset), typed_array_tag};
}

inline bool lazy_elements::iterator::operator==(iterator const &other) const
{
    return offset == other.offset && typed_array_tag == other.typed_array_tag && left == other.left &&
           failure == other.failure &&
           top_level.encoded.data() == other.top_level.encoded.data();
}

inline lazy_elements::iterator &lazy_elements::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    if (typed_array_tag != 0) {
        offset = detail::validity::next(offset, detail::validity::typed_array_element_size(typed_array_tag));
        --left;
        return *this;
    }
    heads::decoder<false> d{detail::validity::subspan(top_level.encoded, offset)};
    if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]] {
        failure = r.error();
        --left;
        return *this;
    }
    offset = detail::validity::distance(top_level.encoded, d.encoded);
    --left;
    if (left != 0)
        if (auto const r = detail::validity::check_pending_items(left, d.encoded.size()); !r) [[unlikely]]
            failure = r.error();
    return *this;
}

inline void lazy_entries::iterator::find_value()
{
    if (left == 0)
        return;
    heads::decoder<false> d{detail::validity::subspan(top_level.encoded, key)};
    if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]] {
        failure = r.error();
        return;
    }
    if (auto const fits = detail::validity::check_bytes_left(heads::initial_byte_size, d.encoded.size());
        !fits) [[unlikely]] {
        failure = fits.error();
        return;
    }
    value = detail::validity::distance(top_level.encoded, d.encoded);
}

inline lazy_entries::iterator::value_type lazy_entries::iterator::operator*() const
{
    if (failure != error{}) [[unlikely]]
        return std::unexpected(failure);
    return std::pair{lazy{top_level, std::to_underlying(key), {}}, lazy{top_level, std::to_underlying(value), {}}};
}

inline bool lazy_entries::iterator::operator==(iterator const &other) const
{
    return key == other.key && value == other.value && left == other.left && failure == other.failure &&
           top_level.encoded.data() == other.top_level.encoded.data();
}

inline lazy_entries::iterator &lazy_entries::iterator::operator++()
{
    if (failure != error{}) [[unlikely]] {
        --left;
        return *this;
    }
    heads::decoder<false> d{detail::validity::subspan(top_level.encoded, value)};
    if (auto const r = well_formedness::skip_items(d, 1); !r) [[unlikely]] {
        failure = r.error();
        --left;
        return *this;
    }
    key = detail::validity::distance(top_level.encoded, d.encoded);
    --left;
    find_value();
    return *this;
}

inline void lazy::throw_logic_error_if_no_top_level(char const *const message) const
{
    detail::validity::throw_logic_error_if(top_level.encoded.data() == nullptr, message);
}

template <borrowed_range_of_chars Encoded>
CBOR_ALWAYS_INLINE inline std::expected<lazy, error> lazy::view(std::shared_ptr<void const> owner, Encoded const &encoded)
{
    detail::validity::throw_logic_error_if_empty(
        owner, "cbor::lazy::view: the owner of the encoded data item is empty");
    std::string_view const content =
        heads::self_described_cbor_content(std::string_view(std::span<char const>(encoded)));
    return lazy{lazy::top_level_item{std::move(owner), content}, 0, {}};
}

template <owning_range_of_chars Encoded>
CBOR_ALWAYS_INLINE inline std::expected<lazy, error> lazy::view(std::shared_ptr<Encoded const> owner)
{
    detail::validity::throw_logic_error_if_null(owner, "cbor::lazy::view: the encoded data item is null");
    std::string_view const bytes{std::span<char const>(*owner)};
    return view(std::shared_ptr<void const>(std::move(owner)), bytes);
}

template <owning_range_of_chars Encoded>
std::expected<lazy, error> lazy::adopt(Encoded &&encoded)
{
    auto owner = detail::validity::make_shared_owner(std::move(encoded));
    if (!owner) [[unlikely]]
        return std::unexpected(owner.error());
    return view(std::move(*owner));
}

template <class Key, class Entries>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<typename Entries::iterator, error> lazy::find_key(resolved found, Key const key)
{
    if (heads::is_typed_array(found.h)) [[unlikely]]
        if (auto const bytes = read_typed_array_bytes(found); !bytes) [[unlikely]]
            return std::unexpected(bytes.error());
    if (found.h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    detail::validity::offset const first = detail::validity::distance(found.source.encoded, found.d.encoded);
    if (auto const r = detail::validity::check_pending_items(found.h.major, found.h.info, found.h.argument,
                                                             found.d.encoded.size());
        !r) [[unlikely]]
        return std::unexpected(r.error());
    typename Entries::iterator it(std::move(found.source), first, found.h.argument);
    auto found_at = find_key<typename Entries::iterator, std::default_sentinel_t, Key>(
        std::move(it), std::default_sentinel, key);
    if (!found_at) [[unlikely]]
        return std::unexpected(found_at.error());
    if (found_at->first == std::default_sentinel && found_at->first.failure != error{}) [[unlikely]]
        return std::unexpected(found_at->first.failure);
    return std::move(found_at->first);
}

template <class Iterator, class Last, class Key>
    requires std::same_as<Key, std::string_view> || std::same_as<Key, std::int64_t>
std::expected<std::pair<Iterator, bool>, error> lazy::find_key(Iterator it, Last const last, Key const key)
{
    for (; it != last; ++it) {
        if (it == std::default_sentinel) [[unlikely]] {
            if constexpr (!std::same_as<Last, std::default_sentinel_t>)
                detail::validity::throw_logic_error(
                    "cbor::lazy: the last iterator is not a later pair of the same map");
            break;
        }
        if (it.failure != error{}) [[unlikely]]
            return std::unexpected(it.failure);
        detail::validity::offset const key_at =
            heads::self_described_cbor_content_at(it.top_level.encoded, it.key);
        heads::decoder<true> probe{detail::validity::subspan(it.top_level.encoded, key_at)};
        auto const k = probe.decode_head();
        if (!k) [[unlikely]]
            return std::unexpected(k.error());
        bool equal = false;
        if constexpr (std::same_as<Key, std::string_view>) {
            if (k->major == major_type::text_string) {
                auto const content = probe.decode_string(k->argument);
                if (!content) [[unlikely]]
                    return std::unexpected(content.error());
                equal = *content == key;
            }
        } else {
            auto const same = integer_key_equivalent(probe, *k, key);
            if (!same) [[unlikely]]
                return std::unexpected(same.error());
            equal = *same;
        }
        if (equal)
            return std::pair{std::move(it), true};
    }
    return std::pair{std::move(it), false};
}

inline std::expected<bool, error> lazy::integer_key_equivalent(heads::decoder<true> const &probe,
                                                               heads::head const &k, std::int64_t const key)
{
    std::uint64_t const magnitude =
        key < 0 ? ~static_cast<std::uint64_t>(key) : static_cast<std::uint64_t>(key);
    if (heads::is_bignum(k.major, k.argument)) [[unlikely]] {
        heads::decoder<true> d = probe;
        auto const read = d.decode_bignum(k.argument);
        if (!read) [[unlikely]]
            return std::unexpected(read.error());
        bool const negative = k.argument == std::to_underlying(rfc8949::tag_number::negative_bignum);
        return *read && negative == (key < 0) && **read == magnitude;
    }
    return k.argument == magnitude &&
           k.major == (key < 0 ? major_type::negative_integer : major_type::unsigned_integer);
}

template <class Entries>
std::expected<typename Entries::iterator, error> lazy::find(std::string_view const key) const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return find_key(std::move(*found), key);
}

template <class Entries>
std::expected<typename Entries::iterator, error> lazy::find(std::int64_t const key) const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return find_key(std::move(*found), key);
}

inline std::expected<std::string_view, error> lazy::read_typed_array_element(lazy const &node)
{
    node.throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    return detail::validity::substr(node.top_level.encoded, detail::validity::offset{node.offset},
                                    detail::validity::typed_array_element_size(node.typed_array_tag));
}

inline std::expected<lazy::resolved, error> lazy::resolve(lazy const &node)
{
    if (node.typed_array_tag == 0) [[likely]]
        return resolve_tag_content(node.top_level, detail::validity::offset{node.offset});
    auto const element = read_typed_array_element(node);
    if (!element) [[unlikely]]
        return std::unexpected(element.error());
    auto const h =
        heads::decode_typed_array_element(node.typed_array_tag, *element, detail::validity::index{});
    if (!h) [[unlikely]]
        return std::unexpected(h.error());
    return resolved{node.top_level, *h, heads::decoder<true>{std::string_view{}},
                    detail::validity::offset{node.offset}};
}

template <std::integral Index>
std::expected<lazy, error> lazy::select_index(resolved const &found, Index const i)
{
    if (heads::is_typed_array(found.h)) {
        auto const bytes = read_typed_array_bytes(found);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        std::size_t const size = detail::validity::typed_array_element_size(found.h.argument);
        auto const position = detail::validity::check_index(i, bytes->bytes.size() / size);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        return lazy{found.source, std::to_underlying(detail::validity::element_offset(bytes->offset, size, *position)),
                    found.h.argument};
    }
    if (found.h.major != major_type::array) [[unlikely]]
        return std::unexpected(error::not_indexable);
    auto const position = detail::validity::check_index(i, found.h.argument);
    if (!position) [[unlikely]]
        return std::unexpected(position.error());
    heads::decoder<true> d = found.d;
    if (auto const r = well_formedness::skip_items(d, std::to_underlying(*position)); !r) [[unlikely]]
        return std::unexpected(r.error());
    detail::validity::offset const element = detail::validity::distance(found.source.encoded, d.encoded);
    if (auto const target = heads::decoder<true>(d).decode_head(); !target) [[unlikely]]
        return std::unexpected(target.error());
    return lazy{found.source, std::to_underlying(element), {}};
}

inline std::expected<std::uint64_t, error> lazy::size() const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto const found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (heads::is_typed_array(found->h))
        return read_typed_array_bytes(*found).transform([&](tag_content const &bytes) -> std::uint64_t {
            return bytes.bytes.size() / detail::validity::typed_array_element_size(found->h.argument);
        });
    auto const &[source, h, d, item_at] = *found;
    if (h.major != major_type::array && h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    auto const items = detail::validity::checked_mul(
        h.argument, h.major == major_type::map ? rfc8949::data_items_per_pair : 1);
    if (!items) [[unlikely]]
        return std::unexpected(error::too_little_data);
    if (auto const r = detail::validity::check_pending_items(*items, d.encoded.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    return h.argument;
}

inline std::expected<bool, error> lazy::empty() const
{
    return size().transform([](std::uint64_t const n) { return n == 0; });
}

inline std::expected<cbor::type, error> lazy::type() const
{
    throw_logic_error_if_no_top_level("cbor::lazy::type: the lazy holds no top-level item");
    if (typed_array_tag != 0) [[unlikely]] {
        auto const found = resolve(*this);
        if (!found) [[unlikely]]
            return std::unexpected(found.error());
        return heads::type_of(found->h.major, found->h.info, found->h.argument);
                            }
                            auto const h = heads::read_raw_head<true>(top_level.encoded, detail::validity::offset{offset});
                            if (!h) [[unlikely]]
                                return std::unexpected(h.error());
                            if (!heads::is_bignum(h->major, h->argument)) [[likely]]
                                return heads::type_of(h->major, h->info, h->argument);
                            auto const c = heads::read_raw_head<true>(top_level.encoded, h->at);
                            if (!c) [[unlikely]]
                                return std::unexpected(c.error());
                            if (c->major != major_type::byte_string ||
                                !detail::validity::check_definite_length(c->major, c->info).has_value())
                                return heads::type_of(h->major, h->info, h->argument);
                            heads::decoder<true> d{detail::validity::subspan(top_level.encoded, h->at)};
                            auto const read = d.decode_bignum(h->argument);
                            if (!read) [[unlikely]]
                                return std::unexpected(read.error());
                            if (!*read)
                                return heads::type_of(h->major, h->info, h->argument);
                            return h->argument == std::to_underlying(rfc8949::tag_number::negative_bignum)
                                       ? cbor::type::negative_integer
                                       : cbor::type::unsigned_integer;
}

template <class Iterator>
std::expected<lazy, error> lazy::value_of(std::expected<Iterator, error> found)
{
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (*found == std::default_sentinel) [[unlikely]]
        return std::unexpected(error::key_not_found);
    return lazy{std::move(found->top_level), std::to_underlying(found->value), {}};
}

inline std::expected<lazy, error> lazy::at(std::string_view const key) const
{
    return value_of(find(key));
}

inline std::expected<lazy, error> lazy::at(key const k) const
{
    if (k.text != nullptr)
        return at(std::string_view(k.text));
    return value_of(find(k.number));
}

inline std::expected<lazy, error> lazy::at(std::size_t const index) const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto const found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    return select_index(*found, index);
}

template <class T>
    requires scalar_result<T> || view_result<T>
std::expected<std::conditional_t<view_result<T>, ref<T>, T>, error> lazy::get() const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    if constexpr (std::is_same_v<T, std::span<std::byte const>>) {
        if (typed_array_tag != 0) {
            auto const element = read_typed_array_element(*this);
            if (!element) [[unlikely]]
                return std::unexpected(element.error());
            return ref<T>(top_level.owner, std::as_bytes(std::span(*element)));
        }
            }
#ifdef __STDCPP_FLOAT128_T__
            if constexpr (std::is_same_v<T, std::float128_t>) {
                if (typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_big_endian) &&
                    typed_array_tag != std::to_underlying(rfc8746::tag_number::float128_little_endian)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
                auto const element = read_typed_array_element(*this);
                if (!element) [[unlikely]]
                    return std::unexpected(element.error());
                std::array<std::byte, sizeof(std::float128_t)> bits{};
                std::ranges::copy(std::as_bytes(std::span(*element)), bits.begin());
                bool const little_endian =
                    typed_array_tag == std::to_underlying(rfc8746::tag_number::float128_little_endian);
                if (little_endian != (std::endian::native == std::endian::little))
                    std::ranges::reverse(bits);
                return std::bit_cast<std::float128_t>(bits);
            } else {
#endif
                auto found = resolve(*this);
                if (!found) [[unlikely]]
                    return std::unexpected(found.error());
                auto &[source, h, d, item_at] = *found;
                if constexpr (std::is_same_v<T, typed_array>) {
                auto const bytes = read_typed_array_bytes(*found);
                if (!bytes) [[unlikely]]
                    return std::unexpected(bytes.error());
                return ref<T>(source.owner, typed_array{h.argument, std::as_bytes(std::span(bytes->bytes))});
            } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>>) {
                auto const value = d.template decode_value<T>(h);
                if (!value) [[unlikely]]
                    return std::unexpected(value.error());
                return ref<T>(source.owner, *value);
            } else {
                return d.template decode_value<T>(h);
            }
#ifdef __STDCPP_FLOAT128_T__
            }
#endif
}

inline std::expected<lazy_elements, error> lazy::elements() const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto const found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    if (heads::is_typed_array(found->h)) {
        auto const bytes = read_typed_array_bytes(*found);
        if (!bytes) [[unlikely]]
            return std::unexpected(bytes.error());
        return lazy_elements(found->source, bytes->offset,
                             bytes->bytes.size() /
                                 detail::validity::typed_array_element_size(found->h.argument),
                             found->h.argument);
            }
            auto const &[source, h, d, item_at] = *found;
            if (h.major != major_type::array) [[unlikely]]
                return std::unexpected(error::not_indexable);
            if (auto const r = detail::validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
                return std::unexpected(r.error());
            if (auto const r = detail::validity::check_pending_items(h.argument, d.encoded.size()); !r)
                [[unlikely]]
                return std::unexpected(r.error());
            return lazy_elements(source, detail::validity::distance(source.encoded, d.encoded), h.argument,
                                 {});
}

inline std::expected<lazy_entries, error> lazy::entries() const
{
    throw_logic_error_if_no_top_level("cbor::lazy: the lazy holds no top-level item");
    auto const found = resolve(*this);
    if (!found) [[unlikely]]
        return std::unexpected(found.error());
    auto const &[source, h, d, item_at] = *found;
    if (h.major != major_type::map) [[unlikely]]
        return std::unexpected(error::not_indexable);
    if (auto const r = detail::validity::check_definite_length(h.major, h.info); !r) [[unlikely]]
        return std::unexpected(r.error());
    auto const items = detail::validity::checked_mul(h.argument, rfc8949::data_items_per_pair);
    if (!items) [[unlikely]]
        return std::unexpected(error::too_little_data);
    if (auto const r = detail::validity::check_pending_items(*items, d.encoded.size()); !r) [[unlikely]]
        return std::unexpected(r.error());
    return lazy_entries(source, detail::validity::distance(source.encoded, d.encoded), h.argument);
}

inline std::expected<owning<item>, error> lazy::clone() const
{
    throw_logic_error_if_no_top_level("cbor::lazy::clone: the lazy holds no top-level item");
    if (typed_array_tag != 0) [[unlikely]] {
        auto const h = resolve(*this).transform([](resolved const &found) { return found.h; });
        if (!h) [[unlikely]]
            return std::unexpected(h.error());
        std::string encoded;
        heads::append_head(encoded, h->major, h->info, h->argument);
        return cbor::decode(encoded);
    }
    if (offset == 0)
        return detail::decode_tree<false>(top_level.encoded)
            .transform([](detail::decoded_tree const &d) { return d.tree; });
    std::string_view const rest = detail::validity::subspan(top_level.encoded, detail::validity::offset{offset});
    auto const size = item_size(rest);
    if (!size) [[unlikely]]
        return std::unexpected(size.error());
    auto const item = detail::validity::substr(rest, detail::validity::offset{0}, *size);
    if (!item) [[unlikely]]
        return std::unexpected(item.error());
    return detail::decode_tree<false>(*item).transform(
        [](detail::decoded_tree const &d) { return d.tree; });
}

}

#include "query.hpp"

# Cost of each function in mbedded-cbor

This document gives the cost of each function in `include/cbor`. A later
change to a function must keep the O form that this document gives for
it. A row names a function by its name, not by a line number.

A row marked "not checked" was not compared with the code. The rows of
the sections describe.hpp, schema.hpp, ref.hpp, lazy.hpp, decode.hpp,
the float rows of head.hpp and the rows of `at_path` and `item` were
compared with the headers of this version. The other rows
were compared with an earlier version of the headers and were not
compared again.

## Variables

| Name | Meaning |
|---|---|
| n | Bytes of the encoded item or of the input. |
| N | Bytes of the whole input, or nodes in a nodelist (stated in the row). |
| N_top | Bytes of the top-level item. |
| n_item, n_map, n_array | Bytes of one item, one map, one array. |
| n_out | Bytes of the encoded output. |
| i | Data items in the input. |
| d | Nesting depth. No build constant bounds it. In a call that recurses, the stack of the thread bounds it; d is at most n, since each level takes at least one byte. |
| s | Shared values (tag 28) in the input. |
| m | Elements of an array. |
| k | Entries of a map. |
| L | Bytes of a bignum magnitude, or characters of a path (stated in the row). |
| t | Bytes of a text to parse, or another count stated in the row. |
| p | Segments or steps of a path. |
| c | Selectors, data members, or tag 28 heads (stated in the row). |
| e | Entries in item_offsets. |
| q | Bytes of a prefix, or characters up to a closing quote. |
| r | Item visits of a walk, or elements of a range. |
| b | Bytes of one element. |
| a | Annotations of a member, or arguments of a function. |
| v | Values that are not arithmetic. |
| \|key\| | Bytes of one key. |
| len | Bytes of one string. |

## head.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `diagnostic_notation(string_view)` friend decl | not given here | not given here | Body in diagnostic_notation.hpp. |
| `heads::read_unsigned<V>(span<char const, sizeof(V)>)` | O(1); sizeof(V) <= 8 | O(1) | No allocation. Confirmed. |
| `heads::initial_byte(major_type, uint64_t)` | O(1) | O(1) | constexpr. Confirmed. |
| `heads::preferred_argument_info(uint64_t)` | O(1) | O(1) | Confirmed. |
| `heads::argument_size(uint8_t)` | O(1) | O(1) | Result is 0, 1, 2, 4 or 8. |
| `heads::head_size(uint64_t)` | O(1) | O(1) | |
| `heads::big_endian<V>(V)` | O(1) | O(1) | |
| `heads::write_head<Exact>(span<char>, size_t, major_type, uint8_t, uint64_t)` | O(1); at most 9 bytes | O(1) | No allocation. head_padding is 8 bytes. Exact checks bytes left. |
| `heads::write_head<Exact>(span<char>, size_t, major_type, uint64_t)` | O(1) | O(1) | |
| `heads::append_head<Container>(Container&, major_type, uint8_t, uint64_t)` | O(1) amortized; O(size of out) on growth | O(1) amortized | The container allocates when it grows. |
| `heads::append_head<Container>(Container&, major_type, uint64_t)` | O(1) amortized | O(1) amortized | As above. |
| `heads::big_endian(uint128)` | O(1) | O(1) | int128 only. |
| `heads::write_u32(span<char>, size_t, size_t)` | O(1) | O(1) | Copies 4 bytes. |
| `heads::write_item_head(span<char>, size_t, major_type, size_t)` | O(1) | O(1) | Writes 5 bytes. |
| `heads::read_unsigned128(span<char const, 16>)` | O(1) | O(1) | Confirmed. |
| `heads::magnitude_without_leading_zeros(string_view)` | O(L) worst; O(1) common | O(1), a view | find_first_not_of. Confirmed. |
| `heads::magnitude_value(string_view)` | O(L) | O(1) | The caller bounds L with check_magnitude_size. |
| `heads::magnitude_plus_one(string_view)` | O(L) | O(L) | Allocates two std::string. Carry loop O(L) worst, O(1) common. Confirmed. |
| `heads::magnitude_minus_one(string_view)` | O(L) | O(L) | Allocates two std::string. Borrow loop O(L) worst, O(1) common. Confirmed. |
| `heads::decoder::decode_head()` | O(1) | O(1) | Checks bytes left and check_definite_length. Changes encoded. |
| `heads::decoder::decode_byte_string(uint64_t)` | O(1) | O(1), a view | Checks bytes left. Copies nothing. |
| `heads::float_decode_binary16(uint16_t)` | O(1) | O(1) | |
| `heads::float_encode_binary16(float)` | O(1) | O(1) | |
| `heads::preferred_float_info(double)`, `preferred_float_info(float)` | O(1) | O(1) | A NaN goes to preferred_nan_info. |
| `heads::preferred_nan_info(uint64_t)` | O(1) | O(1) | The shortest width that keeps the bits. |
| `heads::encode_nan(simple_float_information, uint64_t)` | O(1) | O(1) | |
| `heads::encode_float(simple_float_information, double)`, `encode_float(simple_float_information, float)` | O(1) | O(1) | |
| `heads::is_nan(precision, uint64_t)` | O(1) | O(1) | |
| `heads::significand_zero_pad(precision, uint64_t)` | O(1) | O(1) | |
| `heads::decode_nan(precision, uint64_t)` | O(1) | O(1) | |
| `heads::decode_float(uint8_t, uint64_t)` | O(1) | O(1) | |
| `heads::float_key_of(uint8_t, uint64_t)` | O(1) | O(1) | |
| `heads::is_boolean(head const&)` | O(1) | O(1) | |
| `heads::is_simple_value(head const&)` | O(1) | O(1) | |
| `heads::is_null(head const&)` | O(1) | O(1) | |
| `heads::read_raw_head(string_view, size_t)` | O(1); at most 8 argument bytes | O(1) | Checks bytes left and check_additional_information. |
| `heads::self_described_cbor_content(string_view)` | O(1) | O(1), a view | Removes one tag 55799 head. |
| `heads::break_at(string_view, size_t)` | O(1) | O(1) | Checks at < size. |

## error.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `category()` | O(1) | O(1) | Returns the one `std::error_category` of `cbor::error`. |
| `make_error_code(error)` | O(1) | O(1) | Makes a `std::error_code`; `std::is_error_code_enum<cbor::error>` is true. |
| `error_category::message(int)` | O(1) | O(n) | Returns the text of the error as a `std::string`. |

## validity.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `check_recursion_limit()` | O(1) | O(1) | The stack check: one compare of the frame address with the limit of the thread. The first call of a thread reads the bounds of its stack (one system call or less). |
| `throw_logic_error(char const*)` | O(1) plus a throw | O(1) plus the exception object | std::logic_error allocates its message. Without exceptions: std::abort. |
| `throw_logic_error_if_empty<T>(shared_ptr<T> const&, char const*)` | O(1) | O(1); allocates only on the throw | |
| `throw_logic_error_if_null<T>(shared_ptr<T> const&, char const*)` | O(1) | O(1); allocates only on the throw | |
| `checked_mul<T>(T, T)` | O(1) | O(1) | Overflow check. |
| `checked_add<T>(T, T)` | O(1) | O(1) | Overflow check. |
| `check_key_unique(bool)` | O(1) | O(1) | |
| `writer_error(errc)` | O(1) | O(1) | |
| `check_additional_information(major_type, uint8_t)` | O(1) | O(1) | |
| `check_definite_length(major_type, uint8_t)` | O(1) | O(1) | |
| `check_chunk(major_type, major_type, uint8_t)` | O(1) | O(1) | |
| `check_simple_value(uint8_t, uint64_t)` | O(1) | O(1) | |
| `check_tag_content(uint64_t, major_type, uint8_t)` | O(1) | O(1) | One switch. |
| `typed_array_element_size(uint64_t)` | O(1) | O(1) | |
| `check_typed_array(uint64_t, size_t)` | O(1) | O(1) | |
| `check_sharedref_index(uint64_t, size_t)` | O(1) | O(1) | Bounds a tag 29 index by s. |
| `check_index<I>(I, uint64_t)` | O(1) | O(1) | A negative index counts from the end. |
| `check_count(size_t, size_t)` | O(1) | O(1) | Confirmed. |
| `check_number_range<T, M>(bool, M)` | O(1) | O(1) | Confirmed. |
| `check_magnitude_size(size_t, size_t)` | O(1) | O(1) | Bounds L for magnitude_value. Confirmed. |

`keys_equivalent` is declared here and
defined in shared.hpp. See that section.

## rfc8949.hpp, rfc8746.hpp, rfc9535.hpp

Enums and constants only. No function found by grep.

## item_size.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `well_formedness::no_marks::mark(decoder const&)` | O(1) | O(1) | Empty body. Confirmed. |
| `well_formedness::skip_item<Marks>(decoder&, Marks&)` | O(i) heads, so O(n) | O(1): one count of the items still to read. No allocation. | Iterative. No depth check: nothing recurses. Each add to the count goes through checked_add, and a count larger than the bytes left is too_little_data at once (check_pending_items). Used with no_marks and with value_sharing::sharing_decoder. |
| `item_size(string_view)` | O(i), bounded by O(n) | O(1). No allocation. | One skip_item with no_marks. No depth check. |

## item.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `item` | none; implicit copy O(1) | sizeof only; views, pointers and scalars | `item::type` and `item::additional_information` are data members. |
| `item::get<K>()` | O(1) | O(1) | Gives `incorrect_type` when `type` is not K. |

## sequence.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `sequence(shared_ptr<Encoded const>)`, `sequence(shared_ptr<void const>, Encoded const&)` | O(1) | O(1) | Parses nothing. |
| `sequence::empty()` | O(1) | O(1) | Reads no item. |
| `iterator::operator*() const` | O(1) | O(1) | Returns the cached size. |
| `iterator::operator++()` | O(n_next); O(1) on error | O(1) | Full walk O(N). |
| `iterator::operator++(int)` | O(n_next) | O(1) | Returns void. |
| `iterator::operator==(default_sentinel_t) const` | O(1) | O(1) | |
| `begin() const` | O(n_first) | O(1) | Parses the first item. |
| `end() const` | O(1) | O(1) | |

## shared.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `value_sharing::check_sharedref_index(uint64_t, size_t)` | O(1) | O(1) | Bounds a tag 29 index by s. |

## lazy.hpp

Assumption, not checked: skip_item is O(bytes skipped).

| Function | Time | Memory | Notes |
|---|---|---|---|
| `lazy_elements::iterator::operator*() const` | O(1) | O(1) | One atomic increment. |
| `lazy_elements::iterator::operator++()` | O(b); full walk O(n_array) | O(1) | Depth checked. |
| `lazy_elements::iterator::operator++(int)` | O(b) | O(1) | |
| `lazy_elements::iterator::operator==` | O(1) | O(1) | |
| `lazy_elements::begin()`, `end()` | O(1) | O(1) | |
| `lazy_elements::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy_entries::iterator::find_value()` | O(bytes of the key) | O(1) | |
| `lazy_entries::iterator::operator*() const` | O(1) | O(1) | Two shared_ptr copies. |
| `lazy_entries::iterator::operator++()` | O(value + next key); full walk O(n_map) | O(1) | |
| `lazy_entries::iterator::operator++(int)` | as prefix | O(1) | |
| `lazy_entries::iterator::operator==` | O(1) | O(1) | |
| `lazy_entries::begin()` | O(bytes of the first key) | O(1) | |
| `lazy_entries::end()` | O(1) | O(1) | |
| `lazy_entries::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy::view(shared_ptr<Encoded const>)` | O(1) | O(1) | No allocation. Reads a leading tag 55799 head at most. |
| `lazy::take_ownership<Encoded>(Encoded&&)` | O(1) | O(1) | Moves the string. One allocation: the make_shared that holds it. |
| `lazy::view(shared_ptr<void const>, Encoded const&)` | O(1) | O(1) | No allocation. |
| `lazy::resolve_container(top_level_item, offset)` (private) | Common O(1); worst O(t), t = tag 24 levels <= n/2 | O(1) | No allocation. The nesting depth does not bound tag 24 nesting. |
| `detail::validity::keys_equivalent<Checked>` | Scalar O(1); string O(len); array O(n_item) plus elements; map O(k·n_map + k²·child) per level; worst exponential in d | Stack O(d) | Defined in lazy.hpp. The stack of the thread bounds d. Not checked again. |
| `lazy::clone()` | as `detail::tree_decoder::decode_item` | as `detail::tree_decoder::decode_item` | Returns `owning<item>`. |
| `lazy::type()` | O(1) | O(1) | |
| `lazy::find_key<Key>(resolved, Key)` | O(bytes up to the pair); worst O(n_map) | O(1) | Linear scan. |
| `lazy::find_key<Last, Key>(iterator, Last, Key)` | O(bytes from first up to the pair or to the end) | O(1) | No allocation. |
| `lazy::find(string_view)`, `find(int64_t)` (private) | resolve_container + O(n_map) | O(1) | |
| `lazy::value_of(expected<...>)` | O(1) | O(1) | |
| `lazy::size()`, `empty()` | resolve_container | O(1) | The count of the head of an array or a map. |
| `lazy::at(string_view)` | as find | O(1) | |
| `lazy::at(size_t)` | O(n_array) worst | O(1) | Array only; check_index. A map gives not_indexable. |
| `lazy::at(key)` | as find | O(1) | Map only. A key with text reads as at(string_view). |
| `lazy::operator[]` three forms | as the at of the same argument | O(1) | Calls at. |
| `lazy::select_index(resolved, Index)` (private) | array O(n_array) worst; map linear find_key | O(1) | The index selector of a path: negative counts from the end; on a map the integer key. |
| `lazy::get<T>() const` | resolve_container + O(1); bignum + O(len) | O(1) | |
| `lazy::elements()`, `entries()` | resolve_container | O(1) | |

## decode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `detail::tree_decoder::decode_item` | O(n) heads; O(len) for each string | one `item` per data item in a `std::pmr::monotonic_buffer_resource`; the children of a container in one block; each text and byte string copied into the resource; stack O(d) | Recursive, bounded by the nesting depth. A count is checked against the bytes left before a child is read, so no count reserves memory. |
| `decode(string_view)` | as decode_item, plus O(t log t), t = tag 24 items | as decode_item, plus one sorted vector of the tag 24 items | Returns `owning<item>`. |
| `encoded_cbor_data_item(owning<item> const&, item const&)` | O(log t) | O(1) | Binary search in the sorted vector. |

## ref.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `ref(shared_ptr<void const>, T)` | O(1) | O(1) | Private. |
| copy constructor, copy assignment | O(1) | O(1) | Atomic count. Assignment can free the old buffer. |
| move constructor, move assignment | O(1) | O(1) | |
| `operator*`, `operator->` | O(1) | O(1) | Lvalue only. |
| `ref::clone()` | O(len) for a string view or a byte span; else O(size of the value) | one `std::string` or `std::vector<std::byte>`; else one make_shared and a monotonic resource | Returns `std::string`, `std::vector<std::byte>` or `owning<T>`. |
| `owning::operator*`, `owning::operator->` | O(1) | O(1) | Lvalue only. |

## query.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `jsonpath::name_first(char)` | O(1) | O(1) | |
| `jsonpath::function_name_char(char)` | O(1) | O(1) | |
| `jsonpath::read_int(string_view, size_t)` | O(t), t = digits | O(1) | The digit scan has no bound; the 16-digit check comes after it. |
| `query::parse_string` | O(t) | O(t) | Allocates. |
| `query::parse_literal` | O(t) expected | O(t) | Inner cost not checked. |
| `query::parse_selector` | O(t) | O(t) through callees | The stack of the thread bounds the filter depth. |
| `query::parse_bracketed` | O(t) | O(c) | vector push_back. |
| `query::parse_segments` | O(t) | O(p + c) | Copies a chosen vector. |
| `query::comparable(size_t) const` | O(1) | O(1) | |
| `query::add_expression(expression)` | O(1) amortized | O(1) amortized | |
| `query::parse_function` | O(t) | O(a), a = 1 | |
| `query::parse_primary` | O(t) | O(t) | |
| `query::read_comparison_op` | O(1) | O(1) | |
| `query::parse_paren` | O(t) | O(t) | |
| `query::parse_basic` | O(t) | O(t) | |
| `query::parse_logical_and` | O(t) | O(t) | A loop. |
| `query::parse_logical_or` | O(t) | O(t) | Depth checked. |
| `jsonpath::parse_query(string_view, bool, size_t)` | O(t) | O(t) | Compile time in the fixed_string forms. |
| `walk_query<T>(query_view const&, parsed_query const&, string_view)` | O(n) worst; common O(bytes before the target) | O(1) | No allocation. Not checked again. |
| `walk_query<T>(..., lazy const&)` | O(n·p) worst; common O(n) | O(1); O(n) per tag 24 level | |
| `walk_query<Path, T>(string_view)` | O(n); fallback O(n·p) | O(1) | When the walk over bytes fails, a walk over a lazy with no owner runs. A typed read of a scalar allocates nothing, also with a key that is not a text. |
| `walk_query<Path, T>(shared_ptr<void const>, string_view)` | as `walk_query<Path, T>(string_view)` | as `walk_query<Path, T>(string_view)` | |
| `jsonpath::find_key(lazy const&, string_view)` | text key O(n_map); other key O(k·(n_map + value_equal)) | O(1); tag 24 allocates | |
| `jsonpath::value_equal` | map: 2k² key compares + O(k·n_map) per level; product over d levels worst | O(d) stack | |
| `jsonpath::value_less` | O(1) or O(min len) | O(1) | |
| `jsonpath::comparable_value` | literal O(size); query: apply_segments | allocates per count, length or literal | |
| `jsonpath::test_expression` | sum of subexpressions | O(e) stack | |
| `jsonpath::apply_selector` | O(bytes of the node); filter adds one test_expression per child | O(m) or O(k) | Changes nodelist. nodelist bounded by root bytes. |
| `jsonpath::apply_segment` | child: sum of apply_selector; descendant: O(c·n·d) | O(i) children vectors; O(d) frames | |
| `jsonpath::apply_segments` | worst O(p·N·c·n·d); common O(p·n) | O(N), N <= n | |
| `apply_compiled<Path, Walk>(Walk const&)` | O(1) at run time | O(1) | Compiles the path into arrays at compile time and calls the walk. |
| `is_valid_path<Path>`, `is_valid_path_v<Path>` | O(t) compile time | none at run time | |
| `is_singular_query<Path>`, `is_singular_query_v<Path>` | O(t + c) compile time | none at run time | |
| `at_path<Path, T>(string_view)` | O(n); fallback O(n·p) | O(1) | No allocation. |
| `at_path<Path, T>(shared_ptr<string const>)`, `at_path<Path, T>(shared_ptr<void const>, Encoded const&)` | as `at_path<Path, T>(string_view)` | O(1) | A copy of the owner. |
| `at_path<Path, T>(Encoded&&)` | as `at_path<Path, T>(string_view)` | O(1) | One make_shared: the moved string. |
| `at_path<Path, copied_value T>(string_view)` | as `at_path<Path, T>(string_view)` plus O(len) | O(len) | Copies the text or the bytes of the value into the result. |
| `jsonpath::compile<T>(string_view path)` | parse_query | O(t) | Allocates the parsed path. Once for many applies. |
| `lazy::at_path(query<T> const&)`, `at_path(string_view, query<T> const&)`, `at_path(shared_ptr<void const>, Encoded const&, query<T> const&)`, `at_path(shared_ptr<Encoded const>, query<T> const&)`, `at_path(Encoded&&, query<T> const&)` | as the compiled form with the same T | as the compiled form with the same T | No parse. |
| `lazy::at_path<Path>()` | walk_singular_query | O(1) | The path is compiled. |
| `lazy::at_path<Path, T>()` | as `walk_query<T>(..., lazy const&)` | as `walk_query<T>(..., lazy const&)` | |
| `lazy::at_path<Path, nodelist>()`, `lazy::at_path(query<nodelist> const&)` | walk_query | O(N) nodelist | Allocates the nodelist. |
| local lambdas | O(digits) or O(1) amortized | O(1) | Included in the rows above. |

## encode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `encode` decl | see `encode(item const&, Target&&)` | see `encode(item const&, Target&&)` | |
| `container_message<C>::append` | O(len(part)) amortized | O(len(part)) amortized | |
| `container_message<C>::done` | O(1) | O(1) | |
| `span_message<B>::append` | O(len(part)) | O(1) | Checks bytes left. |
| `span_message<B>::done` | O(1) | O(1) | |
| `byte_container<C>` | compile time | O(1) | |
| `message_of<Target>` | O(1) or Target::allocate | O(1) or Target::allocate | Not confirmed. |
| `encoding::encoder<Writer>::encoder` | O(1) | O(1) direct; 16384-byte member otherwise | No heap. |
| `encoder::flush` | O(1) direct; O(16384) + append otherwise | O(1); writer may allocate | |
| `encoder::write_item` | O(1) plus one flush | O(1) | |
| `encoder::encode_head(major, info, argument)` | O(1) plus one flush | O(1) | |
| `encoder::encode_head(major, argument)` | O(1) | O(1) | |
| `encoder::encode_byte_string` | O(len) | O(1); writer may allocate | |
| `encoder::encode_text_string` | O(len) | O(1); writer may allocate | |
| `encoder::encode_fixed_width_head<T>` | O(1) | O(1) | |
| `encoder::encode_fixed_width_float` | O(1) | O(1) | |
| `cbor::encoder<Target>` calls | O(1) per head; O(len) per string | O(1); a container target grows; a span target allocates nothing | Each call checks that no earlier call failed. |
| `cbor::encoder::finish` | O(1) plus one flush | O(1); an iov_writer gives its storage | |
| `encoding::encode_item(encoder&, item const&, size_t, size_t)` | O(items + string bytes) | stack O(d) | Recursive, bounded by the nesting depth. A tag 29 is written as stored and not followed. |
| `encode(item const&, Target&&)`, `encode_iov(item const&)` | as encode_item | the target | |

## diagnostic_notation.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `extended_diagnostic_notation::blank` | O(1) | O(1) | |
| `blank_end` | O(b) | O(1) | |
| `digit`, `hex_digit_value` | O(1) | O(1) | |
| `append_head` | O(1) amortized | O(1) amortized | Not confirmed. |
| `parse_indicator` | O(1) | O(1) | Not confirmed. |
| `append_utf8` | O(1) amortized | O(1) amortized | Not confirmed. |
| `parse_hex4` | O(1) | O(1) | Not confirmed. |
| `parse_quoted` | O(q) amortized | O(q) | Not confirmed. |
| `parse_hex_string` | O(q) | O(q/2) | Not confirmed. |
| `parse_base64_string` | O(q) | O(3q/4) | Not confirmed. |
| `append_float` | O(1) | O(1) amortized | Not confirmed. |
| `parse_number` | decimal O(t); hex float O(t) plus up to about 10^6 steps | O(1) plus <= 9 bytes | Exponent <= 999999. |
| `parse_separator` | O(b) | O(1) | Not confirmed. |
| `parse_container` | O(t·d) worst, so O(t²); O(t) flat | O(t) | The children are written in place; the head of each container is inserted before them, which moves its children once. |
| `string_finish` | O(len) | O(len) | Not confirmed. |
| `parse_literal` | O(t) scalar; O(t·d) worst for containers | O(t); O(d) frames | Not confirmed. |
| `append_canonical` | scalar O(1); containers O(n·d) worst, so O(n²); maps + O(k log k) compares | O(n) | The elements are written in place; a map copies its pairs once to sort them; the head is inserted before the elements. |
| `encoding_indicator` | O(1) | std::string of 0 or 2 chars | |
| `decimal_of` | O(1) | std::string <= 20 chars | |
| `hex_of` | O(len) | O(len) | |
| `quoted_of` | O(len) amortized | O(len), up to 6x | |
| `number_of` | O(1) | 400-byte stack buffer | |
| `float_of` | O(1) | O(1) | |
| `decode_diagnostic_head` | O(1) | O(1) | Changes d. |
| `break_found` | O(1) | O(1) | Changes d. |
| `write_diagnostic` | O(n) common; worst depends on check_tag_content | O(n) output; O(d) frames | |
| `diagnostic_notation` | O(n) common | O(n) string; O(s); O(d) | |

## describe.hpp

All rows are compile time and have no run-time cost.

| Function | Time | Memory | Notes |
|---|---|---|---|
| `map_key` constructors | O(1); O(len) for a text | O(1), a view | consteval. |
| `CBOR_MEMBER(T, m)`, `CBOR_DEFINE_TYPE(T, tag, members...)`, `CBOR_DEFINE_TYPE_MEMBERS(T, tag, m...)` | O(c) | O(c) static | Macros. CBOR_DEFINE_TYPE and CBOR_DEFINE_TYPE_MEMBERS write the specialization of describe_members. |
| `describe::get<I>` | O(1) | O(1) | |

## schema.hpp

All rows marked "compile time" have no run-time cost. t is the count of
the struct types in a message.

| Function | Time | Memory | Notes |
|---|---|---|---|
| `detail::no_fixed_size<T>()` | O(1) compile time | O(1) | |
| `reflection::descriptions_valid<Root>()` | O(t·c²) compile time | O(c) | pointers_unique compares each pair. |
| `reflection::description_valid<U>()`, `tag_declared`, `tag_number_of`, `members_of_class`, `decode_form_valid` | O(c²) compile time | O(c) | |
| `reflection::tag_taken_by_standard_type`, `tag_invalid` | O(1) compile time | O(1) | |
| `reflection::pointers_differ`, `pointer_differs_from_all`, `pointers_unique` | O(1), O(c), O(c²) compile time | O(1) | |
| `reflection::keys_unique`, `keys_sorted`, `key_order` | O(c log c·len) compile time | O(c) | |
| `reflection::key_encoded`, `key_encoding_of`, `key_encoding_less` | O(len) compile time | O(len) | |
| `reflection::reflected_members_listed`, `public_members`, `listed_once` | O(c²) compile time | O(c) static | Reflection only. True without reflection. |
| `reflection::annotations_absent`, `annotated` | O(c·a) compile time | O(1) | Reflection only. True without reflection. |
| `reflection::key_of(info)`, `data_members<U>()` | O(len), O(c log c·len) compile time | O(len), O(c) static | Reflection only. |
| `reflection::member_index_of` | O(c) compile time | O(1) | |
| `reflection::uses_allocator_aggregate`, `make_element` | O(c) compile time; make_element O(c) | O(1) | With reflection or Boost.PFR; else make_obj_using_allocator. |
| `reflection::make_arguments` | O(c) | O(c) stack | A class with no default constructor. |
| `reflection::for_each_field<U>` | O(c) | O(1) | In key order. |
| `reflection::typed_array_tag<E>()` | O(1) compile time | O(1) | |
| `reflection::read_typed_array_element<E>`, `write_typed_array_element<E>` | O(1) | O(1) stack | |
| `reflection::fixed_member_valid`, `fixed_struct_valid`, `fixed_width`, `fixed_offset` | O(c) compile time per struct | O(1) | |
| `reflection::fixed_layout_native<S>()` | O(c·sizeof(S)) compile time | O(sizeof(S)) | One std::bit_cast probe per member. |
| `reflection::encode_description`, `collect_description_types`, `collect_description_members` | O(t + Σc) compile time | O(t) | |
| `reflection::description_of<Root>()` | O(t·c·len) compile time | O(t·c·len) static | |
| `reflection::encode_fixed_width_head` | O(1) amortized | O(1) | Compile time only. |
| `reflection::encode_zero_initialized<Root, T>` | O(fixed_size) compile time | O(fixed_size) | |
| `reflection::zero_initialized<Root, T>()` | O(fixed_size) compile time; O(1) at run time | O(fixed_size) static | |
| `reflection::record_keys<U>()` | O(c·len) compile time | O(c·len) static | |
| `reflection::collect_record_types<T>`, `collect_record_members` | O(t² + Σc) compile time | O(t) | |
| `reflection::packing_table_of<Root>` | O(t² + Σc) compile time | O(t) static | |
| `reflection::shared_first_of`, `fillers_of`, `argument_index_of`, `table_first_of` | O(1), O(1), O(t), O(1) compile time | O(1) | |
| `reflection::fixed_array_head<Root, S>()` | O(t) compile time | O(1) static | |
| `reflection::encode_straight_reference`, `straight_reference_size` | O(t) compile time | O(1) | |
| `reflection::packing_prefix_of<Root, D>()` | O(q) compile time | O(q) static | With apart, no description. |
| `reflection::float_bits<U>(U)` | O(1) | O(1) | Copies the bits. |
| `second_item::add_bytes`, `add_block` | O(1) | O(1) | checked_add, checked_mul. |
| `second_item::add_elements<E, R>` | O(1) for numbers, enums and fixed-size structs; else O(m) | O(1) heap; O(d) stack | |
| `second_item::add<T>` | O(v); common O(i) | O(1) heap; O(d) stack | Depth follows the value. |
| `reflection::copy_zero_initialized<Root, E>` | O(fixed_size<E>) | O(1) | |
| `reflection::directory_at<Root, D>()` | O(1) | O(1) | |
| `reflection::encode_elements` | O(m·fixed_size + dynamic) | O(1) heap | |
| `reflection::encode_reference` | O(len), O(m) or O(k) plus content | O(1) heap | No allocation. A list of fixed-size structs is one copy of O(m·b) where fixed_layout_native holds. |
| `reflection::write_fixed_struct<S>` | O(fixed_width<S>) | O(1) | |
| `reflection::encode_value` | whole value O(n) | O(1) heap | |
| `reflection::member_named`, `step_end`, `index_end`, `index_of`, `count_index_arguments` | O(c·len), O(L), O(L), O(digits), O(L) compile time | O(1) | |
| `reflection::read_fixed_value<T>`, `fixed_head_valid<T>`, `class_tag_valid`, `fixed_length_head_valid` | O(1) | O(1) | |
| `reflection::read_fixed_leaf<M>` | O(1) | O(1) | |
| `reflection::read_shared_index` | O(1) | O(1) | |
| `reflection::read_fixed_array<Root, S>` | O(1) | O(1) | check_whole_elements. |
| `reflection::read_item_reference` | O(1) | O(1) | checked_mul. |
| `reflection::directory_entry` | O(1) | O(1) | |
| `reflection::read_reference`, `read_item` | O(1) | O(1) | check_index. |
| `reflection::packing_prefix_valid<T, D>` | O(q) | O(1) | |
| `reflection::read_directory<T, Verified, D>` | O(q) | O(1) | |
| `reflection::too_little_data_if_truncated<T, D>` | O(n) | O(1) | Error path only: one skip_items. |
| `reflection::index_arguments`, `index_argument` | O(L) compile time | O(1) | |
| `reflection::path_valid`, `path_result`, `fixed_path_result` | O(p·(L + c·len)) compile time | O(p) instantiations | |
| `reflection::walk_path`, `walk_fixed` | O(p) | O(1) | check_index. |
| `decode_cursor::take_reference` | O(1) | O(1) | Changes the cursor. |
| `decode_cursor::read_value<Root, T>` | O(n); O(n + k log k) with an ordered map | O(n) output; O(d) stack | Allocates per string, list and node. |
| `decode_cursor::read_constructed`, `read_element` | as read_value | as read_value | |
| `decode_cursor::read_fixed_struct<S>` | O(fixed_width<S>) | O(1) | |
| `reflection::read_root<T>` | as read_value | as read_value | |
| `reflection::encoded_size<T, D>` | O(1) | O(1) | |
| `reflection::write_encoded<Exact, D, T>` | O(n) | O(1) heap | |
| `typed_array_view<E>` constructor | O(1) | O(1) | |
| `typed_array_view::iterator` members | O(1) | O(1) | |
| `typed_array_view::size`, `empty`, `begin`, `end` | O(1) | O(1) | |
| `typed_array_view::cbegin`, `cend`, `rbegin`, `rend`, `crbegin`, `crend` | O(1) | O(1) | std::reverse_iterator over the iterator. |
| `typed_array_view::at` | O(1) | O(1) | Checked; index_out_of_bounds as an error value. |
| `typed_array_view::front`, `back`, `operator[]` | O(1) | O(1) | Checked; a wrong index or an empty view throws std::logic_error. |
| `typed_array_view::first`, `last` | O(1) | O(1) | Checked. |
| `tags_registered<Root>()` | O(t log t + t·a) compile time | O(t) | |
| `reflection::fixed_size<T, Root>()` | compile time | O(1) | |
| `reflection::member_offset` | O(c) compile time | O(1) | |
| `schema::fixed_size`, `member_offset` | compile time | O(1) | |
| `schema::description()` | O(1) | O(1), a static span | The compiler makes the bytes. |
| `schema::accessor` constructors | O(1) | O(1) | |
| `schema::accessor::at_path`, `view` | O(p) | O(1) | |
| `schema::accessor::size()` | O(1) | O(1) | |
| `schema::accessor::iterator` members, `begin`, `end` | O(1) | O(1) | A list of fixed-size structs. |
| `schema::accessor<fixed_element<S>>::at_path` | O(p) | O(1) | |
| `schema::accessor<std::variant<A...>>::index`, `get` | O(1) | O(1) | |
| `reflection::read_variant_item` | O(alternatives) | O(1) | One head compare for each alternative before the held one. |
| `schema::path(shared_ptr<string const>)`, `path(shared_ptr<void const>, Encoded const&)` | O(q) in n | O(1) | An error can add too_little_data_if_truncated. |
| `schema::path<Encoded>(Encoded&&)` | O(q) | O(1) | One make_shared. |
| `schema::at_path<Path, D>(string_view, Index...)` | O(q + p) | O(1) | An error can add too_little_data_if_truncated. |
| `schema::decode<D, Encoded>(Encoded&&)` | as decode(owner, view) | O(1) + output | |
| `schema::decode(shared_ptr<string const>)` and the other forms | O(n); + k log k ordered | O(n) output | |
| `schema::unverified_path`, `unverified_at_path`, `unverified_decode` | as path, at_path, decode | as path, at_path, decode | Fewer checks; no too_little_data_if_truncated. |
| `schema::encode<D>(T const&)` | O(i + n) | O(n) | One std::string. |
| `schema::encode<D, Target>(T const&, Target&&)` | O(i + n) | container O(n); span none; other message O(n) temporary | |

## structs.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `generic::read_integer<U, V>` | O(1) | O(1) | |
| `generic::head_accepted<U>` | O(1) | O(1) | |
| `generic::read_wide_integer<U>` | O(len), at most 16 steps | O(1) | |
| `generic::read_variant` | O(alternatives) + read | output | |
| `generic::read_generic<U>` | common O(n); worst O((n/d)^d) with tag 29 | output; O(s) | Each tag 29 adds 1 to depth. |
| `generic::read_generic_value` | scalar O(1); string O(len); array O(m); std::map O(k log k); unordered_map O(k) | output; O(d) stack | Duplicate key dropped by try_emplace. |
| `generic::key_matches<U, I>` | O(1) or O(len) | O(1) | |
| `generic::read_struct` | O(k·c·len) + members + skipped | bitset; O(1) | |
| `generic::member_present`, `float_size` | O(1) | O(1) | |
| `generic::generic_size<U>` | O(i) | O(1) heap | |
| `generic::write_bytes` | O(len) | O(1) | |
| `generic::write_generic<U>` | O(n) | O(1) heap | |
| `structs::decode<Encoded>(Encoded&&)` | as read | one make_shared + read | |
| `structs::decode(shared_ptr<string const>)`, `structs::decode(shared_ptr<void const>, Encoded const&)` | as read | as read | |
| `structs::encode(T const&)` | O(i + n) | O(n) | |
| `structs::encode<Target>(T const&, Target&&)` | O(i + n) | container O(n); else O(n) temporary | Not opened. |
| `structs::read(string_view)` | common O(n); worst O((n/d)^d) | output; O(s); O(d) | |

## Above linear, or allocation on a success path

Above linear in the input:

- `detail::validity::keys_equivalent` (lazy.hpp): map keys O(k²) per level, exponential in d worst.
- `jsonpath::value_equal`: O(k²) key compares per map level.
- `jsonpath::find_key`, non-text key: O(k·n_map).
- `jsonpath::apply_segment`, `apply_segments`: O(c·n·d), O(p·N·c·n·d).
- `walk_query` lazy forms and the fallback of `at_path` to a lazy walk: O(n·p).
- `parse_container`, `parse_literal`, `append_canonical` (diagnostic_notation.hpp): time O(t·d), O(n·d), memory O(t), O(n); maps add O(k log k).
- `read_generic`, `structs::read`: O((n/d)^d) with tag 29.
- `std::map` targets in schema and structs: O(n + k log k).

Allocation on a success path:

- `value_sharing::sharing_decoder::mark`, `detail::tree_decoder::decode_item`.
- `lazy::take_ownership`, `at_path(Encoded&&)`, `schema::path(Encoded&&)`, `schema::decode`, `structs::decode`, `lazy::clone`, `decode`, `ref::clone`.
- jsonpath: every parser, `compile`, a `nodelist` read, `comparable_value`, `apply_selector`, `apply_segment`, `apply_segments`.
- `container_message`.
- diagnostic_notation.hpp: `append_canonical`, `parse_container`, `diagnostic_notation`, the `*_of` functions.

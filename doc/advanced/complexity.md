# Cost of each function in mbedded-cbor

This document gives the cost of each function in `include/cbor`. A later
change to a function must keep the O form that this document gives for
it. A row names a function by its name, not by a line number.

A row marked "not checked" was not compared with the code. The rows of
the sections ref.hpp, lazy.hpp, decode.hpp and the rows of `at_path` and
`item` were compared with the headers of this version. The other rows
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
| d | Nesting depth. The nesting depth of the build bounds it: cbor::limits::nesting_depth, at most nesting_depth_limit. |
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
| `heads::unsigned_read<V>(span<char const, sizeof(V)>)` | O(1); sizeof(V) <= 8 | O(1) | No allocation. Confirmed. |
| `heads::initial_byte(major_type, uint64_t)` | O(1) | O(1) | constexpr. Confirmed. |
| `heads::preferred_argument_info(uint64_t)` | O(1) | O(1) | Confirmed. |
| `heads::argument_size(uint8_t)` | O(1) | O(1) | Result is 0, 1, 2, 4 or 8. |
| `heads::head_size(uint64_t)` | O(1) | O(1) | |
| `heads::big_endian<V>(V)` | O(1) | O(1) | |
| `heads::head_write<Exact>(span<char>, size_t, major_type, uint8_t, uint64_t)` | O(1); at most 9 bytes | O(1) | No allocation. head_padding is 8 bytes. Exact checks bytes left. |
| `heads::head_write<Exact>(span<char>, size_t, major_type, uint64_t)` | O(1) | O(1) | |
| `heads::head_append<Container>(Container&, major_type, uint8_t, uint64_t)` | O(1) amortized; O(size of out) on growth | O(1) amortized | The container allocates when it grows. |
| `heads::head_append<Container>(Container&, major_type, uint64_t)` | O(1) amortized | O(1) amortized | As above. |
| `heads::big_endian(uint128)` | O(1) | O(1) | Reflection and int128 only. |
| `heads::u32_write(span<char>, size_t, size_t)` | O(1) | O(1) | Copies 4 bytes. Reflection only. |
| `heads::item_head_write(span<char>, size_t, major_type, size_t)` | O(1) | O(1) | Writes 5 bytes. Reflection only. |
| `heads::unsigned128_read(span<char const, 16>)` | O(1) | O(1) | Confirmed. |
| `heads::magnitude_without_leading_zeros(string_view)` | O(L) worst; O(1) common | O(1), a view | find_first_not_of. Confirmed. |
| `heads::magnitude_value(string_view)` | O(L) | O(1) | The caller bounds L with check_magnitude_size. |
| `heads::magnitude_plus_one(string_view)` | O(L) | O(L) | Allocates two std::string. Carry loop O(L) worst, O(1) common. Confirmed. |
| `heads::magnitude_minus_one(string_view)` | O(L) | O(L) | Allocates two std::string. Borrow loop O(L) worst, O(1) common. Confirmed. |
| `heads::decoder::head_decode()` | O(1) | O(1) | Checks bytes left and check_definite_length. Changes encoded. |
| `heads::decoder::byte_string_decode(uint64_t)` | O(1) | O(1), a view | Checks bytes left. Copies nothing. |
| `heads::float_decode_binary16(uint16_t)` | O(1) | O(1) | |
| `heads::float_encode_binary16(float)` | O(1) | O(1) | |
| `heads::preferred_float_info(double)` | O(1) | O(1) | |
| `heads::float_encode(simple_float_information, double)` | O(1) | O(1) | |
| `heads::is_nan(precision, uint64_t)` | O(1) | O(1) | |
| `heads::significand_zero_pad(precision, uint64_t)` | O(1) | O(1) | |
| `heads::nan_decode(precision, uint64_t)` | O(1) | O(1) | |
| `heads::float_decode(uint8_t, uint64_t)` | O(1) | O(1) | |
| `heads::float_key_of(uint8_t, uint64_t)` | O(1) | O(1) | |
| `heads::is_boolean(head const&)` | O(1) | O(1) | |
| `heads::is_simple_value(head const&)` | O(1) | O(1) | |
| `heads::is_null(head const&)` | O(1) | O(1) | |
| `heads::raw_head_read(string_view, size_t)` | O(1); at most 8 argument bytes | O(1) | Checks bytes left and check_additional_information. |
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
| `check_nesting_depth(size_t, size_t)` | O(1) | O(1) | The depth check. |
| `throw_logic_error(char const*)` | O(1) plus a throw | O(1) plus the exception object | std::logic_error allocates its message. Without exceptions: std::abort. |
| `throw_logic_error_if_empty<T>(shared_ptr<T> const&, char const*)` | O(1) | O(1); allocates only on the throw | |
| `throw_logic_error_if_null<T>(shared_ptr<T> const&, char const*)` | O(1) | O(1); allocates only on the throw | |
| `checked_mul<T>(T, T)` | O(1) | O(1) | Overflow check. |
| `checked_add<T>(T, T)` | O(1) | O(1) | Overflow check. |
| `keys_unique<R, Projection>(R const&, Projection)` | O(r) | O(1) | adjacent_find. The range must be sorted. |
| `check_key_unique(bool)` | O(1) | O(1) | |
| `writer_error(errc)` | O(1) | O(1) | |
| `check_additional_information(major_type, uint8_t)` | O(1) | O(1) | |
| `check_definite_length(major_type, uint8_t)` | O(1) | O(1) | |
| `check_chunk(major_type, major_type, uint8_t)` | O(1) | O(1) | |
| `check_simple_value(uint8_t, uint64_t)` | O(1) | O(1) | |
| `check_tag_content(uint64_t, major_type, uint8_t)` | O(1) | O(1) | One switch. |
| `typed_array_element_size(uint64_t)` | O(1) | O(1) | |
| `typed_array_check(uint64_t, size_t)` | O(1) | O(1) | |
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
| `well_formedness::item_skip<Marks>(decoder&, Marks&)` | O(i) heads, so O(n) | O(1): one count of the items still to read. No allocation. | Iterative. No depth check: nothing recurses. Each add to the count goes through checked_add, and a count larger than the bytes left is too_little_data at once (check_pending_items). Used with no_marks and with value_sharing::sharing_decoder. |
| `well_formedness::item_skip<Marks>(decoder&, Marks&, size_t depth, size_t depth_max)` | O(i) heads, so O(n) | O(nesting_depth_limit) stack: std::array of nesting_depth_limit + 1 counts of 8 bytes, about 4 KiB. No allocation. | Iterative. Only for marks that record a depth. check_nesting_depth on every item. |
| `item_size(string_view)` | O(i), bounded by O(n) | O(1). No allocation. | One item_skip with no_marks. No depth check. |

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

Assumption, not checked: item_skip is O(bytes skipped).

| Function | Time | Memory | Notes |
|---|---|---|---|
| `lazy_elements::iterator::operator*() const` | O(1) | O(1) | One atomic increment. |
| `lazy_elements::iterator::operator++()` | O(b); full walk O(n_array) | O(1) | Depth checked. |
| `lazy_elements::iterator::operator++(int)` | O(b) | O(1) | |
| `lazy_elements::iterator::operator==` | O(1) | O(1) | |
| `lazy_elements::begin()`, `end()` | O(1) | O(1) | |
| `lazy_elements::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy_entries::iterator::value_find()` | O(bytes of the key) | O(1) | |
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
| `lazy::container_resolve(top_level_item, offset)` (private) | Common O(1); worst O(t), t = tag 24 levels <= n/2 | O(1) | No allocation. The nesting depth does not bound tag 24 nesting. |
| `validity::keys_equivalent<Checked>` | Scalar O(1); string O(len); array O(n_item) plus elements; map O(k·n_map + k²·child) per level; worst exponential in d | Stack O(d) | Defined in lazy.hpp. check_nesting_depth bounds d. Not checked again. |
| `lazy::clone()` | as `tree_decoder::item_decode` | as `tree_decoder::item_decode` | Returns `owning<item>`. |
| `lazy::type()` | O(1) | O(1) | |
| `lazy::key_find<Key>(resolved, Key)` | O(bytes up to the pair); worst O(n_map) | O(1) | Linear scan. |
| `lazy::key_find<Last, Key>(iterator, Last, Key)` | O(bytes from first up to the pair or to the end) | O(1) | No allocation. |
| `lazy::find(string_view)`, `find(int64_t)` (private) | container_resolve + O(n_map) | O(1) | |
| `lazy::value_of(expected<...>)` | O(1) | O(1) | |
| `lazy::size()`, `empty()` | container_resolve | O(1) | The count of the head of an array or a map. |
| `lazy::at(string_view)` | as find | O(1) | |
| `lazy::at(size_t)` | O(n_array) worst | O(1) | Array only; check_index. A map gives not_indexable. |
| `lazy::at(key)` | as find | O(1) | Map only. A key with text reads as at(string_view). |
| `lazy::operator[]` three forms | as the at of the same argument | O(1) | Calls at. |
| `lazy::index_select(resolved, Index)` (private) | array O(n_array) worst; map linear key_find | O(1) | The index selector of a path: negative counts from the end; on a map the integer key. |
| `lazy::get<T>() const` | container_resolve + O(1); bignum + O(len) | O(1) | |
| `lazy::elements()`, `entries()` | container_resolve | O(1) | |

## decode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `tree_decoder::item_decode` | O(n) heads; O(len) for each string | one `item` per data item in a `std::pmr::monotonic_buffer_resource`; the children of a container in one block; each text and byte string copied into the resource; stack O(d) | Recursive, bounded by the nesting depth. A count is checked against the bytes left before a child is read, so no count reserves memory. |
| `decode(string_view)` | as item_decode, plus O(t log t), t = tag 24 items | as item_decode, plus one sorted vector of the tag 24 items | Returns `owning<item>`. |
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
| `jsonpath::int_read(string_view, size_t)` | O(t), t = digits | O(1) | The digit scan has no bound; the 16-digit check comes after it. |
| `jsonpath::number_end(string_view, size_t)` | O(t) | O(1) | |
| `query::string_parse` | O(t) | O(t) | Allocates. |
| `query::literal_parse` | O(t) expected | O(t) | Inner cost not checked. |
| `query::selector_parse` | O(t) | O(t) through callees | Filter depth bounded by depth_max. |
| `query::bracketed_parse` | O(t) | O(c) | vector push_back. |
| `query::segments_parse` | O(t) | O(p + c) | Copies a chosen vector. |
| `query::comparable(size_t) const` | O(1) | O(1) | |
| `query::expression_add(expression)` | O(1) amortized | O(1) amortized | |
| `query::function_parse` | O(t) | O(a), a = 1 | |
| `query::primary_parse` | O(t) | O(t) | |
| `query::comparison_op_read` | O(1) | O(1) | |
| `query::paren_parse` | O(t) | O(t) | |
| `query::basic_parse` | O(t) | O(t) | |
| `query::logical_and_parse` | O(t) | O(t) | A loop. |
| `query::logical_or_parse` | O(t) | O(t) | Depth checked. |
| `jsonpath::query_parse(string_view, bool, size_t)` | O(t) | O(t) | Compile time in the fixed_string forms. |
| `query_walk<T>(query_view const&, parsed_query const&, string_view)` | O(n) worst; common O(bytes before the target) | O(1) | No allocation. Not checked again. |
| `query_walk<T>(..., lazy const&)` | O(n·p) worst; common O(n) | O(1); O(n) per tag 24 level | |
| `query_walk<Path, T>(string_view)` | O(n); fallback O(n·p) | O(1) | When the walk over bytes fails, a walk over a lazy with no owner runs. A typed read of a scalar allocates nothing, also with a key that is not a text. |
| `query_walk<Path, T>(shared_ptr<void const>, string_view)` | as `query_walk<Path, T>(string_view)` | as `query_walk<Path, T>(string_view)` | |
| `jsonpath::key_find(lazy const&, string_view)` | text key O(n_map); other key O(k·(n_map + value_equal)) | O(1); tag 24 allocates | |
| `jsonpath::value_equal` | map: 2k² key compares + O(k·n_map) per level; product over d levels worst | O(d) stack | |
| `jsonpath::value_less` | O(1) or O(min len) | O(1) | |
| `jsonpath::comparable_value` | literal O(size); query: segments_apply | allocates per count, length or literal | |
| `jsonpath::expression_test` | sum of subexpressions | O(e) stack | |
| `jsonpath::selector_apply` | O(bytes of the node); filter adds one expression_test per child | O(m) or O(k) | Changes nodelist. nodelist bounded by root bytes. |
| `jsonpath::segment_apply` | child: sum of selector_apply; descendant: O(c·n·d) | O(i) children vectors; O(d) frames | |
| `jsonpath::segments_apply` | worst O(p·N·c·n·d); common O(p·n) | O(N), N <= n | |
| `compiled_apply<Path, Walk>(Walk const&)` | O(1) at run time | O(1) | Compiles the path into arrays at compile time and calls the walk. |
| `is_valid_path<Path>`, `is_valid_path_v<Path>` | O(t) compile time | none at run time | |
| `is_singular_query<Path>`, `is_singular_query_v<Path>` | O(t + c) compile time | none at run time | |
| `at_path<Path, T>(string_view)` | O(n); fallback O(n·p) | O(1) | No allocation. |
| `at_path<Path, T>(shared_ptr<string const>)`, `at_path<Path, T>(shared_ptr<void const>, Encoded const&)` | as `at_path<Path, T>(string_view)` | O(1) | A copy of the owner. |
| `at_path<Path, T>(Encoded&&)` | as `at_path<Path, T>(string_view)` | O(1) | One make_shared: the moved string. |
| `at_path<Path, copied_value T>(string_view)` | as `at_path<Path, T>(string_view)` plus O(len) | O(len) | Copies the text or the bytes of the value into the result. |
| `jsonpath::compile<T>(string_view path)` | query_parse | O(t) | Allocates the parsed path. Once for many applies. |
| `lazy::at_path(query<T> const&)`, `at_path(string_view, query<T> const&)`, `at_path(shared_ptr<void const>, Encoded const&, query<T> const&)`, `at_path(shared_ptr<Encoded const>, query<T> const&)`, `at_path(Encoded&&, query<T> const&)` | as the compiled form with the same T | as the compiled form with the same T | No parse. |
| `lazy::at_path<Path>()` | singular_query_walk | O(1) | The path is compiled. |
| `lazy::at_path<Path, T>()` | as `query_walk<T>(..., lazy const&)` | as `query_walk<T>(..., lazy const&)` | |
| `lazy::at_path<Path, nodelist>()`, `lazy::at_path(query<nodelist> const&)` | query_walk | O(N) nodelist | Allocates the nodelist. |
| local lambdas | O(digits) or O(1) amortized | O(1) | Included in the rows above. |

## encode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `encode` decl | see `encode(item const&, Target&&)` | see `encode(item const&, Target&&)` | |
| `encoding::string_sink::append` | O(len(part)) amortized | O(len(part)) amortized | std::string growth. |
| `string_sink::done` | O(1) | O(1) | |
| `string_sink::resize_and_overwrite<Op>` | O(size + op) amortized | O(size) amortized | Not checked line by line. |
| `container_message<C>::append` | O(len(part)) amortized | O(len(part)) amortized | |
| `container_message<C>::done` | O(1) | O(1) | |
| `span_message<B>::append` | O(len(part)) | O(1) | Checks bytes left. |
| `span_message<B>::done` | O(1) | O(1) | |
| `byte_container<C>` | compile time | O(1) | |
| `message_of<Target>` | O(1) or Target::allocate | O(1) or Target::allocate | Not confirmed. |
| `encoding::encoder<Writer>::encoder` | O(1) | O(1) direct; 16384-byte member otherwise | No heap. |
| `encoder::flush` | O(1) direct; O(16384) + append otherwise | O(1); writer may allocate | |
| `encoder::item_write` | O(1) plus one flush | O(1) | |
| `encoder::head_encode(major, info, argument)` | O(1) plus one flush | O(1) | |
| `encoder::head_encode(major, argument)` | O(1) | O(1) | |
| `encoder::byte_string_encode` | O(len) | O(1); writer may allocate | |
| `encoder::text_string_encode` | O(len) | O(1); writer may allocate | |
| `encoder::float_encode` | O(1) | O(1) | |
| `encoder::fixed_width_head_encode<T>` | O(1) | O(1) | |
| `encoder::simple_value_encode` | O(1) | O(1) | |
| `encoder::fixed_width_unsigned_encode`, `signed`, `float` | O(1) | O(1) | |
| `cbor::encoder<Target>` calls | O(1) per head; O(len) per string | O(1); a container target grows; a span target allocates nothing | Each call checks that no earlier call failed. |
| `cbor::encoder::finish` | O(1) plus one flush | O(1); an iov_writer gives its storage | |
| `encoding::item_encode(encoder&, item const&, size_t, size_t)` | O(items + string bytes) | stack O(d) | Recursive, bounded by the nesting depth. A tag 29 is written as stored and not followed. |
| `encode(item const&, Target&&)`, `encode_iov(item const&)` | as item_encode | the target | |

## diagnostic_notation.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `extended_diagnostic_notation::blank` | O(1) | O(1) | |
| `blank_end` | O(b) | O(1) | |
| `digit`, `hex_digit_value` | O(1) | O(1) | |
| `head_append` | O(1) amortized | O(1) amortized | Not confirmed. |
| `indicator_parse` | O(1) | O(1) | Not confirmed. |
| `utf8_append` | O(1) amortized | O(1) amortized | Not confirmed. |
| `hex4_parse` | O(1) | O(1) | Not confirmed. |
| `quoted_parse` | O(q) amortized | O(q) | Not confirmed. |
| `hex_string_parse` | O(q) | O(q/2) | Not confirmed. |
| `base64_string_parse` | O(q) | O(3q/4) | Not confirmed. |
| `float_append` | O(1) | O(1) amortized | Not confirmed. |
| `number_parse` | decimal O(t); hex float O(t) plus up to about 10^6 steps | O(1) plus <= 9 bytes | Exponent <= 999999. |
| `separator_parse` | O(b) | O(1) | Not confirmed. |
| `container_parse` | O(t·d) worst; O(t) flat | O(t·d) worst | Children copied at each level. |
| `string_finish` | O(len) | O(len) | Not confirmed. |
| `literal_parse` | O(t) scalar; O(t·d) worst for containers | O(t); O(d) frames | Not confirmed. |
| `canonical_append` | scalar O(1); containers O(n·d) worst; maps + O(k log k) compares | O(n·d) worst | Vector of string pairs per level. |
| `encoding_indicator` | O(1) | std::string of 0 or 2 chars | |
| `decimal_of` | O(1) | std::string <= 20 chars | |
| `hex_of` | O(len) | O(len) | |
| `quoted_of` | O(len) amortized | O(len), up to 6x | |
| `number_of` | O(1) | 400-byte stack buffer | |
| `float_of` | O(1) | O(1) | |
| `diagnostic_head_decode` | O(1) | O(1) | Changes d. |
| `break_found` | O(1) | O(1) | Changes d. |
| `diagnostic_write` | O(n) common; worst depends on check_tag_content | O(n) output; O(d) frames | |
| `diagnostic_notation` | O(n) common | O(n) string; O(s); O(d) | |

## schema.hpp

All rows marked "compile time" have no run-time cost.

| Function | Time | Memory | Notes |
|---|---|---|---|
| `no_fixed_size<T>()` | O(1) compile time | O(1) | |
| `key::key(char const*)` | O(len) compile time | O(len) static | |
| `reflection::key_of` | O(a + len) compile time | O(len) static | |
| `reflection::tag_number_of` | O(a) compile time | O(1) | Not confirmed. |
| `reflection::keys_unique` | O(c log c·len) compile time | O(c) | |
| `reflection::annotated` | O(a) compile time | O(1) | |
| `reflection::data_members<U>()` | O(c·a) compile time | O(c) static | |
| `reflection::typed_array_tag<E>()` | O(1) compile time | O(1) | |
| `reflection::typed_array_element_read<E>` | O(1) | O(1) stack | |
| `reflection::typed_array_element_write<E>` | O(1) | O(1) | |
| `reflection::fixed_width_head_encode` | O(1) amortized | O(1) | Compile time only. |
| `reflection::zero_initialized_encode<Root, T>` | O(fixed_size) compile time | O(fixed_size) | |
| `reflection::zero_initialized<Root, T>()` | O(fixed_size) compile time; O(1) at run time | O(fixed_size) static | |
| `reflection::record_keys<U>()` | O(c·len) compile time | O(c·len) static | |
| `reflection::record_types_collect<T>` | O(t² + Σc) compile time | O(t) | |
| `reflection::packing_table_of<Root>()` | O(t² + Σc) compile time | O(t) static | |
| `reflection::shared_first_of`, `argument_index_of` | O(1), O(t) compile time | O(1) | |
| `reflection::straight_reference_encode`, `_size` | O(t) compile time | O(1) | |
| `reflection::packing_prefix_of<Root>()` | O(q) compile time | O(q) static | |
| `reflection::float_bits<U>(U)` | O(1) | O(1) | |
| `second_item::bytes_add`, `block_add` | O(1) | O(1) | checked_add, checked_mul. |
| `second_item::elements_add<E, R>` | O(1) or O(m) | O(1) heap; O(d) stack | |
| `second_item::add<T>` | O(v); common O(i) | O(1) heap; O(d) stack | Depth follows the value. |
| `reflection::zero_initialized_copy<Root, E>` | O(fixed_size<E>) | O(1) | |
| `reflection::directory_at<Root>()` | O(1) | O(1) | |
| `reflection::elements_encode` | O(m·fixed_size + dynamic) | O(1) heap | |
| `reflection::reference_encode` | O(len), O(m) or O(k) plus content | O(1) heap | No allocation. |
| `reflection::value_encode` | whole value O(n) | O(1) heap | |
| `reflection::member_named`, `step_end`, `index_end`, `index_of` | O(c·len), O(L), O(L), O(digits) compile time | O(1) | |
| `reflection::fixed_value_read<T>` | O(1) | O(1) | |
| `reflection::fixed_head_valid<T>` | O(1) | O(1) | |
| `reflection::class_tag_valid` | O(1) | O(1) | |
| `reflection::shared_index_read` | O(1) | O(1) | Not opened. |
| `reflection::item_reference_read` | O(1) | O(1) | checked_mul. |
| `reflection::directory_entry` | O(1) | O(1) | |
| `reflection::reference_read`, `item_read` | O(1) | O(1) | check_index. |
| `reflection::directory_read<T>` | O(1) in n | O(1) | |
| `reflection::index_arguments`, `index_argument` | O(L) compile time | O(1) | |
| `reflection::path_valid`, `path_result` | O(p·(L + c·len)) compile time | O(p) instantiations | |
| `reflection::path_walk` | O(p) | O(1) | check_index. |
| `decode_cursor::reference_take` | O(1) | O(1) | Changes the cursor. |
| `decode_cursor::value_read<Root, T>` | O(n); O(n + k log k) with an ordered map | O(n) output; O(d) stack | Allocates per string, list and node. |
| `reflection::root_read<T>` | as value_read | as value_read | |
| `reflection::encoded_size<T>` | O(1) | O(1) | |
| `reflection::encoded_write<Exact, T>` | O(n) | O(1) heap | |
| `reflection::members_of<U>()` | O(c log c·len) compile time | O(c) static | |
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
| `schema::accessor` constructors | O(1) | O(1) | |
| `schema::accessor::at_path`, `view` | O(p) | O(1) | |
| `schema::accessor::size()` | O(1) | O(1) | |
| `schema::path(shared_ptr<string const>)` | O(1) in n | O(1) | |
| `schema::at_path<Path>(string_view, Index...)` | O(q + p) | O(1) | |
| `schema::path<Encoded>(Encoded&&)` | O(1) | O(1) | One make_shared. |
| `schema::decode<Encoded>(Encoded&&)` | as decode(owner, view) | O(1) + output | |
| `schema::decode(shared_ptr<string const>)` | O(n); + k log k ordered | O(n) output | |
| `schema::encode(T const&)` | O(i + n) | O(n) | One std::string. |
| `schema::encode<Target>(T const&, Target&&)` | O(i + n) | container O(n); span none; other message O(n) temporary | |

## structs.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `generic::integer_read<U, V>` | O(1) | O(1) | |
| `generic::head_accepted<U>` | O(1) | O(1) | |
| `generic::wide_integer_read<U>` | O(len), at most 16 steps | O(1) | |
| `generic::variant_read` | O(alternatives) + read | output | |
| `generic::generic_read<U>` | common O(n); worst O((n/d)^d) with tag 29 | output; O(s) | Each tag 29 adds 1 to depth. |
| `generic::generic_value_read` | scalar O(1); string O(len); array O(m); std::map O(k log k); unordered_map O(k) | output; O(d) stack | Duplicate key dropped by try_emplace. |
| `generic::key_matches<U, I>` | O(1) or O(len) | O(1) | |
| `generic::struct_read` | O(k·c·len) + members + skipped | bitset; O(1) | |
| `generic::member_present`, `float_size` | O(1) | O(1) | |
| `generic::generic_size<U>` | O(i) | O(1) heap | |
| `generic::bytes_write` | O(len) | O(1) | |
| `generic::generic_write<U>` | O(n) | O(1) heap | |
| `structs::decode<Encoded>(Encoded&&)` | as read | one make_shared + read | |
| `structs::decode(shared_ptr<string const>)`, `structs::decode(shared_ptr<void const>, Encoded const&)` | as read | as read | |
| `structs::encode(T const&)` | O(i + n) | O(n) | |
| `structs::encode<Target>(T const&, Target&&)` | O(i + n) | container O(n); else O(n) temporary | Not opened. |
| `structs::read(string_view)` | common O(n); worst O((n/d)^d) | output; O(s); O(d) | |

## Above linear, or allocation on a success path

Above linear in the input:

- `validity::keys_equivalent` (shared.hpp): map keys O(k²) per level, exponential in d worst.
- `jsonpath::value_equal`: O(k²) key compares per map level.
- `jsonpath::key_find`, non-text key: O(k·n_map).
- `jsonpath::segment_apply`, `segments_apply`: O(c·n·d), O(p·N·c·n·d).
- `query_walk` lazy forms and the fallback of `at_path` to a lazy walk: O(n·p).
- `container_parse`, `literal_parse`, `canonical_append` (diagnostic_notation.hpp): O(t·d), O(n·d); maps add O(k log k).
- `generic_read`, `structs::read`: O((n/d)^d) with tag 29.
- `std::map` targets in schema and structs: O(n + k log k).

Allocation on a success path:

- `value_sharing::sharing_decoder::mark`, `tree_decoder::item_decode`.
- `lazy::take_ownership`, `at_path(Encoded&&)`, `schema::path(Encoded&&)`, `schema::decode`, `structs::decode`, `lazy::clone`, `decode`, `ref::clone`.
- jsonpath: every parser, `compile`, a `nodelist` read, `comparable_value`, `selector_apply`, `segment_apply`, `segments_apply`.
- `string_sink`, `container_message`.
- diagnostic_notation.hpp: `canonical_append`, `container_parse`, `diagnostic_notation`, the `*_of` functions.

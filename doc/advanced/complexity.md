# Cost of each function in mbedded-cbor

This document describes the state at commit `da9a611` on branch
`views-only-item` of `/home/user/mbedded-cbor`, with the change that
removed DepthMax from every name. Line numbers are those of `da9a611`. A later change to a
function must keep the O form that this document gives for it. The rows that branch
`standard-library-api` added or renamed carry no line number.

A row marked "not checked" was not compared with the code.

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
| `diagnostic_notation(string_view)` friend decl, :553-555 | not given here | not given here | Body in diagnostic_notation.hpp. |
| `heads::unsigned_read<V>(span<char const, sizeof(V)>)` :45 | O(1); sizeof(V) <= 8 | O(1) | No allocation. Confirmed. |
| `heads::initial_byte(major_type, uint64_t)` :55 | O(1) | O(1) | constexpr. Confirmed. |
| `heads::preferred_argument_info(uint64_t)` :60 | O(1) | O(1) | Confirmed. |
| `heads::argument_size(uint8_t)` :69 | O(1) | O(1) | Result is 0, 1, 2, 4 or 8. |
| `heads::head_size(uint64_t)` :77 | O(1) | O(1) | |
| `heads::big_endian<V>(V)` :82 | O(1) | O(1) | |
| `heads::head_write<Exact>(span<char>, size_t, major_type, uint8_t, uint64_t)` :90 | O(1); at most 9 bytes | O(1) | No allocation. head_padding is 8 bytes. Exact checks bytes left. |
| `heads::head_write<Exact>(span<char>, size_t, major_type, uint64_t)` :115 | O(1) | O(1) | |
| `heads::head_append<Container>(Container&, major_type, uint8_t, uint64_t)` :122 | O(1) amortized; O(size of out) on growth | O(1) amortized | The container allocates when it grows. |
| `heads::head_append<Container>(Container&, major_type, uint64_t)` :131 | O(1) amortized | O(1) amortized | As above. |
| `heads::big_endian(uint128)` :141 | O(1) | O(1) | Reflection and int128 only. |
| `heads::u32_write(span<char>, size_t, size_t)` :152 | O(1) | O(1) | Copies 4 bytes. Reflection only. |
| `heads::item_head_write(span<char>, size_t, major_type, size_t)` :158 | O(1) | O(1) | Writes 5 bytes. Reflection only. |
| `heads::unsigned128_read(span<char const, 16>)` :167 | O(1) | O(1) | Confirmed. |
| `heads::magnitude_without_leading_zeros(string_view)` :176 | O(L) worst; O(1) common | O(1), a view | find_first_not_of. Confirmed. |
| `heads::magnitude_value(string_view)` :182 | O(L) | O(1) | The caller bounds L with check_magnitude_size. |
| `heads::magnitude_plus_one(string_view)` :190 | O(L) | O(L) | Allocates two std::string. Carry loop O(L) worst, O(1) common. Confirmed. |
| `heads::magnitude_minus_one(string_view)` :202 | O(L) | O(L) | Allocates two std::string. Borrow loop O(L) worst, O(1) common. Confirmed. |
| `heads::decoder::head_decode()` :221 | O(1) | O(1) | Checks bytes left and check_definite_length. Changes encoded. |
| `heads::decoder::byte_string_decode(uint64_t)` :267 | O(1) | O(1), a view | Checks bytes left. Copies nothing. |
| `heads::float_decode_binary16(uint16_t)` :291 | O(1) | O(1) | |
| `heads::float_encode_binary16(float)` :318 | O(1) | O(1) | |
| `heads::preferred_float_info(double)` :344 | O(1) | O(1) | |
| `heads::float_encode(simple_float_information, double)` :382 | O(1) | O(1) | |
| `heads::is_nan(precision, uint64_t)` :395 | O(1) | O(1) | |
| `heads::significand_zero_pad(precision, uint64_t)` :401 | O(1) | O(1) | |
| `heads::nan_decode(precision, uint64_t)` :410 | O(1) | O(1) | |
| `heads::float_decode(uint8_t, uint64_t)` :417 | O(1) | O(1) | |
| `heads::float_key_of(uint8_t, uint64_t)` :438 | O(1) | O(1) | |
| `heads::is_boolean(head const&)` :449 | O(1) | O(1) | |
| `heads::is_simple_value(head const&)` :455 | O(1) | O(1) | |
| `heads::is_null(head const&)` :461 | O(1) | O(1) | |
| `heads::raw_head_read(string_view, size_t)` :473 | O(1); at most 8 argument bytes | O(1) | Checks bytes left and check_additional_information. |
| `heads::self_described_cbor_content(string_view)` :497 | O(1) | O(1), a view | Removes one tag 55799 head. |
| `heads::break_at(string_view, size_t)` :506 | O(1) | O(1) | Checks at < size. |

## error.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `category()` | O(1) | O(1) | Returns the one `std::error_category` of `cbor::error`. |
| `make_error_code(error)` | O(1) | O(1) | Makes a `std::error_code`; `std::is_error_code_enum<cbor::error>` is true. |
| `error_category::message(int)` | O(1) | O(n) | Returns the text of the error as a `std::string`. |

## validity.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `check_nesting_depth(size_t, size_t)` :61 | O(1) | O(1) | The depth check. |
| `throw_logic_error(char const*)` :69 | O(1) plus a throw | O(1) plus the exception object | std::logic_error allocates its message. Without exceptions: std::abort. |
| `throw_logic_error_if_empty<T>(shared_ptr<T> const&, char const*)` :79 | O(1) | O(1); allocates only on the throw | |
| `throw_logic_error_if_null<T>(shared_ptr<T> const&, char const*)` :86 | O(1) | O(1); allocates only on the throw | |
| `checked_mul<T>(T, T)` :93 | O(1) | O(1) | Overflow check. |
| `checked_add<T>(T, T)` :119 | O(1) | O(1) | Overflow check. |
| `keys_unique<R, Projection>(R const&, Projection)` :145 | O(r) | O(1) | adjacent_find. The range must be sorted. |
| `check_key_unique(bool)` :151 | O(1) | O(1) | |
| `writer_error(errc)` :171 | O(1) | O(1) | |
| `check_additional_information(major_type, uint8_t)` :182 | O(1) | O(1) | |
| `check_definite_length(major_type, uint8_t)` :194 | O(1) | O(1) | |
| `check_chunk(major_type, major_type, uint8_t)` :205 | O(1) | O(1) | |
| `check_simple_value(uint8_t, uint64_t)` :214 | O(1) | O(1) | |
| `check_tag_content(uint64_t, major_type, uint8_t)` :223 | O(1) | O(1) | One switch. |
| `typed_array_element_size(uint64_t)` :259 | O(1) | O(1) | |
| `typed_array_check(uint64_t, size_t)` :266 | O(1) | O(1) | |
| `check_sharedref_index(uint64_t, size_t)` :278 | O(1) | O(1) | Bounds a tag 29 index by s. |
| `check_index<I>(I, uint64_t)` :288 | O(1) | O(1) | A negative index counts from the end. |
| `check_count(size_t, size_t)` :305 | O(1) | O(1) | Confirmed. |
| `check_number_range<T, M>(bool, M)` :313 | O(1) | O(1) | Confirmed. |
| `check_magnitude_size(size_t, size_t)` :325 | O(1) | O(1) | Bounds L for magnitude_value. Confirmed. |

`keys_equivalent` is declared here and
defined in shared.hpp. See that section.

## rfc8949.hpp, rfc8746.hpp, rfc9535.hpp

Enums and constants only. No function found by grep.

## item_size.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `well_formedness::no_marks::mark(decoder const&)` :30 | O(1) | O(1) | Empty body. Confirmed. |
| `well_formedness::item_skip<Marks>(decoder&, Marks&)` | O(i) heads, so O(n) | O(1): one count of the items still to read. No allocation. | Iterative. No depth check: nothing recurses. Each add to the count goes through checked_add, and a count larger than the bytes left is too_little_data at once (check_pending_items). Used with no_marks and with value_sharing::sharing_decoder. |
| `well_formedness::item_skip<Marks>(decoder&, Marks&, size_t depth, size_t depth_max)` | O(i) heads, so O(n) | O(nesting_depth_limit) stack: std::array of nesting_depth_limit + 1 counts of 8 bytes, about 4 KiB. No allocation. | Iterative. Only for marks that record a depth. check_nesting_depth on every item. |
| `item_size(string_view)` | O(i), bounded by O(n) | O(1). No allocation. | One item_skip with no_marks. No depth check. |

## item.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `record`, `item`, `data_items`, `sharedref` (aggregates) | none; implicit copy O(1) | sizeof only; views, pointers, indexes and scalars | |
| `data_items::span()` | O(1) | O(1) | A view into the vector of children, made on each call. |
| `item::type()` | O(1) | O(1) | |

## sequence.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `sequence(shared_ptr<string const>)` | O(1) | O(1) | Parses nothing. |
| `sequence::empty()` | O(1) | O(1) | Reads no item. |
| `iterator::operator*() const` :39 | O(1) | O(1) | Returns the cached size. |
| `iterator::operator++()` :46 | O(n_next); O(1) on error | O(1) | Full walk O(N). |
| `iterator::operator++(int)` :57 | O(n_next) | O(1) | Returns void. |
| `iterator::operator==(default_sentinel_t) const` :62 | O(1) | O(1) | |
| `begin() const` :68 | O(n_first) | O(1) | Parses the first item. |
| `end() const` :73 | O(1) | O(1) | |

## shared.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `lazy::container_resolve(shared_ptr<lazy::top_level_item>, size_t)` | Common O(1); worst O(t), t = tag 24 levels <= n/2 | Common O(1); one top_level_item per tag 24 level | make_shared per tag 24 level. The nesting depth does not bound tag 24 nesting. |
| `validity::keys_equivalent<First, Second>` :376 | Scalar O(1); string O(len); array O(n_item) plus elements; map O(k·n_map + k²·child) per level; worst exponential in d | Stack O(d) | check_nesting_depth bounds d. No heap except marks. |

## lazy.hpp

Assumption, not checked in this pass: item_skip is O(bytes skipped).

| Function | Time | Memory | Notes |
|---|---|---|---|
| `lazy_elements::iterator::operator*() const` :55 | O(1) | O(1) | One atomic increment. |
| `lazy_elements::iterator::operator++()` :62 | O(b); full walk O(n_array) | O(1) | Depth checked. |
| `lazy_elements::iterator::operator++(int)` :78 | O(b) | O(1) | |
| `lazy_elements::iterator::operator==` :85, :87 | O(1) | O(1) | |
| `lazy_elements::begin()`, `end()` :93, :98 | O(1) | O(1) | |
| `lazy_elements::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy_entries::iterator::value_find()` :120 | O(bytes of the key) | O(1) | |
| `lazy_entries::iterator::operator*() const` :132 | O(1) | O(1) | Two shared_ptr copies. |
| `lazy_entries::iterator::operator++()` :139 | O(value + next key); full walk O(n_map) | O(1) | |
| `lazy_entries::iterator::operator++(int)` :156 | as prefix | O(1) | |
| `lazy_entries::iterator::operator==` :163, :165 | O(1) | O(1) | |
| `lazy_entries::begin()` :171 | O(bytes of the first key) | O(1) | |
| `lazy_entries::end()` :178 | O(1) | O(1) | |
| `lazy_entries::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy::view(shared_ptr<string const>)` | O(1) | O(1) | One lazy::top_level_item. |
| `lazy::copy<Encoded>(Encoded&&)` | O(1) | O(1) | Moves; two allocations. |
| `lazy::copy(string_view)` :231 | O(n) | O(n) | Copies the input. |
| `lazy::tree_decoder::item_decode(size_t depth, optional<size_t> mark)` | O(n) heads and string views; a tag 29 that names a mark before the start of the lazy adds one item_skip over the top-level item, O(N_top), once | one item per data item in a deque; one pointer per child in one vector; a scratch vector of pointers for the open containers; stack O(d) | Recursive, bounded by the nesting depth. A count is checked against the bytes left before a child is read, so no count reserves memory. An empty container allocates nothing. |
| `lazy::clone()` | as item_decode | as item_decode, plus one make_shared | |
| `lazy::type()` | O(1) | O(1) | |
| `lazy::key_find<Key>(resolved, Key)` | O(bytes up to the pair); worst O(n_map) | O(1) | Linear scan. |
| `lazy::key_find<Last, Key>(iterator, Last, Key)` | O(bytes from first up to the pair or to the end) | O(1) | No allocation. |
| `lazy::find(string_view)`, `find(int64_t)` (private) | container_resolve + O(n_map) | O(1) | |
| `lazy::value_of(expected<...>)` | O(1) | O(1) | |
| `lazy::size()`, `empty()` | container_resolve | O(1) | The count of the head of an array or a map. |
| `lazy::at(string_view)` :425 | as find | O(1) | |
| `lazy::at(size_t)` | O(n_array) worst | O(1) | Array only; check_index. A map gives not_indexable. |
| `lazy::at(key)` | as find | O(1) | Map only. A key with text reads as at(string_view). |
| `lazy::operator[]` three forms | as the at of the same argument | O(1) | Calls at. |
| `jsonpath::index_select(lazy const&, int64_t)` (private) | array O(n_array) worst; map linear key_find | O(1) | The index selector of a path: negative counts from the end; on a map the integer key. |
| `lazy::get<T>() const` :476 | container_resolve + O(1); bignum + O(len) | O(1) | |
| `lazy::elements()`, `entries()` :583, :595 | container_resolve | O(1) | |

## owning_ref.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `owning_ref(shared_ptr<void const>, T)` :26 | O(1) | O(1) | Private. |
| copy constructor, copy assignment :41, :42 | O(1) | O(1) | Atomic count. Assignment can free the old buffer. |
| move constructor, move assignment :44, :50 | O(1) | O(1) | |
| `operator*`, `operator->` :60, :67 | O(1) | O(1) | Lvalue only. |

## query.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `jsonpath::name_first(char)` :139 | O(1) | O(1) | |
| `jsonpath::function_name_char(char)` :144 | O(1) | O(1) | |
| `jsonpath::int_read(string_view, size_t)` :149 | O(t), t = digits | O(1) | The digit scan has no bound; the 16-digit check comes after it. |
| `jsonpath::number_end(string_view, size_t)` :169 | O(t) | O(1) | |
| `query::string_parse` :208 | O(t) | O(t) | Allocates. |
| `query::literal_parse` :229 | O(t) expected | O(t) | Inner cost not checked. |
| `query::selector_parse` :240 | O(t) | O(t) through callees | Filter depth bounded by depth_max. |
| `query::bracketed_parse` :306 | O(t) | O(c) | vector push_back. |
| `query::segments_parse` :326 | O(t) | O(p + c) | Copies a chosen vector. |
| `query::comparable(size_t) const` :384 | O(1) | O(1) | |
| `query::expression_add(expression)` :392 | O(1) amortized | O(1) amortized | |
| `query::function_parse` :398 | O(t) | O(a), a = 1 | |
| `query::primary_parse` :430 | O(t) | O(t) | |
| `query::comparison_op_read` :491 | O(1) | O(1) | |
| `query::paren_parse` :506 | O(t) | O(t) | |
| `query::basic_parse` :517 | O(t) | O(t) | |
| `query::logical_and_parse` :553 | O(t) | O(t) | A loop. |
| `query::logical_or_parse` :571 | O(t) | O(t) | Depth checked. |
| `jsonpath::query_parse(string_view, bool, size_t)` :592 | O(t) | O(t) | Compile time in the fixed_string forms. |
| `query_walk<T>(query_view const&, parsed_query const&, string_view)` :642 | O(n) worst; common O(bytes before the target) | O(1) | No allocation. nullopt on tag 29. |
| `query_walk<T>(..., lazy const&)` :879 | O(n·p) worst; common O(n) | O(1); O(n) per tag 24 level | |
| `query_walk<Path, T>(string_view)` :902 | O(n); fallback O(n·p) | O(1); fallback allocates | Allocation only on tag 29. |
| `query_walk<Path, T>(shared_ptr<void const>, string_view, ...)` :933 | as :902 | as :902 | |
| `jsonpath::key_find(lazy const&, string_view)` :993 | text key O(n_map); other key O(k·(n_map + value_equal)) | O(1); tag 24 allocates | |
| `jsonpath::value_equal` :1021 | map: 2k² key compares + O(k·n_map) per level; product over d levels worst | O(d) stack | |
| `jsonpath::value_less` :1143 | O(1) or O(min len) | O(1) | |
| `jsonpath::comparable_value` :1189 | literal O(size); query: segments_apply | allocates per count, length or literal | |
| `jsonpath::expression_test` :1246 | sum of subexpressions | O(e) stack | |
| `jsonpath::selector_apply` :1318 | O(bytes of the node); filter adds one expression_test per child | O(m) or O(k) | Changes nodelist. nodelist bounded by root bytes. |
| `jsonpath::segment_apply` :1398 | child: sum of selector_apply; descendant: O(c·n·d) | O(i) children vectors; O(d) frames | |
| `jsonpath::segments_apply` :1419 | worst O(p·N·c·n·d); common O(p·n) | O(N), N <= n | |
| `compiled_apply<Path, Walk>(Walk const&)` | O(1) at run time | O(1) | Compiles the path into arrays at compile time and calls the walk. |
| `is_valid_path<Path>`, `is_valid_path_v<Path>` :985 | O(t) compile time | none at run time | |
| `is_singular_query<Path>`, `is_singular_query_v<Path>` :1506 | O(t + c) compile time | none at run time | |
| `at_path<Path, T>(string_view)` :1525 | O(n); fallback O(n·p) | O(1); fallback allocates | |
| `at_path<Path, T>(shared_ptr<string const>)` | as :1525 | as :1525 | |
| `jsonpath::compile<T>(string_view path)` | query_parse | O(t) | Allocates the parsed path. Once for many applies. |
| `lazy::at_path(query<T> const&)`, `at_path(string_view, query<T> const&)`, `at_path(shared_ptr<string const>, query<T> const&)` | as the compiled form with the same T | as the compiled form with the same T | No parse. |
| `lazy::at_path<Path>()` | singular_query_walk | O(1) | The path is compiled. |
| `lazy::at_path<Path, T>()` | as `query_walk<T>(..., lazy const&)` | as `query_walk<T>(..., lazy const&)` | |
| `lazy::at_path<Path, nodelist>()`, `lazy::at_path(query<nodelist> const&)` | query_walk | O(N) nodelist | Allocates the nodelist. |
| local lambdas :171, :446, :1322, :1364, :1036-1045 | O(digits) or O(1) amortized | O(1) | Included in the rows above. |

## encode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `encode` decl :33 | see :745 | see :745 | |
| `encoding::string_sink::append` :42 | O(len(part)) amortized | O(len(part)) amortized | std::string growth. |
| `string_sink::done` :48 | O(1) | O(1) | |
| `string_sink::resize_and_overwrite<Op>` :53 | O(size + op) amortized | O(size) amortized | Not checked line by line. |
| `container_message<C>::append` :68 | O(len(part)) amortized | O(len(part)) amortized | |
| `container_message<C>::done` :77 | O(1) | O(1) | |
| `span_message<B>::append` :88 | O(len(part)) | O(1) | Checks bytes left. |
| `span_message<B>::done` :97 | O(1) | O(1) | |
| `byte_container<C>` :103 | compile time | O(1) | |
| `message_of<Target>` :110 | O(1) or Target::allocate | O(1) or Target::allocate | Not confirmed. |
| `encoding::encoder<Writer>::encoder` :151 | O(1) | O(1) direct; 16384-byte member otherwise | No heap. |
| `encoder::flush` :159 | O(1) direct; O(16384) + append otherwise | O(1); writer may allocate | |
| `encoder::item_write` :173 | O(1) plus one flush | O(1) | |
| `encoder::head_encode(major, info, argument)` :193 | O(1) plus one flush | O(1) | |
| `encoder::head_encode(major, argument)` :230 | O(1) | O(1) | |
| `encoder::byte_string_encode` :235 | O(len) | O(1); writer may allocate | |
| `encoder::text_string_encode` :256 | O(len) | O(1); writer may allocate | |
| `encoder::float_encode` :277 | O(1) | O(1) | |
| `encoder::fixed_width_head_encode<T>` :330 | O(1) | O(1) | |
| `encoder::simple_value_encode` :340 | O(1) | O(1) | |
| `encoder::fixed_width_unsigned_encode`, `signed`, `float` :348, :355, :363 | O(1) | O(1) | |
| `cbor::encoder<Target>` calls | O(1) per head; O(len) per string | O(1); a container target grows; a span target allocates nothing | Each call checks that no earlier call failed. |
| `cbor::encoder::finish` | O(1) plus one flush | O(1); an iov_writer gives its storage | |
| `encoding::item_encode(encoder&, item const&, size_t, size_t)` | O(items + string bytes) | stack O(d) | Recursive, bounded by the nesting depth. A tag 29 is written as stored and not followed. |
| `encode(item const&, Target&&)`, `encode_iov(item const&)` | as item_encode | the target | |

## diagnostic_notation.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `extended_diagnostic_notation::blank` :48 | O(1) | O(1) | |
| `blank_end` :53 | O(b) | O(1) | |
| `digit`, `hex_digit_value` :60, :65 | O(1) | O(1) | |
| `head_append` :76 | O(1) amortized | O(1) amortized | Not confirmed. |
| `indicator_parse` :111 | O(1) | O(1) | Not confirmed. |
| `utf8_append` :125 | O(1) amortized | O(1) amortized | Not confirmed. |
| `hex4_parse` :149 | O(1) | O(1) | Not confirmed. |
| `quoted_parse` :163 | O(q) amortized | O(q) | Not confirmed. |
| `hex_string_parse` :248 | O(q) | O(q/2) | Not confirmed. |
| `base64_string_parse` :275 | O(q) | O(3q/4) | Not confirmed. |
| `float_append` :327 | O(1) | O(1) amortized | Not confirmed. |
| `number_parse` :354 | decimal O(t); hex float O(t) plus up to about 10^6 steps | O(1) plus <= 9 bytes | Exponent <= 999999. |
| `separator_parse` :494 | O(b) | O(1) | Not confirmed. |
| `container_parse` :505 | O(t·d) worst; O(t) flat | O(t·d) worst | Children copied at each level. |
| `string_finish` :558 | O(len) | O(len) | Not confirmed. |
| `literal_parse` :581 | O(t) scalar; O(t·d) worst for containers | O(t); O(d) frames | Not confirmed. |
| `canonical_append` :682 | scalar O(1); containers O(n·d) worst; maps + O(k log k) compares | O(n·d) worst | Vector of string pairs per level. |
| `encoding_indicator` :802 | O(1) | std::string of 0 or 2 chars | |
| `decimal_of` :813 | O(1) | std::string <= 20 chars | |
| `hex_of` :820 | O(len) | O(len) | |
| `quoted_of` :832 | O(len) amortized | O(len), up to 6x | |
| `number_of` :874 | O(1) | 400-byte stack buffer | |
| `float_of` :899 | O(1) | O(1) | |
| `diagnostic_head_decode` :927 | O(1) | O(1) | Changes d. |
| `break_found` :943 | O(1) | O(1) | Changes d. |
| `diagnostic_write` :954 | O(n) common; worst depends on check_tag_content | O(n) output; O(d) frames | |
| `diagnostic_notation` :1084 | O(n) common | O(n) string; O(s); O(d) | |

## packed.hpp

All rows marked "compile time" have no run-time cost.

| Function | Time | Memory | Notes |
|---|---|---|---|
| `no_fixed_size<T>()` :44 | O(1) compile time | O(1) | |
| `key::key(char const*)` :58 | O(len) compile time | O(len) static | |
| `reflection::key_of` :115 | O(a + len) compile time | O(len) static | |
| `reflection::tag_number_of` :124 | O(a) compile time | O(1) | Not confirmed. |
| `reflection::keys_unique` :132 | O(c log c·len) compile time | O(c) | |
| `reflection::annotated` :141 | O(a) compile time | O(1) | |
| `reflection::data_members<U>()` :149 | O(c·a) compile time | O(c) static | |
| `reflection::typed_array_tag<E>()` :221 | O(1) compile time | O(1) | |
| `reflection::typed_array_element_read<E>` :256 | O(1) | O(1) stack | |
| `reflection::typed_array_element_write<E>` :266 | O(1) | O(1) | |
| `reflection::fixed_width_head_encode` :274 | O(1) amortized | O(1) | Compile time only. |
| `reflection::zero_initialized_encode<Root, T>` :284 | O(fixed_size) compile time | O(fixed_size) | |
| `reflection::zero_initialized<Root, T>()` :354 | O(fixed_size) compile time; O(1) at run time | O(fixed_size) static | |
| `reflection::record_keys<U>()` :362 | O(c·len) compile time | O(c·len) static | |
| `reflection::record_types_collect<T>` :386 | O(t² + Σc) compile time | O(t) | |
| `reflection::packing_table_of<Root>()` :408 | O(t² + Σc) compile time | O(t) static | |
| `reflection::shared_first_of`, `argument_index_of` :416, :426 | O(1), O(t) compile time | O(1) | |
| `reflection::straight_reference_encode`, `_size` :433, :447 | O(t) compile time | O(1) | |
| `reflection::packing_prefix_of<Root>()` :455 | O(q) compile time | O(q) static | |
| `reflection::float_bits<U>(U)` :470 | O(1) | O(1) | |
| `second_item::bytes_add`, `block_add` :499, :506 | O(1) | O(1) | checked_add, checked_mul. |
| `second_item::elements_add<E, R>` :514 | O(1) or O(m) | O(1) heap; O(d) stack | |
| `second_item::add<T>` :531 | O(v); common O(i) | O(1) heap; O(d) stack | Depth follows the value. |
| `reflection::zero_initialized_copy<Root, E>` :572 | O(fixed_size<E>) | O(1) | |
| `reflection::directory_at<Root>()` :586 | O(1) | O(1) | |
| `reflection::elements_encode` :592 | O(m·fixed_size + dynamic) | O(1) heap | |
| `reflection::reference_encode` :607 | O(len), O(m) or O(k) plus content | O(1) heap | No allocation. |
| `reflection::value_encode` :684 | whole value O(n) | O(1) heap | |
| `reflection::member_named`, `step_end`, `index_end`, `index_of` :763-784 | O(c·len), O(L), O(L), O(digits) compile time | O(1) | |
| `reflection::fixed_value_read<T>` :794 | O(1) | O(1) | |
| `reflection::fixed_head_valid<T>` :846 | O(1) | O(1) | |
| `reflection::class_tag_valid` :872 | O(1) | O(1) | |
| `reflection::shared_index_read` :889 | O(1) | O(1) | Not opened. |
| `reflection::item_reference_read` :904 | O(1) | O(1) | checked_mul. |
| `reflection::directory_entry` :944 | O(1) | O(1) | |
| `reflection::reference_read`, `item_read` :951, :962 | O(1) | O(1) | check_index. |
| `reflection::directory_read<T>` :978 | O(1) in n | O(1) | |
| `reflection::index_slots`, `index_slot` :1017, :1025 | O(L) compile time | O(1) | |
| `reflection::path_valid`, `path_result` :1034, :1074 | O(p·(L + c·len)) compile time | O(p) instantiations | |
| `reflection::path_walk` :1102 | O(p) | O(1) | check_index. |
| `decode_cursor::reference_take` :1248 | O(1) | O(1) | Changes the cursor. |
| `decode_cursor::value_read<Root, T>` :1267 | O(n); O(n + k log k) with an ordered map | O(n) output; O(d) stack | Allocates per string, list and node. |
| `reflection::root_read<T>` :1411 | as value_read | as value_read | |
| `reflection::encoded_size<T>` :1433 | O(1) | O(1) | |
| `reflection::encoded_write<Exact, T>` :1455 | O(n) | O(1) heap | |
| `reflection::members_of<U>()` :1483 | O(c log c·len) compile time | O(c) static | |
| `typed_array_view<E>` constructor :1505 | O(1) | O(1) | |
| `typed_array_view::iterator` members :1517-1604 | O(1) | O(1) | |
| `typed_array_view::size`, `empty`, `begin`, `end` | O(1) | O(1) | |
| `typed_array_view::cbegin`, `cend`, `rbegin`, `rend`, `crbegin`, `crend` | O(1) | O(1) | std::reverse_iterator over the iterator. |
| `typed_array_view::at` | O(1) | O(1) | Checked; index_out_of_bounds as an error value. |
| `typed_array_view::front`, `back`, `operator[]` | O(1) | O(1) | Checked; a wrong index or an empty view throws std::logic_error. |
| `typed_array_view::first`, `last` | O(1) | O(1) | Checked. |
| `tags_registered<Root>()` :1687 | O(t log t + t·a) compile time | O(t) | |
| `reflection::fixed_size<T, Root>()` :1701 | compile time | O(1) | |
| `reflection::member_offset` :1790 | O(c) compile time | O(1) | |
| `packed::fixed_size`, `member_offset` :1813, :1820 | compile time | O(1) | |
| `packed::accessor` constructors :1837-1850 | O(1) | O(1) | |
| `packed::accessor::at_path`, `view` :1866, :1912 | O(p) | O(1) | |
| `packed::accessor::size()` :1926 | O(1) | O(1) | |
| `packed::path(shared_ptr<string const>)` | O(1) in n | O(1) | |
| `packed::at_path<Path>(string_view, Index...)` :1957 | O(q + p) | O(1) | |
| `packed::path(string_view)` :1967 | O(n) | O(n) | Copies. |
| `packed::path<Encoded>(Encoded&&)` :1977 | O(1) | O(1) | One make_shared. |
| `packed::decode(string_view)` :1987 | O(n); + k log k ordered | O(n) copy + O(n) output | |
| `packed::decode<Encoded>(Encoded&&)` :1999 | as decode(owner, view) | O(1) + output | |
| `packed::decode(shared_ptr<string const>)` | O(n); + k log k ordered | O(n) output | |
| `packed::encode(T const&)` :2023 | O(i + n) | O(n) | One std::string. |
| `packed::encode<Target>(T const&, Target&&)` :2044 | O(i + n) | container O(n); span none; other message O(n) temporary | |

## structs.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `generic::integer_read<U, V>` :81 | O(1) | O(1) | |
| `generic::head_accepted<U>` :98 | O(1) | O(1) | |
| `generic::wide_integer_read<U>` :142 | O(len), at most 16 steps | O(1) | |
| `generic::variant_read` :184 | O(alternatives) + read | output | |
| `generic::generic_read<U>` :197 | common O(n); worst O((n/d)^d) with tag 29 | output; O(s) | Each tag 29 adds 1 to depth. |
| `generic::generic_value_read` :228 | scalar O(1); string O(len); array O(m); std::map O(k log k); unordered_map O(k) | output; O(d) stack | Duplicate key dropped by try_emplace. |
| `generic::key_matches<U, I>` :380 | O(1) or O(len) | O(1) | |
| `generic::struct_read` :395 | O(k·c·len) + members + skipped | bitset; O(1) | |
| `generic::member_present`, `float_size` :471, :479 | O(1) | O(1) | |
| `generic::generic_size<U>` :492 | O(i) | O(1) heap | |
| `generic::bytes_write` :603 | O(len) | O(1) | |
| `generic::generic_write<U>` :610 | O(n) | O(1) heap | |
| `structs::decode(string_view)` :790 | O(n) + read | O(n) copy + read | Copies. |
| `structs::decode<Encoded>(Encoded&&)` :732 | as read | one make_shared + read | |
| `structs::decode(shared_ptr<string const>)` | as read | as read | |
| `structs::encode(T const&)` :755 | O(i + n) | O(n) | |
| `structs::encode<Target>(T const&, Target&&)` :772 | O(i + n) | container O(n); else O(n) temporary | Not opened. |
| `structs::read(string_view)` :821 | common O(n); worst O((n/d)^d) | output; O(s); O(d) | |

## Above linear, or allocation on a success path

Above linear in the input:

- `validity::keys_equivalent` (shared.hpp): map keys O(k²) per level, exponential in d worst.
- `jsonpath::value_equal` :1021: O(k²) key compares per map level.
- `jsonpath::key_find` :993, non-text key: O(k·n_map).
- `jsonpath::segment_apply`, `segments_apply`: O(c·n·d), O(p·N·c·n·d).
- `query_walk` lazy forms and the tag 29 fallback of `at_path`: O(n·p).
- `container_parse`, `literal_parse`, `canonical_append` (diagnostic_notation.hpp): O(t·d), O(n·d); maps add O(k log k).
- `generic_read`, `structs::read`: O((n/d)^d) with tag 29.
- `std::map` targets in packed and structs: O(n + k log k).

Allocation on a success path:

- `value_sharing::sharing_decoder::mark`, `lazy::tree_decoder::item_decode`.
- `lazy::copy`, `lazy::view`, `packed::path`, `packed::decode`, `structs::decode`, `container_resolve` (tag 24), `lazy::clone`.
- jsonpath: every parser, `compile`, a `nodelist` read, `comparable_value`, `selector_apply`, `segment_apply`, `segments_apply`.
- `string_sink`, `container_message`.
- diagnostic_notation.hpp: `canonical_append`, `container_parse`, `diagnostic_notation`, the `*_of` functions.

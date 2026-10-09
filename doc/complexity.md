# Cost of each function in mbedded-cbor

This document describes the state at commit `da9a611` on branch
`views-only-item` of `/home/user/mbedded-cbor`, with the change that
removed DepthMax from every name. Line numbers are those of `da9a611`. A later change to a
function must keep the O form that this document gives for it.

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
| d | Nesting depth. The nesting depth in force bounds it: validity::nesting_depth_max_read(), at most nesting_depth_limit. |
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
| `item_size`, `lazy_decode` friend decls, :545-559 | not given here | not given here | Bodies in other headers. |
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
| `validity::check_tag_content<Marks, Projection>(uint64_t, string_view, size_t, Marks const&, Projection)` :574 | O(s + t), t = tag 28 heads on the chain; worst O(n); common O(1) without tag 28 or 29 | O(1) | Confirmed. Each tag 29 hop goes to a mark strictly before at. marks[index] is O(1) only for a random-access Marks. |

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

`keys_equivalent` and `check_sorted_keys_unique` are declared here and
defined in shared.hpp. See that section.

## rfc8949.hpp, rfc8746.hpp, rfc9535.hpp

Enums and constants only. No function found by grep.

## item_size.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `well_formedness::no_marks::mark(decoder const&)` :30 | O(1) | O(1) | Empty body. Confirmed. |
| `well_formedness::item_skip<Marks>(decoder&, Marks&)` | O(i) heads, so O(n); plus marks.mark per tag 28 | O(1): one count of the items still to read. No allocation. | Iterative. No depth check: nothing recurses. Each add to the count goes through checked_add, and a count larger than the bytes left is too_little_data at once (check_pending_items). Used with no_marks and with top_level_item. |
| `well_formedness::item_skip<Marks>(decoder&, Marks&, size_t depth, size_t depth_max)` | O(i) heads, so O(n); plus marks.mark per tag 28 | O(nesting_depth_limit) stack: std::array of nesting_depth_limit + 2, about 8 KiB. No allocation. | Iterative. Only for marks that record a depth (decoding::prefix). check_nesting_depth on every item. |
| `item_size(string_view)` | O(i), bounded by O(n) | O(1). No allocation. | One item_skip with no_marks. No depth check. |

## item.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `record`, `item` (aggregates) :22, :28 | none; implicit copy O(1) | sizeof only; views, a pointer, lazy and scalars | No function defined. |

## sequence.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `sequence(string_view)` :25 | O(1) | O(1) | Parses nothing. |
| `sequence(Encoded&&) = delete` :30 | none | none | A std::string does not compile. |
| `iterator::operator*() const` :39 | O(1) | O(1) | Returns the cached size. |
| `iterator::operator++()` :46 | O(n_next); O(1) on error | O(1) | Full walk O(N). |
| `iterator::operator++(int)` :57 | O(n_next) | O(1) | Returns void. |
| `iterator::operator==(default_sentinel_t) const` :62 | O(1) | O(1) | |
| `begin() const` :68 | O(n_first) | O(1) | Parses the first item. |
| `end() const` :73 | O(1) | O(1) | |

## decode.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `decoding::marks<Binding>` alias :27 | none | O(s) heap when filled | One slot for each tag 28. |
| `decoding::prefix` aggregate :29 | none beyond mark | evaluating O(s) bits, mark_depths O(s) | |
| `decoding::prefix::mark(decoder const&, size_t)` :34 | O(1) amortized; O(s) on growth | O(1) amortized | mark_depths.push_back allocates. |
| `decoding::value_decoder<Binding>::value_decode(size_t, optional<size_t>)` :48 | Common: O(n) plus binding costs. Bignum: O(length). Forward tag 29: one item_skip over the top-level item, O(N_top), once; total stays O(N_top). | Stack O(d). Heap: shared O(s), evaluating O(s), mark_depths O(s); magnitude_plus_one O(length) for a negative bignum over 8 bytes; plus binding values. | Recursive. Reserve hints min(count, bytes left) and min(count, bytes left / 2). A stored shared value is copied on each use. :212 dereferences before with no null check. Cost of check_tag_content at :212 and top_level.mark at :130 not checked. |
| `lazy_decode` friend decl :255 | not checked | not checked | Defined in lazy.hpp. |

## shared.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `value_sharing::top_level_item::entry(size_t)` :251 | Common O(1); known offset O(log e); worst O(e) for an insert in the middle | O(1) amortized per new item | deque emplace_back and vector insert. |
| `top_level_item::mark(decoder const&)` :266 | O(1) amortized; O(log s) for a known offset | O(1) amortized | push_back on sharedrefs. |
| `top_level_item::sharedref_decode(decoder&, size_t) const` :278 | O(1) | O(1) | Target strictly earlier. |
| `value_sharing::shared_resolve(top_level_item&, size_t)` :308 | Common O(1); worst O(c log s + tag 29 steps), bounded by O(n) | O(1) plus marks | No depth check in the loop. Bytes left bound it. |
| `value_sharing::item_resolve(top_level_item&, size_t)` :333 | Common O(log e); worst O(n + e) | O(1) amortized | Can allocate one item. |
| `value_sharing::container_resolve(shared_ptr<top_level_item>, size_t)` :341 | Common O(1); worst O(t) shared_resolve, t = tag 24 levels <= n/2 | Common O(1); one top_level_item per tag 24 level | make_shared per tag 24 level. The nesting depth does not bound tag 24 nesting. |
| `validity::keys_equivalent<First, Second>` :376 | Scalar O(1); string O(len); array O(n_item) plus elements; map O(k·n_map + k²·child) per level; worst exponential in d | Stack O(d) | check_nesting_depth bounds d. No heap except marks. |
| `validity::check_sorted_keys_unique<Message>` :530 | Common O(n_map); worst O(n_map) plus k-1 keys_equivalent | Stack O(d) | Adjacent keys only; keys must be sorted. |
| `lazy::from(shared_ptr<void const>, Encoded&&) = delete` :177 | - | - | |

## lazy.hpp

Assumption, not checked in this pass: item_skip is O(bytes skipped).

| Function | Time | Memory | Notes |
|---|---|---|---|
| `lazy_elements::iterator::operator*() const` :55 | O(1) | O(1) | One atomic increment. |
| `lazy_elements::iterator::operator++()` :62 | O(b); full walk O(n_array) | O(1) plus marks | Depth checked. |
| `lazy_elements::iterator::operator++(int)` :78 | O(b) | O(1) | |
| `lazy_elements::iterator::operator==` :85, :87 | O(1) | O(1) | |
| `lazy_elements::begin()`, `end()` :93, :98 | O(1) | O(1) | |
| `lazy_elements::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy_entries::iterator::value_find()` :120 | O(bytes of the key) | O(1) plus marks | |
| `lazy_entries::iterator::operator*() const` :132 | O(1) | O(1) | Two shared_ptr copies. |
| `lazy_entries::iterator::operator++()` :139 | O(value + next key); full walk O(n_map) | O(1) plus marks | |
| `lazy_entries::iterator::operator++(int)` :156 | as prefix | O(1) | |
| `lazy_entries::iterator::operator==` :163, :165 | O(1) | O(1) | |
| `lazy_entries::begin()` :171 | O(bytes of the first key) | O(1) plus marks | |
| `lazy_entries::end()` :178 | O(1) | O(1) | |
| `lazy_entries::size()`, `empty()` (view_interface) | O(1) | O(1) | The count of the head. |
| `lazy::from(shared_ptr<void const>, string_view)` :209 | O(1) | O(1) | One top_level_item. |
| `lazy::from(shared_ptr<string const>)` :218 | O(1) | O(1) | |
| `lazy::from<Encoded>(Encoded&&)` :226 | O(1) | O(1) | Moves; two allocations. |
| `lazy::from(string_view)` :231 | O(n) | O(n) | Copies the input. |
| `value_sharing::key_find<Key>(resolved, Key)` :238 | O(bytes up to the pair); worst O(n_map) + k shared_resolve | O(1) plus marks | Linear scan. |
| `value_sharing::key_find<Sorted, Last, Key>(iterator, Last, Key)` | O(bytes from first up to the pair or to last); Sorted stops at the first plain key with greater encoded bytes | O(1) plus marks | No allocation. Throws std::logic_error when the end of the map comes before last. |
| `lazy::find(string_view)`, `find(int64_t)` :373, :383 | container_resolve + O(n_map) | O(1) | |
| `lazy::find(iterator first, Last last, key)` two forms | O(bytes in [first, the pair]) | O(1) | No container_resolve. |
| `lazy::equal_range(iterator first, Last last, key)` two forms | as find; a miss in a sorted map stops at the first greater key; a hit adds one item_skip | O(1) | |
| `value_sharing::value_of(expected<...>)` :414 | O(1) | O(1) | |
| `lazy::contains(string_view)`, `contains(int64_t)` | as find | O(1) | |
| `lazy::count(string_view)`, `count(int64_t)` | O(n_map): reads every pair after the first match | O(1) plus marks | Counts every pair with the key. |
| `lazy::size()`, `empty()` | container_resolve | O(1) | The count of the head of an array or a map. |
| `lazy::at(string_view)` :425 | as find | O(1) | |
| `lazy::at(size_t)` | O(n_array) worst | O(1) plus marks | Array only; check_index. A map gives not_indexable. |
| `lazy::at(key)` | as find | O(1) | Map only. A key with text reads as at(string_view). |
| `jsonpath::index_select(lazy const&, int64_t)` (private) | array O(n_array) worst; map linear key_find | O(1) plus marks | The index selector of a path: negative counts from the end; on a map the integer key. |
| `lazy::get<T>() const` :476 | container_resolve + O(1); bignum + O(len) | O(1) | |
| `lazy::elements()`, `entries()` :583, :595 | container_resolve | O(1) | |
| `value_sharing::item_decode(top_level_item&, size_t, size_t)` :606 | First time: O(log e), worst O(e); containers + O(n_item). Built scalar or string: O(n_item) again. | O(1) amortized per item; stack O(d + s) | Tag 29 recursion is at the same depth, bounded by s. |
| `lazy::decode<Self>(this Self&&)` :721 | as item_decode | as item_decode | |
| `lazy_decode<Binding>(Binding&, lazy const&)` :733 | O(s) + value_decode | O(s) plus the binding | Two allocations of size s. |

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
| `query_walk<Binding>(Binding&, ...)` :1435 | singular O(n·p) + lazy_decode; else segments_apply + decodes | O(1) or O(N) | |
| `is_valid_path<Path>`, `is_valid_path_v<Path>` :985 | O(t) compile time | none at run time | |
| `is_singular_query<Path>`, `is_singular_query_v<Path>` :1506 | O(t + c) compile time | none at run time | |
| `at_path<Binding>(Binding&, string_view, lazy const&)` :1470 | O(t) + query_walk :1435 | O(t) | Parses at run time. |
| `at_path<Path, Binding>(Binding&, lazy const&)` :1480 | query_walk :1435 | as :1435 | |
| `at_path<Path, T>(string_view)` :1525 | O(n); fallback O(n·p) | O(1); fallback allocates | |
| `at_path<Path, T>(shared_ptr<void const>, string_view)` :1543 | as :1525 | as :1525 | |
| `at_path(shared_ptr<void const>, Encoded&&) = delete` :71 | none | none | |
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
| `encoder<Writer>::encoder` :151 | O(1) | O(1) direct; 16384-byte member otherwise | No heap. |
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
| `discarding_writer::append`, `done` :377, :382 | O(1) | O(1) | |
| `sharing<Binding>` aggregate :388 | none | three unordered_map; one node per entry | |
| `walker::walker` :412 | O(1) | O(1) plus the encoder | |
| `walker::keep`, `keep_error`, `head` :417, :423, :429 | O(1) | O(1) | |
| `walker::key`, `value` :434, :442 | as child | as child | |
| `walker::child<Identity>` :450 | O(1) here; whole walk O(r + n_out); r exponential in d for a DAG with pass::plain; embeds O(n_out·d) worst | O(1) per frame; O(d) frames; embed allocates a string | check_nesting_depth at :455. |
| `walker::content_of<Identity>` :501 | before_encode + expected O(1) hash, O(s) worst | pass::count adds one node | |
| `walker::describe<Identity>` :518 | scalar O(1); string O(len); array O(m); map O(k) | O(1) per frame | |
| `walker::bignum` :627 | O(len) | negative: O(len) std::string | |
| `walker::simple` :660 | O(1) | O(1) | |
| `walker` copy = delete :670 | none | none | |
| `cycle_find<Binding>` :673 | O(r·d); r exponential in d for a DAG | O(d) vector + O(d) stack | Cold path only. |
| `encoding::encode_from<Sharing, Binding, Writer>` (private) :707 | off: O(r + n_out); on: O(i + n_out) expected | off: O(d); on: O(i) hash entries | |
| `encode<Sharing, Binding, Writer>` :745 | as encode_from | as encode_from plus target | |

## binding.hpp

| Function | Time | Memory | Notes |
|---|---|---|---|
| `binding<Value>::*`, 35 members :38-73 | none (deleted) | none | The user binding defines the cost. |
| `fixed_string<N>::fixed_string` :80 | O(N) compile time | O(N) | |
| `fixed_string<N>::view` :85 | O(1) | O(1) | |

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
| `diagnostic_write` :954 | O(n) common; worst depends on check_tag_content | O(n) output; O(s) marks; O(d) frames | |
| `diagnostic_notation` :1084 | O(n) common | O(n) string; O(s); O(d) | |

## schema.hpp

All rows marked "compile time" have no run-time cost.

| Function | Time | Memory | Notes |
|---|---|---|---|
| `no_fixed_size<T>()` :44 | O(1) compile time | O(1) | |
| `key::key(char const*)` :58 | O(len) compile time | O(len) static | |
| `packed::key_of` :115 | O(a + len) compile time | O(len) static | |
| `packed::tag_number_of` :124 | O(a) compile time | O(1) | Not confirmed. |
| `packed::keys_unique` :132 | O(c log c·len) compile time | O(c) | |
| `packed::annotated` :141 | O(a) compile time | O(1) | |
| `packed::data_members<U>()` :149 | O(c·a) compile time | O(c) static | |
| `packed::typed_array_tag<E>()` :221 | O(1) compile time | O(1) | |
| `packed::typed_array_element_read<E>` :256 | O(1) | O(1) stack | |
| `packed::typed_array_element_write<E>` :266 | O(1) | O(1) | |
| `packed::fixed_width_head_encode` :274 | O(1) amortized | O(1) | Compile time only. |
| `packed::zero_initialized_encode<Root, T>` :284 | O(fixed_size) compile time | O(fixed_size) | |
| `packed::zero_initialized<Root, T>()` :354 | O(fixed_size) compile time; O(1) at run time | O(fixed_size) static | |
| `packed::record_keys<U>()` :362 | O(c·len) compile time | O(c·len) static | |
| `packed::record_types_collect<T>` :386 | O(t² + Σc) compile time | O(t) | |
| `packed::packing_table_of<Root>()` :408 | O(t² + Σc) compile time | O(t) static | |
| `packed::shared_first_of`, `argument_index_of` :416, :426 | O(1), O(t) compile time | O(1) | |
| `packed::straight_reference_encode`, `_size` :433, :447 | O(t) compile time | O(1) | |
| `packed::packing_prefix_of<Root>()` :455 | O(q) compile time | O(q) static | |
| `packed::float_bits<U>(U)` :470 | O(1) | O(1) | |
| `second_item::bytes_add`, `block_add` :499, :506 | O(1) | O(1) | checked_add, checked_mul. |
| `second_item::elements_add<E, R>` :514 | O(1) or O(m) | O(1) heap; O(d) stack | |
| `second_item::add<T>` :531 | O(v); common O(i) | O(1) heap; O(d) stack | Depth follows the value. |
| `packed::zero_initialized_copy<Root, E>` :572 | O(fixed_size<E>) | O(1) | |
| `packed::directory_at<Root>()` :586 | O(1) | O(1) | |
| `packed::elements_encode` :592 | O(m·fixed_size + dynamic) | O(1) heap | |
| `packed::reference_encode` :607 | O(len), O(m) or O(k) plus content | O(1) heap | No allocation. |
| `packed::value_encode` :684 | whole value O(n) | O(1) heap | |
| `packed::member_named`, `step_end`, `index_end`, `index_of` :763-784 | O(c·len), O(L), O(L), O(digits) compile time | O(1) | |
| `packed::fixed_value_read<T>` :794 | O(1) | O(1) | |
| `packed::fixed_head_valid<T>` :846 | O(1) | O(1) | |
| `packed::class_tag_valid` :872 | O(1) | O(1) | |
| `packed::shared_index_read` :889 | O(1) | O(1) | Not opened. |
| `packed::item_reference_read` :904 | O(1) | O(1) | checked_mul. |
| `packed::directory_entry` :944 | O(1) | O(1) | |
| `packed::reference_read`, `item_read` :951, :962 | O(1) | O(1) | check_index. |
| `packed::directory_read<T>` :978 | O(1) in n | O(1) | |
| `packed::index_slots`, `index_slot` :1017, :1025 | O(L) compile time | O(1) | |
| `packed::path_valid`, `path_result` :1034, :1074 | O(p·(L + c·len)) compile time | O(p) instantiations | |
| `packed::path_walk` :1102 | O(p) | O(1) | check_index. |
| `decode_cursor::reference_take` :1248 | O(1) | O(1) | Changes the cursor. |
| `decode_cursor::value_read<Root, T>` :1267 | O(n); O(n + k log k) with an ordered map | O(n) output; O(d) stack | Allocates per string, list and node. |
| `packed::root_read<T>` :1411 | as value_read | as value_read | |
| `packed::encoded_size<T>` :1433 | O(1) | O(1) | |
| `packed::encoded_write<Exact, T>` :1455 | O(n) | O(1) heap | |
| `packed::members_of<U>()` :1483 | O(c log c·len) compile time | O(c) static | |
| `typed_array_view<E>` constructor :1505 | O(1) | O(1) | |
| `typed_array_view::iterator` members :1517-1604 | O(1) | O(1) | |
| `typed_array_view::size`, `empty`, `begin`, `end` | O(1) | O(1) | |
| `typed_array_view::front`, `back`, `first`, `last`, `operator[]` | O(1) | O(1) | Checked. |
| `tags_registered<Root>()` :1687 | O(t log t + t·a) compile time | O(t) | |
| `packed::fixed_size<T, Root>()` :1701 | compile time | O(1) | |
| `packed::member_offset` :1790 | O(c) compile time | O(1) | |
| `schema::fixed_size`, `member_offset` :1813, :1820 | compile time | O(1) | |
| `schema::accessor` constructors :1837-1850 | O(1) | O(1) | |
| `schema::accessor::at_path`, `view` :1866, :1912 | O(p) | O(1) | |
| `schema::accessor::size()` :1926 | O(1) | O(1) | |
| `schema::path(shared_ptr<void const>, string_view)` :1933 | O(1) in n | O(1) | |
| `schema::path(shared_ptr, Encoded&&) = delete` :1949 | - | - | |
| `schema::at_path<Path>(string_view, Index...)` :1957 | O(q + p) | O(1) | |
| `schema::path(string_view)` :1967 | O(n) | O(n) | Copies. |
| `schema::path<Encoded>(Encoded&&)` :1977 | O(1) | O(1) | One make_shared. |
| `schema::decode(string_view)` :1987 | O(n); + k log k ordered | O(n) copy + O(n) output | |
| `schema::decode<Encoded>(Encoded&&)` :1999 | as decode(owner, view) | O(1) + output | |
| `schema::decode(shared_ptr<void const>, string_view)` :2009 | O(n); + k log k ordered | O(n) output | |
| `schema::decode(shared_ptr, Encoded&&) = delete` :2021 | - | - | |
| `schema::encode(T const&)` :2023 | O(i + n) | O(n) | One std::string. |
| `schema::encode<Target>(T const&, Target&&)` :2044 | O(i + n) | container O(n); span none; other message O(n) temporary | |

## databind.hpp

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
| `databind::decode(string_view) = delete` :728 | - | - | |
| `databind::decode<Encoded>(Encoded&&)` :732 | as read | one make_shared + read | |
| `databind::decode(shared_ptr<void const>, string_view)` :741 | as read | as read | |
| `databind::decode(shared_ptr, Encoded&&) = delete` :753 | - | - | |
| `databind::encode(T const&)` :755 | O(i + n) | O(n) | |
| `databind::encode<Target>(T const&, Target&&)` :772 | O(i + n) | container O(n); else O(n) temporary | Not opened. |
| `databind::read(string_view)` :821 | common O(n); worst O((n/d)^d) | output; O(s); O(d) | |

## Above linear, or allocation on a success path

Above linear in the input:

- `validity::keys_equivalent`, `check_sorted_keys_unique` (shared.hpp): map keys O(k²) per level, exponential in d worst.
- `jsonpath::value_equal` :1021: O(k²) key compares per map level.
- `jsonpath::key_find` :993, non-text key: O(k·n_map).
- `jsonpath::segment_apply`, `segments_apply`: O(c·n·d), O(p·N·c·n·d).
- `query_walk` lazy forms and the tag 29 fallback of `at_path`: O(n·p).
- `walker::child`, `encode_from` with sharing off, `cycle_find`: exponential in d for a DAG.
- `container_parse`, `literal_parse`, `canonical_append` (diagnostic_notation.hpp): O(t·d), O(n·d); maps add O(k log k).
- `generic_read`, `databind::read`: O((n/d)^d) with tag 29.
- `value_sharing::item_decode`: rebuilt scalars skip O(n_item) again on each call.
- `std::map` targets in schema and databind: O(n + k log k).

Allocation on a success path:

- `magnitude_plus_one`, `magnitude_minus_one`, `walker::bignum` (negative), `value_decode` (negative bignum).
- `top_level_item::entry`, `mark`, `prefix::mark`.
- `lazy::from`, `schema::path`, `schema::decode`, `databind::decode`, `container_resolve` (tag 24), `lazy_decode`.
- jsonpath: every parser, `at_path(string_view path)`, `comparable_value`, `selector_apply`, `segment_apply`, `segments_apply`.
- `encode_from` with sharing on, `walker` embeds, `cycle_find`, `string_sink`, `container_message`.
- diagnostic_notation.hpp: `canonical_append`, `container_parse`, `diagnostic_notation`, the `*_of` functions.
